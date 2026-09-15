#ifndef TLESCOPE_UTIL_VERSION_H
#define TLESCOPE_UTIL_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Compare two TLEscope version strings ("vX.Y.Z..." or "X.Y.Z...") using only
 * the MAJOR.MINOR.PATCH tag prefix. Suffixes such as "-12-g4f2a1c9" or
 * "-dirty" are ignored, so a dev build compares equal to its base release.
 *
 * Returns <0 if a < b, 0 if equal, >0 if a > b. If either string cannot be
 * parsed as a numeric tag, falls back to strcmp().
 *
 * Intended for the update checker, which must not do naive string comparison
 * on git-describe strings (e.g. "v3.9.2-12-g4f2a1c9" vs "v3.9.2"). */
int tlescope_version_compare(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* TLESCOPE_UTIL_VERSION_H */