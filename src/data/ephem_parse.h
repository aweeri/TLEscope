#ifndef EPHEM_PARSE_H
#define EPHEM_PARSE_H

#include "data/ephem.h"

#include <cstddef>

/* Parse a Horizons VECTORS export; on failure writes a message into `err`. */
bool EphemParseHorizons(const char *text, size_t len, EphemMission *out, char *err, size_t err_sz);

#endif // EPHEM_PARSE_H
