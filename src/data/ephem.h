#ifndef EPHEM_H
#define EPHEM_H

#include "core/types.h"
#include <raylib.h>
#include <raymath.h>
#include <stdbool.h>
#include <stdint.h>
#include <vector>

/* hard limits */
#define EPHEM_MAX_MISSIONS 32 /* registry slots */
#define EPHEM_MAX_SAMPLES  20000 /* stored samples per mission, after simplification */

/* one stored state, sorted by t_unix (strictly increasing) */
typedef struct
{
    double t_unix;   /* sample time, unix seconds */
    Vector3 p, v;    /* position (km) / velocity (km/s), app axis convention */
    float lat, lon;  /* sub-satellite latitude/longitude, degrees */
    double gmst;     /* GMST at t, degrees */
    bool sunlit;     /* true when outside Earth's umbra at t */
} EphemSample;

/* one imported mission; the registry owns the sample vector */
typedef struct
{
    int id;                     /* registry id, nonzero once added */
    char name[48];              /* display name */
    char target[32];            /* Horizons target id/name, informational */
    double t0_unix, t1_unix;    /* inclusive unix-second window */
    uint32_t norad_id_num;      /* numeric fake NORAD id for legacy paths */
    int ref_body;               /* RefBody: 0 = Earth, 1 = Moon, 2 = Sun */
    std::vector<EphemSample> s; /* sorted by t_unix, at least 2 entries for interpolation */
} EphemMission;

/* -- Registry --------------------------------------------------------------- */

/** number of missions currently stored (0..EPHEM_MAX_MISSIONS-1 indexed) */
int EphemCount();

/** mission at a registry index [0, EphemCount()), or NULL when out of range */
EphemMission *EphemGet(int index);

/** mission with the given id, or NULL when not found */
EphemMission *EphemGetById(int id);

/** store a copy of m, assigning a fresh nonzero id; returns the new id, or 0 on failure (registry full) */
int EphemAdd(const EphemMission &m);

/* Overwrite the mission in place, keeping its id so existing references stay valid. */
bool EphemReplace(int id, const EphemMission &m);

/** remove the mission with the given id; returns true when it existed */
bool EphemRemove(int id);

/* Registry id of the mission whose `target` exactly matches, or 0. */
int EphemFindByTarget(const char *target);

/** drop every stored mission */
void EphemClear();

/* -- Sampling --------------------------------------------------------------- */

/* Cubic Hermite interpolation of position/velocity at t_unix; false when unusable. */
bool EphemInterp(const EphemMission *m, double t_unix, Vector3 *out_p, Vector3 *out_v);

/* Spherical sub-satellite latitude/longitude (degrees, lon in [-180,180]). */
bool EphemLatLon(const EphemMission *m, double t_unix, float *out_lat, float *out_lon);

/** sunlit flag of the nearest sample; false when out of range */
bool EphemSunlit(const EphemMission *m, double t_unix);

/** true when m has >= 2 samples and t_unix lies inside its window */
bool EphemInWindow(const EphemMission *m, double t_unix);

#endif // EPHEM_H
