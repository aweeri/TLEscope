#include "provider.h"
#include "cache.h"
#include "storage.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>

/* -- Built-in Source Lists ------------------------------------------------- */

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

/* -- Group name mapping ---------------------------------------------------- */

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

/* -- Celestrak URL builder ------------------------------------------------- */

static bool celestrak_build_url(const DataSource *source, OrbitalDataFormat format,
                                 char *url, size_t url_size)
{
    // extract the group name from the source index
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

/* -- Retlector URL builder ------------------------------------------------- */

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

/* -- Provider Registry ----------------------------------------------------- */

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

/* -- libcurl memory callback ----------------------------------------------- */

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

/* -- HTTP Fetch ------------------------------------------------------------ */

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
        LOG_ERROR("curl_easy_init failed for URL: %s", url);
        free(chunk.memory);
        return result;
    }

    LOG_DEBUG("HTTP fetch: %s", url);

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

    // error handling per Celestrak guidelines:
    // 301, 403, 404, 500 -> halt retries to prevent IP ban
    if (res == CURLE_OK && http_code != 301 && http_code != 403 &&
        http_code != 404 && http_code != 500)
    {
        result.data = chunk.memory;
        result.size = chunk.size;
        result.success = true;
        LOG_INFO("HTTP fetch OK: %s (%zu bytes, HTTP %ld)", url, chunk.size, http_code);
    }
    else
    {
        LOG_ERROR("HTTP fetch failed: %s (HTTP %ld)", url, http_code);
        free(chunk.memory);
    }

    return result;
}

/* -- High-Level Fetch ------------------------------------------------------ */

FetchResult FetchFromSource(const DataSource *source, OrbitalDataFormat format)
{
    FetchResult result = {0};
    result.success = false;

    // build URL
    char url[512];
    if (source->type == PROVIDER_CUSTOM)
    {
        // custom sources use their URL directly; append format if needed
        snprintf(url, sizeof(url), "%s", source->base_url);
    }
    else
    {
        const DataProvider *provider = GetProvider(source->type);
        if (!provider || !provider->build_url(source, format, url, sizeof(url)))
        {
            LOG_ERROR("Failed to build URL for source %s", source->name);
            return result;
        }
    }

    // check cache first
    CacheEntry *cached = CacheGet(url);
    if (cached)
    {
        LOG_DEBUG("Cache HIT for %s", url);
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

    LOG_DEBUG("Cache MISS for %s - fetching from network", url);
    // fetch from network
    result = http_fetch(url);
    if (result.success)
    {
        result.format = format;
        // store in cache
        CachePut(url, result.data, result.size, format);
        LOG_DEBUG("Cached %zu bytes for %s", result.size, url);
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

/* -- Retlector Group List Fetch -------------------------------------------- */

/** helper: find matching closing brace, handling nested braces */
static const char* find_matching_brace(const char *open_brace)
{
    if (!open_brace || *open_brace != '{') return NULL;
    int depth = 1;
    const char *p = open_brace + 1;
    while (*p && depth > 0)
    {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        if (depth > 0) p++;
    }
    return (depth == 0) ? p : NULL;
}

int FetchRetlectorGroups(RetlectorGroup *groups, int max_groups)
{
    if (!groups || max_groups <= 0) return -1;

    const char *url = "https://retlector.eu/api/v1/groups";
    FetchResult result = http_fetch(url);
    if (!result.success)
    {
        LOG_ERROR("Failed to fetch retlector groups from %s", url);
        return -1;
    }

    // parse the JSON response manually
    // expected format: {"count": N, "groups": [{...}, ...]}
    // each group object has nested objects (e.g. "endpoints": {...})
    const char *data = result.data;
    const char *groups_array = strstr(data, "\"groups\"");
    if (!groups_array)
    {
        LOG_ERROR("Retlector API response missing 'groups' array");
        FreeFetchResult(&result);
        return -1;
    }

    const char *array_start = strchr(groups_array, '[');
    if (!array_start)
    {
        FreeFetchResult(&result);
        return -1;
    }

    int count = 0;
    const char *curr = array_start + 1;
    while (curr && *curr && *curr != ']' && count < max_groups)
    {
        // skip whitespace and commas
        while (*curr && (*curr == ' ' || *curr == '\n' || *curr == '\r' || *curr == '\t' || *curr == ','))
            curr++;
        if (!curr || *curr != '{') break;

        // find the matching closing brace (handles nested objects)
        const char *obj_end = find_matching_brace(curr);
        if (!obj_end) break;

        // extract fields from this object using the full object text
        // we create a temporary null-terminated copy for strstr safety
        size_t obj_len = obj_end - curr + 1;
        char *obj_text = (char*)malloc(obj_len + 1);
        if (!obj_text) break;
        strncpy(obj_text, curr, obj_len);
        obj_text[obj_len] = '\0';

        char name_buf[64] = {0};
        char status_buf[16] = {0};
        char status_label_buf[32] = {0};
        char last_updated_buf[32] = {0};
        int age_seconds = 0;
        int cache_duration = 0;

        // parse "name"
        const char *name_key = strstr(obj_text, "\"name\"");
        if (name_key)
        {
            const char *colon = strchr(name_key, ':');
            if (colon)
            {
                const char *q = strchr(colon, '"');
                if (q)
                {
                    q++;
                    int i = 0;
                    while (*q && *q != '"' && i < 63) name_buf[i++] = *q++;
                    name_buf[i] = '\0';
                }
            }
        }

        // parse "status"
        const char *status_key = strstr(obj_text, "\"status\"");
        if (status_key)
        {
            const char *colon = strchr(status_key, ':');
            if (colon)
            {
                const char *q = strchr(colon, '"');
                if (q)
                {
                    q++;
                    int i = 0;
                    while (*q && *q != '"' && i < 15) status_buf[i++] = *q++;
                    status_buf[i] = '\0';
                }
            }
        }

        // parse "statusLabel"
        const char *sl_key = strstr(obj_text, "\"statusLabel\"");
        if (sl_key)
        {
            const char *colon = strchr(sl_key, ':');
            if (colon)
            {
                const char *q = strchr(colon, '"');
                if (q)
                {
                    q++;
                    int i = 0;
                    while (*q && *q != '"' && i < 31) status_label_buf[i++] = *q++;
                    status_label_buf[i] = '\0';
                }
            }
        }

        // parse "lastUpdated"
        const char *lu_key = strstr(obj_text, "\"lastUpdated\"");
        if (lu_key)
        {
            const char *colon = strchr(lu_key, ':');
            if (colon)
            {
                const char *q = strchr(colon, '"');
                if (q)
                {
                    q++;
                    int i = 0;
                    while (*q && *q != '"' && i < 31) last_updated_buf[i++] = *q++;
                    last_updated_buf[i] = '\0';
                }
            }
        }

        // parse "ageSeconds"
        const char *age_key = strstr(obj_text, "\"ageSeconds\"");
        if (age_key)
        {
            const char *colon = strchr(age_key, ':');
            if (colon)
            {
                colon++;
                while (*colon == ' ') colon++;
                age_seconds = atoi(colon);
            }
        }

        // parse "cacheDurationSeconds"
        const char *cd_key = strstr(obj_text, "\"cacheDurationSeconds\"");
        if (cd_key)
        {
            const char *colon = strchr(cd_key, ':');
            if (colon)
            {
                colon++;
                while (*colon == ' ') colon++;
                cache_duration = atoi(colon);
            }
        }

        // populate the group entry
        if (name_buf[0])
        {
            RetlectorGroup *g = &groups[count];
            strncpy(g->name, name_buf, sizeof(g->name) - 1);
            snprintf(g->csv_endpoint, sizeof(g->csv_endpoint),
                     "https://retlector.eu/%s/csv", name_buf);
            strncpy(g->status, status_buf, sizeof(g->status) - 1);
            strncpy(g->status_label, status_label_buf, sizeof(g->status_label) - 1);
            strncpy(g->last_updated, last_updated_buf, sizeof(g->last_updated) - 1);
            g->age_seconds = age_seconds;
            g->cache_duration_seconds = cache_duration;
            g->selected = false;
            count++;
        }

        free(obj_text);
        curr = obj_end + 1;
    }

    LOG_INFO("Fetched %d retlector groups from API", count);
    FreeFetchResult(&result);
    return count;
}

/* -- Format Detection ------------------------------------------------------ */

OrbitalDataFormat DetectDataFormat(const char *data, size_t size)
{
    if (!data || size == 0) return FORMAT_UNKNOWN;

    // skip leading whitespace
    while (size > 0 && (*data == ' ' || *data == '\r' || *data == '\n' || *data == '\t'))
    {
        data++;
        size--;
    }
    if (size == 0) return FORMAT_UNKNOWN;

    // 1. XML detection: starts with <?xml or <ndm
    if (strncmp(data, "<?xml", 5) == 0 || strncmp(data, "<ndm", 4) == 0)
    {
        LOG_INFO("Detected format: OMM_XML");
        return FORMAT_OMM_XML;
    }

    // 2. JSON detection: starts with [ or { and contains OMM keywords
    if (*data == '[' || *data == '{')
    {
        if (strstr(data, "\"OBJECT_NAME\"") || strstr(data, "\"CCSDS_OMM_VERS\""))
        {
            LOG_INFO("Detected format: OMM_JSON");
            return FORMAT_OMM_JSON;
        }
        return FORMAT_UNKNOWN;
    }

    // 3. KVN detection: contains "CCSDS_OMM_VERS ="
    if (strstr(data, "CCSDS_OMM_VERS ="))
    {
        LOG_INFO("Detected format: OMM_KVN");
        return FORMAT_OMM_KVN;
    }

    // 4. CSV detection: check first line for OMM CSV header patterns
    //    (CCSDS_OMM_VERS,CREATION_DATE... or OBJECT_NAME,OBJECT_ID...)
    {
        // look at the first line only
        const char *first_line = data;
        const char *nl = strchr(first_line, '\n');
        size_t first_line_len = nl ? (size_t)(nl - first_line) : strlen(first_line);

        // check for comma-separated OMM field names in the first line
        if (first_line_len > 10 && strchr(first_line, ','))
        {
            // check for known OMM CSV headers
            if (strstr(data, "CCSDS_OMM_VERS,CREATION_DATE") ||
                strstr(data, "OBJECT_NAME,OBJECT_ID") ||
                strstr(data, "NORAD_CAT_ID"))
            {
                LOG_INFO("Detected format: OMM_CSV");
                return FORMAT_OMM_CSV;
            }
        }
    }

    // 5. TLE detection: find "1 " line followed by "2 " line
    //    TLE can be 2-line (line1+line2) or 3-line (name+line1+line2)
    {
        const char *scan = data;
        // scan through lines looking for "1 " then "2 "
        while (*scan)
        {
            // skip blank lines and comment lines
            if (*scan == '#' || *scan == '\r' || *scan == '\n')
            {
                while (*scan && *scan != '\n') scan++;
                if (*scan == '\n') scan++;
                continue;
            }

            // check if this line starts with "1 "
            if (scan[0] == '1' && scan[1] == ' ')
            {
                // find the next line
                const char *next = strchr(scan, '\n');
                if (!next) break;
                next++;
                while (*next == '\r' || *next == '\n') next++;

                // check if it starts with "2 "
                if (next[0] == '2' && next[1] == ' ')
                {
                    LOG_INFO("Detected format: TLE");
                    return FORMAT_TLE;
                }
                break; // found "1 " but not followed by "2 " - not TLE
            }

            // skip this line (could be a name line in 3-line TLE)
            while (*scan && *scan != '\n') scan++;
            if (*scan == '\n') scan++;
        }
    }

    LOG_INFO("Detected format: UNKNOWN");
    return FORMAT_UNKNOWN;
}

/* -- Custom URL Fetch with Auto-Detect ------------------------------------- */

FetchResult FetchFromCustomURL(const char *url)
{
    FetchResult result = {0};
    result.success = false;

    if (!url || !*url)
    {
        LOG_ERROR("FetchFromCustomURL: empty URL");
        return result;
    }

    LOG_INFO("Fetching custom URL: %s", url);
    result = http_fetch(url);

    if (result.success)
    {
        // auto-detect the format
        result.format = DetectDataFormat(result.data, result.size);
        LOG_INFO("Custom URL fetch OK: %s (%zu bytes, detected: %s)",
                 url, result.size, FormatToString(result.format));
    }

    return result;
}