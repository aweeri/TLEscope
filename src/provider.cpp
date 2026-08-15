#include "provider.h"
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>

/* ── Built-in Source Lists ────────────────────────────────────────────────── */

#define CELESTRAK_BASE "https://celestrak.org/NORAD/elements/gp.php"

const DataSource CELESTRAK_SOURCES[] = {
    {"1",  "Last 30 Days' Launches", CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"2",  "Space Stations",         CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"3",  "100 Brightest",          CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"4",  "Active Satellites",      CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"5",  "Analyst Satellites",     CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"6",  "Russian ASAT Debris",    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"7",  "Chinese ASAT Debris",    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"8",  "IRIDIUM 33 Debris",      CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"9",  "COSMOS 2251 Debris",     CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"10", "Weather",                CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"11", "NOAA",                   CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"12", "GOES",                   CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"13", "Earth Resources",        CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"14", "SARSAT",                 CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"15", "Disaster Monitoring",    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"16", "TDRSS",                  CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"17", "ARGOS",                  CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"18", "Planet",                 CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"19", "Spire",                  CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"20", "Starlink",               CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"21", "OneWeb",                 CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"22", "GPS Operational",        CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"23", "Galileo",                CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"24", "Amateur Radio",          CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
    {"25", "CubeSats",               CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON,  {0}},
};
const int NUM_CELESTRAK_SOURCES = sizeof(CELESTRAK_SOURCES) / sizeof(CELESTRAK_SOURCES[0]);

#define RETLECTOR_BASE "https://retlector.eu"

const DataSource RETLECTOR_SOURCES[] = {
    {"1",  "Last 30 Days' Launches", RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"2",  "Space Stations",         RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"3",  "100 Brightest",          RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"4",  "Active Satellites",      RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"5",  "Analyst Satellites",     RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"6",  "Russian ASAT Debris",    RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"7",  "Chinese ASAT Debris",    RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"8",  "IRIDIUM 33 Debris",      RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"9",  "COSMOS 2251 Debris",     RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"10", "Weather",                RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"11", "NOAA",                   RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"12", "GOES",                   RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"13", "Earth Resources",        RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"14", "SARSAT",                 RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"15", "Disaster Monitoring",    RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"16", "TDRSS",                  RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"17", "ARGOS",                  RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"18", "Planet",                 RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"19", "Spire",                  RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"20", "Starlink",               RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"21", "OneWeb",                 RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"22", "GPS Operational",        RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"23", "Galileo",                RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"24", "Amateur Radio",          RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
    {"25", "CubeSats",               RETLECTOR_BASE, PROVIDER_RETLECTOR, FORMAT_OMM_JSON,  {0}},
};
const int NUM_RETLECTOR_SOURCES = sizeof(RETLECTOR_SOURCES) / sizeof(RETLECTOR_SOURCES[0]);

/* ── Group name mapping ───────────────────────────────────────────────────── */

static const char* celestrak_group_for_index(int idx)
{
    static const char *groups[] = {
        "last-30-days", "stations", "visual", "active", "analyst",
        "cosmos-1408-debris", "fengyun-1c-debris", "iridium-33-debris",
        "cosmos-2251-debris", "weather", "noaa", "goes",
        "resource", "sarsat", "dmc", "tdrss", "argos",
        "planet", "spire", "starlink", "oneweb", "gps-ops",
        "galileo", "amateur", "cubesat"
    };
    if (idx < 0 || idx >= 25) return NULL;
    return groups[idx];
}

/* ── Celestrak URL builder ────────────────────────────────────────────────── */

static bool celestrak_build_url(const DataSource *source, OrbitalDataFormat format,
                                 char *url, size_t url_size)
{
    // Extract the group name from the source index
    int idx = atoi(source->id) - 1;
    const char *group = celestrak_group_for_index(idx);
    if (!group) return false;

    const char *fmt_str = "TLE";
    switch (format)
    {
        case FORMAT_OMM_JSON: fmt_str = "JSON"; break;
        case FORMAT_OMM_CSV:  fmt_str = "CSV"; break;
        case FORMAT_TLE:      fmt_str = "TLE"; break;
        default:              fmt_str = "JSON"; break;
    }

    snprintf(url, url_size, "%s?GROUP=%s&FORMAT=%s",
             CELESTRAK_BASE, group, fmt_str);
    return true;
}

/* ── Retlector URL builder ────────────────────────────────────────────────── */

static bool retlector_build_url(const DataSource *source, OrbitalDataFormat format,
                                 char *url, size_t url_size)
{
    int idx = atoi(source->id) - 1;
    const char *group = celestrak_group_for_index(idx);
    if (!group) return false;

    const char *path = "tle";
    switch (format)
    {
        case FORMAT_OMM_JSON: path = "json"; break;
        case FORMAT_OMM_CSV:  path = "csv";  break;
        case FORMAT_TLE:      path = "tle";  break;
        default:              path = "json"; break;
    }

    snprintf(url, url_size, "%s/%s/%s", RETLECTOR_BASE, path, group);
    return true;
}

/* ── Provider Registry ────────────────────────────────────────────────────── */

static const DataProvider celestrak_provider = {
    .name = "Celestrak",
    .build_url = celestrak_build_url
};

static const DataProvider retlector_provider = {
    .name = "Retlector",
    .build_url = retlector_build_url
};

const DataProvider* GetProvider(ProviderType type)
{
    switch (type)
    {
        case PROVIDER_CELESTRAK: return &celestrak_provider;
        case PROVIDER_RETLECTOR: return &retlector_provider;
        case PROVIDER_CUSTOM:    return NULL;  // Custom sources use raw URL
        default:                 return NULL;
    }
}

/* ── libcurl memory callback ──────────────────────────────────────────────── */

struct MemoryBuf {
    char *memory;
    size_t size;
};

static size_t write_memory_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryBuf *mem = (struct MemoryBuf *)userp;

    char *ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/* ── HTTP Fetch ───────────────────────────────────────────────────────────── */

static FetchResult http_fetch(const char *url)
{
    FetchResult result = {0};
    result.success = false;

    struct MemoryBuf chunk = {0};
    chunk.memory = (char*)malloc(1);
    chunk.size = 0;

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        free(chunk.memory);
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_cb);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    char user_agent[256];
    snprintf(user_agent, sizeof(user_agent),
             "Mozilla 5.0 (compatible; TLEscope/%s; +https://github.com/aweeri/TLEscope)",
             TLESCOPE_VERSION);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);

#if defined(_WIN32) || defined(_WIN64)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    result.http_code = http_code;

    // Error handling per Celestrak guidelines:
    // 301, 403, 404, 500 → halt retries to prevent IP ban
    if (res == CURLE_OK && http_code == 200)
    {
        result.data = chunk.memory;
        result.size = chunk.size;
        result.success = true;
    }
    else
    {
        printf("HTTP fetch failed: %s (HTTP %ld)\n", url, http_code);
        free(chunk.memory);
    }

    return result;
}

/* ── High-Level Fetch ─────────────────────────────────────────────────────── */

FetchResult FetchFromSource(const DataSource *source, OrbitalDataFormat format)
{
    FetchResult result = {0};
    result.success = false;

    // Build URL
    char url[512];
    if (source->type == PROVIDER_CUSTOM)
    {
        // Custom sources use their URL directly; append format if needed
        snprintf(url, sizeof(url), "%s", source->base_url);
    }
    else
    {
        const DataProvider *provider = GetProvider(source->type);
        if (!provider || !provider->build_url(source, format, url, sizeof(url)))
        {
            printf("Failed to build URL for source %s\n", source->name);
            return result;
        }
    }

    // Check cache first
    CacheEntry *cached = CacheGet(url);
    if (cached)
    {
        result.data = (char*)malloc(cached->data_size + 1);
        if (result.data)
        {
            memcpy(result.data, cached->data, cached->data_size);
            result.data[cached->data_size] = '\0';
            result.size = cached->data_size;
            result.format = cached->format;
            result.http_code = 200;
            result.success = true;
        }
        return result;
    }

    // Fetch from network
    result = http_fetch(url);
    if (result.success)
    {
        result.format = format;
        // Store in cache
        CachePut(url, result.data, result.size, format);
    }

    return result;
}

void FreeFetchResult(FetchResult *result)
{
    if (result && result->data)
    {
        free(result->data);
        result->data = NULL;
        result->size = 0;
        result->success = false;
    }
}