#include "provider.h"
#include "cache.h"
#include "storage.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// -- Built-in Source Lists --------------------------------------------------

#define CELESTRAK_BASE "https://celestrak.org/NORAD/elements/gp.php"

// CelesTrak GROUP values are stored directly in DataSource::id.  This keeps
// the user-visible catalog and the API query in one auditable table instead
// of relying on a second, position-dependent index-to-group mapping.
//
// This list mirrors the ordinary GROUP= datasets advertised on CelesTrak's
// Current GP Element Sets page.  Special queries such as GPZ/GPZ-PLUS and
// filtered views such as OLDEST/DOCKED/MOVERS are intentionally not groups.
const DataSource CELESTRAK_SOURCES[] = {
    // Special-interest satellites
    {"last-30-days",       "Last 30 Days' Launches",                    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"stations",           "Space Stations",                            CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"visual",             "100 Brightest",                             CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"active",             "Active Satellites",                         CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"analyst",            "Analyst Satellites",                        CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"fengyun-1c-debris",  "Chinese ASAT Test Debris (FENGYUN 1C)",     CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"iridium-33-debris",  "IRIDIUM 33 Debris",                         CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"cosmos-2251-debris", "COSMOS 2251 Debris",                        CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},

    // Weather and Earth resources
    {"weather",            "Weather",                                    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"resource",           "Earth Resources",                            CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"sar",                "Synthetic Aperture Radar",                   CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"sarsat",             "Search & Rescue (SARSAT)",                  CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"dmc",                "Disaster Monitoring",                        CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"tdrss",              "Tracking and Data Relay Satellites (TDRSS)", CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"argos",              "ARGOS Data Collection System",              CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"planet",             "Planet",                                     CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"spire",              "Spire",                                      CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},

    // Communications satellites
    {"geo",                "Active Geosynchronous",                      CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"intelsat",           "Intelsat",                                   CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"ses",                "SES",                                        CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"eutelsat",           "Eutelsat",                                   CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"telesat",            "Telesat",                                    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"starlink",           "Starlink",                                   CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"oneweb",             "OneWeb",                                     CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"qianfan",            "Qianfan",                                    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"hulianwang",         "Hulianwang Digui",                           CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"kuiper",             "Kuiper",                                     CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"iridium-NEXT",       "Iridium NEXT",                               CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"orbcomm",            "Orbcomm",                                    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"globalstar",         "Globalstar",                                 CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"amateur",            "Amateur Radio",                              CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"satnogs",            "SatNOGS",                                    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"x-comm",             "Experimental Comm",                          CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"other-comm",         "Other Comm",                                 CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},

    // Navigation satellites
    {"gnss",               "GNSS",                                       CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"gps-ops",            "GPS Operational",                            CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"glo-ops",            "GLONASS Operational",                        CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"galileo",            "Galileo",                                    CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"beidou",             "Beidou",                                     CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"sbas",               "Satellite-Based Augmentation System",        CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},

    // Scientific satellites
    {"science",            "Space & Earth Science",                      CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"geodetic",           "Geodetic",                                   CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"engineering",        "Engineering",                                CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"education",          "Education",                                  CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},

    // Miscellaneous satellites
    {"misc",               "Miscellaneous",                              CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"military",           "Miscellaneous Military",                     CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"radar",              "Radar Calibration",                          CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
    {"cubesat",            "CubeSats",                                   CELESTRAK_BASE, PROVIDER_CELESTRAK, FORMAT_OMM_JSON, {0}},
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

// -- Retlector group name mapping ------------------------------------------

static const char* retlector_group_for_index(int idx)
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

// -- Celestrak URL builder --------------------------------------------------

static bool celestrak_build_url(const DataSource *source, OrbitalDataFormat format,
                                 char *url, size_t url_size)
{
    const char *group = source ? source->id : NULL;
    if (!group || !group[0]) return false;

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

// -- Retlector URL builder --------------------------------------------------

static bool retlector_build_url(const DataSource *source, OrbitalDataFormat format,
                                 char *url, size_t url_size)
{
    int idx = atoi(source->id) - 1;
    const char *group = retlector_group_for_index(idx);
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

// -- Provider Registry ------------------------------------------------------

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

// -- libcurl memory callback ------------------------------------------------

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

// -- HTTP Fetch -------------------------------------------------------------

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

// -- High-Level Fetch -------------------------------------------------------

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

// -- Retlector Group List Fetch ---------------------------------------------

// helper: copy a string value from a JSON node into a fixed-size char buffer
static void copy_str(char *dst, size_t dst_size, const std::string &src)
{
    if (!dst || dst_size == 0) return;
    size_t n = src.size();
    if (n > dst_size - 1) n = dst_size - 1;
    memcpy(dst, src.data(), n);
    dst[n] = '\0';
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

    // expected format: {"count": N, "groups": [{...}, ...]}
    // each group object has nested objects (e.g. "endpoints": {...})
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(result.data, result.data + result.size);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Retlector API returned invalid JSON: %s", e.what());
        FreeFetchResult(&result);
        return -1;
    }

    auto groups_it = root.find("groups");
    if (groups_it == root.end() || !groups_it->is_array())
    {
        LOG_ERROR("Retlector API response missing 'groups' array");
        FreeFetchResult(&result);
        return -1;
    }

    int count = 0;
    for (const auto &obj : *groups_it)
    {
        if (count >= max_groups) break;

        std::string name;
        if (obj.contains("name") && obj["name"].is_string())
            name = obj["name"].get<std::string>();
        if (name.empty()) continue;

        RetlectorGroup *g = &groups[count];
        copy_str(g->name, sizeof(g->name), name);
        snprintf(g->csv_endpoint, sizeof(g->csv_endpoint),
                 "https://retlector.eu/%s/csv", name.c_str());
        copy_str(g->status, sizeof(g->status),
                 obj.value("status", std::string()));
        copy_str(g->status_label, sizeof(g->status_label),
                 obj.value("statusLabel", std::string()));
        copy_str(g->last_updated, sizeof(g->last_updated),
                 obj.value("lastUpdated", std::string()));
        g->age_seconds = (obj.contains("ageSeconds") && obj["ageSeconds"].is_number())
                             ? obj["ageSeconds"].get<int>() : 0;
        g->cache_duration_seconds = (obj.contains("cacheDurationSeconds") && obj["cacheDurationSeconds"].is_number())
                             ? obj["cacheDurationSeconds"].get<int>() : 0;
        g->selected = false;
        count++;
    }

    LOG_INFO("Fetched %d retlector groups from API", count);
    FreeFetchResult(&result);
    return count;
}

// -- Format Detection -------------------------------------------------------

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

// -- Custom URL Fetch with Auto-Detect --------------------------------------

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
