#ifndef ASTRO_H
#define ASTRO_H

#include "types.h"

#define MAX_PASSES 5
typedef struct {
    double aos_epoch;
    double los_epoch;
    double max_el_epoch;
    float max_el;
    Vector2 path_pts[100];
    int num_pts;
} SatPass;

extern SatPass passes[MAX_PASSES];
extern int num_passes;
extern Satellite* last_pass_calc_sat;

double get_current_real_time_epoch(void);
double epoch_to_gmst(double epoch);
void epoch_to_datetime_str(double epoch, char* buffer);
void load_tle_data(const char* filename);
double normalize_epoch(double epoch);
double get_unix_from_epoch(double epoch);

// orbit math stuff
Vector3 calculate_sun_position(double current_time_days);
void get_map_coordinates(Vector3 pos, double gmst_deg, float earth_offset, float map_w, float map_h, float* out_x, float* out_y);
Vector3 calculate_position(Satellite* sat, double current_unix);
Vector3 calculate_moon_position(double current_time_days);
void get_apsis_2d(Satellite* sat, double current_time, bool is_apoapsis, double gmst_deg, float earth_offset, float map_w, float map_h, Vector2* out);

void get_az_el(Vector3 eci_pos, double gmst_deg, float obs_lat, float obs_lon, float obs_alt, double* az, double* el);
void CalculatePasses(Satellite* sat, double start_epoch);
void epoch_to_time_str(double epoch, char* str);
void update_orbit_cache(Satellite* sat, double current_epoch);
void get_apsis_times(Satellite* sat, double current_epoch, double* out_peri_unix, double* out_apo_unix);

#endif // ASTRO_H
