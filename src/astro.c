#include "astro.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>

#define CSGP4_IMPLEMENTATION 
#include "csgp4.h"

#include <raymath.h>

Satellite satellites[MAX_SATELLITES];
int sat_count = 0;

Marker markers[MAX_MARKERS];
int marker_count = 0;

SatPass passes[MAX_PASSES];
int num_passes = 0;
Satellite* last_pass_calc_sat = NULL;

static double parse_tle_double(const char* str, int start, int len) {
    char buf[32] = {0};
    strncpy(buf, str + start, len);
    return atof(buf);
}

double get_current_real_time_epoch(void) {
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);
    
    int year = gmt->tm_year + 1900;
    int yy = year % 100;
    double day_of_year = gmt->tm_yday + 1.0; 
    double fraction_of_day = (gmt->tm_hour + gmt->tm_min / 60.0 + gmt->tm_sec / 3600.0) / 24.0;
    return (yy * 1000.0) + day_of_year + fraction_of_day;
}

double normalize_epoch(double epoch) {
    int yy = (int)(epoch / 1000.0);
    double day_of_year = fmod(epoch, 1000.0);
    int year = (yy < 57) ? 2000 + yy : 1900 + yy;

    while (1) {
        int days_in_yr = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
        
        if (day_of_year >= days_in_yr + 1.0) {
            day_of_year -= days_in_yr;
            year++;
            yy = year % 100;
        } else if (day_of_year < 1.0) {
            year--;
            yy = year % 100;
            int prev_days_in_yr = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
            day_of_year += prev_days_in_yr;
        } else {
            break;
        }
    }
    
    return (yy * 1000.0) + day_of_year;
}

// utility to convert normalized epoch format to unix time for sgp4 math
double get_unix_from_epoch(double epoch) {
    epoch = normalize_epoch(epoch);
    int yy = (int)(epoch / 1000.0);
    double day = fmod(epoch, 1000.0);
    return ConvertEpochYearAndDayToUnix(yy, day);
}

double epoch_to_gmst(double epoch) {
    double unix_time = get_unix_from_epoch(epoch);
    double jd = (unix_time / 86400.0) + 2440587.5;
    
    double gmst = fmod(280.46061837 + 360.98564736629 * (jd - 2451545.0), 360.0);
    if (gmst < 0) gmst += 360.0;
    return gmst;
}

void epoch_to_datetime_str(double epoch, char* buffer) {
    epoch = normalize_epoch(epoch);
    int yy = (int)(epoch / 1000.0);
    double day_of_year = fmod(epoch, 1000.0);
    int year = (yy < 57) ? 2000 + yy : 1900 + yy;

    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) days_in_month[1] = 29;

    int day = (int)day_of_year;
    double frac = day_of_year - day;

    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (day <= days_in_month[i]) { month = i + 1; break; }
        day -= days_in_month[i];
    }

    double hours = frac * 24.0;
    int h = (int)hours;
    double minutes = (hours - h) * 60.0;
    int m = (int)minutes;
    double seconds = (minutes - m) * 60.0;
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02.0f UTC", year, month, day, h, m, seconds);
}

void load_tle_data(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) { printf("Failed to open %s\n", filename); return; }

    char line0[256], line1[256], line2[256];
    while (fgets(line0, sizeof(line0), file) && fgets(line1, sizeof(line1), file) && fgets(line2, sizeof(line2), file)) {
        if (sat_count >= MAX_SATELLITES) break;
        Satellite* sat = &satellites[sat_count];

        strncpy(sat->name, line0, 24);
        sat->name[24] = '\0';
        for(int i = 23; i >= 0; i--) { if(sat->name[i] == ' ' || sat->name[i] == '\r' || sat->name[i] == '\n') sat->name[i] = '\0'; else break; }

        line0[strcspn(line0, "\r\n")] = 0;
        line1[strcspn(line1, "\r\n")] = 0;
        line2[strcspn(line2, "\r\n")] = 0;

        char combined[768];
        snprintf(combined, sizeof(combined), "%s\n%s\n%s\n", line0, line1, line2);
        
        struct TLEObject *parsed_objs = NULL;
        int num_objs = 0;
        ParseFileOrString(NULL, combined, &parsed_objs, &num_objs);
        
        if (num_objs > 0 && parsed_objs != NULL) {
            double initial_r[3] = {0};
            double initial_v[3] = {0};
            
            ConvertTLEToSGP4(&sat->satrec, &parsed_objs[0], 0.0, initial_r, initial_v);
            free(parsed_objs); 
        } else {
            printf("CSGP4 PARSER REJECTED: %s\n", sat->name);
        }

        sat->epoch_days = parse_tle_double(line1, 18, 14);
        sat->epoch_unix = get_unix_from_epoch(sat->epoch_days); // precalculated for perf
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
        sat_count++;
    }
    fclose(file);
}

// precalculated unix time passed down to prevent excessyear/day conversions
Vector3 calculate_position(Satellite* sat, double current_unix) {
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

void get_map_coordinates(Vector3 pos, double gmst_deg, float earth_offset, float map_w, float map_h, float* out_x, float* out_y) {
    float r = Vector3Length(pos);
    float phi = acosf(pos.y / r); 
    float v = phi / PI; 

    float theta_sat = atan2f(-pos.z, pos.x); 
    float R_rad = (gmst_deg + earth_offset) * DEG2RAD;
    float theta_tex = theta_sat - R_rad;
    
    while (theta_tex > PI) theta_tex -= 2.0f * PI;
    while (theta_tex < -PI) theta_tex += 2.0f * PI;

    float u = theta_tex / (2.0f * PI) + 0.5f; 
    *out_x = (u - 0.5f) * map_w;
    *out_y = (v - 0.5f) * map_h;
}

void get_apsis_2d(Satellite* sat, double current_time, bool is_apoapsis, double gmst_deg, float earth_offset, float map_w, float map_h, Vector2* out) {
    (void)gmst_deg; 

    double current_unix = get_unix_from_epoch(current_time);
    double delta_time_s = current_unix - sat->epoch_unix;
    
    double M = fmod(sat->mean_anomaly + sat->mean_motion * delta_time_s, 2.0 * PI);
    if (M < 0) M += 2.0 * PI;

    double target_M = is_apoapsis ? PI : 0.0;
    double diff = target_M - M;
    if (diff < 0) diff += 2.0 * PI;

    double t_target = current_time + (diff / sat->mean_motion) / 86400.0;
    double t_target_unix = get_unix_from_epoch(t_target);
    Vector3 pos3d = calculate_position(sat, t_target_unix);
    double gmst_target = epoch_to_gmst(t_target);
    
    get_map_coordinates(pos3d, gmst_target, earth_offset, map_w, map_h, &out->x, &out->y);
}

void get_apsis_times(Satellite* sat, double current_time, double* out_peri_unix, double* out_apo_unix) {
    double current_unix = get_unix_from_epoch(current_time);
    double delta_time_s = current_unix - sat->epoch_unix;
    
    double M = fmod(sat->mean_anomaly + sat->mean_motion * delta_time_s, 2.0 * PI);
    if (M < 0) M += 2.0 * PI;

    double diff_peri = 0.0 - M; if (diff_peri < 0) diff_peri += 2.0 * PI;
    double diff_apo = PI - M; if (diff_apo < 0) diff_apo += 2.0 * PI;

    double t_peri = current_time + (diff_peri / sat->mean_motion) / 86400.0;
    double t_apo = current_time + (diff_apo / sat->mean_motion) / 86400.0;
    
    *out_peri_unix = get_unix_from_epoch(t_peri);
    *out_apo_unix = get_unix_from_epoch(t_apo);
}

void update_orbit_cache(Satellite* sat, double current_epoch) {
    double period_days = (2.0 * PI / sat->mean_motion) / 86400.0;
    double time_step = period_days / (ORBIT_CACHE_SIZE - 1);
    for (int i = 0; i < ORBIT_CACHE_SIZE; i++) {
        double t = current_epoch + (i * time_step);
        double t_unix = get_unix_from_epoch(t);
        sat->orbit_cache[i] = Vector3Scale(calculate_position(sat, t_unix), 1.0f / DRAW_SCALE);
    }
    sat->orbit_cached = true;
}

void get_az_el(Vector3 eci_pos, double gmst_deg, float obs_lat, float obs_lon, float obs_alt, double* az, double* el) {
    double sat_r = Vector3Length(eci_pos);
    if (sat_r == 0) { *az = 0; *el = -90; return; }
    
    double sat_lat = asin(eci_pos.y / sat_r);
    double sat_lon_eci = atan2(-eci_pos.z, eci_pos.x);
    double theta = (gmst_deg + 0) * DEG2RAD; // assuming earth_rotation_offset handled befor
    double sat_lon_ecef = sat_lon_eci - theta;

    double s_x = sat_r * cos(sat_lat) * cos(sat_lon_ecef);
    double s_y = sat_r * cos(sat_lat) * sin(sat_lon_ecef);
    double s_z = sat_r * sin(sat_lat);

    double lat_rad = obs_lat * DEG2RAD;
    double lon_rad = obs_lon * DEG2RAD;

    double obs_rad = EARTH_RADIUS_KM + obs_alt/1000.0;

    double o_x = obs_rad * cos(lat_rad) * cos(lon_rad);
    double o_y = obs_rad * cos(lat_rad) * sin(lon_rad);
    double o_z = obs_rad * sin(lat_rad);

    double dx = s_x - o_x;
    double dy = s_y - o_y;
    double dz = s_z - o_z;

    double clat = cos(lat_rad);
    double slat = sin(lat_rad);
    double clon = cos(lon_rad);
    double slon = sin(lon_rad);

    double east  = -slon * dx + clon * dy;
    double north = -slat * clon * dx - slat * slon * dy + clat * dz;
    double up    =  clat * clon * dx + clat * slon * dy + slat * dz;

    *el = atan2(up, sqrt(east*east + north*north)) * RAD2DEG;
    *az = atan2(east, north) * RAD2DEG;
    if (*az < 0) *az += 360.0;
}

void CalculatePasses(Satellite* sat, double start_epoch) {
    num_passes = 0;
    last_pass_calc_sat = sat;
    if (!sat) return;

    double t = start_epoch;
    double t_unix = get_unix_from_epoch(t);
    double gmst = epoch_to_gmst(t);
    double az, el;
    
    get_az_el(calculate_position(sat, t_unix), gmst, home_location.lat, home_location.lon, home_location.alt, &az, &el);
    
    // back up if happens to already be in a pass to catch the true start
    if (el > 0) {
        for (int i = 0; i < 30 && el > 0; i++) {
            t -= (1.0 / 1440.0);
            t_unix = get_unix_from_epoch(t);
            gmst = epoch_to_gmst(t);
            get_az_el(calculate_position(sat, t_unix), gmst, home_location.lat, home_location.lon, home_location.alt, &az, &el);
        }
    }

    bool in_pass = false;
    SatPass current_pass = {0};
    
    for (int i = 0; i < 3 * 1440 && num_passes < MAX_PASSES; i++) {
        t_unix = get_unix_from_epoch(t);
        gmst = epoch_to_gmst(t);
        get_az_el(calculate_position(sat, t_unix), gmst, home_location.lat, home_location.lon, home_location.alt, &az, &el);
        
        if (el >= 0.0) {
            if (!in_pass) {
                in_pass = true;
                
                // binary search to find exact AOS
                double t_low = t - (1.0/1440.0);
                double t_high = t;
                for(int b = 0; b < 10; b++) {
                    double t_mid = (t_low + t_high) / 2.0;
                    double mid_unix = get_unix_from_epoch(t_mid);
                    double mid_gmst = epoch_to_gmst(t_mid);
                    double mid_az, mid_el;
                    get_az_el(calculate_position(sat, mid_unix), mid_gmst, home_location.lat, home_location.lon, home_location.alt, &mid_az, &mid_el);
                    if (mid_el >= 0.0) t_high = t_mid;
                    else t_low = t_mid;
                }
                
                current_pass.aos_epoch = t_high;
                current_pass.max_el = el;
                current_pass.max_el_epoch = t;
            }
            if (el > current_pass.max_el) {
                current_pass.max_el = el;
                current_pass.max_el_epoch = t;
            }
        } else {
            if (in_pass) {
                in_pass = false;
                
                // binary search to find exact LOS crossing because otherwise fuckywucky happens sometimes
                double t_low = t - (1.0/1440.0);
                double t_high = t;
                for(int b = 0; b < 10; b++) {
                    double t_mid = (t_low + t_high) / 2.0;
                    double mid_unix = get_unix_from_epoch(t_mid);
                    double mid_gmst = epoch_to_gmst(t_mid);
                    double mid_az, mid_el;
                    get_az_el(calculate_position(sat, mid_unix), mid_gmst, home_location.lat, home_location.lon, home_location.alt, &mid_az, &mid_el);
                    if (mid_el < 0.0) t_high = t_mid;
                    else t_low = t_mid;
                }
                
                current_pass.los_epoch = t_low;
                
                current_pass.num_pts = 0;
                double step = (current_pass.los_epoch - current_pass.aos_epoch) / 99.0;
                if (step > 0) {
                    for (int k = 0; k < 100; k++) {
                        double pt = current_pass.aos_epoch + k * step;
                        double pt_unix = get_unix_from_epoch(pt);
                        double p_gmst = epoch_to_gmst(pt);
                        double p_az, p_el;
                        get_az_el(calculate_position(sat, pt_unix), p_gmst, home_location.lat, home_location.lon, home_location.alt, &p_az, &p_el);
                        current_pass.path_pts[current_pass.num_pts++] = (Vector2){(float)p_az, (float)p_el};
                    }
                }
                passes[num_passes++] = current_pass;
            }
        }
        t += (1.0 / 1440.0);
    }
}

void epoch_to_time_str(double epoch, char* str) {
    time_t t = (time_t)get_unix_from_epoch(epoch);
    struct tm *tm_info = gmtime(&t);
    if (tm_info) {
        sprintf(str, "%02d:%02d:%02d", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    } else {
        strcpy(str, "00:00:00");
    }
}

// i truly do hope this doesnt drift or something its so eyeballed istg
Vector3 calculate_sun_position(double current_time_days) {
    double unix_time = get_unix_from_epoch(current_time_days);
    double jd = (unix_time / 86400.0) + 2440587.5;
    double n = jd - 2451545.0;

    double L = fmod(280.460 + 0.9856474 * n, 360.0);
    if (L < 0) L += 360.0;
    
    double g = fmod(357.528 + 0.9856003 * n, 360.0);
    if (g < 0) g += 360.0;

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

Vector3 calculate_moon_position(double current_time_days) {
    double unix_time = get_unix_from_epoch(current_time_days);
    double jd = (unix_time / 86400.0) + 2440587.5;
    double D = jd - 2451545.0; 

    double L = fmod(218.316 + 13.176396 * D, 360.0) * DEG2RAD;
    double M = fmod(134.963 + 13.064993 * D, 360.0) * DEG2RAD;
    double F = fmod(93.272 + 13.229350 * D, 360.0) * DEG2RAD;

    double lambda = L + (6.289 * DEG2RAD) * sin(M); 
    double beta = (5.128 * DEG2RAD) * sin(F);       
    double dist_km = 385000.0 - 20905.0 * cos(M);   

    double x_ecl = dist_km * cos(beta) * cos(lambda);
    double y_ecl = dist_km * cos(beta) * sin(lambda);
    double z_ecl = dist_km * sin(beta);

    double eps = 23.439 * DEG2RAD;
    double x_eci = x_ecl;
    double y_eci = y_ecl * cos(eps) - z_ecl * sin(eps);
    double z_eci = y_ecl * sin(eps) + z_ecl * cos(eps);

    Vector3 pos;
    pos.x = (float)(x_eci);
    pos.y = (float)(z_eci);
    pos.z = (float)(-y_eci);

    return pos;
}
