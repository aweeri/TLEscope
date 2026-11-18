#ifndef SPACETRACK_API_H
#define SPACETRACK_API_H

/* Space-Track.org GP access: credential file, session login, paced queries.
   Query payloads are OMM JSON; parse them with ParseOMMJson(). */

#include "data/provider.h"
#include <stdbool.h>
#include <stddef.h>

// -- Credentials -------------------------------------------------------------

/** load identity/password from spacetrack.json next to the executable;
    returns false when absent or unreadable (not an error worth a toast) */
bool SpacetrackLoadCredentials(char *identity, size_t identity_size,
                               char *password, size_t password_size);

/** write spacetrack.json (0600 on POSIX); refuses empty identity or password */
bool SpacetrackSaveCredentials(const char *identity, const char *password);

/** true when a non-empty identity and password are available */
bool SpacetrackConfigured(void);

// -- Session -----------------------------------------------------------------

/** drop the cached session cookie (call when credentials change) */
void SpacetrackLogout(void);

/** synchronous login on the calling thread; true on success.
    Safe to call from a worker thread or from a detached UI thread. */
bool SpacetrackLogin(void);

/** true when a session cookie is held and not past its expiry */
bool SpacetrackSignedIn(void);

// -- Presets -----------------------------------------------------------------

/** resolve a preset slug ("active", "last-30-days", ...) to its GP predicate path;
    returns NULL for an unknown slug */
const char *SpacetrackPredicateForSlug(const char *slug);

/** predicate path to use for a cart entry: identifiers starting with "custom:"
    read the predicate from custom_query (the entry's paste_data), anything else
    resolves through the preset slug */
const char *SpacetrackResolvePredicate(const char *identifier, const char *custom_query);

/** build the cart identifier for an arbitrary predicate: "custom:<8 hex djb2>".
    Unique per predicate so the cart's (type, identifier) dedupe cannot swallow a
    second, different custom query. */
void SpacetrackCustomIdentifier(const char *predicate, char *out, size_t out_size);

// -- Queries -----------------------------------------------------------------

/** build the full unauthenticated class/gp/<predicate>/format/json URL.
    Percent-encodes '>', '<', '"' and space. False on empty predicate or overflow. */
bool SpacetrackBuildQueryUrl(const char *predicate, char *url, size_t url_size);

/** fetch class/gp/<predicate>/format/json, logging in and re-logging in as needed.
    The returned FetchResult is heap-allocated OMM JSON with format == FORMAT_OMM_JSON;
    free with FreeFetchResult(). Failure clears success and leaves data NULL. */
FetchResult SpacetrackFetchQuery(const char *predicate);

/** last human-readable failure, e.g. "login failed (HTTP 401)",
    "rate limited (HTTP 429)", "request failed (HTTP 503)"; empty when the last
    request succeeded. Copy it, the buffer is replaced on the next failure. */
const char *SpacetrackLastError(void);

/** seconds left in the post-failure cooldown; 0 when a request may be issued.
    Non-blocking (lock-free), so the UI can disable buttons while cooling down. */
int SpacetrackCooldownRemainingSeconds(void);

#endif // SPACETRACK_API_H
