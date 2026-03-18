#define _GNU_SOURCE
#include "astro.h"
#include "types.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#define CSGP4_IMPLEMENTATION
#include "../lib/csgp4.h"

#include <raymath.h>

/* WGS-84 ellipsoid constants */
#define WGS84_A  6378.137
#define WGS84_E2 0.00669437999014

/* geodetic lat/lon/alt to ECEF using WGS-84 instead of spherical earth */
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

/* string extraction (sscanf is a bit too beefy for tight TLE loops) */
static double parse_tle_double(const char *str, int start, int len)
{
    char buf[32] = {0};
    strncpy(buf, str + start, len);
    return atof(buf);
}

/* pulls the system clock and mashes it into our custom YYYYDDD.FFFF format */
double get_current_real_time_epoch(void)
{
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    int year = gmt->tm_year + 1900;
    double day_of_year = gmt->tm_yday + 1.0;
    double fraction_of_day = (gmt->tm_hour + gmt->tm_min / 60.0 + gmt->tm_sec / 3600.0) / 24.0;

    /* modified to return full YYYY format to make global time sim less fuckywucky,
       afterwards we just use the YY format for SGP4 data. */
    return (year * 1000.0) + day_of_year + fraction_of_day;
}

/* handles year rollover/underflow so the math doesnt explode when looking at past/future passes */
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

/* utility to convert normalized epoch format to unix time for sgp4 math */
double get_unix_from_epoch(double epoch)
{
    epoch = normalize_epoch(epoch);
    int year = (int)(epoch / 1000.0);
    double day = fmod(epoch, 1000.0);

    /* pure mathematical unix conversion to avoid OS-level timegm() quantization and stutter */
    int y = year - 1;
    int leaps_to_year = (y / 4) - (y / 100) + (y / 400);
    int leaps_to_1970 = (1969 / 4) - (1969 / 100) + (1969 / 400);
    int leaps = leaps_to_year - leaps_to_1970;

    double unix_days = (year - 1970) * 365.0 + leaps + (day - 1.0);
    return unix_days * 86400.0;
}

/* sidereal time keeps the earth spinning under the sats; without this, everything is static */
double epoch_to_gmst(double epoch)
{
    double unix_time = get_unix_from_epoch(epoch);
    double jd = (unix_time / 86400.0) + 2440587.5;

    double gmst = fmod(280.46061837 + 360.98564736629 * (jd - 2451545.0), 360.0);
    if (gmst < 0)
        gmst += 360.0;
    return gmst;
}

/* pretty-print for the ui so humans can actually read the time */
void epoch_to_datetime_str(double epoch, char *buffer)
{
    epoch = normalize_epoch(epoch);
    int year = (int)(epoch / 1000.0);
    double day_of_year = fmod(epoch, 1000.0);

    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
        days_in_month[1] = 29;

    int day = (int)day_of_year;
    double frac = day_of_year - day;

    int month = 0;
    for (int i = 0; i < 12; i++)
    {
        if (day <= days_in_month[i])
        {
            month = i + 1;
            break;
        }
        day -= days_in_month[i];
    }

    double hours = frac * 24.0;
    int h = (int)hours;
    double minutes = (hours - h) * 60.0;
    int m = (int)minutes;
    double seconds = (minutes - m) * 60.0;
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02.0f UTC", year, month, day, h, m, seconds);
}

/* rips lines from a TLE file and populates the satellite struct */
bool add_satellite_from_tle(const char* line0, const char* line1, const char* line2)
{
    if (sat_count >= MAX_SATELLITES) return false;
    Satellite *sat = &satellites[sat_count];

    strncpy(sat->name, line0, 24);
    sat->name[24] = '\0';
    for (int i = 23; i >= 0; i--)
    {
        if (sat->name[i] == ' ' || sat->name[i] == '\r' || sat->name[i] == '\n') sat->name[i] = '\0';
        else break;
    }

    memset(&sat->norad_id, 0, sizeof(sat->norad_id));
    memset(&sat->intl_designator, 0, sizeof(sat->intl_designator));
    strncpy(sat->norad_id, line1 + 2, sizeof(sat->norad_id));
    strncpy(sat->intl_designator, line1 + 9, sizeof(sat->intl_designator));

    char combined[768];
    snprintf(combined, sizeof(combined), "%s\n%s\n%s\n", line0, line1, line2);

    struct TLEObject *parsed_objs = NULL;
    int num_objs = 0;
    ParseFileOrString(NULL, combined, &parsed_objs, &num_objs);

    if (num_objs > 0 && parsed_objs != NULL)
    {
        double initial_r[3] = {0};
        double initial_v[3] = {0};

        /* shove the TLE into the sgp4 state machine */
        ConvertTLEToSGP4(&sat->satrec, &parsed_objs[0], 0.0, initial_r, initial_v);
        free(parsed_objs);

        /* manual scraping for the rest of the struct because we like control */
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
        sat->is_active = true;
        sat_count++;
        return true;
    }
    return false;
}

/* bulk loading of celestial junk from flat files */
void load_tle_data(const char *tlefile)
{
    char filename[1024];

    #if defined (BUILD_FOR_DIST)
    //The full path, with /<the tle file> at the end
    char fullPath[1024];
    const char *homeDir = getenv("XDG_CONFIG_HOME");
    if (!homeDir) homeDir = getenv("HOME");
    snprintf(fullPath,sizeof(fullPath),"%s/.config/TLEscope/%s", homeDir,tlefile);
    
    printf("DEBUG: %s\n", fullPath);
    snprintf(filename,sizeof(filename),fullPath);
    #else 
    snprintf(filename, sizeof(filename),tlefile);
    #endif

    FILE *file = fopen(filename, "r"); 
    if (!file)
    {
        printf("Failed to open %s\n", filename);
        return;
    }

    sat_count = 0;
    char line0[256];

    /* Check for custom header to restore TLE Manager state */
    if (fgets(line0, sizeof(line0), file))
    {
        if (strncmp(line0, "# EPOCH:", 8) == 0)
        {
            unsigned int mask = 0, cust_mask = 0, ret_mask = 0;
            /* scan past it, variables are unused here but keeps pointer advanced */
            if (strstr(line0, "RET_MASK:"))
            {
                sscanf(line0, "# EPOCH:%*d MASK:%u CUST_MASK:%u RET_MASK:%u", &mask, &cust_mask, &ret_mask);
            }
            else if (strstr(line0, "CUST_MASK:"))
            {
                sscanf(line0, "# EPOCH:%*d MASK:%u CUST_MASK:%u", &mask, &cust_mask);
            }
        }
        else
        {
            rewind(file); /* not a header, restart */
        }
    }

    char line1[256], line2[256];
    while (fgets(line0, sizeof(line0), file))
    {
        if (line0[0] == '#' || line0[0] == '\n' || line0[0] == '\r')
            continue;

        if (fgets(line1, sizeof(line1), file) && fgets(line2, sizeof(line2), file))
        {
            line0[strcspn(line0, "\r\n")] = 0;
            line1[strcspn(line1, "\r\n")] = 0;
            line2[strcspn(line2, "\r\n")] = 0;
            add_satellite_from_tle(line0, line1, line2);
        }
    }
    fclose(file);
}

/* parsing for strings that were likely copy-pasted in a hurry */
void load_manual_tles(AppConfig *config)
{
    for (int i = 0; i < config->manual_tle_count; i++)
    {
        char temp[512];
        strcpy(temp, config->manual_tles[i]);

        char *line0 = temp;
        char *line1 = strchr(line0, '|');
        if (!line1) continue;
        *line1 = '\0';
        line1++;

        char *line2 = strchr(line1, '|');
        if (!line2) continue;
        *line2 = '\0';
        line2++;

        add_satellite_from_tle(line0, line1, line2);
    }
}

/* main sgp4 crank; outputs raw ECI coordinates */
/* precalculated unix time passed down to prevent excessyear/day conversions */
Vector3 calculate_position(Satellite *sat, double current_unix)
{
    double tsince = (current_unix - sat->epoch_unix) / 60.0;

    double ro[3] = {0};
    double vo[3] = {0};

    sgp4(&sat->satrec, tsince, ro, vo);

    Vector3 pos;
    pos.x = (float)(ro[0]);
    pos.y = (float)(ro[2]);
    pos.z = (float)(-ro[1]);

    return pos;
}

/* projects 3D orbital space onto a 2D equirectangular map plane */
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

/* finds where the sat hits the high and low points of its orbit in 2D */
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

/* predicts the timestamps for the next perigee and apoapsis */
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

/* bakes the future orbital path into a vertex buffer so sgp4 isnt re-ran every frame */
void update_orbit_cache(Satellite *sat, double current_epoch)
{
    double period_days = (2.0 * PI / sat->mean_motion) / 86400.0;
    double time_step = period_days / (ORBIT_CACHE_SIZE - 1);
    for (int i = 0; i < ORBIT_CACHE_SIZE; i++)
    {
        double t = current_epoch + (i * time_step);
        double t_unix = get_unix_from_epoch(t);
        sat->orbit_cache[i] = Vector3Scale(calculate_position(sat, t_unix), 1.0f / DRAW_SCALE);
    }
    sat->orbit_cached = true;
}

/* converts raw orbital data into azimuth/elevation for a specific ground station */
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
    double theta = (gmst_deg + 0) * DEG2RAD; /* assuming earth_rotation_offset handled befor */
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

/* qsort callback to keep passes chronological */
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

/* heavy lifting for pass prediction; brute force search with binary search refinement */
void CalculatePasses(Satellite *sat, double start_epoch)
{
    num_passes = 0;
    last_pass_calc_sat = sat;

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

        /* back up if happens to already be in a pass to catch the true start */
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
                    /* binary search to find exact AOS because stepping by 1min is too crunchy for radio work */
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
                            
                            /* ensure max elevation is pinpointed */
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

    /* make sure the list actually makes sense chronologically */
    /* Sort overall passes generated by timeframe chronological arrival order */
    qsort(passes, num_passes, sizeof(SatPass), compare_passes);
}

/* formats the internal epoch into a HH:MM:SS string for quick glancing */
void epoch_to_time_str(double epoch, char *str)
{
    time_t t = (time_t)get_unix_from_epoch(epoch);
    struct tm *tm_info = gmtime(&t);
    if (tm_info)
    {
        sprintf(str, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    else
    {
        strcpy(str, "00:00:00");
    }
}

/* i truly do hope this doesnt drift or something its so eyeballed istg */
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

/* basic shadow-cone check; tells us if the sat is in the dark (no visual/solar power) */
bool is_sat_eclipsed(Vector3 pos_km, Vector3 sun_dir_norm)
{
    float dot = Vector3DotProduct(pos_km, sun_dir_norm);
    if (dot > 0.0f)
        return false;
    float dist_sq = Vector3LengthSqr(pos_km) - (dot * dot);
    return dist_sq < (EARTH_RADIUS_KM * EARTH_RADIUS_KM);
}

/* HYPER-ENHANCED MOON FUNCTION OMEGABLOCK BING BONG MK.3 PRO [OVERCLOCKED & OPTIMIZED] */
Vector3 calculate_moon_position(double current_time_days)
{
    double unix_time = get_unix_from_epoch(current_time_days);
    double jd = (unix_time / 86400.0) + 2440587.5;
    double D_days = jd - 2451545.0; /* days since J2000 */

    /* some arguments */
    double L_moon = fmod(218.316 + 13.176396 * D_days, 360.0) * DEG2RAD;
    double M_moon = fmod(134.963 + 13.064993 * D_days, 360.0) * DEG2RAD;
    double F_moon = fmod(93.272 + 13.229350 * D_days, 360.0) * DEG2RAD;

    /* solar mean anomaly and lunar elongation hehe for perturbation calculations */
    double M_sun = fmod(357.528 + 0.9856003 * D_days, 360.0) * DEG2RAD;
    double D_elong = fmod(297.850 + 12.190749 * D_days, 360.0) * DEG2RAD;

    /* she perturbate on my variation till I evect */
    double E = 1.0 - 0.002516 * cos(M_sun);
    double evection = 1.274 * DEG2RAD * sin(2.0 * D_elong - M_moon);
    double variation = 0.658 * DEG2RAD * sin(2.0 * D_elong);
    double annual_eq = -0.186 * DEG2RAD * E * sin(M_sun);
    double parallactic = -0.035 * DEG2RAD * sin(D_elong);

    /* apply perturbations to longitude and distance */
    double lambda = L_moon + evection + variation + annual_eq + parallactic + (6.289 * DEG2RAD) * sin(M_moon) + (-0.059 * DEG2RAD) * sin(2.0 * D_elong + M_moon);

    double beta = (5.128 * DEG2RAD) * sin(F_moon) + (0.280 * DEG2RAD) * sin(F_moon + M_moon) + (0.277 * DEG2RAD) * sin(F_moon - M_moon) + (0.173 * DEG2RAD) * sin(2.0 * D_elong - F_moon);

    double dist_km = 385001.0 - 20905.0 * cos(M_moon) - 3699.0 * cos(2.0 * D_elong - M_moon) - 2956.0 * cos(2.0 * D_elong);

    /* ecliptic to ECI because that wouldn't worky whatsoever */
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

/* internal helper to figure out straight-line distance to a satellite */
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

/* shifts the frequency based on velocity relative to the observer; essential for tuning */
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

// herobrine