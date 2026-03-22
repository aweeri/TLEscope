#ifndef ASTRO_H
#define ASTRO_H

#include "types.h"

#define MAX_PASSES 1000
typedef struct
{
    Satellite *sat;
    double aos_epoch;
    double los_epoch;
    double max_el_epoch;
    float max_el;
    Vector2 path_pts[400];
    int num_pts;
} SatPass;

extern SatPass passes[MAX_PASSES];
extern int num_passes;
extern Satellite *last_pass_calc_sat;

double get_current_real_time_epoch(void);
double epoch_to_gmst(double epoch);
void epoch_to_datetime_str(double epoch, char *buffer);
void load_tle_data(const char *filename);
void load_manual_tles(AppConfig *config);
double normalize_epoch(double epoch);
double get_unix_from_epoch(double epoch);

// orbit math stuff
Vector3 calculate_sun_position(double current_time_days);
bool is_sat_eclipsed(Vector3 pos_km, Vector3 sun_dir_norm);
void get_map_coordinates(Vector3 pos, double gmst_deg, float earth_offset, float map_w, float map_h, float *out_x,
                         float *out_y);
Vector3 calculate_position(Satellite *sat, double current_unix);
Vector3 calculate_moon_position(double current_time_days);
void get_apsis_2d(Satellite *sat, double current_time, bool is_apoapsis, double gmst_deg, float earth_offset,
                  float map_w, float map_h, Vector2 *out);
void get_apsis_times(Satellite *sat, double current_time, double *out_peri_unix, double *out_apo_unix);
void geodetic_to_ecef(double lat_deg, double lon_deg, double alt_m, double *ox, double *oy, double *oz);
void get_az_el(Vector3 eci_pos, double gmst_deg, float obs_lat, float obs_lon, float obs_alt, double *az, double *el);
void CalculatePasses(Satellite *sat, double start_epoch);
void epoch_to_time_str(double epoch, char *str);
void update_orbit_cache(Satellite *sat, double current_epoch);

double get_sat_range(Satellite *sat, double epoch, Marker obs);
double calculate_doppler_freq(Satellite *sat, double epoch, Marker obs, double base_freq);
void draw_satellite_orbit_arch(Satellite *sat, double current_epoch, double gmst_deg, Marker obs, 
                               Vector2 scope_center, float scope_radius, float scope_az, float scope_el, 
                               float scope_beam, Color orbit_color);

#endif // ASTRO_H
