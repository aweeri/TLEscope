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

#include "types.h"
#include <time.h>
#include <stdbool.h>

// -- Orbital Data Store ------------------------------------------------------

/** save all satellite data to a structured JSON file */
bool SaveOrbitalData(const char *filename, Satellite *sats, int count);

/** load all satellite data from a structured JSON file */
bool LoadOrbitalData(const char *filename, Satellite *sats, int *count, int max);

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

// -- Utility -----------------------------------------------------------------

const char* FormatToString(OrbitalDataFormat fmt);
OrbitalDataFormat StringToFormat(const char *str);

#endif // STORAGE_H