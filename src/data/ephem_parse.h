#ifndef EPHEM_PARSE_H
#define EPHEM_PARSE_H

/**
 * @file ephem_parse.h
 * @brief Parser for JPL Horizons VECTORS (CSV) ephemeris exports
 *
 * Pure computation: no UI, no drawing, no file I/O. The caller reads the file
 * and owns the text buffer.
 *
 * The parser converts the raw Horizons table into the app's storage layout:
 *   - times:      unix seconds (UTC), TDB -> UTC corrected
 *   - positions:  km, app axis convention (x, z, -y)
 *   - velocities: km/s, same axis convention
 * and precomputes per-sample latitude/longitude/GMST/sunlit flags.
 *
 * On any rejection (no $$SOE/$$EOE block, ecliptic export, fewer than two
 * usable rows, ...) it returns false and writes a human-readable message into
 * `err`, which the caller may show to the user verbatim.
 */

#include "data/ephem.h"

#include <cstddef>

/**
 * Parse a Horizons VECTORS export.
 *
 * @param text    raw file contents (not necessarily NUL-terminated)
 * @param len     number of bytes in `text`
 * @param out     mission to fill; `id` is left 0 (assigned by EphemAdd),
 *                `name`/`target` are left untouched (the caller provides them)
 * @param err     receives a human-readable error message on failure
 * @param err_sz  capacity of `err`
 * @return true on success, false on failure (with `err` filled in)
 */
bool EphemParseHorizons(const char *text, size_t len, EphemMission *out, char *err, size_t err_sz);

#endif // EPHEM_PARSE_H
