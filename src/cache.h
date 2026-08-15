#ifndef CACHE_H
#define CACHE_H

#include "types.h"
#include <time.h>
#include <stdbool.h>

/*
 * cache.h — Response cache manager
 *
 * Implements the 120-minute TTL recommended by Celestrak's API guidelines.
 * Caches responses keyed by URL hash to avoid redundant network requests.
 */

#define CACHE_TTL_SECONDS 7200  // 120 minutes
#define MAX_CACHE_ENTRIES 64

typedef struct {
    char url_hash[64];      // Hash of the URL
    time_t fetch_time;      // When data was fetched
    char *data;             // Cached response
    size_t data_size;
    OrbitalDataFormat format;
    bool valid;
} CacheEntry;

// ── Cache Operations ────────────────────────────────────────────────────────

// Check if a cache entry is still valid (within TTL)
bool IsCacheValid(const CacheEntry *entry);

// Get a cache entry by URL. Returns NULL if not found or expired.
CacheEntry* CacheGet(const char *url);

// Store a response in the cache
void CachePut(const char *url, const char *data, size_t size, OrbitalDataFormat format);

// Invalidate all cache entries
void CacheClear(void);

// Save cache to disk
bool CacheSave(const char *filename);

// Load cache from disk
bool CacheLoad(const char *filename);

#endif // CACHE_H