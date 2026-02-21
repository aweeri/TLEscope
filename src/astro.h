#ifndef ASTRO_H
#define ASTRO_H

#include "types.h"

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

#endif // ASTRO_H