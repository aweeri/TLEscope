#ifndef CACHE_H
#define CACHE_H

#include "types.h"
#include <time.h>
#include <stdbool.h>

/**
 * @file cache.h
 * @brief Response cache manager
 *
 * Implements the 120-minute TTL recommended by Celestrak's API guidelines.
 * Caches responses keyed by URL hash to avoid redundant network requests.
 */

#define CACHE_TTL_SECONDS 7200  // 120 minutes
#define MAX_CACHE_ENTRIES 64

typedef struct {
    char url_hash[64];      // hash of the URL
    time_t fetch_time;      // when data was fetched
    char *data;             // cached response
    size_t data_size;
    OrbitalDataFormat format;
    bool valid;
} CacheEntry;

// -- Cache Operations --------------------------------------------------------

/** check if a cache entry is still valid (within TTL) */
bool IsCacheValid(const CacheEntry *entry);

/** get a cache entry by URL. returns NULL if not found or expired. */
CacheEntry* CacheGet(const char *url);

/** store a response in the cache */
void CachePut(const char *url, const char *data, size_t size, OrbitalDataFormat format);

/** invalidate all cache entries */
void CacheClear(void);

/** save cache to disk */
bool CacheSave(const char *filename);

/** load cache from disk */
bool CacheLoad(const char *filename);

#endif // CACHE_H