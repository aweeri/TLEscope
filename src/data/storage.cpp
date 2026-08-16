#include "storage.h"
#include "core/types.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <raylib.h>

/* -- Format string conversion ----------------------------------------------- */

const char* FormatToString(OrbitalDataFormat fmt)
{
    switch (fmt)
    {
        case FORMAT_TLE:     return "TLE";
        case FORMAT_OMM_JSON: return "OMM_JSON";
        case FORMAT_OMM_CSV:  return "OMM_CSV";
        case FORMAT_OMM_XML:  return "OMM_XML";
        case FORMAT_OMM_KVN:  return "OMM_KVN";
        default:              return "UNKNOWN";
    }
}

OrbitalDataFormat StringToFormat(const char *str)
{
    if (!str) return FORMAT_UNKNOWN;
    if (strcmp(str, "TLE") == 0)      return FORMAT_TLE;
    if (strcmp(str, "OMM_JSON") == 0) return FORMAT_OMM_JSON;
    if (strcmp(str, "OMM_CSV") == 0)  return FORMAT_OMM_CSV;
    if (strcmp(str, "OMM_XML") == 0)  return FORMAT_OMM_XML;
    if (strcmp(str, "OMM_KVN") == 0)  return FORMAT_OMM_KVN;
    return FORMAT_UNKNOWN;
}

/* -- Manual JSON helpers (mirrors config.cpp approach) ---------------------- */

static const char* find_key(const char *text, const char *key)
{
    if (!text || !key) return NULL;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *ptr = strstr(text, needle);
    if (!ptr) return NULL;
    ptr = strchr(ptr, ':');
    return ptr ? ptr + 1 : NULL;
}

static char* read_string(const char *text, const char *key, char *buf, size_t buf_size)
{
    const char *val = find_key(text, key);
    if (!val) { buf[0] = '\0'; return buf; }
    while (*val && isspace((unsigned char)*val)) val++;
    if (*val != '"') { buf[0] = '\0'; return buf; }
    val++;
    size_t i = 0;
    while (*val && *val != '"' && i < buf_size - 1) buf[i++] = *val++;
    buf[i] = '\0';
    return buf;
}

static int read_int(const char *text, const char *key, int def)
{
    const char *val = find_key(text, key);
    if (!val) return def;
    while (*val && isspace((unsigned char)*val)) val++;
    return (int)strtol(val, NULL, 10);
}

static long read_long(const char *text, const char *key, long def)
{
    const char *val = find_key(text, key);
    if (!val) return def;
    while (*val && isspace((unsigned char)*val)) val++;
    return strtol(val, NULL, 10);
}

static double read_double(const char *text, const char *key, double def)
{
    const char *val = find_key(text, key);
    if (!val) return def;
    while (*val && isspace((unsigned char)*val)) val++;
    return strtod(val, NULL);
}

static bool read_bool(const char *text, const char *key, bool def)
{
    const char *val = find_key(text, key);
    if (!val) return def;
    while (*val && isspace((unsigned char)*val)) val++;
    return (strncmp(val, "true", 4) == 0);
}

/* -- Satellite serialization ------------------------------------------------ */

static void write_satellite(FILE *f, const Satellite *sat, int index)
{
    fprintf(f, "    {\n");
    fprintf(f, "      \"index\": %d,\n", index);
    fprintf(f, "      \"name\": \"%s\",\n", sat->name);
    fprintf(f, "      \"norad_id\": \"%s\",\n", sat->norad_id);
    fprintf(f, "      \"norad_id_num\": %u,\n", sat->norad_id_num);
    fprintf(f, "      \"intl_designator\": \"%s\",\n", sat->intl_designator);
    fprintf(f, "      \"epoch_days\": %.15f,\n", sat->epoch_days);
    fprintf(f, "      \"epoch_unix\": %.6f,\n", sat->epoch_unix);
    fprintf(f, "      \"inclination\": %.15f,\n", sat->inclination);
    fprintf(f, "      \"raan\": %.15f,\n", sat->raan);
    fprintf(f, "      \"eccentricity\": %.15f,\n", sat->eccentricity);
    fprintf(f, "      \"arg_perigee\": %.15f,\n", sat->arg_perigee);
    fprintf(f, "      \"mean_anomaly\": %.15f,\n", sat->mean_anomaly);
    fprintf(f, "      \"mean_motion\": %.15f,\n", sat->mean_motion);
    fprintf(f, "      \"semi_major_axis\": %.6f,\n", sat->semi_major_axis);
    fprintf(f, "      \"is_active\": %s,\n", sat->is_active ? "true" : "false");
    fprintf(f, "      \"data_meta\": {\n");
    fprintf(f, "        \"source_name\": \"%s\",\n", sat->data_meta.source_name);
    fprintf(f, "        \"format\": \"%s\",\n", FormatToString(sat->data_meta.format));
    fprintf(f, "        \"fetch_time\": %ld,\n", (long)sat->data_meta.fetch_time);
    fprintf(f, "        \"epoch_time\": %ld\n", (long)sat->data_meta.epoch_time);
    fprintf(f, "      }\n");
    fprintf(f, "    }");
}

static bool read_satellite(const char *text, Satellite *sat)
{
    memset(sat, 0, sizeof(Satellite));

    read_string(text, "name", sat->name, sizeof(sat->name));
    read_string(text, "norad_id", sat->norad_id, sizeof(sat->norad_id));
    sat->norad_id_num = (uint32_t)read_int(text, "norad_id_num", 0);
    read_string(text, "intl_designator", sat->intl_designator, sizeof(sat->intl_designator));
    sat->epoch_days = read_double(text, "epoch_days", 0.0);
    sat->epoch_unix = read_double(text, "epoch_unix", 0.0);
    sat->inclination = read_double(text, "inclination", 0.0);
    sat->raan = read_double(text, "raan", 0.0);
    sat->eccentricity = read_double(text, "eccentricity", 0.0);
    sat->arg_perigee = read_double(text, "arg_perigee", 0.0);
    sat->mean_anomaly = read_double(text, "mean_anomaly", 0.0);
    sat->mean_motion = read_double(text, "mean_motion", 0.0);
    sat->semi_major_axis = read_double(text, "semi_major_axis", 0.0);
    sat->is_active = read_bool(text, "is_active", true);

    // Parse nested data_meta
    const char *meta = strstr(text, "\"data_meta\"");
    if (meta)
    {
        const char *meta_obj = strchr(meta, '{');
        if (meta_obj)
        {
            read_string(meta_obj, "source_name", sat->data_meta.source_name, sizeof(sat->data_meta.source_name));
            char fmt_buf[32];
            read_string(meta_obj, "format", fmt_buf, sizeof(fmt_buf));
            sat->data_meta.format = StringToFormat(fmt_buf);
            sat->data_meta.fetch_time = (time_t)read_long(meta_obj, "fetch_time", 0);
            sat->data_meta.epoch_time = (time_t)read_long(meta_obj, "epoch_time", 0);
        }
    }

    return true;
}

/* -- Public API ------------------------------------------------------------- */

bool SaveOrbitalData(const char *filename, Satellite *sats, int count)
{
    FILE *f = fopen(filename, "w");
    if (!f) {
        LOG_ERROR("Failed to save orbital data to %s", filename);
        return false;
    }
    LOG_INFO("Saving %d satellites to %s", count, filename);

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 2,\n");
    fprintf(f, "  \"satellite_count\": %d,\n", count);
    fprintf(f, "  \"satellites\": [\n");

    for (int i = 0; i < count; i++)
    {
        write_satellite(f, &sats[i], i);
        if (i < count - 1) fprintf(f, ",\n");
        else fprintf(f, "\n");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    return true;
}

bool LoadOrbitalData(const char *filename, Satellite *sats, int *count, int max)
{
    if (!FileExists(filename))
    {
        *count = 0;
        return false;
    }

    char *text = LoadFileText(filename);
    if (!text)
    {
        *count = 0;
        return false;
    }

    int loaded = 0;
    const char *ptr = text;
    int brace_depth = 0;
    int obj_start = -1;

    // Find the satellites array
    const char *array_start = strstr(text, "\"satellites\"");
    if (!array_start)
    {
        UnloadFileText(text);
        *count = 0;
        return false;
    }

    // Walk through the array, extract each object
    ptr = strchr(array_start, '[');
    if (!ptr)
    {
        UnloadFileText(text);
        *count = 0;
        return false;
    }

    while (*ptr && loaded < max)
    {
        if (*ptr == '{')
        {
            if (brace_depth == 0)
                obj_start = (int)(ptr - text);
            brace_depth++;
        }
        else if (*ptr == '}')
        {
            brace_depth--;
            if (brace_depth == 0 && obj_start >= 0)
            {
                // Extract the object substring
                int obj_len = (int)(ptr - text) - obj_start + 1;
                char *obj_text = (char*)malloc(obj_len + 1);
                if (obj_text)
                {
                    strncpy(obj_text, text + obj_start, obj_len);
                    obj_text[obj_len] = '\0';
                    read_satellite(obj_text, &sats[loaded]);
                    free(obj_text);
                    loaded++;
                }
                obj_start = -1;
            }
        }
        else if (*ptr == ']' && brace_depth == 0)
        {
            break;
        }
        ptr++;
    }

    UnloadFileText(text);
    *count = loaded;
    return loaded > 0;
}

/* -- Source State Persistence ------------------------------------------------ */

bool SaveSourceState(const char *filename, DataSourceState *sources, int count)
{
    FILE *f = fopen(filename, "w");
    if (!f) {
        LOG_ERROR("Failed to save source state to %s", filename);
        return false;
    }
    LOG_INFO("Saving %d source states to %s", count, filename);

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 2,\n");
    fprintf(f, "  \"source_count\": %d,\n", count);
    fprintf(f, "  \"sources\": [\n");

    for (int i = 0; i < count; i++)
    {
        fprintf(f, "    {\n");
        fprintf(f, "      \"provider_name\": \"%s\",\n", sources[i].provider_name);
        fprintf(f, "      \"group_name\": \"%s\",\n", sources[i].group_name);
        fprintf(f, "      \"url\": \"%s\",\n", sources[i].url);
        fprintf(f, "      \"preferred_format\": \"%s\",\n", FormatToString(sources[i].preferred_format));
        fprintf(f, "      \"selected\": %s,\n", sources[i].selected ? "true" : "false");
        fprintf(f, "      \"last_fetch\": %ld\n", (long)sources[i].last_fetch);
        fprintf(f, "    }");
        if (i < count - 1) fprintf(f, ",\n");
        else fprintf(f, "\n");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    return true;
}

bool LoadSourceState(const char *filename, DataSourceState *sources, int *count, int max)
{
    if (!FileExists(filename))
    {
        *count = 0;
        return false;
    }

    char *text = LoadFileText(filename);
    if (!text)
    {
        *count = 0;
        return false;
    }

    int loaded = 0;
    const char *array_start = strstr(text, "\"sources\"");
    if (!array_start)
    {
        UnloadFileText(text);
        *count = 0;
        return false;
    }

    const char *ptr = strchr(array_start, '[');
    if (!ptr)
    {
        UnloadFileText(text);
        *count = 0;
        return false;
    }

    int brace_depth = 0;
    int obj_start = -1;

    while (*ptr && loaded < max)
    {
        if (*ptr == '{')
        {
            if (brace_depth == 0) obj_start = (int)(ptr - text);
            brace_depth++;
        }
        else if (*ptr == '}')
        {
            brace_depth--;
            if (brace_depth == 0 && obj_start >= 0)
            {
                int obj_len = (int)(ptr - text) - obj_start + 1;
                char *obj_text = (char*)malloc(obj_len + 1);
                if (obj_text)
                {
                    strncpy(obj_text, text + obj_start, obj_len);
                    obj_text[obj_len] = '\0';

                    DataSourceState *src = &sources[loaded];
                    memset(src, 0, sizeof(DataSourceState));
                    read_string(obj_text, "provider_name", src->provider_name, sizeof(src->provider_name));
                    read_string(obj_text, "group_name", src->group_name, sizeof(src->group_name));
                    read_string(obj_text, "url", src->url, sizeof(src->url));
                    char fmt_buf[32];
                    read_string(obj_text, "preferred_format", fmt_buf, sizeof(fmt_buf));
                    src->preferred_format = StringToFormat(fmt_buf);
                    src->selected = read_bool(obj_text, "selected", false);
                    src->last_fetch = (time_t)read_long(obj_text, "last_fetch", 0);

                    free(obj_text);
                    loaded++;
                }
                obj_start = -1;
            }
        }
        else if (*ptr == ']' && brace_depth == 0)
        {
            break;
        }
        ptr++;
    }

    UnloadFileText(text);
    *count = loaded;
    return loaded > 0;
}