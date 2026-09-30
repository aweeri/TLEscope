#ifndef ASTRO_CONSTANTS_H
#define ASTRO_CONSTANTS_H

// SGP4 (wgs72) - must match lib/csgp4.h getgravconst(wgs72)
#define ASTRO_MU_SGP4      398600.8
#define ASTRO_RE_SGP4      6378.135
#define ASTRO_J2_SGP4      1.082616e-3
#define ASTRO_J3_SGP4     -2.53881e-6
#define ASTRO_J4_SGP4     -1.65597e-6

// WGS-84 - geodetic / ECEF only
#define ASTRO_MU_WGS84     398600.4418
#define ASTRO_RE_WGS84     6378.137
#define ASTRO_E2_WGS84     0.00669437999014

// earth rotation (rad/s), matches epoch_to_gmst's 360.98564736629 deg/day
#define ASTRO_EARTH_ROT    7.29211514668855e-5

#endif /* ASTRO_CONSTANTS_H */
