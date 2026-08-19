#define _GNU_SOURCE
#include "astro.h"
#include "types.h"
#include "data/storage.h"

#include <math.h>
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define CSGP4_IMPLEMENTATION
#include "../../lib/csgp4.h"

#include <raymath.h>

/* WGS-84 ellipsoid constants */
#define WGS84_A  6378.137
#define WGS84_E2 0.00669437999014

/** converts geodetic lat/lon/alt to ECEF using WGS-84 instead of spherical earth */
void geodetic_to_ecef(double lat_deg, double lon_deg, double alt_m, double *ox, double *oy, double *oz)
{
    double lat = lat_deg * DEG2RAD;
    double lon = lon_deg * DEG2RAD;
    double sin_lat = sin(lat), cos_lat = cos(lat);
    double alt_km = alt_m / 1000.0;
    double N = WGS84_A / sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
    *ox = (N + alt_km) * cos_lat * cos(lon);
    *oy = (N + alt_km) * cos_lat * sin(lon);
    *oz = (N * (1.0 - WGS84_E2) + alt_km) * sin_lat;
}

Satellite satellites[MAX_SATELLITES];
int sat_count = 0;

Marker markers[MAX_MARKERS];
int marker_count = 0;

SatPass passes[MAX_PASSES];
int num_passes = 0;
Satellite *last_pass_calc_sat = NULL;

/* display preference: show dates/times in the system local timezone.
 * Backend math (SGP4, GMST, sun/moon, epoch conversions) always stays UTC. */
static bool g_use_local_time = true;

void SetUseLocalTime(bool use_local) { g_use_local_time = use_local; }
bool GetUseLocalTime(void) { return g_use_local_time; }

/** simple string-to-double extraction, avoids sscanf overhead in tight loops */
static double parse_tle_double(const char *str, int start, int len)
{
    char buf[32] = {0};
    strncpy(buf, str + start, len);
    return atof(buf);
}

/** grabs the system clock and converts it to our custom YYYYDDD.FFFF format */
double get_current_real_time_epoch(void)
{
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    int year = gmt->tm_year + 1900;
    double day_of_year = gmt->tm_yday + 1.0;
    double fraction_of_day = (gmt->tm_hour + gmt->tm_min / 60.0 + gmt->tm_sec / 3600.0) / 24.0;

    /* returns full YYYY format for global time consistency,
       then we use the YY format for SGP4 data internally. */
    return (year * 1000.0) + day_of_year + fraction_of_day;
}

/** handles year rollover/underflow so the math doesnt blow up on past/future passes */
double normalize_epoch(double epoch)
{
    int year = (int)(epoch / 1000.0);
    double day_of_year = fmod(epoch, 1000.0);

    while (1)
    {
        int days_in_yr = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;

        if (day_of_year >= days_in_yr + 1.0)
        {
            day_of_year -= days_in_yr;
            year++;
        }
        else if (day_of_year < 1.0)
        {
            year--;
            int prev_days_in_yr = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
            day_of_year += prev_days_in_yr;
        }
        else
        {
            break;
        }
    }
    return (year * 1000.0) + day_of_year;
}

/** converts normalized epoch format to unix time for sgp4 math */
double get_unix_from_epoch(double epoch)
{
    epoch = normalize_epoch(epoch);
    int year = (int)(epoch / 1000.0);
    double day = fmod(epoch, 1000.0);

    /* pure math unix conversion, avoids OS timegm() quantization issues */
    int y = year - 1;
    int leaps_to_year = (y / 4) - (y / 100) + (y / 400);
    int leaps_to_1970 = (1969 / 4) - (1969 / 100) + (1969 / 400);
    int leaps = leaps_to_year - leaps_to_1970;

    double unix_days = (year - 1970) * 365.0 + leaps + (day - 1.0);
    return unix_days * 86400.0;
}

/** sidereal time keeps the earth spinning under the satellites; without this everything is static */
double epoch_to_gmst(double epoch)
{
    double unix_time = get_unix_from_epoch(epoch);
    double jd = (unix_time / 86400.0) + 2440587.5;

    double gmst = fmod(280.46061837 + 360.98564736629 * (jd - 2451545.0), 360.0);
    if (gmst < 0)
        gmst += 360.0;
    return gmst;
}

/** pretty-print for the ui so humans can actually read the time.
 *  Respects the local-time display preference; the underlying epoch stays UTC. */
void epoch_to_datetime_str(double epoch, char *buffer)
{
    time_t t = (time_t)get_unix_from_epoch(epoch);
    struct tm *tm_info = g_use_local_time ? localtime(&t) : gmtime(&t);
    if (!tm_info)
    {
        strcpy(buffer, "----/--/-- --:--:--");
        return;
    }

    if (g_use_local_time)
    {
        /* compact numeric UTC offset (e.g. +0200) instead of the long
         * Windows timezone name like "Central European Summer Time" */
        char tz_off[16] = "";
        strftime(tz_off, sizeof(tz_off), "%z", tm_info);
        if (tz_off[0] == '\0') strcpy(tz_off, "+0000");
        sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d UTC%s",
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tz_off);
    }
    else
    {
        sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d UTC",
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
}

/**
 * @brief Initialize SGP4 directly from stored orbital elements
 *
 * Calls sgp4init_from_elements() with the satellite's stored orbital data,
 * bypassing the legacy TLE generation/parsing round-trip entirely.
 * This is the modern path for JSON/CSV OMM data and cached orbital stores.
 */
static bool init_sgp4_from_satellite(Satellite *sat)
{
    // mean motion: convert from rad/s (stored) to rad/min (SGP4 expects)
    double no_kozai = sat->mean_motion * 60.0;

    int ret = sgp4init_from_elements(
        &sat->satrec,
        sat->epoch_unix,
        (double)sat->bstar,       // B* drag term (decimal)
        0.0,                      // ndot: first derivative (rad/min^2) - typically 0 for OMM
        0.0,                      // nddot: second derivative (rad/min^3) - typically 0 for OMM
        sat->eccentricity,
        sat->arg_perigee,
        sat->inclination,
        sat->mean_anomaly,
        no_kozai,
        sat->raan
    );

    if (ret != 0 || sat->satrec.error != 0)
    {
        LOG_WARN("SGP4 init failed for %s - error=%d", sat->name, sat->satrec.error);
        return false;
    }
    return true;
}

/** parses TLE lines and populates the satellite struct */
bool add_satellite_from_tle(const char* line0, const char* line1, const char* line2, OrbitalDataMeta *meta)
{
    if (sat_count >= MAX_SATELLITES) {
        LOG_WARN("Cannot add satellite - MAX_SATELLITES (%d) reached", MAX_SATELLITES);
        return false;
    }
    Satellite *sat = &satellites[sat_count];
    memset(sat, 0, sizeof(Satellite));

    strncpy(sat->name, line0, 24);
    sat->name[24] = '\0';
    for (int i = 23; i >= 0; i--)
    {
        if (sat->name[i] == ' ' || sat->name[i] == '\r' || sat->name[i] == '\n') sat->name[i] = '\0';
        else break;
    }

    memset(&sat->norad_id, 0, sizeof(sat->norad_id));
    memset(&sat->intl_designator, 0, sizeof(sat->intl_designator));
    strncpy(sat->norad_id, line1 + 2, sizeof(sat->norad_id) - 1);
    strncpy(sat->intl_designator, line1 + 9, sizeof(sat->intl_designator) - 1);
    sat->norad_id_num = (uint32_t)atoi(sat->norad_id);

    /* scrape orbital elements directly from TLE strings */
    double raw_epoch = parse_tle_double(line1, 18, 14);
    int yy = (int)(raw_epoch / 1000.0);
    int year = (yy < 57) ? 2000 + yy : 1900 + yy;
    sat->epoch_days = (year * 1000.0) + fmod(raw_epoch, 1000.0);
    sat->epoch_unix = get_unix_from_epoch(sat->epoch_days);
    sat->inclination = parse_tle_double(line2, 8, 8) * DEG2RAD;
    sat->raan = parse_tle_double(line2, 17, 8) * DEG2RAD;

    char ecc_buf[32] = "0.";
    strncpy(ecc_buf + 2, line2 + 26, 7);
    sat->eccentricity = atof(ecc_buf);

    sat->arg_perigee = parse_tle_double(line2, 34, 8) * DEG2RAD;
    sat->mean_anomaly = parse_tle_double(line2, 43, 8) * DEG2RAD;

    double revs_per_day = parse_tle_double(line2, 52, 11);
    sat->mean_motion = (revs_per_day * 2.0 * PI) / 86400.0;
    sat->semi_major_axis = pow(MU / (sat->mean_motion * sat->mean_motion), 1.0 / 3.0);

    // parse B* drag term from TLE line 1 (positions 53-61)
    char bstar_buf[16] = {0};
    strncpy(bstar_buf, line1 + 53, 8);
    bstar_buf[8] = '\0';
    sat->bstar = ParseFixedEponential(bstar_buf, 0, NULL);

    sat->is_active = true;

    // store metadata
    if (meta)
        sat->data_meta = *meta;
    else
    {
        memset(&sat->data_meta, 0, sizeof(sat->data_meta));
        sat->data_meta.format = FORMAT_TLE;
    }

    // initialize SGP4 directly from orbital elements (no TLE round-trip)
    if (!init_sgp4_from_satellite(sat))
    {
        LOG_WARN("SGP4 init failed for TLE satellite: %s", line0);
        memset(sat, 0, sizeof(Satellite));
        return false;
    }

    sat_count++;
    return true;
}

/** adds a satellite from parsed OMM orbital elements (JSON/CSV OMM -> SGP4) */
bool add_satellite_from_omm_elements(const char *name, const char *norad_id,
                                     const char *intl_desig, double epoch,
                                     double inclination_deg, double raan_deg,
                                     double eccentricity, double arg_perigee_deg,
                                     double mean_anomaly_deg, double mean_motion_revday,
                                     double bstar, OrbitalDataMeta *meta)
{
    if (sat_count >= MAX_SATELLITES) {
        LOG_WARN("Cannot add OMM satellite %s - MAX_SATELLITES (%d) reached", name, MAX_SATELLITES);
        return false;
    }
    Satellite *sat = &satellites[sat_count];
    memset(sat, 0, sizeof(Satellite));

    // copy identification
    strncpy(sat->name, name, sizeof(sat->name) - 1);
    strncpy(sat->norad_id, norad_id, sizeof(sat->norad_id) - 1);
    sat->norad_id_num = (uint32_t)atoi(norad_id);
    if (intl_desig)
        strncpy(sat->intl_designator, intl_desig, sizeof(sat->intl_designator) - 1);

    // store orbital elements
    sat->epoch_days = epoch;
    sat->epoch_unix = get_unix_from_epoch(epoch);
    sat->inclination = inclination_deg * DEG2RAD;
    sat->raan = raan_deg * DEG2RAD;
    sat->eccentricity = eccentricity;
    sat->arg_perigee = arg_perigee_deg * DEG2RAD;
    sat->mean_anomaly = mean_anomaly_deg * DEG2RAD;
    sat->mean_motion = (mean_motion_revday * 2.0 * PI) / 86400.0;
    sat->semi_major_axis = pow(MU / (sat->mean_motion * sat->mean_motion), 1.0 / 3.0);
    sat->bstar = bstar;
    sat->is_active = true;

    // store metadata
    if (meta)
        sat->data_meta = *meta;
    else
    {
        memset(&sat->data_meta, 0, sizeof(sat->data_meta));
        sat->data_meta.format = FORMAT_OMM_JSON;
    }

    // initialize SGP4 directly from orbital elements (no TLE round-trip)
    if (!init_sgp4_from_satellite(sat))
    {
        LOG_WARN("SGP4 init failed for %s (NORAD: %s)", name, norad_id);
        memset(sat, 0, sizeof(Satellite));
        return false;
    }

    sat_count++;
    LOG_DEBUG("Added OMM satellite: %s (NORAD: %s)", name, norad_id);
    return true;
}

/** bulk loading of orbital data from structured storage */
void load_orbital_data(const char *filename)
{
    // try loading from the new structured storage first
    if (LoadOrbitalData(filename, satellites, &sat_count, MAX_SATELLITES))
    {
        LOG_INFO("Loaded %d satellites from %s", sat_count, filename);

        // re-init SGP4 for each satellite from stored orbital elements
        for (int i = 0; i < sat_count; i++)
        {
            Satellite *sat = &satellites[i];
            if (!sat->is_active) continue;

            // initialize SGP4 directly from stored elements (no TLE round-trip)
            if (!init_sgp4_from_satellite(sat))
            {
                LOG_WARN("SGP4 re-init failed for %s - deactivating", sat->name);
                sat->is_active = false;
            }
        }
        return;
    }

    LOG_WARN("No orbital data file found at %s", filename);
    sat_count = 0;
}

/** parses manually entered orbital data (pipe-delimited TLE or OMM fields) */
void load_manual_entries(AppConfig *config)
{
    for (int i = 0; i < config->manual_entry_count; i++)
    {
        char temp[512];
        strcpy(temp, config->manual_entries[i]);

        // try pipe-delimited TLE format first (backward compat)
        char *line0 = temp;
        char *line1 = strchr(line0, '|');
        if (line1)
        {
            *line1 = '\0';
            line1++;
            char *line2 = strchr(line1, '|');
            if (line2)
            {
                *line2 = '\0';
                line2++;
                OrbitalDataMeta meta = {0};
                strcpy(meta.source_name, "Manual Entry");
                meta.format = FORMAT_TLE;
                meta.fetch_time = time(NULL);
                add_satellite_from_tle(line0, line1, line2, &meta);
                continue;
            }
        }

        // could add OMM JSON paste support here in the future
        LOG_WARN("Could not parse manual entry %d", i);
    }
}

/**
 * @brief main sgp4 crank; outputs raw ECI coordinates
 *
 * precalculated unix time passed down to prevent extra year/day conversions
 */
Vector3 calculate_position(Satellite *sat, double current_unix)
{
    double tsince = (current_unix - sat->epoch_unix) / 60.0;

    double ro[3] = {0};
    double vo[3] = {0};

    sgp4(&sat->satrec, tsince, ro, vo);

    /* Check for SGP4 errors that produce NaN positions */
    if (sat->satrec.error != 0)
    {
        LOG_WARN("SGP4 error %d for %s at tsince=%.1f", sat->satrec.error, sat->name, tsince);
        return (Vector3){NAN, NAN, NAN};
    }

    /* Check for NaN/Inf in output */
    bool nan_or_inf = false;
    for (int i = 0; i < 3; i++)
    {
        if (isnan(ro[i]) || isinf(ro[i]))
            nan_or_inf = true;
    }
    if (nan_or_inf)
    {
        LOG_WARN("SGP4 produced NaN/Inf for %s at tsince=%.1f", sat->name, tsince);
        sat->satrec.error = 7;
        return (Vector3){NAN, NAN, NAN};
    }

    Vector3 pos;
    pos.x = (float)(ro[0]);
    pos.y = (float)(ro[2]);
    pos.z = (float)(-ro[1]);

    return pos;
}

/** projects 3D orbital space onto a 2D equirectangular map plane */
void get_map_coordinates(Vector3 pos, double gmst_deg, float earth_offset, float map_w, float map_h, float *out_x, float *out_y)
{
    float r = Vector3Length(pos);
    if (r == 0)
        r = 0.0001f;
    float phi = acosf(pos.y / r);
    float v = phi / PI;

    float theta_sat = atan2f(-pos.z, pos.x);
    float R_rad = (gmst_deg + earth_offset) * DEG2RAD;
    float theta_tex = theta_sat - R_rad;

    while (theta_tex > PI)
        theta_tex -= 2.0f * PI;
    while (theta_tex < -PI)
        theta_tex += 2.0f * PI;

    float u = theta_tex / (2.0f * PI) + 0.5f;
    *out_x = (u - 0.5f) * map_w;
    *out_y = (v - 0.5f) * map_h;
}

/** finds where the satellite hits the high and low points of its orbit in 2D */
void get_apsis_2d(Satellite *sat, double current_time, bool is_apoapsis, double gmst_deg, float earth_offset, float map_w, float map_h, Vector2 *out)
{
    (void)gmst_deg;

    double current_unix = get_unix_from_epoch(current_time);
    double delta_time_s = current_unix - sat->epoch_unix;

    double M = fmod(sat->mean_anomaly + sat->mean_motion * delta_time_s, 2.0 * PI);
    if (M < 0)
        M += 2.0 * PI;

    double target_M = is_apoapsis ? PI : 0.0;
    double diff = target_M - M;
    if (diff < 0)
        diff += 2.0 * PI;

    double t_target = current_time + (diff / sat->mean_motion) / 86400.0;
    double t_target_unix = get_unix_from_epoch(t_target);
    Vector3 pos3d = calculate_position(sat, t_target_unix);
    double gmst_target = epoch_to_gmst(t_target);

    get_map_coordinates(pos3d, gmst_target, earth_offset, map_w, map_h, &out->x, &out->y);
}

/** compute apogee altitude (km) from semi-major axis and eccentricity */
double calc_apogee_km(const Satellite *sat)
{
    return sat->semi_major_axis * (1.0 + sat->eccentricity) - EARTH_RADIUS_KM;
}

/** compute perigee altitude (km) from semi-major axis and eccentricity */
double calc_perigee_km(const Satellite *sat)
{
    return sat->semi_major_axis * (1.0 - sat->eccentricity) - EARTH_RADIUS_KM;
}

/** predicts the timestamps for the next perigee and apoapsis */
void get_apsis_times(Satellite *sat, double current_time, double *out_peri_unix, double *out_apo_unix)
{
    double current_unix = get_unix_from_epoch(current_time);
    double delta_time_s = current_unix - sat->epoch_unix;

    double M = fmod(sat->mean_anomaly + sat->mean_motion * delta_time_s, 2.0 * PI);
    if (M < 0)
        M += 2.0 * PI;

    double diff_peri = 0.0 - M;
    if (diff_peri < 0)
        diff_peri += 2.0 * PI;
    double diff_apo = PI - M;
    if (diff_apo < 0)
        diff_apo += 2.0 * PI;

    double t_peri = current_time + (diff_peri / sat->mean_motion) / 86400.0;
    double t_apo = current_time + (diff_apo / sat->mean_motion) / 86400.0;

    *out_peri_unix = get_unix_from_epoch(t_peri);
    *out_apo_unix = get_unix_from_epoch(t_apo);
}

/** calculates cache resolution based on orbital eccentricity */
int calculate_orbit_cache_resolution(double eccentricity, int active_sat_count, int total_sat_count)
{
    (void)active_sat_count;  // unused
    (void)total_sat_count;   // unused
    
    // low eccentricity
    if (eccentricity < 0.05)
        return 180;
    // moderate eccentricity
    if (eccentricity < 0.3)
        return 270;
    // high eccentricity
    return 361;
}

/** checks if cached orbit is still valid based on satellite drift */
bool is_orbit_cache_valid(Satellite *sat, Vector3 current_pos, float drift_threshold_km)
{
    if (!sat->orbit_cached)
        return false;
    
    float drift = Vector3Distance(sat->cached_orbit_base_pos, current_pos);
    return drift < drift_threshold_km;
}

/** bakes the future orbital path into a vertex buffer so sgp4 isnt re-run every frame */
void update_orbit_cache(Satellite *sat, double current_epoch)
{
    int new_res = calculate_orbit_cache_resolution(sat->eccentricity, 0, sat_count);
    if (new_res != sat->orbit_cache_resolution)
    {
        LOG_DEBUG("Orbit cache resolution changed for %s: %d -> %d pts",
                  sat->name, sat->orbit_cache_resolution, new_res);
        sat->orbit_cache_resolution = new_res;
    }
    
    double period_days = (2.0 * PI / sat->mean_motion) / 86400.0;
    double time_step = period_days / (sat->orbit_cache_resolution - 1);
    
    for (int i = 0; i < sat->orbit_cache_resolution; i++)
    {
        double t = current_epoch + (i * time_step);
        double t_unix = get_unix_from_epoch(t);
        sat->orbit_cache[i] = Vector3Scale(calculate_position(sat, t_unix), 1.0f / DRAW_SCALE);
    }
    
    // track cache validity
    double current_unix = get_unix_from_epoch(current_epoch);
    sat->cached_orbit_base_pos = calculate_position(sat, current_unix);
    sat->cached_orbit_epoch = current_epoch;
    sat->orbit_cached = true;
}

/** converts raw orbital data into azimuth/elevation for a specific ground station */
void get_az_el(Vector3 eci_pos, double gmst_deg, float obs_lat, float obs_lon, float obs_alt, double *az, double *el)
{
    double sat_r = Vector3Length(eci_pos);
    if (sat_r == 0)
    {
        *az = 0;
        *el = -90;
        return;
    }

    double sat_lat = asin(eci_pos.y / sat_r);
    double sat_lon_eci = atan2(-eci_pos.z, eci_pos.x);
    double theta = (gmst_deg + 0) * DEG2RAD; /* assuming earth_rotation_offset handled before */
    double sat_lon_ecef = sat_lon_eci - theta;

    double s_x = sat_r * cos(sat_lat) * cos(sat_lon_ecef);
    double s_y = sat_r * cos(sat_lat) * sin(sat_lon_ecef);
    double s_z = sat_r * sin(sat_lat);

    double o_x, o_y, o_z;
    geodetic_to_ecef(obs_lat, obs_lon, obs_alt, &o_x, &o_y, &o_z);

    double dx = s_x - o_x;
    double dy = s_y - o_y;
    double dz = s_z - o_z;

    double lat_rad = obs_lat * DEG2RAD;
    double lon_rad = obs_lon * DEG2RAD;
    double clat = cos(lat_rad);
    double slat = sin(lat_rad);
    double clon = cos(lon_rad);
    double slon = sin(lon_rad);

    double east = -slon * dx + clon * dy;
    double north = -slat * clon * dx - slat * slon * dy + clat * dz;
    double up = clat * clon * dx + clat * slon * dy + slat * dz;

    *el = atan2(up, sqrt(east * east + north * north)) * RAD2DEG;
    *az = atan2(east, north) * RAD2DEG;
    if (*az < 0)
        *az += 360.0;
}

/** qsort callback to keep passes in chronological order */
int compare_passes(const void *a, const void *b)
{
    const SatPass *p1 = (const SatPass *)a;
    const SatPass *p2 = (const SatPass *)b;
    if (p1->aos_epoch < p2->aos_epoch)
        return -1;
    if (p1->aos_epoch > p2->aos_epoch)
        return 1;
    return 0;
}

/** heavy lifting for pass prediction; brute force search with binary search refinement */
void CalculatePasses(Satellite *sat, double start_epoch)
{
    num_passes = 0;
    last_pass_calc_sat = sat;
    LOG_INFO("Calculating passes for %s starting at epoch %.2f",
             sat ? sat->name : "ALL satellites", start_epoch);

    int target_count = sat ? 1 : sat_count;
    int max_days = sat ? 3 : 1;
    double coarse_step = sat ? (1.0 / 1440.0) : (4.0 / 1440.0);

    for (int s = 0; s < target_count; s++)
    {
        Satellite *current_sat = sat ? sat : &satellites[s];
        if (!current_sat || !current_sat->is_active)
            continue;

        double t = start_epoch;
        double t_unix = get_unix_from_epoch(t);
        double gmst = epoch_to_gmst(t);
        double az, el;

        get_az_el(calculate_position(current_sat, t_unix), gmst, home_location.lat, home_location.lon, home_location.alt, &az, &el);

        /* back up if we're already in a pass to catch the true start */
        if (el > 0)
        {
            for (int i = 0; i < 30 && el > 0; i++)
            {
                t -= (1.0 / 1440.0);
                t_unix = get_unix_from_epoch(t);
                gmst = epoch_to_gmst(t);
                get_az_el(calculate_position(current_sat, t_unix), gmst, home_location.lat, home_location.lon, home_location.alt, &az, &el);
            }
        }

        bool in_pass = false;
        SatPass current_pass = {0};
        current_pass.sat = current_sat;

        int steps = (max_days * 1440) / (coarse_step * 1440.0);
        for (int i = 0; i < steps && num_passes < MAX_PASSES; i++)
        {
            t_unix = get_unix_from_epoch(t);
            gmst = epoch_to_gmst(t);
            get_az_el(calculate_position(current_sat, t_unix), gmst, home_location.lat, home_location.lon, home_location.alt, &az, &el);

            if (el >= 0.0)
            {
                if (!in_pass)
                {
                    in_pass = true;
                    /* binary search to find exact AOS, 1min stepping is too coarse for radio work */
                    double t_low = t - coarse_step;
                    double t_high = t;
                    for (int b = 0; b < 10; b++)
                    {
                        double t_mid = (t_low + t_high) / 2.0;
                        double mid_unix = get_unix_from_epoch(t_mid);
                        double mid_gmst = epoch_to_gmst(t_mid);
                        double mid_az, mid_el;
                        get_az_el(calculate_position(current_sat, mid_unix), mid_gmst, home_location.lat, home_location.lon, home_location.alt, &mid_az, &mid_el);
                        if (mid_el >= 0.0)
                            t_high = t_mid;
                        else
                            t_low = t_mid;
                    }

                    current_pass.aos_epoch = t_high;
                    current_pass.max_el = el;
                    current_pass.max_el_epoch = t;
                }
                if (el > current_pass.max_el)
                {
                    current_pass.max_el = el;
                    current_pass.max_el_epoch = t;
                }
            }
            else
            {
                if (in_pass)
                {
                    in_pass = false;
                    /* binary search to find exact LOS crossing */
                    double t_low = t - coarse_step;
                    double t_high = t;
                    for (int b = 0; b < 10; b++)
                    {
                        double t_mid = (t_low + t_high) / 2.0;
                        double mid_unix = get_unix_from_epoch(t_mid);
                        double mid_gmst = epoch_to_gmst(t_mid);
                        double mid_az, mid_el;
                        get_az_el(calculate_position(current_sat, mid_unix), mid_gmst, home_location.lat, home_location.lon, home_location.alt, &mid_az, &mid_el);
                        if (mid_el < 0.0)
                            t_high = t_mid;
                        else
                            t_low = t_mid;
                    }

                    current_pass.los_epoch = t_low;

                    current_pass.num_pts = 0;
                    double step = (current_pass.los_epoch - current_pass.aos_epoch) / 399.0;
                    if (step > 0)
                    {
                        current_pass.max_el = -90.0f; /* reset to find true max during high-res pass */
                        for (int k = 0; k < 400; k++)
                        {
                            double pt = current_pass.aos_epoch + k * step;
                            double pt_unix = get_unix_from_epoch(pt);
                            double p_gmst = epoch_to_gmst(pt);
                            double p_az, p_el;
                            get_az_el(calculate_position(current_sat, pt_unix), p_gmst, home_location.lat, home_location.lon, home_location.alt, &p_az, &p_el);
                            current_pass.path_pts[current_pass.num_pts++] = (Vector2){(float)p_az, (float)p_el};
                            
                            /* make sure we pinpoint the max elevation */
                            if (p_el > current_pass.max_el)
                            {
                                current_pass.max_el = (float)p_el;
                                current_pass.max_el_epoch = pt;
                            }
                        }
                    }
                    passes[num_passes++] = current_pass;
                    current_pass = (SatPass){0};
                    current_pass.sat = current_sat;
                }
            }
            t += coarse_step;
        }

        if (in_pass && num_passes < MAX_PASSES)
        {
            current_pass.los_epoch = t;
            current_pass.num_pts = 0;
            double step = (current_pass.los_epoch - current_pass.aos_epoch) / 399.0;
            if (step > 0)
            {
                current_pass.max_el = -90.0f;
                for (int k = 0; k < 400; k++)
                {
                    double pt = current_pass.aos_epoch + k * step;
                    double pt_unix = get_unix_from_epoch(pt);
                    double p_gmst = epoch_to_gmst(pt);
                    double p_az, p_el;
                    get_az_el(calculate_position(current_sat, pt_unix), p_gmst, home_location.lat, home_location.lon, home_location.alt, &p_az, &p_el);
                    current_pass.path_pts[current_pass.num_pts++] = (Vector2){(float)p_az, (float)p_el};
                    
                    if (p_el > current_pass.max_el)
                    {
                        current_pass.max_el = (float)p_el;
                        current_pass.max_el_epoch = pt;
                    }
                }
            }
            passes[num_passes++] = current_pass;
        }
    }

    /* sort passes chronologically so the list actually makes sense */
    qsort(passes, num_passes, sizeof(SatPass), compare_passes);
    LOG_INFO("Pass calculation complete: %d passes found", num_passes);
}

/** formats the internal epoch into a HH:MM:SS string for quick glancing.
 *  Respects the local-time display preference; the underlying epoch stays UTC. */
void epoch_to_time_str(double epoch, char *str)
{
    time_t t = (time_t)get_unix_from_epoch(epoch);
    struct tm *tm_info = g_use_local_time ? localtime(&t) : gmtime(&t);
    if (tm_info)
    {
        sprintf(str, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    else
    {
        strcpy(str, "00:00:00");
    }
}

/** populate year / day-of-year / hour / min / sec fields for the time setter,
 *  honouring the local-time display preference. */
void epoch_to_local_fields(double epoch, int *year, int *day, int *hour, int *min, int *sec)
{
    time_t t = (time_t)get_unix_from_epoch(epoch);
    struct tm *tm_info = g_use_local_time ? localtime(&t) : gmtime(&t);
    if (!tm_info) return;
    *year = tm_info->tm_year + 1900;
    *day  = tm_info->tm_yday + 1;
    *hour = tm_info->tm_hour;
    *min  = tm_info->tm_min;
    *sec  = tm_info->tm_sec;
}

/** convert user-entered local fields (year, day-of-year, h:m:s) back into a
 *  UTC-based epoch. When local time is disabled the fields are already UTC. */
double local_fields_to_epoch(int year, int day, int hour, int min, int sec)
{
    if (!g_use_local_time)
    {
        double day_fraction = (hour + min / 60.0 + sec / 3600.0) / 24.0;
        return (year * 1000.0) + day + day_fraction;
    }

    /* convert day-of-year to month/day (leap-aware) */
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
        days_in_month[1] = 29;
    int month = 1;
    while (month <= 12 && day > days_in_month[month - 1])
    {
        day -= days_in_month[month - 1];
        month++;
    }
    if (month > 12) month = 12;

    /* interpret the fields in the system local timezone, then convert to UTC */
    struct tm tm_info;
    memset(&tm_info, 0, sizeof(tm_info));
    tm_info.tm_year = year - 1900;
    tm_info.tm_mon  = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_hour = hour;
    tm_info.tm_min  = min;
    tm_info.tm_sec  = sec;
    tm_info.tm_isdst = -1;
    time_t unix = mktime(&tm_info);

    struct tm *utc = gmtime(&unix);
    if (!utc) return (year * 1000.0) + 1.0;
    double day_fraction = (utc->tm_hour + utc->tm_min / 60.0 + utc->tm_sec / 3600.0) / 24.0;
    return (utc->tm_year + 1900) * 1000.0 + (utc->tm_yday + 1) + day_fraction;
}

/**
 * @brief calculates the sun's position in ECI coordinates
 * @note this is a simplified model, validated against known ephemeris
 */
Vector3 calculate_sun_position(double current_time_days)
{
    double unix_time = get_unix_from_epoch(current_time_days);
    double jd = (unix_time / 86400.0) + 2440587.5;
    double n = jd - 2451545.0;

    double L = fmod(280.460 + 0.9856474 * n, 360.0);
    if (L < 0)
        L += 360.0;

    double g = fmod(357.528 + 0.9856003 * n, 360.0);
    if (g < 0)
        g += 360.0;

    double lambda = L + 1.915 * sin(g * DEG2RAD) + 0.020 * sin(2.0 * g * DEG2RAD);
    double epsilon = 23.439 - 0.0000004 * n;

    double x_ecl = cos(lambda * DEG2RAD);
    double y_ecl = sin(lambda * DEG2RAD);
    double z_ecl = 0.0;

    double x_eci = x_ecl;
    double y_eci = y_ecl * cos(epsilon * DEG2RAD) - z_ecl * sin(epsilon * DEG2RAD);
    double z_eci = y_ecl * sin(epsilon * DEG2RAD) + z_ecl * cos(epsilon * DEG2RAD);

    Vector3 pos;
    pos.x = (float)(x_eci);
    pos.y = (float)(z_eci);
    pos.z = (float)(-y_eci);

    return Vector3Normalize(pos);
}

/** basic shadow-cone check; tells us if the satellite is in the dark (no visual/solar power) */
bool is_sat_eclipsed(Vector3 pos_km, Vector3 sun_dir_norm)
{
    float dot = Vector3DotProduct(pos_km, sun_dir_norm);
    if (dot > 0.0f)
        return false;
    float dist_sq = Vector3LengthSqr(pos_km) - (dot * dot);
    return dist_sq < (EARTH_RADIUS_KM * EARTH_RADIUS_KM);
}

/**
 * @brief calculates the moon's position in ECI coordinates
 * @note includes evection, variation, and annual equation perturbations
 */
Vector3 calculate_moon_position(double current_time_days)
{
    double unix_time = get_unix_from_epoch(current_time_days);
    double jd = (unix_time / 86400.0) + 2440587.5;
    double D_days = jd - 2451545.0; /* days since J2000 */

    /* orbital arguments for the moon */
    double L_moon = fmod(218.316 + 13.176396 * D_days, 360.0) * DEG2RAD;
    double M_moon = fmod(134.963 + 13.064993 * D_days, 360.0) * DEG2RAD;
    double F_moon = fmod(93.272 + 13.229350 * D_days, 360.0) * DEG2RAD;

    /* solar mean anomaly and lunar elongation for perturbation calculations */
    double M_sun = fmod(357.528 + 0.9856003 * D_days, 360.0) * DEG2RAD;
    double D_elong = fmod(297.850 + 12.190749 * D_days, 360.0) * DEG2RAD;

    /* apply evection, variation, and annual equation perturbations */
    double E = 1.0 - 0.002516 * cos(M_sun);
    double evection = 1.274 * DEG2RAD * sin(2.0 * D_elong - M_moon);
    double variation = 0.658 * DEG2RAD * sin(2.0 * D_elong);
    double annual_eq = -0.186 * DEG2RAD * E * sin(M_sun);
    double parallactic = -0.035 * DEG2RAD * sin(D_elong);

    /* apply perturbations to longitude and distance */
    double lambda = L_moon + evection + variation + annual_eq + parallactic + (6.289 * DEG2RAD) * sin(M_moon) + (-0.059 * DEG2RAD) * sin(2.0 * D_elong + M_moon);

    double beta = (5.128 * DEG2RAD) * sin(F_moon) + (0.280 * DEG2RAD) * sin(F_moon + M_moon) + (0.277 * DEG2RAD) * sin(F_moon - M_moon) + (0.173 * DEG2RAD) * sin(2.0 * D_elong - F_moon);

    double dist_km = 385001.0 - 20905.0 * cos(M_moon) - 3699.0 * cos(2.0 * D_elong - M_moon) - 2956.0 * cos(2.0 * D_elong);

    /* convert from ecliptic to ECI coordinates */
    double x_ecl = dist_km * cos(beta) * cos(lambda);
    double y_ecl = dist_km * cos(beta) * sin(lambda);
    double z_ecl = dist_km * sin(beta);

    /* obliquity of the ecliptic (T is centuries since J2000) */
    double T = D_days / 36525.0;
    double eps = (23.439291 - 0.0130042 * T) * DEG2RAD;

    Vector3 pos;
    pos.x = (float)(x_ecl);
    pos.y = (float)(y_ecl * sin(eps) + z_ecl * cos(eps));
    pos.z = (float)-(y_ecl * cos(eps) - z_ecl * sin(eps));

    return pos;
}

/** internal helper to figure out straight-line distance to a satellite */
double get_sat_range(Satellite *sat, double epoch, Marker obs)
{
    double t_unix = get_unix_from_epoch(epoch);
    double theta = epoch_to_gmst(epoch) * DEG2RAD;

    Vector3 eci = calculate_position(sat, t_unix);

    /* direct cartesian rotation (ECI to ECEF) */
    double cos_t = cos(theta);
    double sin_t = sin(theta);

    double s_x = eci.x * cos_t - eci.z * sin_t;
    double s_y = -eci.x * sin_t - eci.z * cos_t;
    double s_z = eci.y;

    /* observer ECEF (WGS-84 ellipsoid) */
    double o_x, o_y, o_z;
    geodetic_to_ecef(obs.lat, obs.lon, obs.alt, &o_x, &o_y, &o_z);

    /* dist */
    double dx = s_x - o_x;
    double dy = s_y - o_y;
    double dz = s_z - o_z;

    return sqrt(dx * dx + dy * dy + dz * dz);
}

/** shifts the frequency based on velocity relative to the observer; essential for tuning */
double calculate_doppler_freq(Satellite *sat, double epoch, Marker obs, double base_freq)
{
    /* line-of-sight range */
    double dt = 0.1 / 86400.0; /* 0.1 seconds step */
    double r1 = get_sat_range(sat, epoch - dt, obs);
    double r2 = get_sat_range(sat, epoch + dt, obs);
    double range_rate = (r2 - r1) / 0.2; /* km/s */

    double c = 299792.458; /* in km/s */
    return base_freq * (c / (c + range_rate));
}

/** draws the satellite's orbital path as an arc on the radar scope */
void draw_satellite_orbit_arch(Satellite *sat, double current_epoch, double gmst_deg, Marker obs, 
                               Vector2 scope_center, float scope_radius, float scope_az, float scope_el, 
                               float scope_beam, Color orbit_color)
{
    if (!sat || !sat->is_active) return;
    
    // how long it takes this satellite to go around the planet
    double period_days = (2.0 * PI / sat->mean_motion) / 86400.0;
    int num_points = 360; // orbit res
    double time_step = period_days / num_points;
    
    // setup the radar scope projection
    float rad_beam_half = (scope_beam / 2.0f) * DEG2RAD;
    float c_az_rad = scope_az * DEG2RAD;
    float c_el_rad = scope_el * DEG2RAD;
    
    // get observer's position in ECI
    double ecef_x, ecef_y, ecef_z;
    geodetic_to_ecef(obs.lat, obs.lon + gmst_deg, obs.alt, &ecef_x, &ecef_y, &ecef_z);
    Vector3 O_eci = { (float)ecef_x, (float)ecef_z, (float)-ecef_y };
    
    Vector2 prev_point = {0};
    bool has_prev_point = false;
    
    // generate all the points that make up the orbit path
    for (int i = 0; i <= num_points; i++) {
        double t = current_epoch + (i * time_step);
        double t_unix = get_unix_from_epoch(t);
        Vector3 sat_pos = calculate_position(sat, t_unix);
        
        // convert to azimuth/elevation to know where to draw it
        double s_az, s_el;
        get_az_el(sat_pos, gmst_deg, obs.lat, obs.lon, obs.alt, &s_az, &s_el);
        
        // don't draw parts of the orbit that are below the horizon
        if (s_el < 0) {
            has_prev_point = false;
            continue;
        }
        
        // project the satellite position
        float s_az_rad = s_az * DEG2RAD;
        float s_el_rad = s_el * DEG2RAD;
        
        // calculate how far this point is from the center of scope view
        float cos_theta = sinf(c_el_rad) * sinf(s_el_rad) + cosf(c_el_rad) * cosf(s_el_rad) * cosf(s_az_rad - c_az_rad);
        if (cos_theta < -1.0f) cos_theta = -1.0f;
        if (cos_theta > 1.0f) cos_theta = 1.0f;
        
        // cull if outside scope view
        if (cos_theta >= cosf(rad_beam_half)) {
            float theta = acosf(cos_theta);
            float dx = cosf(s_el_rad) * sinf(s_az_rad - c_az_rad);
            float dy = cosf(c_el_rad) * sinf(s_el_rad) - sinf(c_el_rad) * cosf(s_el_rad) * cosf(s_az_rad - c_az_rad);
            
            float r_dist = (theta / rad_beam_half) * scope_radius;
            float angle = atan2f(-dy, dx);
            
            Vector2 current_point = { 
                scope_center.x + r_dist * cosf(angle), 
                scope_center.y + r_dist * sinf(angle) 
            };
            
            // connect points
            if (has_prev_point) {
                float prev_dist = Vector2Distance(prev_point, scope_center);
                float curr_dist = Vector2Distance(current_point, scope_center);
                
                if (prev_dist <= scope_radius && curr_dist <= scope_radius) {
                    Color faded_color = orbit_color;
                    faded_color.a = (unsigned char)(orbit_color.a * 0.3f);
                    DrawLineEx(prev_point, current_point, 1.5f, faded_color);
                } else if (prev_dist <= scope_radius || curr_dist <= scope_radius) {
                    // clip the line to the edge of the radar scope
                    Vector2 clipped_point = current_point;
                    if (curr_dist > scope_radius) {
                        float t = (scope_radius - prev_dist) / (curr_dist - prev_dist);
                        clipped_point.x = prev_point.x + t * (current_point.x - prev_point.x);
                        clipped_point.y = prev_point.y + t * (current_point.y - prev_point.y);
                    }
                    Color faded_color = orbit_color;
                    faded_color.a = (unsigned char)(orbit_color.a * 0.3f);
                    DrawLineEx(prev_point, clipped_point, 1.5f, faded_color);
                }
            }
            
            prev_point = current_point;
            has_prev_point = true;
        } else {
            has_prev_point = false;
        }
    }
}
