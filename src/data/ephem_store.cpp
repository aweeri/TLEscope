/**
 * @file ephem_store.cpp
 * @brief Fixed-capacity registry and interpolation for tabulated ephemerides
 *
 * Pure computation: no UI, no drawing, no file I/O. The registry owns one
 * std::vector<EphemSample> per mission; the mission records themselves live in
 * a static array so callers never depend on heap addresses.
 */

#include "data/ephem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

/* static mission storage; occupied slots are kept contiguous (0..g_count-1) */
static EphemMission g_missions[EPHEM_MAX_MISSIONS];
static int g_count = 0;
static int g_next_id = 1; /* nonzero, monotonically increasing */

/* -- helpers ---------------------------------------------------------------- */

static bool ephem_usable(const EphemMission *m)
{
    return m != NULL && m->s.size() >= 2 && m->s.size() <= EPHEM_MAX_SAMPLES;
}

static bool ephem_in_window(const EphemMission *m, double t_unix)
{
    if (!ephem_usable(m))
        return false;
    const double t0 = m->s.front().t_unix;
    const double t1 = m->s.back().t_unix;
    if (!(t1 > t0))
        return false;
    return t_unix >= t0 && t_unix <= t1;
}

/* index of the last sample with t <= t_unix; requires a usable mission */
static size_t ephem_lower_index(const EphemMission *m, double t_unix)
{
    const std::vector<EphemSample> &s = m->s;
    size_t lo = 0;
    size_t hi = s.size() - 1;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (s[mid].t_unix <= t_unix)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (lo > 0) ? lo - 1 : 0;
}

/* cubic Hermite between a and b; dt is b->t - a->t in seconds */
static void ephem_hermite(const EphemSample *a, const EphemSample *b, double u, Vector3 *out_p, Vector3 *out_v)
{
    const double dt = b->t_unix - a->t_unix;
    const double u2 = u * u;
    const double u3 = u2 * u;

    const double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    const double h10 = u3 - 2.0 * u2 + u;
    const double h01 = -2.0 * u3 + 3.0 * u2;
    const double h11 = u3 - u2;

    if (out_p)
    {
        out_p->x = (float)(h00 * a->p.x + h10 * dt * a->v.x + h01 * b->p.x + h11 * dt * b->v.x);
        out_p->y = (float)(h00 * a->p.y + h10 * dt * a->v.y + h01 * b->p.y + h11 * dt * b->v.y);
        out_p->z = (float)(h00 * a->p.z + h10 * dt * a->v.z + h01 * b->p.z + h11 * dt * b->v.z);
    }

    if (out_v)
    {
        const double d00 = 6.0 * u2 - 6.0 * u;
        const double d10 = 3.0 * u2 - 4.0 * u + 1.0;
        const double d01 = -6.0 * u2 + 6.0 * u;
        const double d11 = 3.0 * u2 - 2.0 * u;

        /* d/du of the position, divided by dt -> per-second velocity */
        out_v->x = (float)((d00 * a->p.x + d10 * dt * a->v.x + d01 * b->p.x + d11 * dt * b->v.x) / dt);
        out_v->y = (float)((d00 * a->p.y + d10 * dt * a->v.y + d01 * b->p.y + d11 * dt * b->v.y) / dt);
        out_v->z = (float)((d00 * a->p.z + d10 * dt * a->v.z + d01 * b->p.z + d11 * dt * b->v.z) / dt);
    }
}

/* linear blend of two sample times/values, clamped to the interval */
static double ephem_lerp_u(const EphemSample *a, const EphemSample *b, double t_unix)
{
    const double dt = b->t_unix - a->t_unix;
    if (dt <= 0.0)
        return 0.0;
    double u = (t_unix - a->t_unix) / dt;
    if (u < 0.0)
        u = 0.0;
    if (u > 1.0)
        u = 1.0;
    return u;
}

/* shortest-arc interpolation of longitudes, both in degrees */
static float ephem_lerp_lon(float lon_a, float lon_b, double u)
{
    float d = lon_b - lon_a;
    while (d > 180.0f)
        d -= 360.0f;
    while (d < -180.0f)
        d += 360.0f;
    float lon = (float)(lon_a + d * u);
    while (lon > 180.0f)
        lon -= 360.0f;
    while (lon < -180.0f)
        lon += 360.0f;
    return lon;
}

/* true when the stored lat/lon columns actually carry data */
static bool ephem_has_latlon(const EphemMission *m)
{
    for (size_t i = 0; i < m->s.size(); ++i)
    {
        if (m->s[i].lat != 0.0f || m->s[i].lon != 0.0f)
            return true;
    }
    return false;
}

/* normalise a freshly assigned mission: NUL-terminate the strings and keep the
 * stored samples sorted and strictly increasing in time */
static void ephem_normalise(EphemMission *slot)
{
    slot->name[sizeof(slot->name) - 1] = '\0';
    slot->target[sizeof(slot->target) - 1] = '\0';

    std::stable_sort(slot->s.begin(), slot->s.end(),
                     [](const EphemSample &a, const EphemSample &b) { return a.t_unix < b.t_unix; });
    slot->s.erase(std::unique(slot->s.begin(), slot->s.end(),
                              [](const EphemSample &a, const EphemSample &b) { return a.t_unix == b.t_unix; }),
                  slot->s.end());

    if (!slot->s.empty())
    {
        slot->t0_unix = slot->s.front().t_unix;
        slot->t1_unix = slot->s.back().t_unix;
    }
}

/* -- registry --------------------------------------------------------------- */

int EphemCount()
{
    return g_count;
}

EphemMission *EphemGet(int index)
{
    if (index < 0 || index >= g_count)
        return NULL;
    return &g_missions[index];
}

EphemMission *EphemGetById(int id)
{
    if (id == 0)
        return NULL;
    for (int i = 0; i < g_count; ++i)
    {
        if (g_missions[i].id == id)
            return &g_missions[i];
    }
    return NULL;
}

int EphemAdd(const EphemMission &m)
{
    if (g_count >= EPHEM_MAX_MISSIONS)
        return 0;

    EphemMission &slot = g_missions[g_count];
    slot = m; /* copies the sample vector */

    if (g_next_id == 0)
        g_next_id = 1;
    slot.id = g_next_id++;

    ephem_normalise(&slot);

    ++g_count;
    return slot.id;
}

bool EphemReplace(int id, const EphemMission &m)
{
    EphemMission *slot = EphemGetById(id);
    if (slot == NULL)
        return false;

    /* the identity of the slot is what favourites/selections/rows reference;
       everything else is replaced by the refresh payload. A supplied nonzero
       norad_id_num (the canonical 900000 + id) is preserved as-is; otherwise
       the slot's existing value is kept. */
    const int keep_id = slot->id;
    const uint32_t keep_norad = slot->norad_id_num;

    *slot = m;
    slot->id = keep_id;
    slot->norad_id_num = (m.norad_id_num != 0) ? m.norad_id_num : keep_norad;

    ephem_normalise(slot);
    return true;
}

int EphemFindByTarget(const char *target)
{
    if (!target || !target[0])
        return 0;
    for (int i = 0; i < g_count; ++i)
    {
        if (g_missions[i].target[0] && std::strcmp(g_missions[i].target, target) == 0)
            return g_missions[i].id;
    }
    return 0;
}

bool EphemRemove(int id)
{
    for (int i = 0; i < g_count; ++i)
    {
        if (g_missions[i].id == id)
        {
            /* compact: keep occupied slots contiguous so indices stay dense */
            for (int j = i; j + 1 < g_count; ++j)
                g_missions[j] = g_missions[j + 1];
            g_missions[g_count - 1] = EphemMission();
            --g_count;
            return true;
        }
    }
    return false;
}

void EphemClear()
{
    for (int i = 0; i < g_count; ++i)
        g_missions[i] = EphemMission();
    g_count = 0;
}

/* -- sampling --------------------------------------------------------------- */

bool EphemInterp(const EphemMission *m, double t_unix, Vector3 *out_p, Vector3 *out_v)
{
    if (!ephem_usable(m) || !ephem_in_window(m, t_unix))
        return false;

    const std::vector<EphemSample> &s = m->s;
    const size_t i = ephem_lower_index(m, t_unix);
    const size_t j = (i + 1 < s.size()) ? i + 1 : i;

    Vector3 p = {0};
    Vector3 v = {0};
    if (i == j)
    {
        p = s[i].p;
        v = s[i].v;
    }
    else
    {
        const double u = ephem_lerp_u(&s[i], &s[j], t_unix);
        ephem_hermite(&s[i], &s[j], u, &p, &v);
    }

    if (out_p)
        *out_p = p;
    if (out_v)
        *out_v = v;
    return true;
}

bool EphemLatLon(const EphemMission *m, double t_unix, float *out_lat, float *out_lon)
{
    if (!ephem_usable(m) || !ephem_in_window(m, t_unix))
        return false;

    const std::vector<EphemSample> &s = m->s;
    const size_t i = ephem_lower_index(m, t_unix);
    const size_t j = (i + 1 < s.size()) ? i + 1 : i;

    float lat;
    float lon;
    if (i == j)
    {
        lat = s[i].lat;
        lon = s[i].lon;
    }
    else
    {
        const double u = ephem_lerp_u(&s[i], &s[j], t_unix);
        lat = (float)(s[i].lat + (s[j].lat - s[i].lat) * u);
        lon = ephem_lerp_lon(s[i].lon, s[j].lon, u);
    }

    /* fall back to the spherical sub-point when the samples carry no lat/lon */
    if (!ephem_has_latlon(m))
    {
        Vector3 p = {0};
        if (EphemInterp(m, t_unix, &p, NULL))
        {
            const float r = Vector3Length(p);
            if (r > 1e-3f)
            {
                lat = asinf(p.y / r) * RAD2DEG;

                /* sample gmst is stored in degrees; blend it the same way */
                double gmst;
                if (i == j)
                    gmst = s[i].gmst;
                else
                    gmst = s[i].gmst + (s[j].gmst - s[i].gmst) * ephem_lerp_u(&s[i], &s[j], t_unix);

                lon = atan2f(-p.z, p.x) * RAD2DEG - (float)gmst;
                while (lon > 180.0f)
                    lon -= 360.0f;
                while (lon < -180.0f)
                    lon += 360.0f;
            }
        }
    }

    if (out_lat)
        *out_lat = lat;
    if (out_lon)
        *out_lon = lon;
    return true;
}

bool EphemSunlit(const EphemMission *m, double t_unix)
{
    if (!ephem_usable(m) || !ephem_in_window(m, t_unix))
        return false;

    const std::vector<EphemSample> &s = m->s;
    const size_t i = ephem_lower_index(m, t_unix);
    const size_t j = (i + 1 < s.size()) ? i + 1 : i;
    if (i == j)
        return s[i].sunlit;

    /* nearest sample wins; ties resolve to the earlier one */
    const double da = t_unix - s[i].t_unix;
    const double db = s[j].t_unix - t_unix;
    return (db < da) ? s[j].sunlit : s[i].sunlit;
}

bool EphemInWindow(const EphemMission *m, double t_unix)
{
    return ephem_in_window(m, t_unix);
}
