#include "omm_parser.h"
#include "core/astro.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <nlohmann/json.hpp>

//
// JSON OMM Parser
//
// Parses satellite orbital data in JSON OMM (Orbital Mean-Elements Message) format.
// This is the modern standard for distributing TLE-equivalent data.
//
// Expected JSON format (CCSDS OMM):
//  [
//    {
//      "OBJECT_NAME": "ISS (ZARYA)",
//      "OBJECT_ID": "1998-067A",
//      "EPOCH": "2024-01-15 12:00:00.000000",
//      "MEAN_MOTION": 15.50123456,
//      "ECCENTRICITY": 0.0001234,
//      "INCLINATION": 51.6400,
//      "RA_OF_ASC_NODE": 120.0000,
//      "ARG_OF_PERICENTER": 200.0000,
//      "MEAN_ANOMALY": 300.0000,
//      "BSTAR": 0.00012345,
//      "NORAD_CAT_ID": 25544,
//      "EPOCH_MICROSECONDS": 0
//    },
//    ...
//  ]

// converts a UTC OMM timestamp (ISO T or space separator) to YYYYDDD.FFFF
static double omm_epoch_to_epoch(const char *epoch_str)
{
    if (!epoch_str || !*epoch_str) return 0;

    int year = 0, month = 0, day = 0, hour = 0, min = 0;
    double sec = 0.0;
    char separator = '\0';
    int end = 0;
    if (sscanf(epoch_str, "%d-%d-%d%c%d:%d:%lf%n", &year, &month, &day,
               &separator, &hour, &min, &sec, &end) != 7 ||
        (separator != 'T' && separator != ' ') ||
        year < 1 || month < 1 || month > 12 || hour < 0 || hour > 23 ||
        min < 0 || min > 59 || !isfinite(sec) || sec < 0.0 || sec >= 60.0)
        return 0;

    const char *tail = epoch_str + end;
    if (*tail == 'Z') tail++;
    while (isspace((unsigned char)*tail)) tail++;
    if (*tail != '\0') return 0;

    if (year < 100) year += 2000;

    // calculate day of year
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
        days_in_month[1] = 29;
    if (day < 1 || day > days_in_month[month - 1]) return 0;

    int doy = 0;
    for (int m = 0; m < month - 1; m++)
        doy += days_in_month[m];
    doy += day;

    double fraction = (hour + min / 60.0 + sec / 3600.0) / 24.0;
    return year * 1000.0 + doy + fraction;
}

int ParseOMMJson(const char *json, size_t size, Satellite *sats, int *count, int max,
                  const char *source_name, OrbitalDataFormat fmt)
{
    (void)size;
    (void)count;
    (void)sats;
    if (!json || !count || !sats) return 0;

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(json);
    }
    catch (const std::exception &)
    {
        LOG_WARN("OMM JSON: failed to parse JSON document");
        return 0;
    }
    if (!root.is_array())
    {
        LOG_WARN("OMM JSON: expected a top-level array of objects");
        return 0;
    }

    int parsed = 0;
    for (size_t i = 0; i < root.size() && parsed < max; i++)
    {
        if (!root[i].is_object())
            continue;
        const nlohmann::json &o = root[i];

        // extract fields
        char name[64] = {0};
        char norad_id[16] = {0};
        char intl_desig[16] = {0};
        char epoch_str[32] = {0};

        auto get_s = [&o](const char *key, char *buf, size_t buf_size) {
            auto it = o.find(key);
            if (it != o.end() && it->is_string())
                snprintf(buf, buf_size, "%s", it->get_ref<const std::string &>().c_str());
        };
        auto get_d = [&o](const char *key, double def) -> double {
            auto it = o.find(key);
            if (it != o.end() && it->is_number())
                return it->get<double>();
            return def;
        };
        auto get_l = [&o](const char *key, long def) -> long {
            auto it = o.find(key);
            if (it != o.end() && it->is_number())
                return it->get<long long>();
            return def;
        };

        get_s("OBJECT_NAME", name, sizeof(name));
        long norad = get_l("NORAD_CAT_ID", 0);
        snprintf(norad_id, sizeof(norad_id), "%ld", norad);
        get_s("OBJECT_ID", intl_desig, sizeof(intl_desig));
        LOG_DEBUG("Parsed OMM JSON sat: %s (NORAD: %s)", name, norad_id);
        get_s("EPOCH", epoch_str, sizeof(epoch_str));

        double epoch = omm_epoch_to_epoch(epoch_str);
        if (epoch == 0)
        {
            LOG_WARN("Skipping OMM satellite %s: invalid epoch '%s'", name, epoch_str);
            continue;
        }
        double inclination = get_d("INCLINATION", 0.0);
        double raan = get_d("RA_OF_ASC_NODE", 0.0);
        double eccentricity = get_d("ECCENTRICITY", 0.0);
        double arg_perigee = get_d("ARG_OF_PERICENTER", 0.0);
        double mean_anomaly = get_d("MEAN_ANOMALY", 0.0);
        double mean_motion = get_d("MEAN_MOTION", 0.0);
        double bstar = get_d("BSTAR", 0.0);

        // handle microsecond precision
        long epoch_us = get_l("EPOCH_MICROSECONDS", 0);
        if (epoch_us > 0)
            epoch += (double)epoch_us / 86400000000.0;

        OrbitalDataMeta meta = {0};
        if (source_name)
            strncpy(meta.source_name, source_name, sizeof(meta.source_name) - 1);
        meta.format = fmt;
        meta.fetch_time = time(NULL);
        meta.epoch_time = (time_t)get_unix_from_epoch(epoch);

        if (add_satellite_from_omm_elements_to(
                sats, count, name, norad_id, intl_desig, epoch,
                inclination, raan, eccentricity, arg_perigee,
                mean_anomaly, mean_motion, bstar, &meta))
        {
            parsed++;
        }
    }

    // NOTE: add_satellite_from_omm_elements already increments sat_count,
    // so we do NOT do *count += parsed here to avoid double-counting.
    return parsed;
}

//
// CSV OMM Parser
//
// Expected CSV format (CCSDS OMM):
// OBJECT_NAME,OBJECT_ID,EPOCH,INCLINATION,RA_OF_ASC_NODE,ECCENTRICITY,ARG_OF_PERICENTER,MEAN_ANOMALY,MEAN_MOTION,BSTAR,NORAD_CAT_ID
// "ISS (ZARYA)","1998-067A","2024-01-15 12:00:00.000000",51.6400,120.0000,0.0001234,200.0000,300.0000,15.50123456,0.00012345,25544
// ...

// find column index in CSV header
static int csv_find_column(const char *header, const char *name)
{
    if (!header || !name) return -1;

    int col = 0;
    const char *ptr = header;
    char col_name[64];

    while (*ptr)
    {
        int i = 0;
        while (*ptr && *ptr != ',' && *ptr != '\r' && *ptr != '\n' && i < 63)
            col_name[i++] = *ptr++;
        col_name[i] = '\0';

        // trim quotes
        char *start = col_name;
        char *end = col_name + strlen(col_name) - 1;
        if (*start == '"') start++;
        if (end > start && *end == '"') *end = '\0';

        if (strcmp(start, name) == 0)
            return col;

        if (*ptr == ',') { ptr++; col++; }
        else break;
    }
    return -1;
}

// get value at column index from a CSV line
static const char* csv_get_column(const char *line, int col_idx, char *buf, size_t buf_size)
{
    if (!line || col_idx < 0) return NULL;

    int col = 0;
    const char *ptr = line;

    while (*ptr && col < col_idx)
    {
        if (*ptr == ',') col++;
        ptr++;
    }

    if (col != col_idx) return NULL;

    // extract value
    size_t i = 0;
    bool quoted = (*ptr == '"');
    if (quoted) ptr++;

    while (*ptr && i < buf_size - 1)
    {
        if (quoted)
        {
            if (*ptr == '"') { ptr++; break; }
            buf[i++] = *ptr++;
        }
        else
        {
            if (*ptr == ',' || *ptr == '\r' || *ptr == '\n') break;
            buf[i++] = *ptr++;
        }
    }
    buf[i] = '\0';

    return buf;
}

int ParseOMMCsv(const char *csv, size_t size, Satellite *sats, int *count, int max,
                 const char *source_name, OrbitalDataFormat fmt)
{
    (void)size;
    (void)count;
    (void)sats;
    if (!csv || !count || !sats) return 0;

    // find the header line (first non-empty line)
    const char *header = csv;
    while (*header && (*header == '\r' || *header == '\n')) header++;
    if (!*header) return 0;

    // find column indices
    int col_name = csv_find_column(header, "OBJECT_NAME");
    int col_norad = csv_find_column(header, "NORAD_CAT_ID");
    int col_id = csv_find_column(header, "OBJECT_ID");
    int col_epoch = csv_find_column(header, "EPOCH");
    int col_incl = csv_find_column(header, "INCLINATION");
    int col_raan = csv_find_column(header, "RA_OF_ASC_NODE");
    int col_ecc = csv_find_column(header, "ECCENTRICITY");
    int col_argp = csv_find_column(header, "ARG_OF_PERICENTER");
    int col_ma = csv_find_column(header, "MEAN_ANOMALY");
    int col_mm = csv_find_column(header, "MEAN_MOTION");
    int col_bstar = csv_find_column(header, "BSTAR");

    if (col_norad < 0 || col_epoch < 0 || col_incl < 0 || col_mm < 0)
    {
        LOG_WARN("CSV OMM: Missing required columns");
        return 0;
    }

    // skip to first data line
    const char *ptr = header;
    while (*ptr && *ptr != '\n') ptr++;
    if (*ptr == '\n') ptr++;

    int parsed = 0;
    char buf[256];

    while (*ptr && parsed < max)
    {
        // skip empty lines
        while (*ptr == '\r' || *ptr == '\n') ptr++;
        if (!*ptr) break;

        // get end of line
        const char *eol = ptr;
        while (*eol && *eol != '\n') eol++;

        // extract line
        size_t line_len = (size_t)(eol - ptr);
        if (line_len > sizeof(buf) - 1) line_len = sizeof(buf) - 1;
        strncpy(buf, ptr, line_len);
        buf[line_len] = '\0';

        // parse columns
        char val[128];

        // norad ID
        csv_get_column(buf, col_norad, val, sizeof(val));
        char norad_id[16];
        snprintf(norad_id, sizeof(norad_id), "%s", val);
        LOG_DEBUG("Parsed OMM CSV sat (NORAD: %s)", norad_id);

        // name
        char name[64] = "";
        if (col_name >= 0)
            csv_get_column(buf, col_name, name, sizeof(name));

        // international designator
        char intl_desig[16] = "";
        if (col_id >= 0)
            csv_get_column(buf, col_id, intl_desig, sizeof(intl_desig));

        // epoch
        csv_get_column(buf, col_epoch, val, sizeof(val));
        double epoch = omm_epoch_to_epoch(val);
        if (epoch == 0)
        {
            LOG_WARN("Skipping OMM satellite %s: invalid epoch '%s'", name, val);
            ptr = eol;
            if (*ptr == '\n') ptr++;
            continue;
        }

        // inclination
        csv_get_column(buf, col_incl, val, sizeof(val));
        double inclination = strtod(val, NULL);

        // raan
        csv_get_column(buf, col_raan, val, sizeof(val));
        double raan = strtod(val, NULL);

        // eccentricity
        double eccentricity = 0.0;
        if (col_ecc >= 0)
        {
            csv_get_column(buf, col_ecc, val, sizeof(val));
            eccentricity = strtod(val, NULL);
        }

        // arg of perigee
        double arg_perigee = 0.0;
        if (col_argp >= 0)
        {
            csv_get_column(buf, col_argp, val, sizeof(val));
            arg_perigee = strtod(val, NULL);
        }

        // mean anomaly
        double mean_anomaly = 0.0;
        if (col_ma >= 0)
        {
            csv_get_column(buf, col_ma, val, sizeof(val));
            mean_anomaly = strtod(val, NULL);
        }

        // mean motion
        csv_get_column(buf, col_mm, val, sizeof(val));
        double mean_motion = strtod(val, NULL);

        // b*
        double bstar = 0.0;
        if (col_bstar >= 0)
        {
            csv_get_column(buf, col_bstar, val, sizeof(val));
            bstar = strtod(val, NULL);
        }

        OrbitalDataMeta meta = {0};
        if (source_name)
            strncpy(meta.source_name, source_name, sizeof(meta.source_name) - 1);
        meta.format = fmt;
        meta.fetch_time = time(NULL);
        meta.epoch_time = (time_t)get_unix_from_epoch(epoch);

        if (add_satellite_from_omm_elements_to(
                sats, count, name, norad_id, intl_desig, epoch,
                inclination, raan, eccentricity, arg_perigee,
                mean_anomaly, mean_motion, bstar, &meta))
        {
            parsed++;
        }

        ptr = eol;
        if (*ptr == '\n') ptr++;
    }

    // NOTE: add_satellite_from_omm_elements already increments sat_count,
    // so we do NOT do *count += parsed here to avoid double-counting.
    return parsed;
}