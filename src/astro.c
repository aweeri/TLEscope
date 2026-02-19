#include "astro.h"

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

double epoch_to_gmst(double epoch) {
    epoch = normalize_epoch(epoch);
    int yy = (int)(epoch / 1000.0);
    double day = fmod(epoch, 1000.0);
    
    double unix_time = ConvertEpochYearAndDayToUnix(yy, day);
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

Vector3 calculate_position(Satellite* sat, double current_time_days) {
    current_time_days = normalize_epoch(current_time_days);
    
    int cur_yy = (int)(current_time_days / 1000.0);
    double cur_day = fmod(current_time_days, 1000.0);
    double current_unix = ConvertEpochYearAndDayToUnix(cur_yy, cur_day);

    int sat_yy = (int)(sat->epoch_days / 1000.0);
    double sat_day = fmod(sat->epoch_days, 1000.0);
    double sat_unix = ConvertEpochYearAndDayToUnix(sat_yy, sat_day);

    double tsince = (current_unix - sat_unix) / 60.0;

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
    current_time = normalize_epoch(current_time);

    int cur_yy = (int)(current_time / 1000.0);
    double cur_day = fmod(current_time, 1000.0);
    double current_unix = ConvertEpochYearAndDayToUnix(cur_yy, cur_day);

    int sat_yy = (int)(sat->epoch_days / 1000.0);
    double sat_day = fmod(sat->epoch_days, 1000.0);
    double sat_unix = ConvertEpochYearAndDayToUnix(sat_yy, sat_day);

    double delta_time_s = current_unix - sat_unix;
    
    double M = fmod(sat->mean_anomaly + sat->mean_motion * delta_time_s, 2.0 * PI);
    if (M < 0) M += 2.0 * PI;

    double target_M = is_apoapsis ? PI : 0.0;
    double diff = target_M - M;
    if (diff < 0) diff += 2.0 * PI;

    double t_target = current_time + (diff / sat->mean_motion) / 86400.0;
    Vector3 pos3d = calculate_position(sat, t_target);
    double gmst_target = epoch_to_gmst(t_target);
    
    get_map_coordinates(pos3d, gmst_target, earth_offset, map_w, map_h, &out->x, &out->y);
}

Vector3 calculate_moon_position(double current_time_days) {
    current_time_days = normalize_epoch(current_time_days);
    
    int yy = (int)(current_time_days / 1000.0);
    double day = fmod(current_time_days, 1000.0);
    
    double unix_time = ConvertEpochYearAndDayToUnix(yy, day);
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