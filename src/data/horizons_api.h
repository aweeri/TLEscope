#ifndef HORIZONS_API_H
#define HORIZONS_API_H

/**
 * @file horizons_api.h
 * @brief JPL Horizons VECTORS client (async fetch + parse)
 *
 * Builds a Horizons API request for a catalogued mission, fetches it on a
 * detached worker thread and unwraps the JSON `result` field into the same
 * text the existing ephem_parse.cpp parser consumes.
 *
 * No UI, no raylib calls: the UI thread only starts a fetch and polls state.
 */

#include "data/ephem.h"
#include <cstddef>
#include <string>

/* Body an orbit polyline is drawn relative to. Storage stays geocentric. */
typedef enum {
    REF_BODY_EARTH = 0,
    REF_BODY_MOON  = 1,
    REF_BODY_SUN   = 2,
    REF_BODY_COUNT
} RefBody;

/* One entry in the mission dropdown; `command` is a JPL target id, not a NORAD id. */
typedef struct {
    const char *name;
    const char *command;
    const char *start_time;
    const char *stop_time;
    const char *step_size;
} HorizonsMissionEntry;

/**
 * One row of the bundled, generated catalog (see horizons_catalog_data.h).
 *
 * `group` is one of "Crewed", "Lunar", "Interplanetary", "L1/L2", "Other" or
 * "Reference". The Reference group is exposed ONLY through
 * HorizonsReferenceBodyAt() so the mission combo can exclude it.
 *
 * `command` (the Horizons target id) is deliberately kept out of the combo
 * label; it is available here for tooltips and info panels.
 */
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

/**
 * Resolve a user-typed target id ("-1024") or name substring ("Artemis") to a
 * catalog index, or -1 when nothing matches. Pure, no network.
 *
 * An exact `command` match wins; otherwise a case-insensitive substring match
 * on the display name (first hit in catalog order). Reference bodies are not
 * searched by this function.
 */
int HorizonsCatalogFind(const char *query);

/**
 * Build a temporary entry for a Horizons COMMAND id that is not in the
 * snapshot, so a typed target is never a dead end. The caller keeps the string
 * arguments alive for as long as the entry (and any fetch started from it) is
 * in use; typically pass string literals or `static`/function-scope buffers.
 *
 * Empty `start_time`/`stop_time` request the widest practical window
 * ("1600-01-01" -> "2500-01-01", step "100 d"); Horizons rejects a window that
 * is not fully inside the target's coverage, so the worker's existing single
 * clamp-and-retry narrows it to the real prior/after bound automatically.
 *
 * `out->name` is set to `command`; the caller may overwrite it afterwards (the
 * struct is a plain value).
 */
bool HorizonsMakeAdHocEntry(const char *command, const char *start_time,
                            const char *stop_time, const char *step_size,
                            HorizonsMissionEntry *out);

const char *HorizonsCenterForRef(RefBody ref);

bool HorizonsBuildUrl(const HorizonsMissionEntry *e, char *url, size_t url_size);

/**
 * Coarsen `step_seconds` so the span produces at most ~40000 rows. Pure and
 * side-effect free; returns `step_seconds` unchanged when no clamp is needed.
 */
double HorizonsClampStep(double span_seconds, double step_seconds);

/**
 * Extract a Horizons coverage-limit datetime from an empty-table response body
 * ("... prior to A.D. <dt>" / "... after A.D. <dt>"). Normalises the value to
 * the "YYYY-MMM-DD HH:MM" form accepted as START_TIME/STOP_TIME. `is_prior` is
 * set true when the requested START is too early, false when STOP is too late.
 */
bool HorizonsParseCoverageLimit(const std::string &body, bool *is_prior,
                                char *dt_iso, size_t dt_sz);

/* HZ_CANCELLED is appended last so the existing numeric values stay stable for
 * the UI switch. */
enum HorizonsFetchState {
    HZ_IDLE = 0,
    HZ_RUNNING,
    HZ_DONE,
    HZ_FAILED,
    HZ_CANCELLED
};

bool HorizonsFetchStart(const HorizonsMissionEntry *e);

/**
 * Request cancellation of the in-flight fetch. Cooperative: the worker checks
 * the flag before and immediately after the HTTP request, so cancel takes
 * effect at the next checkpoint (a blocking curl transfer is not aborted
 * mid-flight). On success the state becomes HZ_CANCELLED and HorizonsFetchTake
 * reports false with err == "Cancelled.".
 */
void HorizonsFetchCancel(void);

HorizonsFetchState HorizonsFetchPoll(void);
bool HorizonsFetchTake(EphemMission *out, char *err, size_t err_sz);
long HorizonsFetchHttpCode(void);

#endif // HORIZONS_API_H
