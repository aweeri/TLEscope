#ifndef HORIZONS_API_H
#define HORIZONS_API_H

#include "data/ephem.h"
#include <cstddef>
#include <string>

/* One entry in the mission dropdown; `command` is a JPL target id, not a NORAD id. */
typedef struct {
    const char *name;
    const char *command;
    const char *start_time;
    const char *stop_time;
    const char *step_size;
} HorizonsMissionEntry;

/* Bundled catalog row; `command` is the Horizons target id, kept out of the combo label. */
typedef struct {
    const char *name;
    const char *command;
    const char *start_time;
    const char *stop_time;
    const char *step_size;
    const char *group;
} HorizonsCatalogEntry;

/* -- Bundled catalog (offline snapshot; no network at startup) -------------- */

/** number of artificial-object entries in the bundled catalog */
int HorizonsCatalogCount(void);

/** catalog entry at [0, HorizonsCatalogCount()), or NULL when out of range */
const HorizonsCatalogEntry *HorizonsCatalogAt(int index);

/** number of reference-body entries (Sun, Moon, planets, L1/L2) */
int HorizonsReferenceBodyCount(void);

/** reference-body entry at [0, HorizonsReferenceBodyCount()), or NULL */
const HorizonsCatalogEntry *HorizonsReferenceBodyAt(int index);

/* Resolve a typed target id or name substring to a catalog index, or -1. */
int HorizonsCatalogFind(const char *query);

/* Build a temporary entry for a COMMAND id not in the snapshot; caller keeps the string args alive. */
bool HorizonsMakeAdHocEntry(const char *command, const char *start_time,
                            const char *stop_time, const char *step_size,
                            HorizonsMissionEntry *out);

bool HorizonsBuildUrl(const HorizonsMissionEntry *e, char *url, size_t url_size);

/* Coarsen `step_seconds` so the span produces at most ~40000 rows; unchanged when no clamp is needed. */
double HorizonsClampStep(double span_seconds, double step_seconds);

/* Extract a coverage-limit datetime from an empty-table response and normalise it to "YYYY-MMM-DD HH:MM". */
bool HorizonsParseCoverageLimit(const std::string &body, bool *is_prior,
                                char *dt_iso, size_t dt_sz);

/* HZ_CANCELLED is appended last so existing numeric values stay stable. */
enum HorizonsFetchState {
    HZ_IDLE = 0,
    HZ_RUNNING,
    HZ_DONE,
    HZ_FAILED,
    HZ_CANCELLED
};

bool HorizonsFetchStart(const HorizonsMissionEntry *e);

/* Request cooperative cancellation; a blocking curl transfer is not aborted mid-flight. */
void HorizonsFetchCancel(void);

HorizonsFetchState HorizonsFetchPoll(void);
bool HorizonsFetchTake(EphemMission *out, char *err, size_t err_sz);
long HorizonsFetchHttpCode(void);

#endif // HORIZONS_API_H
