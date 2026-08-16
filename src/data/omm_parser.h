#ifndef OMM_PARSER_H
#define OMM_PARSER_H

/**
 * @file omm_parser.h
 * @brief CCSDS Orbit Mean-Elements Message (OMM) parser
 *
 * Parses JSON and CSV formatted OMM data (CCSDS 502.0-B-3 / 505.0-B-3)
 * and converts to internal Satellite structs for SGP4 propagation.
 *
 * Supported formats:
 *   - JSON: array of OMM objects with standard CCSDS keywords
 *   - CSV:  comma-separated values with OMM keyword header
 */

#include "core/types.h"
#include <stdbool.h>
#include <time.h>

/** parse a JSON OMM response into Satellite structs
 *  returns the number of satellites parsed */
int ParseOMMJson(const char *json, size_t size,
                 Satellite *sats, int *count, int max,
                 const char *source_name, OrbitalDataFormat fmt);

/** parse a CSV OMM response into Satellite structs
 *  returns the number of satellites parsed */
int ParseOMMCsv(const char *csv, size_t size,
                Satellite *sats, int *count, int max,
                const char *source_name, OrbitalDataFormat fmt);

#endif // OMM_PARSER_H