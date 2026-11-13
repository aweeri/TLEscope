#ifndef PROVIDER_H
#define PROVIDER_H

// Data provider abstraction: fetch orbital data from Celestrak, Retlector and
// custom sources in multiple formats (TLE, JSON OMM, CSV OMM).

#include "core/types.h"
#include "data/curl_diagnostics.h"
#include <time.h>
#include <stdbool.h>

// -- Provider Types ----------------------------------------------------------

typedef enum {
    PROVIDER_CELESTRAK,
    PROVIDER_RETLECTOR,
    PROVIDER_CUSTOM
} ProviderType;

// -- Data Source Definition --------------------------------------------------

typedef struct {
    char id[32];   // CelesTrak group ids like "cosmos-2251-debris" need >16
    char name[64];
    char base_url[256];
    ProviderType type;
    OrbitalDataFormat default_format;
    bool supports_format[8];  // indexed by OrbitalDataFormat enum
} DataSource;

// -- Fetch Result ------------------------------------------------------------

typedef struct {
    char *data;
    size_t size;
    OrbitalDataFormat format;
    long http_code;
    bool success;
    int curl_code;  // CURLcode of the last attempt; 0 == CURLE_OK. Non-zero with http_code == 0 means a transport failure before any status line.
} FetchResult;

// -- Provider Operations -----------------------------------------------------

typedef struct {
    const char *name;
    bool (*build_url)(const DataSource *source, OrbitalDataFormat format,
                      char *url, size_t url_size);
} DataProvider;

// -- Provider Registry -------------------------------------------------------

const DataProvider* GetProvider(ProviderType type);

// -- High-Level Fetch --------------------------------------------------------

/** fetch data from a source in the specified format.
 *  handles caching internally (120-min TTL).
 *  returns a FetchResult that must be freed with FreeFetchResult(). */
FetchResult FetchFromSource(const DataSource *source, OrbitalDataFormat format);

/** free a fetch result */
void FreeFetchResult(FetchResult *result);

// -- Retlector Group Fetch ---------------------------------------------------

/** fetch available groups from retlector.eu API.
 *  parses the JSON response into RetlectorGroup array.
 *  returns number of groups parsed, or -1 on error. */
int FetchRetlectorGroups(RetlectorGroup *groups, int max_groups, long *out_http_code);

// -- Format Detection --------------------------------------------------------

bool tleHasNameLine(const char *data);

/** detect orbital data format from raw content.
 *  examines content signatures in order: XML -> JSON -> KVN -> CSV -> TLE. */
OrbitalDataFormat DetectDataFormat(const char *data, size_t size);

// -- Custom URL Fetch --------------------------------------------------------

/** fetch from a custom URL and auto-detect the format.
 *  the result.format will be set to the detected format. */
FetchResult FetchFromCustomURL(const char *url);

// -- Custom File Fetch --------------------------------------------------------

/** fetch from a custom file and auto-detect the format.
 *  the result.format will be set to the detected format. */
FetchResult FetchFromCustomFile(const char *filename);

// -- Raw URL Fetch -----------------------------------------------------------

/// raw GET of any URL using the shared curl config (UA, timeouts, retries, proxy)
FetchResult HttpFetchURL(const char *url);

// -- Built-in Source Lists ---------------------------------------------------

extern const DataSource CELESTRAK_SOURCES[];
extern const int NUM_CELESTRAK_SOURCES;

extern const DataSource RETLECTOR_SOURCES[];
extern const int NUM_RETLECTOR_SOURCES;

#endif // PROVIDER_H
