#ifndef PROVIDER_H
#define PROVIDER_H

#include "types.h"
#include <time.h>
#include <stdbool.h>

/*
 * provider.h — Data provider abstraction layer
 *
 * Allows the application to fetch orbital data from multiple sources
 * (Celestrak, Retlector, custom) in multiple formats (TLE, JSON OMM, CSV OMM).
 */

// ── Provider Types ──────────────────────────────────────────────────────────

typedef enum {
    PROVIDER_CELESTRAK,
    PROVIDER_RETLECTOR,
    PROVIDER_CUSTOM
} ProviderType;

// ── Data Source Definition ──────────────────────────────────────────────────

typedef struct {
    char id[16];
    char name[64];
    char base_url[256];
    ProviderType type;
    OrbitalDataFormat default_format;
    bool supports_format[8];  // Indexed by OrbitalDataFormat enum
} DataSource;

// ── Fetch Result ────────────────────────────────────────────────────────────

typedef struct {
    char *data;
    size_t size;
    OrbitalDataFormat format;
    long http_code;
    bool success;
} FetchResult;

// ── Provider Operations ─────────────────────────────────────────────────────

typedef struct {
    const char *name;
    bool (*build_url)(const DataSource *source, OrbitalDataFormat format,
                      char *url, size_t url_size);
} DataProvider;

// ── Provider Registry ───────────────────────────────────────────────────────

const DataProvider* GetProvider(ProviderType type);

// ── High-Level Fetch ────────────────────────────────────────────────────────

// Fetch data from a source in the specified format.
// Handles caching internally (120-min TTL).
// Returns a FetchResult that must be freed with FreeFetchResult().
FetchResult FetchFromSource(const DataSource *source, OrbitalDataFormat format);

// Free a fetch result
void FreeFetchResult(FetchResult *result);

// ── Built-in Source Lists ───────────────────────────────────────────────────

extern const DataSource CELESTRAK_SOURCES[];
extern const int NUM_CELESTRAK_SOURCES;

extern const DataSource RETLECTOR_SOURCES[];
extern const int NUM_RETLECTOR_SOURCES;

#endif // PROVIDER_H