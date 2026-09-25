#include "demo_director.h"

#include "core/astro.h"
#include "core/location.h"
#include "core/theme.h"
#include "data/storage.h"
#include "demo_data.h"
#include "ui/labels.h"
#include "ui/tools/tools_common.h"
#include "ui/tools/tools_settings.h"
#include "ui/ui_layout.h"
#include "util/log.h"

#include <math.h>
#include <raymath.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Demo Mode director: a scripted 15-phase camera/selection/layer sequence. */

/* -- Internal types -------------------------------------------------------- */

typedef enum
{
    DEMO_MOVE_ORBIT,
    DEMO_MOVE_DOLLY,
    DEMO_MOVE_PAN,
    DEMO_MOVE_TILT,
    DEMO_MOVE_ZOOM,
    DEMO_MOVE_FLYTHROUGH,
    DEMO_MOVE_HOLD
} DemoMoveType;

/* A camera pose key. The spherical fields keep a full 2*pi ORBIT sweep exact. */
typedef struct
{
    Vector3 position, target, up;
    float fovy;
    Vector2 map_target;
    float map_zoom;
    /* spherical camera parameters (3D) */
    float dist, angle_x, angle_y;
} DemoCameraKey;

typedef struct
{
    const char *name;
    float duration;
    DemoMoveType move;
    DemoCameraKey from, to;
    Vector3 waypoints[4];
    int waypoint_count;
    bool is_2d;
    uint32_t select_norad; /* 0 = none */
    bool hide_unselected;
    double time_multiplier;
    bool orbits, fav_orbits, sunlit_all, gc_all, gc_fav, labels_all, labels_fav, grid;
    bool apsides, coast, borders, follow_sat;
    bool slant;          /* home->satellite slant-range line (3D) */
    bool compute_passes; /* predict passes + select one (polar-plot hook) */
    int ease; /* 0 = cubic, 1 = sine, 2 = out-quint */
} DemoPhase;

/* -- Static state ---------------------------------------------------------- */

#define DEMO_PHASE_COUNT 15

/* altitude bands (km above the surface) for random satellite picks */
#define DEMO_BAND_LEO_MAX 2000.0
#define DEMO_BAND_MEO_MIN 15000.0
#define DEMO_BAND_MEO_MAX 30000.0

/* favourites showcase: how many satellites to star for a run */
#define DEMO_FAV_MIN 2
#define DEMO_FAV_MAX 6

/* fixed demo epoch: 2026 day 267 (matches the embedded dataset's epoch) */
#define DEMO_EPOCH 2026267.5

static bool s_active = false;
static bool s_start_requested = false;
static bool s_stop_requested = false;
static bool s_stopping = false;
static double s_elapsed = 0.0;
static int s_phase_index = -1;
static bool s_prev_is_2d = false;
static float s_fade = 0.0f;

static DemoPhase s_phases[DEMO_PHASE_COUNT];
static bool s_phases_built = false;

/* per-run RNG state, seed, and the randomly chosen favourites for this run */
static uint32_t s_rng_state = 0x9E3779B9u;
static uint32_t s_demo_seed = 0;
static uint32_t s_demo_fav_ids[DEMO_FAV_MAX];
static int s_demo_fav_count = 0;

/* critically-damped smoothing state (persists across phases) */
static Vector3 s_smooth_target = {0};
static Vector3 s_smooth_target_vel = {0};
static Vector3 s_smooth_pos = {0};
static Vector3 s_smooth_pos_vel = {0};
static Vector2 s_smooth_map_target = {0};
static Vector2 s_smooth_map_vel = {0};

/* -- Snapshot -------------------------------------------------------------- */

typedef struct
{
    /* cfg booleans */
    bool show_clouds, show_night_lights, show_markers, show_statistics;
    bool highlight_sunlit, show_slant_range, show_scattering, show_skybox;
    bool show_ground_coverage, show_apsides, show_earth_texture, show_latlon_grid;
    bool show_country_borders, show_coast_lines, night_mode;
    char theme[64];

    /* tool settings */
    bool orbits, orbits_dimmed, fav_orbits, labels_enabled;
    int sunlit_scope, gc_mode, labels_mode, grid_spacing;

    /* view state */
    bool is_2d_view, is_pov_mode, is_ecliptic_frame;
    double current_epoch, time_multiplier;
    uint32_t selected_norad;
    bool hide_unselected;

    /* antenna scope + pass selection (optional director-owned overlays) */
    bool show_scope;
    float scope_az, scope_el, scope_beam;
    int selected_pass_idx;

    /* camera */
    Camera3D camera3d;
    Camera2D camera2d;
    float cam_distance, cam_angle_x, cam_angle_y;
    float target_cam_distance, target_cam_angle_x, target_cam_angle_y;
    Vector3 target_camera3d_target;
    float target_camera2d_zoom;
    Vector2 target_camera2d_target;
    TargetLock active_lock;

    /* layout */
    bool left_visible, right_visible, left_hidden, right_hidden;
    bool show_bottom_bar, clean_view, settings_open, tools_open;

    /* active satellite set (by NORAD id) */
    uint32_t active_ids[MAX_SATELLITES];
    int active_count;

    /* favourites (by NORAD id) so the user's real set is restored on exit */
    uint32_t fav_ids[MAX_SATELLITES];
    int fav_count;

    /* user's real locations; restored on exit after the demo's marker swap */
    Location locations[MAX_LOCATIONS];
    int location_count;
} DemoSnapshot;

static DemoSnapshot s_snapshot;

/* -- Small helpers --------------------------------------------------------- */

static float Clamp01(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}

static float EaseInOutCubic(float t)
{
    return (t < 0.5f) ? (4.0f * t * t * t) : (1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f);
}

static float EaseInOutSine(float t)
{
    return -(cosf(PI * t) - 1.0f) * 0.5f;
}

static float EaseOutQuint(float t)
{
    return 1.0f - powf(1.0f - t, 5.0f);
}

static float EaseForPhase(const DemoPhase *ph, float t)
{
    switch (ph->ease)
    {
    case 1:
        return EaseInOutSine(t);
    case 2:
        return EaseOutQuint(t);
    default:
        return EaseInOutCubic(t);
    }
}

/** critically-damped smoothing (Game Programming Gems 4 / Unity SmoothDamp) */
static float SmoothDampF(float current, float target, float *vel, float smooth_time, float dt)
{
    if (smooth_time < 0.0001f)
        smooth_time = 0.0001f;
    float omega = 2.0f / smooth_time;
    float x = omega * dt;
    float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float temp = (*vel + omega * change) * dt;
    *vel = (*vel - omega * temp) * decay;
    return target + (change + temp) * decay;
}

static Vector3 SmoothDampV3(Vector3 current, Vector3 target, Vector3 *vel, float smooth_time, float dt)
{
    Vector3 r;
    r.x = SmoothDampF(current.x, target.x, &vel->x, smooth_time, dt);
    r.y = SmoothDampF(current.y, target.y, &vel->y, smooth_time, dt);
    r.z = SmoothDampF(current.z, target.z, &vel->z, smooth_time, dt);
    return r;
}

static Vector2 SmoothDampV2(Vector2 current, Vector2 target, Vector2 *vel, float smooth_time, float dt)
{
    Vector2 r;
    r.x = SmoothDampF(current.x, target.x, &vel->x, smooth_time, dt);
    r.y = SmoothDampF(current.y, target.y, &vel->y, smooth_time, dt);
    return r;
}

/** spherical offset matching main.cpp's camera model */
static Vector3 SphericalOffset(float dist, float angle_x, float angle_y)
{
    Vector3 o = {
        dist * cosf(angle_y) * sinf(angle_x),
        dist * sinf(angle_y),
        dist * cosf(angle_y) * cosf(angle_x)};
    return o;
}

/** Catmull-Rom spline through `n` points, t in [0,1] */
static Vector3 CatmullRomSpline(const Vector3 *pts, int n, float t)
{
    if (n <= 0)
        return (Vector3){0};
    if (n == 1)
        return pts[0];
    t = Clamp01(t);
    float seg = t * (float)(n - 1);
    int i = (int)seg;
    if (i >= n - 1)
        i = n - 2;
    float u = seg - (float)i;
    Vector3 p0 = pts[i > 0 ? i - 1 : 0];
    Vector3 p1 = pts[i];
    Vector3 p2 = pts[i + 1];
    Vector3 p3 = pts[i + 2 < n ? i + 2 : n - 1];
    float u2 = u * u;
    float u3 = u2 * u;
    Vector3 r;
    r.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * u +
                  (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * u2 +
                  (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * u3);
    r.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * u +
                  (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * u2 +
                  (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * u3);
    r.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * u +
                  (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * u2 +
                  (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * u3);
    return r;
}

/** resolve a satellite pointer by NORAD id (0 = none) */
static Satellite *ResolveSatByNorad(uint32_t norad)
{
    if (norad == 0)
        return NULL;
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].norad_id_num == norad)
            return &satellites[i];
    }
    return NULL;
}

/* -- Random selection ------------------------------------------------------ */

/** seed the RNG: TLESCOPE_DEMO_SEED (any base) if set, else the wall clock */
static uint32_t ResolveDemoSeed(void)
{
    const char *env = getenv("TLESCOPE_DEMO_SEED");
    if (env && env[0])
    {
        char *end = NULL;
        unsigned long v = strtoul(env, &end, 0);
        if (end && *end == '\0')
            return (uint32_t)v;
    }
    return (uint32_t)time(NULL);
}

/** xorshift32: tiny, fast, deterministic for a given seed */
static uint32_t RandU32(void)
{
    uint32_t x = s_rng_state;
    if (x == 0)
        x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x;
    return x;
}

/** inclusive integer range [lo, hi] */
static int RandRange(int lo, int hi)
{
    if (hi <= lo)
        return lo;
    return lo + (int)(RandU32() % (uint32_t)(hi - lo + 1));
}

/** true if the satellite's altitude (km above the surface) is within the band */
static bool SatAltitudeInBand(const Satellite *sat, double min_km, double max_km)
{
    if (!sat || sat->semi_major_axis <= 0.0)
        return false;
    double alt = sat->semi_major_axis - (double)EARTH_RADIUS_KM;
    return alt >= min_km && alt <= max_km;
}

/** pick the NORAD id of a random active satellite within an altitude band */
static uint32_t PickSatNoradInBand(double min_km, double max_km)
{
    int matches = 0;
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].is_active && SatAltitudeInBand(&satellites[i], min_km, max_km))
            matches++;
    }
    if (matches <= 0)
        return 0;

    int pick = RandRange(0, matches - 1);
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].is_active && SatAltitudeInBand(&satellites[i], min_km, max_km))
        {
            if (pick-- == 0)
                return satellites[i].norad_id_num;
        }
    }
    return 0;
}

/** like PickSatNoradInBand but avoids `avoid` when the band has alternatives */
static uint32_t PickSatNoradInBandExcept(double min_km, double max_km, uint32_t avoid)
{
    for (int tries = 0; tries < 8; tries++)
    {
        uint32_t norad = PickSatNoradInBand(min_km, max_km);
        if (norad != 0 && norad != avoid)
            return norad;
    }
    return PickSatNoradInBand(min_km, max_km);
}

/** pick any active satellite at random */
static uint32_t PickAnySatNorad(void)
{
    int matches = 0;
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].is_active)
            matches++;
    }
    if (matches <= 0)
        return 0;

    int pick = RandRange(0, matches - 1);
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].is_active)
        {
            if (pick-- == 0)
                return satellites[i].norad_id_num;
        }
    }
    return 0;
}

/** drop every favorite from the in-memory set */
static void ClearFavorites(void)
{
    uint32_t ids[MAX_SATELLITES];
    int n = GetFavoriteIds(ids, MAX_SATELLITES);
    for (int i = 0; i < n; i++)
        SetFavorite(ids[i], false);
}

/** star 2..6 distinct random satellites for the duration of the demo run */
static void SetupDemoFavorites(void)
{
    s_demo_fav_count = 0;
    int want = RandRange(DEMO_FAV_MIN, DEMO_FAV_MAX);
    for (int attempt = 0; attempt < want * 24 && s_demo_fav_count < want; attempt++)
    {
        uint32_t norad = PickAnySatNorad();
        if (norad == 0)
            break;

        bool dup = false;
        for (int i = 0; i < s_demo_fav_count; i++)
        {
            if (s_demo_fav_ids[i] == norad)
            {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;

        s_demo_fav_ids[s_demo_fav_count++] = norad;
        SetFavorite(norad, true);
    }
}

/** pick one of this run's random favourites (falls back to any satellite) */
static uint32_t PickDemoFavoriteNorad(void)
{
    if (s_demo_fav_count <= 0)
        return PickAnySatNorad();
    return s_demo_fav_ids[RandRange(0, s_demo_fav_count - 1)];
}

/* -- Phase table ----------------------------------------------------------- */

static DemoCameraKey Key3D(float dist, float ax, float ay, Vector3 target, float fovy)
{
    DemoCameraKey k;
    memset(&k, 0, sizeof(k));
    k.dist = dist;
    k.angle_x = ax;
    k.angle_y = ay;
    k.target = target;
    k.fovy = fovy;
    k.up = (Vector3){0.0f, 1.0f, 0.0f};
    return k;
}

static DemoCameraKey Key2D(Vector2 target, float zoom)
{
    DemoCameraKey k;
    memset(&k, 0, sizeof(k));
    k.map_target = target;
    k.map_zoom = zoom;
    k.fovy = 45.0f;
    k.up = (Vector3){0.0f, 1.0f, 0.0f};
    return k;
}

static void BuildPhases(void)
{
    if (s_phases_built)
        return;
    s_phases_built = true;

    const float TAU = 6.28318530718f;
    const Vector3 O = {0.0f, 0.0f, 0.0f};
    float ang = 0.0f; /* running azimuth so consecutive phases stay continuous */

    /* random satellites for this run (dataset is loaded before the rebuild) */
    const uint32_t leoA = PickSatNoradInBand(0.0, DEMO_BAND_LEO_MAX);
    const uint32_t leoB = PickSatNoradInBandExcept(0.0, DEMO_BAND_LEO_MAX, leoA);
    const uint32_t leoC = PickSatNoradInBandExcept(0.0, DEMO_BAND_LEO_MAX, leoB);
    const uint32_t meoA = PickSatNoradInBand(DEMO_BAND_MEO_MIN, DEMO_BAND_MEO_MAX);
    const uint32_t favA = PickDemoFavoriteNorad();

    /* 1 - Establishing shot: fast 3D sweep of the fully-dressed globe */
    {
        DemoPhase *p = &s_phases[0];
        memset(p, 0, sizeof(*p));
        p->name = "Establishing globe";
        p->duration = 6.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(14.0f, ang, 0.45f, O, 45.0f);
        p->to = Key3D(11.0f, ang + TAU, 0.45f, O, 45.0f);
        ang += TAU;
        p->select_norad = leoA;
        p->time_multiplier = 120.0;
        p->orbits = true;
        p->ease = 1;
    }

    /* 2 - Constellation panorama: favourite orbits, all labels, grid, coast */
    {
        DemoPhase *p = &s_phases[1];
        memset(p, 0, sizeof(*p));
        p->name = "Constellation panorama";
        p->duration = 7.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(11.0f, ang, 0.45f, O, 45.0f);
        p->to = Key3D(17.0f, ang + TAU, 0.35f, O, 45.0f);
        ang += TAU;
        p->time_multiplier = 300.0;
        p->orbits = true;
        p->fav_orbits = true;
        p->labels_all = true;
        p->grid = true;
        p->coast = true;
        p->borders = true;
        p->ease = 0;
    }

    /* 3 - Dive to LEO: isolate a random LEO satellite, show apsis markers */
    {
        DemoPhase *p = &s_phases[2];
        memset(p, 0, sizeof(*p));
        p->name = "Dive to LEO";
        p->duration = 7.0f;
        p->move = DEMO_MOVE_DOLLY;
        p->is_2d = false;
        p->from = Key3D(17.0f, ang, 0.35f, O, 45.0f);
        p->to = Key3D(3.5f, ang, 0.8f, O, 45.0f);
        p->select_norad = leoA;
        p->hide_unselected = true;
        p->time_multiplier = 150.0;
        p->orbits = true;
        p->apsides = true;
        p->follow_sat = true;
        p->ease = 2;
    }

    /* 4 - Orbit showcase: one fast lap with sunlit highlight and apsides */
    {
        DemoPhase *p = &s_phases[3];
        memset(p, 0, sizeof(*p));
        p->name = "Orbit showcase";
        p->duration = 8.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(3.5f, ang, 0.8f, O, 45.0f);
        p->to = Key3D(3.5f, ang + TAU, 0.8f, O, 45.0f);
        ang += TAU;
        p->select_norad = leoA;
        p->hide_unselected = true;
        p->time_multiplier = 600.0;
        p->orbits = true;
        p->sunlit_all = true;
        p->apsides = true;
        p->follow_sat = true;
        p->ease = 1;
    }

    /* 5 - GPS ascent: pull out to the MEO shell around a random GPS satellite */
    {
        DemoPhase *p = &s_phases[4];
        memset(p, 0, sizeof(*p));
        p->name = "GPS ascent (MEO)";
        p->duration = 8.0f;
        p->move = DEMO_MOVE_DOLLY;
        p->is_2d = false;
        p->from = Key3D(3.5f, ang, 0.8f, O, 45.0f);
        p->to = Key3D(12.0f, ang + 0.7f, 0.45f, O, 45.0f);
        ang += 0.7f;
        p->select_norad = meoA;
        p->time_multiplier = 200.0;
        p->orbits = true;
        p->apsides = true;
        p->ease = 0;
    }

    /* 6 - MEO shell: slow orbit at GPS constellation altitude */
    {
        DemoPhase *p = &s_phases[5];
        memset(p, 0, sizeof(*p));
        p->name = "MEO shell / GPS constellation";
        p->duration = 7.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(12.0f, ang, 0.45f, O, 45.0f);
        p->to = Key3D(13.0f, ang + TAU, 0.5f, O, 45.0f);
        ang += TAU;
        p->select_norad = meoA;
        p->time_multiplier = 400.0;
        p->orbits = true;
        p->fav_orbits = true;
        p->labels_all = true;
        p->grid = true;
        p->ease = 1;
    }

    /* 7 - Ground coverage: pull back to reveal the LOS footprint mesh */
    {
        DemoPhase *p = &s_phases[6];
        memset(p, 0, sizeof(*p));
        p->name = "Ground coverage mesh";
        p->duration = 7.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(13.0f, ang, 0.5f, O, 45.0f);
        p->to = Key3D(9.0f, ang + TAU, 0.55f, O, 45.0f);
        ang += TAU;
        p->select_norad = leoA;
        p->time_multiplier = 300.0;
        p->orbits = true;
        /* never the "all coverage zones" mode: favourites only */
        p->gc_fav = true;
        p->grid = true;
        p->ease = 0;
    }

    /* 8 - Slant range */
    {
        DemoPhase *p = &s_phases[7];
        memset(p, 0, sizeof(*p));
        p->name = "Slant range";
        p->duration = 6.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(9.0f, ang, 0.55f, O, 45.0f);
        p->to = Key3D(7.0f, ang + TAU, 0.5f, O, 45.0f);
        ang += TAU;
        p->select_norad = leoB;
        p->time_multiplier = 200.0;
        p->orbits = true;
        p->slant = true;
        p->ease = 0;
    }

    /* 9 - Pass prediction: compute passes and let the polar plot stamp AOS/LOS */
    {
        DemoPhase *p = &s_phases[8];
        memset(p, 0, sizeof(*p));
        p->name = "Pass prediction";
        p->duration = 7.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(7.0f, ang, 0.5f, O, 45.0f);
        p->to = Key3D(6.0f, ang + TAU, 0.7f, O, 45.0f);
        ang += TAU;
        p->select_norad = leoB;
        p->hide_unselected = true;
        p->time_multiplier = 180.0;
        p->orbits = true;
        p->apsides = true;
        p->compute_passes = true;
        p->follow_sat = true;
        p->ease = 1;
    }

    /* 10 - 2D map transition: snappy zoom onto the LEO satellite */
    {
        DemoPhase *p = &s_phases[9];
        memset(p, 0, sizeof(*p));
        p->name = "2D map transition";
        p->duration = 6.0f;
        p->move = DEMO_MOVE_ZOOM;
        p->is_2d = true;
        p->from = Key2D((Vector2){0.0f, 0.0f}, 1.0f);
        p->to = Key2D((Vector2){0.0f, 0.0f}, 3.0f);
        p->select_norad = leoB;
        p->hide_unselected = true;
        p->time_multiplier = 120.0;
        p->orbits = true;
        p->labels_all = true;
        p->grid = true;
        p->coast = true;
        p->borders = true;
        p->follow_sat = true;
        p->ease = 2;
    }

    /* 11 - 2D ground track flyover: chase the LEO satellite across the map */
    {
        DemoPhase *p = &s_phases[10];
        memset(p, 0, sizeof(*p));
        p->name = "2D ground track";
        p->duration = 6.0f;
        p->move = DEMO_MOVE_PAN;
        p->is_2d = true;
        p->from = Key2D((Vector2){0.0f, 0.0f}, 3.0f);
        p->to = Key2D((Vector2){0.0f, 0.0f}, 4.5f);
        p->select_norad = leoB;
        p->hide_unselected = true;
        p->time_multiplier = 600.0;
        p->orbits = true;
        p->labels_all = true;
        p->grid = true;
        p->coast = true;
        p->follow_sat = true;
        p->ease = 1;
    }

    /* 12 - Favourites showcase: 3D sweep with starred orbits/footprints/labels */
    {
        DemoPhase *p = &s_phases[11];
        memset(p, 0, sizeof(*p));
        p->name = "Favorites showcase";
        p->duration = 8.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(14.0f, ang, 0.4f, O, 45.0f);
        p->to = Key3D(11.0f, ang + TAU, 0.55f, O, 45.0f);
        ang += TAU;
        p->select_norad = favA;
        p->time_multiplier = 300.0;
        p->orbits = true;
        p->fav_orbits = true;
        p->labels_fav = true;
        p->gc_fav = true;
        p->grid = true;
        p->ease = 0;
    }

    /* 13 - Favourites ground tracks: 2D map chasing a favourite */
    {
        DemoPhase *p = &s_phases[12];
        memset(p, 0, sizeof(*p));
        p->name = "Favorites ground tracks";
        p->duration = 7.0f;
        p->move = DEMO_MOVE_PAN;
        p->is_2d = true;
        p->from = Key2D((Vector2){0.0f, 0.0f}, 2.0f);
        p->to = Key2D((Vector2){0.0f, 0.0f}, 3.5f);
        p->select_norad = favA;
        p->time_multiplier = 400.0;
        p->orbits = true;
        p->fav_orbits = true;
        p->labels_fav = true;
        p->gc_fav = true;
        p->grid = true;
        p->coast = true;
        p->follow_sat = true;
        p->ease = 1;
    }

    /* 14 - 3D flythrough: spline flyby of a random LEO satellite */
    {
        DemoPhase *p = &s_phases[13];
        memset(p, 0, sizeof(*p));
        p->name = "3D flythrough";
        p->duration = 7.0f;
        p->move = DEMO_MOVE_FLYTHROUGH;
        p->is_2d = false;
        p->from = Key3D(4.0f, ang, 0.5f, O, 45.0f);
        p->to = Key3D(4.0f, ang, 0.5f, O, 45.0f);
        p->waypoints[0] = (Vector3){0.0f, 1.5f, 5.0f};
        p->waypoints[1] = (Vector3){2.5f, 0.5f, 1.5f};
        p->waypoints[2] = (Vector3){-2.5f, -0.5f, -1.5f};
        p->waypoints[3] = (Vector3){0.0f, -1.5f, -5.0f};
        p->waypoint_count = 4;
        p->select_norad = leoC;
        p->hide_unselected = true;
        p->time_multiplier = 300.0;
        p->orbits = true;
        p->sunlit_all = true;
        p->apsides = true;
        p->follow_sat = true;
        p->ease = 0;
    }

    /* 15 - Grand finale: pull back to the phase-1 pose with every overlay on */
    {
        DemoPhase *p = &s_phases[14];
        memset(p, 0, sizeof(*p));
        p->name = "Grand finale";
        p->duration = 7.0f;
        p->move = DEMO_MOVE_ORBIT;
        p->is_2d = false;
        p->from = Key3D(4.0f, ang, 0.5f, O, 45.0f);
        p->to = Key3D(14.0f, ang + TAU, 0.45f, O, 45.0f);
        ang += TAU;
        p->time_multiplier = 400.0;
        p->orbits = true;
        p->fav_orbits = true;
        p->sunlit_all = true;
        /* never the "all coverage zones" mode: favourites only */
        p->gc_fav = true;
        p->labels_all = true;
        p->grid = true;
        p->apsides = true;
        p->coast = true;
        p->borders = true;
        p->ease = 0;
    }
}

/** rebuild the table on demo start to re-roll the random picks */
static void RebuildPhases(void)
{
    s_phases_built = false;
    BuildPhases();
}

/* -- Snapshot / restore ---------------------------------------------------- */

static void SnapshotState(DemoContext *ctx)
{
    AppConfig *cfg = ctx->cfg;
    DemoSnapshot *s = &s_snapshot;
    memset(s, 0, sizeof(*s));

    s->show_clouds = cfg->show_clouds;
    s->show_night_lights = cfg->show_night_lights;
    s->show_markers = cfg->show_markers;
    s->show_statistics = cfg->show_statistics;
    s->highlight_sunlit = cfg->highlight_sunlit;
    s->show_slant_range = cfg->show_slant_range;
    s->show_scattering = cfg->show_scattering;
    s->show_skybox = cfg->show_skybox;
    s->show_ground_coverage = cfg->show_ground_coverage;
    s->show_apsides = cfg->show_apsides;
    s->show_earth_texture = cfg->show_earth_texture;
    s->show_latlon_grid = cfg->show_latlon_grid;
    s->show_country_borders = cfg->show_country_borders;
    s->show_coast_lines = cfg->show_coast_lines;
    s->night_mode = cfg->night_mode;
    strncpy(s->theme, cfg->theme, sizeof(s->theme) - 1);
    s->theme[sizeof(s->theme) - 1] = '\0';

    s->orbits = ToolSettingGetBool(cfg, LAYERS_KEY_ORBITS, true);
    s->orbits_dimmed = ToolSettingGetBool(cfg, LAYERS_KEY_ORBITS_DIMMED, true);
    s->fav_orbits = ToolSettingGetBool(cfg, LAYERS_KEY_FAV_ORBITS_3D, false);
    s->sunlit_scope = ToolSettingGetInt(cfg, LAYERS_KEY_ORBITS_SUNLIT_SCOPE, LAYERS_ORBITS_SUNLIT_SELECTED);
    s->gc_mode = ToolSettingGetInt(cfg, LAYERS_KEY_GC_MODE, LAYERS_GC_MODE_SELECTED);
    s->labels_enabled = ToolSettingGetBool(cfg, LABELS_KEY_ENABLED, true);
    s->labels_mode = ToolSettingGetInt(cfg, LABELS_KEY_MODE, LABELS_MODE_SELECTED_ONLY);
    s->grid_spacing = ToolSettingGetInt(cfg, "layers.latlon_grid_spacing", 30);

    s->is_2d_view = *ctx->is_2d_view;
    s->is_pov_mode = *ctx->is_pov_mode;
    s->is_ecliptic_frame = *ctx->is_ecliptic_frame;
    s->current_epoch = *ctx->current_epoch;
    s->time_multiplier = *ctx->time_multiplier;
    s->selected_norad = (*ctx->selected_sat) ? (*ctx->selected_sat)->norad_id_num : 0;
    s->hide_unselected = *ctx->hide_unselected;

    if (ctx->show_scope)
    {
        s->show_scope = *ctx->show_scope;
        s->scope_az = *ctx->scope_az;
        s->scope_el = *ctx->scope_el;
        s->scope_beam = *ctx->scope_beam;
    }
    if (ctx->selected_pass_idx)
        s->selected_pass_idx = *ctx->selected_pass_idx;

    s->camera3d = *ctx->camera3d;
    s->camera2d = *ctx->camera2d;
    s->cam_distance = *ctx->cam_distance;
    s->cam_angle_x = *ctx->cam_angle_x;
    s->cam_angle_y = *ctx->cam_angle_y;
    s->target_cam_distance = *ctx->target_cam_distance;
    s->target_cam_angle_x = *ctx->target_cam_angle_x;
    s->target_cam_angle_y = *ctx->target_cam_angle_y;
    s->target_camera3d_target = *ctx->target_camera3d_target;
    s->target_camera2d_zoom = *ctx->target_camera2d_zoom;
    s->target_camera2d_target = *ctx->target_camera2d_target;
    s->active_lock = *ctx->active_lock;

    s->left_visible = g_layout.left_visible;
    s->right_visible = g_layout.right_visible;
    s->left_hidden = g_layout.left_hidden;
    s->right_hidden = g_layout.right_hidden;
    s->show_bottom_bar = g_layout.show_bottom_bar;
    s->clean_view = g_layout.clean_view;
    s->settings_open = g_layout.settings_open;
    s->tools_open = g_layout.tools_open;

    s->active_count = 0;
    for (int i = 0; i < sat_count && s->active_count < MAX_SATELLITES; i++)
    {
        if (satellites[i].is_active)
            s->active_ids[s->active_count++] = satellites[i].norad_id_num;
    }

    s->fav_count = GetFavoriteIds(s->fav_ids, MAX_SATELLITES);

    /* save the user's real locations so the demo's marker swap can be undone */
    s->location_count = location_count;
    if (s->location_count > 0)
        memcpy(s->locations, locations, sizeof(Location) * (size_t)s->location_count);
}

static void RestoreState(DemoContext *ctx)
{
    AppConfig *cfg = ctx->cfg;
    DemoSnapshot *s = &s_snapshot;

    cfg->show_clouds = s->show_clouds;
    cfg->show_night_lights = s->show_night_lights;
    cfg->show_markers = s->show_markers;
    cfg->show_statistics = s->show_statistics;
    cfg->highlight_sunlit = s->highlight_sunlit;
    cfg->show_slant_range = s->show_slant_range;
    cfg->show_scattering = s->show_scattering;
    cfg->show_skybox = s->show_skybox;
    cfg->show_ground_coverage = s->show_ground_coverage;
    cfg->show_apsides = s->show_apsides;
    cfg->show_earth_texture = s->show_earth_texture;
    cfg->show_latlon_grid = s->show_latlon_grid;
    cfg->show_country_borders = s->show_country_borders;
    cfg->show_coast_lines = s->show_coast_lines;
    cfg->night_mode = s->night_mode;
    strncpy(cfg->theme, s->theme, sizeof(cfg->theme) - 1);
    cfg->theme[sizeof(cfg->theme) - 1] = '\0';
    cfg->reload_theme = true;

    ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS, s->orbits);
    ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS_DIMMED, s->orbits_dimmed);
    ToolSettingSetBool(cfg, LAYERS_KEY_FAV_ORBITS_3D, s->fav_orbits);
    ToolSettingSetInt(cfg, LAYERS_KEY_ORBITS_SUNLIT_SCOPE, s->sunlit_scope);
    ToolSettingSetInt(cfg, LAYERS_KEY_GC_MODE, s->gc_mode);
    ToolSettingSetBool(cfg, LABELS_KEY_ENABLED, s->labels_enabled);
    ToolSettingSetInt(cfg, LABELS_KEY_MODE, s->labels_mode);
    ToolSettingSetInt(cfg, "layers.latlon_grid_spacing", s->grid_spacing);

    *ctx->is_2d_view = s->is_2d_view;
    *ctx->is_pov_mode = s->is_pov_mode;
    *ctx->is_ecliptic_frame = s->is_ecliptic_frame;
    *ctx->current_epoch = s->current_epoch;
    *ctx->time_multiplier = s->time_multiplier;
    *ctx->hide_unselected = s->hide_unselected;
    *ctx->unselected_fade = s->hide_unselected ? 0.0f : 1.0f;

    if (ctx->show_scope)
    {
        *ctx->show_scope = s->show_scope;
        *ctx->scope_az = s->scope_az;
        *ctx->scope_el = s->scope_el;
        *ctx->scope_beam = s->scope_beam;
    }
    if (ctx->selected_pass_idx)
        *ctx->selected_pass_idx = s->selected_pass_idx;

    *ctx->camera3d = s->camera3d;
    *ctx->camera2d = s->camera2d;
    *ctx->cam_distance = s->cam_distance;
    *ctx->cam_angle_x = s->cam_angle_x;
    *ctx->cam_angle_y = s->cam_angle_y;
    *ctx->target_cam_distance = s->target_cam_distance;
    *ctx->target_cam_angle_x = s->target_cam_angle_x;
    *ctx->target_cam_angle_y = s->target_cam_angle_y;
    *ctx->target_camera3d_target = s->target_camera3d_target;
    *ctx->target_camera2d_zoom = s->target_camera2d_zoom;
    *ctx->target_camera2d_target = s->target_camera2d_target;
    *ctx->active_lock = s->active_lock;

    g_layout.left_visible = s->left_visible;
    g_layout.right_visible = s->right_visible;
    g_layout.left_hidden = s->left_hidden;
    g_layout.right_hidden = s->right_hidden;
    g_layout.show_bottom_bar = s->show_bottom_bar;
    g_layout.clean_view = s->clean_view;
    g_layout.settings_open = s->settings_open;
    g_layout.tools_open = s->tools_open;

    /* drop demo passes: their sat pointers refer to the demo dataset */
    num_passes = 0;
    last_pass_calc_sat = NULL;

    /* reload the real dataset and re-apply the saved active set */
    load_orbital_data("data.json");
    load_manual_entries(cfg);
    for (int i = 0; i < sat_count; i++)
    {
        bool active = false;
        for (int k = 0; k < s->active_count; k++)
        {
            if (satellites[i].norad_id_num == s->active_ids[k])
            {
                active = true;
                break;
            }
        }
        satellites[i].is_active = active;
    }

    /* restore the user's real favourites (the demo temporarily replaced them) */
    ClearFavorites();
    for (int i = 0; i < s->fav_count; i++)
        SetFavorite(s->fav_ids[i], true);

    /* restore the user's real locations (nothing here is ever persisted) */
    location_count = s->location_count;
    if (location_count > 0)
        memcpy(locations, s->locations, sizeof(Location) * (size_t)location_count);

    /* re-resolve the saved selection by NORAD id */
    *ctx->selected_sat = ResolveSatByNorad(s->selected_norad);
}

/* -- Demo setup ------------------------------------------------------------ */

static void ApplyDemoSetup(DemoContext *ctx)
{
    AppConfig *cfg = ctx->cfg;

    /* force the default theme (reloaded by the main loop) */
    strncpy(cfg->theme, "default", sizeof(cfg->theme) - 1);
    cfg->theme[sizeof(cfg->theme) - 1] = '\0';
    cfg->reload_theme = true;

    /* "Aesthetic" graphics profile (mirrors ui.cpp's first-run button) */
    cfg->show_clouds = true;
    cfg->show_night_lights = true;
    cfg->show_scattering = true;
    cfg->show_skybox = true;
    cfg->night_mode = false;
    cfg->show_earth_texture = true;
    cfg->show_latlon_grid = false;
    cfg->show_country_borders = false;
    cfg->show_coast_lines = true;

    /* neutral demo defaults */
    cfg->show_markers = true;
    cfg->show_statistics = false;
    cfg->show_ground_coverage = false;
    cfg->show_apsides = false;
    cfg->highlight_sunlit = false;
    cfg->show_slant_range = false;

    /* demo-only markers: neutral set with Gliwice as home (restored on exit) */
    LoadDemoAerospaceMarkers();

    /* never enable the "all ground tracks" (unselected/dimmed) overlay */
    ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS_DIMMED, false);

    /* never enable the "all coverage zones" mode; snapshot restores it on exit */
    ToolSettingSetInt(cfg, LAYERS_KEY_GC_MODE, LAYERS_GC_MODE_SELECTED);

    /* load the embedded demo dataset and activate every satellite */
    load_orbital_data_from_string(DEMO_DATA_JSON);
    for (int i = 0; i < sat_count; i++)
        satellites[i].is_active = true;

    /* seed the RNG, star this run's favourites, then rebuild the phase table */
    s_demo_seed = ResolveDemoSeed();
    s_rng_state = (s_demo_seed != 0) ? s_demo_seed : 0x9E3779B9u;
    LOG_INFO("Demo mode: RNG seed=%u", (unsigned)s_demo_seed);

    ClearFavorites();
    SetupDemoFavorites();
    RebuildPhases();

    /* hide all chrome */
    g_layout.clean_view = true;
    g_layout.left_visible = false;
    g_layout.right_visible = false;
    g_layout.left_hidden = true;
    g_layout.right_hidden = true;
    g_layout.show_bottom_bar = false;
    g_layout.settings_open = false;
    g_layout.tools_open = false;

    /* view state */
    *ctx->is_2d_view = false;
    *ctx->is_pov_mode = false;
    *ctx->is_ecliptic_frame = false;
    *ctx->hide_unselected = false;
    *ctx->unselected_fade = 1.0f;
    *ctx->selected_sat = NULL;
    if (ctx->hovered_sat)
        *ctx->hovered_sat = NULL;
    *ctx->active_lock = LOCK_NONE;

    /* the demo never showcases the unfinished 3D scope beam */
    if (ctx->show_scope)
        *ctx->show_scope = false;
    if (ctx->selected_pass_idx)
        *ctx->selected_pass_idx = -1;

    /* fixed demo epoch */
    *ctx->current_epoch = DEMO_EPOCH;
    *ctx->time_multiplier = 1.0;

    /* reset smoothing state */
    s_smooth_target = (Vector3){0.0f, 0.0f, 0.0f};
    s_smooth_target_vel = (Vector3){0.0f, 0.0f, 0.0f};
    s_smooth_pos = (Vector3){0.0f, 0.0f, 0.0f};
    s_smooth_pos_vel = (Vector3){0.0f, 0.0f, 0.0f};
    s_smooth_map_target = (Vector2){0.0f, 0.0f};
    s_smooth_map_vel = (Vector2){0.0f, 0.0f};
}

static void ApplyPhaseSetup(DemoContext *ctx, const DemoPhase *ph)
{
    AppConfig *cfg = ctx->cfg;

    *ctx->is_2d_view = ph->is_2d;

    ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS, ph->orbits);
    /* the demo never enables the "all ground tracks" (unselected/dimmed) overlay */
    ToolSettingSetBool(cfg, LAYERS_KEY_ORBITS_DIMMED, false);
    ToolSettingSetBool(cfg, LAYERS_KEY_FAV_ORBITS_3D, ph->fav_orbits);
    ToolSettingSetInt(cfg, LAYERS_KEY_ORBITS_SUNLIT_SCOPE,
                      ph->sunlit_all ? LAYERS_ORBITS_SUNLIT_ALL : LAYERS_ORBITS_SUNLIT_SELECTED);
    /* downgrade any gc_all request to the favourites scope (ALL is never used) */
    ToolSettingSetInt(cfg, LAYERS_KEY_GC_MODE,
                      (ph->gc_all || ph->gc_fav) ? LAYERS_GC_MODE_FAV : LAYERS_GC_MODE_SELECTED);
    ToolSettingSetBool(cfg, LABELS_KEY_ENABLED, true);
    ToolSettingSetInt(cfg, LABELS_KEY_MODE,
                      ph->labels_all ? LABELS_MODE_ALL
                                     : (ph->labels_fav ? LABELS_MODE_FAV : LABELS_MODE_SELECTED_ONLY));

    cfg->show_ground_coverage = ph->gc_all || ph->gc_fav;
    cfg->show_apsides = ph->apsides;
    cfg->show_latlon_grid = ph->grid;
    cfg->show_coast_lines = ph->coast;
    cfg->show_country_borders = ph->borders;
    cfg->highlight_sunlit = ph->sunlit_all;
    cfg->show_slant_range = ph->slant;
}

/* -- Camera application ---------------------------------------------------- */

static void ApplyCamera(DemoContext *ctx, const DemoPhase *ph, float t, float dt)
{
    /* neutralise the main loop's Earth/Moon target lock */
    *ctx->active_lock = LOCK_NONE;
    if (ctx->hovered_sat)
        *ctx->hovered_sat = NULL;

    if (ph->is_2d)
    {
        Vector2 desired_target = ph->from.map_target;
        if (ph->follow_sat)
        {
            Satellite *sel = ResolveSatByNorad(ph->select_norad);
            if (sel)
            {
                double gmst = epoch_to_gmst(*ctx->current_epoch);
                float mx = 0.0f, my = 0.0f;
                get_map_coordinates(sel->current_pos, gmst, ctx->cfg->earth_rotation_offset,
                                    ctx->map_w, ctx->map_h, &mx, &my);
                desired_target = (Vector2){mx, my};
            }
        }
        else
        {
            desired_target = Vector2Lerp(ph->from.map_target, ph->to.map_target, t);
        }

        float desired_zoom = ph->from.map_zoom + (ph->to.map_zoom - ph->from.map_zoom) * t;

        s_smooth_map_target = SmoothDampV2(s_smooth_map_target, desired_target, &s_smooth_map_vel, 0.16f, dt);

        ctx->camera2d->target = s_smooth_map_target;
        ctx->camera2d->zoom = desired_zoom;
        *ctx->target_camera2d_zoom = desired_zoom;
        *ctx->target_camera2d_target = s_smooth_map_target;
        return;
    }

    /* desired look-at target */
    Vector3 desired_target;
    if (ph->follow_sat)
    {
        Satellite *sel = ResolveSatByNorad(ph->select_norad);
        desired_target = sel ? Vector3Scale(sel->current_pos, 1.0f / DRAW_SCALE) : ph->from.target;
    }
    else
    {
        desired_target = Vector3Lerp(ph->from.target, ph->to.target, t);
    }
    s_smooth_target = SmoothDampV3(s_smooth_target, desired_target, &s_smooth_target_vel, 0.18f, dt);

    /* desired camera position */
    Vector3 desired_pos;
    float dist, ax, ay, fovy;

    if (ph->move == DEMO_MOVE_FLYTHROUGH && ph->waypoint_count > 0)
    {
        Vector3 wp[4];
        for (int i = 0; i < ph->waypoint_count; i++)
            wp[i] = Vector3Add(desired_target, ph->waypoints[i]);
        desired_pos = CatmullRomSpline(wp, ph->waypoint_count, t);

        Vector3 off = Vector3Subtract(desired_pos, s_smooth_target);
        dist = Vector3Length(off);
        if (dist < 0.0001f)
            dist = 0.0001f;
        ax = atan2f(off.x, off.z);
        float sin_ay = off.y / dist;
        if (sin_ay < -1.0f)
            sin_ay = -1.0f;
        if (sin_ay > 1.0f)
            sin_ay = 1.0f;
        ay = asinf(sin_ay);
        fovy = ph->from.fovy + (ph->to.fovy - ph->from.fovy) * t;
    }
    else if (ph->move == DEMO_MOVE_HOLD)
    {
        dist = ph->from.dist;
        ax = ph->from.angle_x;
        ay = ph->from.angle_y;
        fovy = ph->from.fovy;
        desired_pos = Vector3Add(s_smooth_target, SphericalOffset(dist, ax, ay));
    }
    else
    {
        dist = ph->from.dist + (ph->to.dist - ph->from.dist) * t;
        ax = ph->from.angle_x + (ph->to.angle_x - ph->from.angle_x) * t;
        ay = ph->from.angle_y + (ph->to.angle_y - ph->from.angle_y) * t;
        fovy = ph->from.fovy + (ph->to.fovy - ph->from.fovy) * t;
        desired_pos = Vector3Add(s_smooth_target, SphericalOffset(dist, ax, ay));
    }

    s_smooth_pos = SmoothDampV3(s_smooth_pos, desired_pos, &s_smooth_pos_vel, 0.14f, dt);

    ctx->camera3d->position = s_smooth_pos;
    ctx->camera3d->target = s_smooth_target;
    ctx->camera3d->up = (Vector3){0.0f, 1.0f, 0.0f};
    ctx->camera3d->fovy = fovy;

    /* keep the legacy spherical params sane for any residual reader */
    *ctx->cam_distance = dist;
    *ctx->cam_angle_x = ax;
    *ctx->cam_angle_y = ay;
    *ctx->target_cam_distance = dist;
    *ctx->target_cam_angle_x = ax;
    *ctx->target_cam_angle_y = ay;
    *ctx->target_camera3d_target = s_smooth_target;
}

/* -- Public API ------------------------------------------------------------ */

void DemoDirectorInit(void)
{
    BuildPhases();
}

bool DemoDirectorActive(void)
{
    return s_active;
}

void DemoDirectorRequestStart(void)
{
    s_start_requested = true;
}

void DemoDirectorRequestStop(void)
{
    s_stop_requested = true;
}

void DemoDirectorUpdate(DemoContext *ctx, float dt)
{
    if (!ctx)
        return;

    BuildPhases();

    /* deferred start */
    if (s_start_requested)
    {
        s_start_requested = false;
        if (!s_active)
        {
            SnapshotState(ctx);
            ApplyDemoSetup(ctx);
            s_active = true;
            s_stopping = false;
            s_elapsed = 0.0;
            s_phase_index = -1;
            s_prev_is_2d = false;
            s_fade = 0.0f;
        }
    }

    /* deferred stop (fade out, then restore) */
    if (s_stop_requested)
    {
        s_stop_requested = false;
        if (s_active)
            s_stopping = true;
    }

    if (!s_active)
        return;

    if (s_stopping)
    {
        s_fade -= dt * 5.0f;
        if (s_fade <= 0.0f)
        {
            s_fade = 0.0f;
            RestoreState(ctx);
            s_active = false;
            s_stopping = false;
            return;
        }
    }
    else
    {
        s_fade += dt * 3.0f;
        if (s_fade > 1.0f)
            s_fade = 1.0f;
    }

    /* advance the timeline (loops seamlessly) */
    s_elapsed += dt;

    double total = 0.0;
    for (int i = 0; i < DEMO_PHASE_COUNT; i++)
        total += s_phases[i].duration;
    if (total <= 0.0)
        total = 1.0;

    double e = fmod(s_elapsed, total);
    if (e < 0.0)
        e += total;

    int idx = DEMO_PHASE_COUNT - 1;
    double acc = 0.0;
    for (int i = 0; i < DEMO_PHASE_COUNT; i++)
    {
        if (e < acc + s_phases[i].duration)
        {
            idx = i;
            break;
        }
        acc += s_phases[i].duration;
    }

    double t_in = (e - acc) / s_phases[idx].duration;
    float t = Clamp01((float)t_in);

    if (idx != s_phase_index)
    {
        bool first = (s_phase_index < 0);
        s_phase_index = idx;
        ApplyPhaseSetup(ctx, &s_phases[idx]);

        /* only the phase that asks for it keeps a pass selected for AOS/LOS */
        if (ctx->selected_pass_idx)
        {
            *ctx->selected_pass_idx = -1;
            if (s_phases[idx].compute_passes)
            {
                Satellite *sel = ResolveSatByNorad(s_phases[idx].select_norad);
                if (sel)
                {
                    CalculatePasses(sel, *ctx->current_epoch);
                    *ctx->selected_pass_idx = (num_passes > 0) ? 0 : -1;
                }
            }
        }

        /* snap the smoothing state so the 2D<->3D cut does not swing */
        if (first || s_phases[idx].is_2d != s_prev_is_2d)
        {
            if (s_phases[idx].is_2d)
            {
                s_smooth_map_target = s_phases[idx].from.map_target;
                s_smooth_map_vel = (Vector2){0.0f, 0.0f};
            }
            else
            {
                s_smooth_target = s_phases[idx].from.target;
                s_smooth_target_vel = (Vector3){0.0f, 0.0f, 0.0f};
                s_smooth_pos = Vector3Add(s_phases[idx].from.target,
                                          SphericalOffset(s_phases[idx].from.dist,
                                                          s_phases[idx].from.angle_x,
                                                          s_phases[idx].from.angle_y));
                s_smooth_pos_vel = (Vector3){0.0f, 0.0f, 0.0f};
            }
        }
        s_prev_is_2d = s_phases[idx].is_2d;
    }

    const DemoPhase *ph = &s_phases[idx];
    float et = EaseForPhase(ph, t);
    ApplyCamera(ctx, ph, et, dt);

    /* resolve selection by NORAD id every frame */
    *ctx->selected_sat = ResolveSatByNorad(ph->select_norad);

    /* drive simulation time */
    *ctx->time_multiplier = ph->time_multiplier;

    /* selection isolation + fade */
    *ctx->hide_unselected = ph->hide_unselected;
    float target_fade = ph->hide_unselected ? 0.0f : 1.0f;
    float rate = 6.0f * dt;
    if (*ctx->unselected_fade < target_fade)
    {
        *ctx->unselected_fade += rate;
        if (*ctx->unselected_fade > target_fade)
            *ctx->unselected_fade = target_fade;
    }
    else if (*ctx->unselected_fade > target_fade)
    {
        *ctx->unselected_fade -= rate;
        if (*ctx->unselected_fade < target_fade)
            *ctx->unselected_fade = target_fade;
    }
}

void DemoDirectorDrawOverlay(Texture2D logo, Font font, AppConfig *cfg)
{
    if (!s_active)
        return;

    float fade = Clamp01(s_fade);
    if (fade <= 0.0f)
        return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float scale = cfg ? cfg->ui_scale : 1.0f;
    if (scale <= 0.0f)
        scale = 1.0f;

    /* letterbox bars; the top bar is taller to fit the branding banner */
    float bar = (float)sh * 0.055f * fade;
    float top_bar = (float)sh * 0.10f * fade;
    if (bar > 0.5f)
    {
        DrawRectangle(0, 0, sw, (int)top_bar, Fade(BLACK, 0.85f * fade));
        DrawRectangle(0, sh - (int)bar, sw, (int)bar, Fade(BLACK, 0.85f * fade));
    }

    /* top-left: exit hint with a 1px shadow */
    const char *hint = "Press Esc to exit demo mode";
    float hint_size = 16.0f * scale;
    float hint_x = 20.0f * scale;
    float hint_y = 20.0f * scale;
    DrawUIText(font, hint, hint_x + 1.0f, hint_y + 1.0f, hint_size, Fade(BLACK, 0.6f * fade));
    DrawUIText(font, hint, hint_x, hint_y, hint_size, Fade(g_theme.ui.text_dim, fade));

    /* top-center: branding banner above the cinematic crop bar */
    if (logo.id != 0)
    {
        float logo_h = 44.0f * scale;
        float logo_scale = logo_h / (float)logo.height;
        float lw = (float)logo.width * logo_scale;
        float lh = (float)logo.height * logo_scale;

        const char *word = "TLEscope";
        float word_size = 34.0f * scale;
        Vector2 word_m = MeasureTextEx(font, word, word_size, 1.0f);

        const char *gh = "available on GitHub";
        float gh_size = 18.0f * scale;
        Vector2 gh_m = MeasureTextEx(font, gh, gh_size, 1.0f);

        float gap = 14.0f * scale;
        float text_w = (word_m.x > gh_m.x) ? word_m.x : gh_m.x;
        float text_h = word_m.y + gh_m.y + 2.0f * scale;
        float group_w = lw + gap + text_w;
        float group_h = (lh > text_h) ? lh : text_h;

        float gx = ((float)sw - group_w) * 0.5f;
        float gy = (top_bar - group_h) * 0.5f;
        if (gy < 4.0f * scale)
            gy = 4.0f * scale;

        DrawTextureEx(logo, (Vector2){gx, gy + (group_h - lh) * 0.5f}, 0.0f, logo_scale,
                      Fade(WHITE, 0.9f * fade));

        float tx = gx + lw + gap;
        float ty = gy + (group_h - text_h) * 0.5f;
        DrawUIText(font, word, tx + 1.0f, ty + 1.0f, word_size, Fade(BLACK, 0.6f * fade));
        DrawUIText(font, word, tx, ty, word_size, Fade(g_theme.ui.text, 0.95f * fade));
        DrawUIText(font, gh, tx + 1.0f, ty + word_m.y + 2.0f * scale + 1.0f, gh_size,
                   Fade(BLACK, 0.6f * fade));
        DrawUIText(font, gh, tx, ty + word_m.y + 2.0f * scale, gh_size,
                   Fade(g_theme.ui.text_dim, 0.9f * fade));
    }
}
