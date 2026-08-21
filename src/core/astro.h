#ifndef ASTRO_H
#define ASTRO_H

/**
 * @file astro.h
 * @brief Orbital mechanics, pass prediction, and coordinate transforms
 */

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

/* pass prediction settings (ROADMAP 12.3) — defaults applied by CalculatePasses */
extern float pass_min_elev;        /* minimum elevation (deg) for a pass to count */
extern float pass_time_span_hours; /* prediction window in hours (default 24) */

double get_current_real_time_epoch(void);
double epoch_to_gmst(double epoch);
void epoch_to_datetime_str(double epoch, char *buffer);

// orbital data loading (replaces TLE-specific loading)
void load_orbital_data(const char *filename);
void load_manual_entries(AppConfig *config);

// SGP4 initialization from orbital elements
bool add_satellite_from_tle(const char* line0, const char* line1,
                            const char* line2, OrbitalDataMeta *meta);
bool add_satellite_from_omm_elements(const char *name, const char *norad_id,
                                     const char *intl_desig, double epoch,
                                     double inclination_deg, double raan_deg,
                                     double eccentricity, double arg_perigee_deg,
                                     double mean_anomaly_deg, double mean_motion_revday,
                                     double bstar, OrbitalDataMeta *meta);

// Buffer-based variants that write into an array instead of the
// global `satellites[]`, used by the async fetch worker thread so it never
// touches the global array because the render loop touches that every frame
bool add_satellite_from_tle_to(Satellite *sats, int *count,
                               const char* line0, const char* line1,
                               const char* line2, OrbitalDataMeta *meta);
bool add_satellite_from_omm_elements_to(Satellite *sats, int *count,
                                        const char *name, const char *norad_id,
                                        const char *intl_desig, double epoch,
                                        double inclination_deg, double raan_deg,
                                        double eccentricity, double arg_perigee_deg,
                                        double mean_anomaly_deg, double mean_motion_revday,
                                        double bstar, OrbitalDataMeta *meta);

double normalize_epoch(double epoch);
double get_unix_from_epoch(double epoch);

// orbit math stuff
Vector3 calculate_sun_position(double current_time_days);
bool is_sat_eclipsed(Vector3 pos_km, Vector3 sun_dir_norm);
void get_map_coordinates(Vector3 pos, double gmst_deg, float earth_offset, float map_w, float map_h,
                         float *out_x, float *out_y);
Vector3 calculate_position(Satellite *sat, double current_unix);
Vector3 calculate_moon_position(double current_time_days);
void get_apsis_2d(Satellite *sat, double current_time, bool is_apoapsis, double gmst_deg, float earth_offset,
                  float map_w, float map_h, Vector2 *out);
void get_apsis_times(Satellite *sat, double current_time, double *out_peri_unix, double *out_apo_unix);
double calc_apogee_km(const Satellite *sat);
double calc_perigee_km(const Satellite *sat);
void geodetic_to_ecef(double lat_deg, double lon_deg, double alt_m, double *ox, double *oy, double *oz);
void get_az_el(Vector3 eci_pos, double gmst_deg, float obs_lat, float obs_lon, float obs_alt, double *az, double *el);
void CalculatePasses(Satellite *sat, double start_epoch);
void epoch_to_time_str(double epoch, char *str);

// local-time display preference (backend is UTC)
void SetUseLocalTime(bool use_local);
bool GetUseLocalTime(void);
void epoch_to_local_fields(double epoch, int *year, int *day, int *hour, int *min, int *sec);
double local_fields_to_epoch(int year, int day, int hour, int min, int sec);
void update_orbit_cache(Satellite *sat, double current_epoch);
bool is_orbit_cache_valid(Satellite *sat, Vector3 current_pos, float drift_threshold_km);
int calculate_orbit_cache_resolution(double eccentricity, int active_sat_count, int total_sat_count);

double get_sat_range(Satellite *sat, double epoch, Location obs);
double calculate_doppler_freq(Satellite *sat, double epoch, Location obs, double base_freq);
void draw_satellite_orbit_arch(Satellite *sat, double current_epoch, double gmst_deg, Location obs,
                               Vector2 scope_center, float scope_radius, float scope_az, float scope_el,
                               float scope_beam, Color orbit_color);

#endif // ASTRO_H
