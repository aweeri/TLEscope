#include "cache.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <raylib.h>

/* -- Static cache storage -------------------------------------------------- */

static CacheEntry cache[MAX_CACHE_ENTRIES];
static int cache_count = 0;

/* guards all cache state so worker threads can safely call CacheGet/CachePut */
static std::mutex s_cache_mutex;

/* -- Simple URL hash (djb2) ------------------------------------------------ */

static void hash_url(const char *url, char *out, size_t out_size)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *url++) && out_size > 1)
        hash = ((hash << 5) + hash) + c;
    snprintf(out, out_size, "%016lx", hash);
}

/* -- Public API ------------------------------------------------------------ */

bool IsCacheValid(const CacheEntry *entry)
{
    if (!entry || !entry->valid) return false;
    time_t now = time(NULL);
    return (now - entry->fetch_time) < CACHE_TTL_SECONDS;
}

CacheEntry* CacheGet(const char *url)
{
    if (!url) return NULL;

    std::lock_guard<std::mutex> lock(s_cache_mutex);

    char h[64];
    hash_url(url, h, sizeof(h));

    for (int i = 0; i < cache_count; i++)
    {
        if (strcmp(cache[i].url_hash, h) == 0)
        {
            if (IsCacheValid(&cache[i]))
                return &cache[i];
            // expired - mark invalid
            cache[i].valid = false;
            return NULL;
        }
    }
    return NULL;
}

void CachePut(const char *url, const char *data, size_t size, OrbitalDataFormat format)
{
    if (!url || !data || size == 0) return;

    std::lock_guard<std::mutex> lock(s_cache_mutex);

    char h[64];
    hash_url(url, h, sizeof(h));

    // find existing entry to overwrite, or use next slot
    int idx = -1;
    for (int i = 0; i < cache_count; i++)
    {
        if (strcmp(cache[i].url_hash, h) == 0)
        {
            idx = i;
            break;
        }
    }

    if (idx < 0)
    {
        if (cache_count >= MAX_CACHE_ENTRIES)
        {
            // evict oldest entry
            time_t oldest = cache[0].fetch_time;
            idx = 0;
            for (int i = 1; i < cache_count; i++)
            {
                if (cache[i].fetch_time < oldest)
                {
                    oldest = cache[i].fetch_time;
                    idx = i;
                }
            }
            free(cache[idx].data);
        }
        else
        {
            idx = cache_count++;
        }
    }
    else
    {
        free(cache[idx].data);
    }

    cache[idx].data = (char*)malloc(size + 1);
    if (cache[idx].data)
    {
        memcpy(cache[idx].data, data, size);
        cache[idx].data[size] = '\0';
    }
    strncpy(cache[idx].url_hash, h, sizeof(cache[idx].url_hash) - 1);
    cache[idx].fetch_time = time(NULL);
    cache[idx].data_size = size;
    cache[idx].format = format;
    cache[idx].valid = true;
}

void CacheClear(void)
{
    std::lock_guard<std::mutex> lock(s_cache_mutex);

    for (int i = 0; i < cache_count; i++)
    {
        free(cache[i].data);
        cache[i].data = NULL;
        cache[i].valid = false;
    }
    cache_count = 0;
}

bool CacheSave(const char *filename)
{
    std::lock_guard<std::mutex> lock(s_cache_mutex);

    FILE *f = fopen(filename, "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"cache_entries\": %d,\n", cache_count);
    fprintf(f, "  \"entries\": [\n");
    for (int i = 0; i < cache_count; i++)
    {
        if (!cache[i].valid) continue;
        fprintf(f, "    {\n");
        fprintf(f, "      \"url_hash\": \"%s\",\n", cache[i].url_hash);
        fprintf(f, "      \"fetch_time\": %ld,\n", (long)cache[i].fetch_time);
        fprintf(f, "      \"format\": %d,\n", (int)cache[i].format);
        fprintf(f, "      \"data_size\": %zu,\n", cache[i].data_size);
        // store data as base64 or hex? for simplicity, skip data persistence for now.
        fprintf(f, "      \"data\": \"\"\n");
        fprintf(f, "    }");
        if (i < cache_count - 1) fprintf(f, ",");
        fprintf(f, "\n");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    return true;
}

bool CacheLoad(const char *filename)
{
    // cache is ephemeral - we just track metadata on disk
    // actual data is re-fetched on next launch
    (void)filename;
    cache_count = 0;
    return true;
}