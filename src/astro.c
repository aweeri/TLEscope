#include "astro.h"
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

Satellite satellites[MAX_SATELLITES];
int sat_count = 0;

Marker markers[MAX_MARKERS];
int marker_count = 0;

// helper to grab a double from the text
static double parse_tle_double(const char* str, int start, int len) {
    char buf[32] = {0};
    strncpy(buf, str + start, len);
    return atof(buf);
}

// gets the actual current time 
double get_current_real_time_epoch(void) {
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);
    
    int year = gmt->tm_year + 1900;
    int yy = year % 100;
    double day_of_year = gmt->tm_yday + 1.0; 
    double fraction_of_day = (gmt->tm_hour + gmt->tm_min / 60.0 + gmt->tm_sec / 3600.0) / 24.0;
    return (yy * 1000.0) + day_of_year + fraction_of_day;
}

// time conversion math stuff
static double epoch_to_jd(double epoch) {
    int yy = (int)(epoch / 1000.0);
    double day_of_year = fmod(epoch, 1000.0);
    int year = (yy < 57) ? 2000 + yy : 1900 + yy;

    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    // oops leap year 
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) days_in_month[1] = 29;

    int day = (int)day_of_year;
    double frac = day_of_year - day;

    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (day <= days_in_month[i]) { month = i + 1; break; }
        day -= days_in_month[i];
    }

    int y = year, mo = month;
    double d = day + frac;
    if (mo <= 2) { y -= 1; mo += 12; }
    int A = y / 100;
    int B = 2 - A + (A / 4);
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (mo + 1)) + d + B - 1524.5;
}

double epoch_to_gmst(double epoch) {
    double jd = epoch_to_jd(epoch);
    double gmst = fmod(280.46061837 + 360.98564736629 * (jd - 2451545.0), 360.0);
    if (gmst < 0) gmst += 360.0;
    return gmst;
}

void epoch_to_datetime_str(double epoch, char* buffer) {
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

// read the text file line by line
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

// figure out where the satellite is right now (with j2 spheroid perturbations)
Vector3 calculate_position(Satellite* sat, double current_time_days) {
    double delta_time_s = (current_time_days - sat->epoch_days) * 86400.0;
    
    // earth oblateness (j2) effects
    double e2 = sat->eccentricity * sat->eccentricity;
    double p = sat->semi_major_axis * (1.0 - e2);
    
    // j2 math constants
    const double J2 = 0.00108262668;
    const double RE = 6378.137; // earth radius in km
    
    double n_J2_Re2_p2 = sat->mean_motion * J2 * (RE * RE) / (p * p);
    double raan_dot = -1.5 * n_J2_Re2_p2 * cos(sat->inclination);
    double arg_perigee_dot = 0.75 * n_J2_Re2_p2 * (4.0 - 5.0 * pow(sin(sat->inclination), 2));

    double current_raan = sat->raan + raan_dot * delta_time_s;
    double current_arg_perigee = sat->arg_perigee + arg_perigee_dot * delta_time_s;

    double M = sat->mean_anomaly + sat->mean_motion * delta_time_s;
    M = fmod(M, 2.0 * PI);
    if (M < 0.0) M += 2.0 * PI;

    // improved guess for kepler's equation
    double E = M + sat->eccentricity * sin(M);
    double sinE, cosE;
    for (int i = 0; i < 10; i++) {
        sinE = sin(E);
        cosE = cos(E);
        double delta = (E - sat->eccentricity * sinE - M) / (1.0 - sat->eccentricity * cosE);
        E -= delta;
        if (fabs(delta) < 1e-6) break;
    }

    // bypass true anomaly calculation
    double sqrt_1_minus_e2 = sqrt(1.0 - e2);
    double x_orb = sat->semi_major_axis * (cosE - sat->eccentricity);
    double y_orb = sat->semi_major_axis * sqrt_1_minus_e2 * sinE;

    // use perturbed angles for orientation
    double cw = cos(current_arg_perigee), sw = sin(current_arg_perigee);
    double cO = cos(current_raan), sO = sin(current_raan);
    double ci = cos(sat->inclination), si = sin(sat->inclination);

    Vector3 pos;
    pos.x = (cO * cw - sO * sw * ci) * x_orb + (-cO * sw - sO * cw * ci) * y_orb;
    pos.y = (sw * si) * x_orb + (cw * si) * y_orb;
    pos.z = -((sO * cw + cO * sw * ci) * x_orb + (-sO * sw + cO * cw * ci) * y_orb);

    return pos;
}

// turn 3d position into coordinates for the flat map
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

    double delta_time_s = (current_time - sat->epoch_days) * 86400.0;
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

// simplified Meeus implementation for Moon tracking
Vector3 calculate_moon_position(double current_time_days) {
    double jd = epoch_to_jd(current_time_days);
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
    pos.x = x_eci;
    pos.y = z_eci;
    pos.z = -y_eci;

    return pos;
}