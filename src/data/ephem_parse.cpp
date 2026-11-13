/**
 * @file ephem_parse.cpp
 * @brief Parser for JPL Horizons VECTORS (CSV) ephemeris exports
 *
 * Pipeline (in order):
 *   1. locate the $$SOE/$$EOE block (report Horizons diagnostics when missing)
 *   2. reject ecliptic exports (REF_PLANE='ECLIPTIC') and non-Earth centres
 *   3. parse each data row (CSV, with a plain-text fallback)
 *   4. JDTDB -> unix seconds, TDB -> UTC with a leap-second-aware offset
 *   5. unit normalisation (km-s / au-d heuristic)
 *   6. axis remap (x, y, z) -> (x, z, -y)
 *   7. sort, drop duplicate/non-monotonic rows, decimate to EPHEM_MAX_SAMPLES
 *   8. Douglas-Peucker simplification on position (1 km tolerance)
 *   9. per-sample lat/lon/gmst/sunlit precomputation
 *
 * The parser is pure computation: it has no UI and no drawing dependencies.
 * The only external helpers it uses are the project's own ephemeris maths in
 * src/core/astro.cpp (epoch_to_gmst, calculate_sun_position, is_sat_eclipsed),
 * so the app frame and shadow model stay single-sourced.
 */

#include "data/ephem_parse.h"

#include "core/astro.h"
#include "core/astro_constants.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* -- constants -------------------------------------------------------------- */

/* JD of 1970-01-01T00:00:00 (unix epoch) */
static const double JD_UNIX_EPOCH = 2440587.5;
static const double SECONDS_PER_DAY = 86400.0;
static const double AU_KM = 149597870.700;
static const double SIMPLIFY_TOL_KM = 1.0;

/* minimum accepted data rows after cleaning */
#define EPHEM_MIN_SAMPLES 2

/* -- error helper ----------------------------------------------------------- */

static void set_err(char *err, size_t err_sz, const char *fmt, ...)
{
    if (!err || err_sz == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_sz, fmt, ap);
    va_end(ap);
    err[err_sz - 1] = '\0';
}

/* -- small string helpers --------------------------------------------------- */

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static std::string trim(const std::string &s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && is_space(s[a]))
        ++a;
    while (b > a && is_space(s[b - 1]))
        --b;
    return s.substr(a, b - a);
}

static std::string upper(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = (char)toupper((unsigned char)s[i]);
    return s;
}

/* parse a whole trimmed token as a double; rejects blanks and '*' filler */
static bool parse_num(const std::string &raw, double *out)
{
    std::string t = trim(raw);
    if (t.empty())
        return false;
    if (t[0] == '*')
        return false;
    const char *c = t.c_str();
    char *end = NULL;
    double v = strtod(c, &end);
    if (end == c)
        return false;
    /* allow a trailing '*' filler column marker only */
    while (*end && is_space(*end))
        ++end;
    if (*end != '\0')
        return false;
    *out = v;
    return true;
}

/* the value part of a "Key : value" header line, trimmed and cut at '{' */
static std::string header_value(const std::string &text, const char *key)
{
    std::string k(key);
    size_t pos = text.find(k);
    if (pos == std::string::npos)
        return std::string();
    pos += k.size();
    size_t eol = text.find('\n', pos);
    std::string v = text.substr(pos, (eol == std::string::npos) ? std::string::npos : eol - pos);
    size_t brace = v.find('{');
    if (brace != std::string::npos)
        v = v.substr(0, brace);
    return trim(v);
}

/* -- time helpers ----------------------------------------------------------- */

/* TAI-UTC (delta AT) in seconds from the IERS leap-second table. */
struct LeapEntry
{
    long long jd; /* start of the day the offset became valid */
    int dat;      /* TAI - UTC, seconds */
};

static const LeapEntry LEAPS[] = {
    { 2441317LL, 10 }, /* 1972-01-01 */
    { 2441499LL, 11 }, /* 1972-07-01 */
    { 2441683LL, 12 }, /* 1973-01-01 */
    { 2442048LL, 13 }, /* 1974-01-01 */
    { 2442413LL, 14 }, /* 1975-01-01 */
    { 2442778LL, 15 }, /* 1976-01-01 */
    { 2443144LL, 16 }, /* 1977-01-01 */
    { 2443509LL, 17 }, /* 1978-01-01 */
    { 2443874LL, 18 }, /* 1979-01-01 */
    { 2444239LL, 19 }, /* 1980-01-01 */
    { 2444786LL, 20 }, /* 1981-07-01 */
    { 2445151LL, 21 }, /* 1982-07-01 */
    { 2445516LL, 22 }, /* 1983-07-01 */
    { 2446247LL, 23 }, /* 1985-07-01 */
    { 2447161LL, 24 }, /* 1988-01-01 */
    { 2447892LL, 25 }, /* 1990-01-01 */
    { 2448257LL, 26 }, /* 1991-01-01 */
    { 2448804LL, 27 }, /* 1992-07-01 */
    { 2449169LL, 28 }, /* 1993-07-01 */
    { 2449534LL, 29 }, /* 1994-07-01 */
    { 2450083LL, 30 }, /* 1996-01-01 */
    { 2450630LL, 31 }, /* 1997-07-01 */
    { 2451179LL, 32 }, /* 1999-01-01 */
    { 2453736LL, 33 }, /* 2006-01-01 */
    { 2454832LL, 34 }, /* 2009-01-01 */
    { 2456109LL, 35 }, /* 2012-07-01 */
    { 2457204LL, 36 }, /* 2015-07-01 */
    { 2457754LL, 37 }, /* 2017-01-01 */
};

/* TDB -> UTC offset in seconds (approximately TT-UTC; TDB-TT < 2 ms).
 * For epochs after 2017-01-01 this is 32.184 + 37 = 69.184 s.
 * Before 1972 the leap-second table does not exist; we clamp to the first
 * entry (10 s), giving an error of a few seconds at worst for very old data. */
static double tdb_minus_utc_seconds(double jd)
{
    long long ijd = (long long)floor(jd);
    int dat = LEAPS[0].dat;
    for (size_t i = 0; i < sizeof(LEAPS) / sizeof(LEAPS[0]); ++i)
    {
        if (ijd >= LEAPS[i].jd)
            dat = LEAPS[i].dat;
        else
            break;
    }
    return 32.184 + (double)dat;
}

/* days since 1970-01-01 -> civil date (Howard Hinnant's algorithm) */
static void civil_from_days(long long z, int *y, int *m, int *d)
{
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned long long doe = (unsigned long long)(z - era * 146097);
    const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long yy = (long long)yoe + era * 400;
    const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned long long mp = (5 * doy + 2) / 153;
    const unsigned long long dd = doy - (153 * mp + 2) / 5 + 1;
    const unsigned long long mm = (mp < 10) ? (mp + 3) : (mp - 9);
    *y = (int)(yy + (mm <= 2 ? 1 : 0));
    *m = (int)mm;
    *d = (int)dd;
}

/* days since 1970-01-01 for a civil date */
static long long days_from_civil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

/* unix seconds -> the app's year*1000 + day-of-year epoch format.
 * This mirrors get_unix_from_epoch()'s definition so that astro.cpp's
 * epoch_to_gmst() can be reused unchanged. */
static double epoch_from_unix(double t_unix)
{
    const double days_f = floor(t_unix / SECONDS_PER_DAY);
    const long long days = (long long)days_f;
    const double frac = t_unix / SECONDS_PER_DAY - days_f; /* [0,1) */

    int y, m, d;
    civil_from_days(days, &y, &m, &d);

    const long long jan1 = days_from_civil(y, 1, 1);
    const double doy = (double)(days - jan1) + 1.0; /* 1-based, fractional */

    return (double)y * 1000.0 + doy + frac;
}

/* -- record parsing --------------------------------------------------------- */

typedef struct
{
    double jd;
    double x, y, z;
    double vx, vy, vz;
    int ncomp; /* number of velocity components actually present (0 or 3) */
} RawRow;

/* split a CSV row on commas */
static void split_csv(const std::string &line, std::vector<std::string> *out)
{
    out->clear();
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i)
    {
        if (i == line.size() || line[i] == ',')
        {
            out->push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
}

/* Parse one CSV data row. Returns false when the row has no usable state. */
static bool parse_csv_row(const std::string &line, RawRow *r)
{
    std::vector<std::string> f;
    split_csv(line, &f);
    if (f.size() < 4) /* JD, date, X, Y at minimum */
        return false;

    double jd = 0.0;
    if (!parse_num(f[0], &jd) || jd <= 0.0)
        return false;

    /* f[1] is the calendar date string; never parsed (JD is authoritative) */
    double nums[6];
    int n = 0;
    for (size_t i = 2; i < f.size() && n < 6; ++i)
    {
        double v;
        if (parse_num(f[i], &v))
            nums[n++] = v;
        /* blank / '*' filler columns are silently skipped */
    }
    if (n < 3)
        return false;

    r->jd = jd;
    r->x = nums[0];
    r->y = nums[1];
    r->z = nums[2];
    if (n >= 6)
    {
        r->vx = nums[3];
        r->vy = nums[4];
        r->vz = nums[5];
        r->ncomp = 3;
    }
    else
    {
        r->vx = r->vy = r->vz = 0.0;
        r->ncomp = 0;
    }
    return true;
}

/* Parse the plain-text (non-CSV) vector block. Returns false when the line is
 * not a record start. The record's X/Y/Z fields are left zero.
 *
 *  2459899.916666667 = A.D. 2022-Nov-16 10:00:00.0000 TDB
 *  X =-2.92E+04 Y = 9.67E+03 Z = 8.74E+03
 *  VX=-4.74E+00 VY=-5.96E-01 VZ= 1.63E-01
 */
static bool parse_block_start(const std::string &trimmed, RawRow *r)
{
    /* a record starts with a numeric JD followed (anywhere) by '=' */
    if (trimmed.empty() || !(isdigit((unsigned char)trimmed[0]) || trimmed[0] == '.'))
        return false;
    size_t eq = trimmed.find('=');
    if (eq == std::string::npos)
        return false;

    std::string jd_str = trim(trimmed.substr(0, eq));
    double jd;
    if (!parse_num(jd_str, &jd) || jd <= 0.0)
        return false;

    r->jd = jd;
    r->x = r->y = r->z = r->vx = r->vy = r->vz = 0.0;
    r->ncomp = 0;
    return true;
}

/* Apply one whitespace-separated token of a block line to a record.
 *
 * Horizons emits "X =-1.2E+03", "VX=-4.7E+00" or "X = 1.2E+03"; dropping '='
 * and pairing each label token with the following numeric token handles all
 * three shapes. Tokens are trimmed first. */
static void block_apply_token(const std::string &raw_tok, RawRow *r, std::string *pending_label,
                              bool *has_pos, bool *has_vel)
{
    const std::string t = trim(raw_tok);
    if (t.empty())
        return;

    /* split on '=' so "VX=-1.2" becomes label "VX" + value "-1.2" */
    size_t eq = t.find('=');
    std::string label;
    std::string valuestr;
    if (eq != std::string::npos)
    {
        label = upper(trim(t.substr(0, eq)));
        valuestr = trim(t.substr(eq + 1));
    }

    if (eq == std::string::npos)
    {
        /* bare label ("X") or bare number ("-1.2E+03") */
        if (!t.empty() && isalpha((unsigned char)t[0]))
        {
            *pending_label = upper(t);
            return;
        }
        if (!pending_label->empty())
            valuestr = t;
        else
            return;
    }
    else if (!label.empty())
    {
        *pending_label = label;
    }

    if (pending_label->empty())
        return;

    if (valuestr.empty())
        return; /* value arrives as the next token */

    double v;
    if (!parse_num(valuestr, &v))
        return;

    const std::string &lb = *pending_label;
    if (lb == "X")
    {
        r->x = v;
        *has_pos = true;
    }
    else if (lb == "Y")
    {
        r->y = v;
        *has_pos = true;
    }
    else if (lb == "Z")
    {
        r->z = v;
        *has_pos = true;
    }
    else if (lb == "VX")
    {
        r->vx = v;
        *has_vel = true;
    }
    else if (lb == "VY")
    {
        r->vy = v;
        *has_vel = true;
    }
    else if (lb == "VZ")
    {
        r->vz = v;
        *has_vel = true;
    }
}

/* -- simplification --------------------------------------------------------- */

/* squared distance from p to segment a-b, in km^2 */
static double point_seg_dist2(const Vector3 &p, const Vector3 &a, const Vector3 &b)
{
    const double ax = a.x, ay = a.y, az = a.z;
    const double dx = (double)b.x - ax;
    const double dy = (double)b.y - ay;
    const double dz = (double)b.z - az;
    const double len2 = dx * dx + dy * dy + dz * dz;

    double t = 0.0;
    if (len2 > 0.0)
    {
        t = (((double)p.x - ax) * dx + ((double)p.y - ay) * dy + ((double)p.z - az) * dz) / len2;
        if (t < 0.0)
            t = 0.0;
        else if (t > 1.0)
            t = 1.0;
    }
    const double cx = ax + dx * t;
    const double cy = ay + dy * t;
    const double cz = az + dz * t;
    const double ex = (double)p.x - cx;
    const double ey = (double)p.y - cy;
    const double ez = (double)p.z - cz;
    return ex * ex + ey * ey + ez * ez;
}

/* Douglas-Peucker on position, endpoints always kept.
 * Iterative to avoid deep recursion on pathological input. */
static void simplify_dp(const std::vector<EphemSample> &in, double tol_km, std::vector<EphemSample> *out)
{
    const int n = (int)in.size();
    out->clear();
    if (n <= 2)
    {
        *out = in;
        return;
    }

    std::vector<char> keep((size_t)n, 0);
    keep.front() = 1;
    keep.back() = 1;

    std::vector<std::pair<int, int> > stack;
    stack.push_back(std::make_pair(0, n - 1));
    const double tol2 = tol_km * tol_km;

    while (!stack.empty())
    {
        const int a = stack.back().first;
        const int b = stack.back().second;
        stack.pop_back();
        if (b <= a + 1)
            continue;

        double worst = -1.0;
        int worst_i = -1;
        for (int i = a + 1; i < b; ++i)
        {
            const double d2 = point_seg_dist2(in[(size_t)i].p, in[(size_t)a].p, in[(size_t)b].p);
            if (d2 > worst)
            {
                worst = d2;
                worst_i = i;
            }
        }

        if (worst > tol2 && worst_i > a)
        {
            keep[(size_t)worst_i] = 1;
            stack.push_back(std::make_pair(a, worst_i));
            stack.push_back(std::make_pair(worst_i, b));
        }
    }

    out->reserve((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        if (keep[(size_t)i])
            out->push_back(in[(size_t)i]);
    }
}

/* -- main entry point ------------------------------------------------------- */

bool EphemParseHorizons(const char *text, size_t len, EphemMission *out, char *err, size_t err_sz)
{
    if (err && err_sz)
        err[0] = '\0';
    if (!out)
    {
        set_err(err, err_sz, "internal error: null output mission");
        return false;
    }
    if (!text || len == 0)
    {
        set_err(err, err_sz, "empty file");
        return false;
    }

    /* work on a bounded, NUL-terminated copy: never strstr past `len` */
    std::string doc(text, len);

    const size_t soe = doc.find("$$SOE");
    const size_t eoe = (soe == std::string::npos) ? std::string::npos : doc.find("$$EOE", soe + 5);
    if (soe == std::string::npos || eoe == std::string::npos)
    {
        /* surface a Horizons diagnostic before falling back to the generic error */
        const char *diag_keys[] = { "API ERROR:", "API MESSAGE:", "No ephemeris for target" };
        for (size_t i = 0; i < sizeof(diag_keys) / sizeof(diag_keys[0]); ++i)
        {
            size_t p = doc.find(diag_keys[i]);
            if (p != std::string::npos)
            {
                size_t eol = doc.find('\n', p);
                std::string line = trim(doc.substr(p, (eol == std::string::npos) ? std::string::npos : eol - p));
                set_err(err, err_sz, "%s", line.c_str());
                return false;
            }
        }
        set_err(err, err_sz, "No ephemeris table found ($$SOE/$$EOE markers missing)");
        return false;
    }

    /* header lives before $$SOE */
    std::string header = doc.substr(0, soe);
    std::string header_upper = upper(header);

    /* Ecliptic exports are silently wrong in this frame; refuse them. The check
     * is scoped to the "Reference frame" field value, because the word
     * "ecliptic" also appears in Horizons' prose documentation blocks. */
    {
        std::string frame = header_value(header, "Reference frame");
        if (upper(frame).find("ECLIPTIC") != std::string::npos)
        {
            set_err(err, err_sz,
                    "This file was exported in ecliptic coordinates. Re-export with REF_PLANE='FRAME'.");
            return false;
        }
    }

    /* only Earth-centred ephemerides are supported */
    std::string center = header_value(header, "Center body name:");
    if (!center.empty() && upper(center).find("EARTH") == std::string::npos)
    {
        set_err(err, err_sz,
                "Only Earth-centred ephemerides are supported (this file is centred on %s).", center.c_str());
        return false;
    }

    /* units from the header, if present */
    bool header_km = false;
    bool header_au = false;
    {
        std::string units = upper(header_value(header, "Output units"));
        if (units.find("KM") != std::string::npos)
            header_km = true;
        else if (units.find("AU") != std::string::npos)
            header_au = true;
    }

    /* timescale: JD in TDB unless the header says otherwise */
    bool timescale_tdb = true;
    if (header_upper.find("JDUT") != std::string::npos ||
        header_upper.find("CALENDAR DATE (UT)") != std::string::npos)
        timescale_tdb = false;

    /* -- collect raw rows between the markers -- */
    std::vector<RawRow> rows;
    rows.reserve(4096);

    std::string block = doc.substr(soe + 5, eoe - (soe + 5));
    {
        size_t pos = 0;
        bool csv_mode = true;   /* decided by the first non-blank line */
        bool decided = false;
        RawRow cur;
        bool have_cur = false;
        bool cur_pos = false, cur_vel = false;
        std::string pending_label;

        while (pos <= block.size())
        {
            size_t eol = block.find('\n', pos);
            if (eol == std::string::npos)
                eol = block.size();
            std::string line = block.substr(pos, eol - pos);
            pos = eol + 1;
            if (eol == block.size())
                pos = block.size() + 1;
            if (pos > block.size())
                pos = block.size() + 1;

            std::string t = trim(line);
            if (t.empty())
                continue;

            if (!decided)
            {
                /* after $$SOE: "=" without commas -> block reader, else CSV */
                csv_mode = !(t.find('=') != std::string::npos && t.find(',') == std::string::npos);
                decided = true;
            }

            if (csv_mode)
            {
                RawRow r;
                if (parse_csv_row(t, &r))
                    rows.push_back(r);
                continue;
            }

            /* plain-text block reader */
            RawRow r;
            if (parse_block_start(t, &r))
            {
                if (have_cur && cur_pos)
                {
                    cur.ncomp = cur_vel ? 3 : 0;
                    rows.push_back(cur);
                }
                cur = r;
                have_cur = true;
                cur_pos = false;
                cur_vel = false;
                pending_label.clear();
                continue;
            }

            if (!have_cur)
                continue;

            /* label/value tokens; handles "X =-1.2", "VX=-1.2" and "X = -1.2" */
            size_t ts = 0;
            while (ts < t.size())
            {
                while (ts < t.size() && is_space(t[ts]))
                    ++ts;
                if (ts >= t.size())
                    break;
                size_t te = ts;
                while (te < t.size() && !is_space(t[te]))
                    ++te;
                const std::string tok = t.substr(ts, te - ts);
                block_apply_token(tok, &cur, &pending_label, &cur_pos, &cur_vel);
                ts = te;
            }
        }

        if (!csv_mode && have_cur && cur_pos)
        {
            cur.ncomp = cur_vel ? 3 : 0;
            rows.push_back(cur);
        }
    }

    if (rows.empty())
    {
        /* the most common Horizons failure: an empty table with a coverage note */
        std::string tail = (doc.size() > 2048) ? doc.substr(doc.size() - 2048) : doc;
        size_t p = tail.find("No ephemeris for target");
        if (p != std::string::npos)
        {
            size_t eol = tail.find('\n', p);
            std::string line = trim(tail.substr(p, (eol == std::string::npos) ? std::string::npos : eol - p));
            set_err(err, err_sz, "%s", line.c_str());
        }
        else
        {
            set_err(err, err_sz, "No ephemeris rows found between $$SOE and $$EOE");
        }
        return false;
    }

    /* -- decide units (header first, magnitude heuristic as fallback) -- */
    bool units_au = false;
    bool vel_au_per_day = false;
    {
        const RawRow &r0 = rows[0];
        const double pnorm = sqrt(r0.x * r0.x + r0.y * r0.y + r0.z * r0.z);
        const double vnorm = sqrt(r0.vx * r0.vx + r0.vy * r0.vy + r0.vz * r0.vz);

        if (header_au)
        {
            units_au = true;
            vel_au_per_day = true;
        }
        else if (header_km)
        {
            units_au = false;
            /* a km-s export with AU/day-sized velocities is impossible, so leave it */
            vel_au_per_day = false;
        }
        else
        {
            /* Magnitude heuristic: an AU position is < 1e4 for all solar-system
             * targets, while plausible km positions near Earth are >= ~6.4e3.
             * Horizons AU exports always carry AU/day velocities, so the unit
             * decision follows the position magnitude. */
            units_au = (pnorm < 1.0e4);
            vel_au_per_day = units_au || (vnorm < 1.0e-2);
        }
    }

    const double pos_scale = units_au ? AU_KM : 1.0;
    const double vel_scale = vel_au_per_day ? (AU_KM / SECONDS_PER_DAY) : 1.0;

    /* -- convert rows to samples -- */
    std::vector<EphemSample> samples;
    samples.reserve(rows.size());

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const RawRow &r = rows[i];

        double t_unix = (r.jd - JD_UNIX_EPOCH) * SECONDS_PER_DAY;
        if (timescale_tdb)
            t_unix -= tdb_minus_utc_seconds(r.jd);

        /* unit scaling first */
        double x = r.x * pos_scale;
        double y = r.y * pos_scale;
        double z = r.z * pos_scale;
        double vx = r.vx * vel_scale;
        double vy = r.vy * vel_scale;
        double vz = r.vz * vel_scale;

        /* axis remap to the app frame: (x, y, z)_icrf -> (x, z, -y) */
        EphemSample s;
        memset(&s, 0, sizeof(s));
        s.t_unix = t_unix;
        s.p.x = (float)x;
        s.p.y = (float)z;
        s.p.z = (float)(-y);
        s.v.x = (float)vx;
        s.v.y = (float)vz;
        s.v.z = (float)(-vy);
        samples.push_back(s);
    }

    /* -- sort, drop duplicate/non-monotonic times -- */
    std::stable_sort(samples.begin(), samples.end(),
                     [](const EphemSample &a, const EphemSample &b) { return a.t_unix < b.t_unix; });

    std::vector<EphemSample> clean;
    clean.reserve(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (clean.empty() || samples[i].t_unix > clean.back().t_unix)
            clean.push_back(samples[i]);
    }

    if (clean.size() < EPHEM_MIN_SAMPLES)
    {
        set_err(err, err_sz, "Ephemeris has fewer than %d usable samples", EPHEM_MIN_SAMPLES);
        return false;
    }

    /* -- synthesise velocity when the export was position-only -- */
    bool any_velocity = false;
    for (size_t i = 0; i < clean.size(); ++i)
    {
        if (clean[i].v.x != 0.0f || clean[i].v.y != 0.0f || clean[i].v.z != 0.0f)
        {
            any_velocity = true;
            break;
        }
    }
    if (!any_velocity)
    {
        const size_t n = clean.size();
        for (size_t i = 0; i < n; ++i)
        {
            const size_t a = (i == 0) ? 0 : i - 1;
            const size_t b = (i + 1 >= n) ? n - 1 : i + 1;
            const double dt = clean[b].t_unix - clean[a].t_unix;
            if (dt <= 0.0)
                continue;
            clean[i].v.x = (float)((clean[b].p.x - clean[a].p.x) / dt);
            clean[i].v.y = (float)((clean[b].p.y - clean[a].p.y) / dt);
            clean[i].v.z = (float)((clean[b].p.z - clean[a].p.z) / dt);
        }
    }

    /* -- decimate uniformly when above the storage cap -- */
    if (clean.size() > EPHEM_MAX_SAMPLES)
    {
        std::vector<EphemSample> dec;
        dec.reserve(EPHEM_MAX_SAMPLES);
        const size_t n_src = clean.size();
        const size_t n_out = EPHEM_MAX_SAMPLES;
        for (size_t i = 0; i < n_out; ++i)
        {
            size_t k = (size_t)llround((double)i * (double)(n_src - 1) / (double)(n_out - 1));
            if (k >= n_src)
                k = n_src - 1;
            dec.push_back(clean[k]);
        }
        clean.swap(dec);
    }

    /* -- simplify to 1 km on position, endpoints preserved -- */
    std::vector<EphemSample> simplified;
    simplify_dp(clean, SIMPLIFY_TOL_KM, &simplified);
    if (simplified.size() < EPHEM_MIN_SAMPLES)
    {
        set_err(err, err_sz, "Ephemeris has fewer than %d usable samples after simplification", EPHEM_MIN_SAMPLES);
        return false;
    }

    /* -- precompute lat/lon/gmst/sunlit -- */
    for (size_t i = 0; i < simplified.size(); ++i)
    {
        EphemSample &s = simplified[i];
        const double r = sqrt((double)s.p.x * s.p.x + (double)s.p.y * s.p.y + (double)s.p.z * s.p.z);

        double lat = 0.0;
        double lon = 0.0;
        if (r > 0.0)
        {
            double sy = (double)s.p.y / r;
            if (sy > 1.0)
                sy = 1.0;
            else if (sy < -1.0)
                sy = -1.0;
            lat = asin(sy) * RAD2DEG;
            lon = atan2(-(double)s.p.z, (double)s.p.x) * RAD2DEG;
        }

        const double epoch = epoch_from_unix(s.t_unix);
        const double gmst = epoch_to_gmst(epoch);

        /* lon is offset-free: the map rotation is subtracted later, exactly as
         * get_map_coordinates() does it. */
        lon -= gmst;
        while (lon > 180.0)
            lon -= 360.0;
        while (lon <= -180.0)
            lon += 360.0;

        s.lat = (float)lat;
        if (s.lat > 90.0f)
            s.lat = 90.0f;
        else if (s.lat < -90.0f)
            s.lat = -90.0f;
        s.lon = (float)lon;
        s.gmst = gmst;

        /* Sunlit test reuses the project's existing Sun model + umbra test. */
        const Vector3 sun_dir = calculate_sun_position(epoch);
        s.sunlit = !is_sat_eclipsed(s.p, sun_dir);
    }

    /* -- fill the mission -- */
    out->s = simplified;
    out->t0_unix = simplified.front().t_unix;
    out->t1_unix = simplified.back().t_unix;
    out->id = 0; /* assigned by EphemAdd() */
    /* name/target are provided by the caller and left untouched here */

    return true;
}
