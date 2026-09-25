#ifndef STORAGE_H
#define STORAGE_H

/**
 * @file storage.h
 * @brief Structured orbital data persistence
 *
 * Replaces the flat data.tle file with a JSON-based structured store.
 * Each satellite's orbital elements and metadata are serialized/deserialized
 * using the same manual JSON approach as config.cpp.
 */

#include "core/types.h"
#include <time.h>
#include <stdbool.h>

// -- Orbital Data Store ------------------------------------------------------

/** save all satellite data to a structured JSON file */
bool SaveOrbitalData(const char *filename, Satellite *sats, int count);

/** load all satellite data from a structured JSON file */
bool LoadOrbitalData(const char *filename, Satellite *sats, int *count, int max);

/** load all satellite data from an in-memory JSON string (no disk access) */
bool LoadOrbitalDataFromString(const char *json, Satellite *sats, int *count, int max);

// -- Source State Persistence ------------------------------------------------

typedef struct {
    char provider_name[64];
    char group_name[64];
    char url[256];
    OrbitalDataFormat preferred_format;
    bool selected;
    time_t last_fetch;
} DataSourceState;

bool SaveSourceState(const char *filename, DataSourceState *sources, int count);
bool LoadSourceState(const char *filename, DataSourceState *sources, int *count, int max);

// -- Favorites Persistence ---------------------------------------------------
// Favorites are keyed by NORAD catalog number (norad_id_num). The set is kept
// in memory and persisted as a JSON array of ids, so the same id set can be
// reused for filters and orbit-layer support elsewhere in the app.

/** load the favorites set from a JSON file into memory; returns id count */
int LoadFavorites(const char *filename);

/** persist the in-memory favorites set to a JSON file; returns success */
bool SaveFavorites(const char *filename);

/** true if the given NORAD id is currently a favorite */
bool IsFavorite(uint32_t norad_id);

/** mark/unmark the given NORAD id as a favorite (mutates in-memory set) */
void SetFavorite(uint32_t norad_id, bool fav);

/** number of favorites currently in the in-memory set */
int FavoriteCount(void);

/** copy the in-memory favorite NORAD ids into `out`; returns id count */
int GetFavoriteIds(uint32_t *out, int max);

// -- Utility -----------------------------------------------------------------

const char* FormatToString(OrbitalDataFormat fmt);
OrbitalDataFormat StringToFormat(const char *str);

#endif // STORAGE_H