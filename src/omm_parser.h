#ifndef OMM_PARSER_H
#define OMM_PARSER_H

#include "types.h"
#include <stdbool.h>
#include <time.h>

/*
 * omm_parser.h — CCSDS Orbit Mean-Elements Message (OMM) parser
 *
 * Parses JSON and CSV formatted OMM data (CCSDS 502.0-B-3 / 505.0-B-3)
 * and converts to internal Satellite structs for SGP4 propagation.
 *
 * Supported formats:
 *   - JSON: Array of OMM objects with standard CCSDS keywords
 *   - CSV:  Comma-separated values with OMM keyword header
 */

// Parse a JSON OMM response into Satellite structs
// Returns the number of satellites parsed
int ParseOMMJson(const char *json, size_t size,
                 Satellite *sats, int *count, int max,
                 const char *source_name, OrbitalDataFormat fmt);

// Parse a CSV OMM response into Satellite structs
// Returns the number of satellites parsed
int ParseOMMCsv(const char *csv, size_t size,
                Satellite *sats, int *count, int max,
                const char *source_name, OrbitalDataFormat fmt);

#endif // OMM_PARSER_H