#include "storage.h"
#include "core/types.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <raylib.h>

#include <nlohmann/json.hpp>

// -- Format string conversion -----------------------------------------------

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

static void copy_str(char *dst, size_t dst_size, const std::string &src)
{
    snprintf(dst, dst_size, "%s", src.c_str());
}

// -- Satellite serialization ------------------------------------------------

static void write_satellite(nlohmann::json &arr, const Satellite *sat, int index)
{
    nlohmann::json j = nlohmann::json::object();
    j["index"] = index;
    j["name"] = sat->name;
    j["norad_id"] = sat->norad_id;
    j["norad_id_num"] = sat->norad_id_num;
    j["intl_designator"] = sat->intl_designator;
    j["epoch_days"] = sat->epoch_days;
    j["epoch_unix"] = sat->epoch_unix;
    j["inclination"] = sat->inclination;
    j["raan"] = sat->raan;
    j["eccentricity"] = sat->eccentricity;
    j["arg_perigee"] = sat->arg_perigee;
    j["mean_anomaly"] = sat->mean_anomaly;
    j["mean_motion"] = sat->mean_motion;
    j["semi_major_axis"] = sat->semi_major_axis;
    j["bstar"] = sat->bstar;
    j["is_active"] = sat->is_active;
    j["data_meta"] = nlohmann::json{
        {"source_name", sat->data_meta.source_name},
        {"format", FormatToString(sat->data_meta.format)},
        {"fetch_time", (long)sat->data_meta.fetch_time},
        {"epoch_time", (long)sat->data_meta.epoch_time}};
    arr.push_back(j);
}

static bool read_satellite_obj(const nlohmann::json &j, Satellite *sat)
{
    memset(sat, 0, sizeof(Satellite));

    const nlohmann::json &data_meta = j.value("data_meta", nlohmann::json::object());

    copy_str(sat->name, sizeof(sat->name), j.value("name", ""));
    copy_str(sat->norad_id, sizeof(sat->norad_id), j.value("norad_id", ""));
    sat->norad_id_num = (uint32_t)j.value("norad_id_num", 0u);
    copy_str(sat->intl_designator, sizeof(sat->intl_designator), j.value("intl_designator", ""));
    sat->epoch_days = j.value("epoch_days", 0.0);
    sat->epoch_unix = j.value("epoch_unix", 0.0);
    sat->inclination = j.value("inclination", 0.0);
    sat->raan = j.value("raan", 0.0);
    sat->eccentricity = j.value("eccentricity", 0.0);
    sat->arg_perigee = j.value("arg_perigee", 0.0);
    sat->mean_anomaly = j.value("mean_anomaly", 0.0);
    sat->mean_motion = j.value("mean_motion", 0.0);
    sat->semi_major_axis = j.value("semi_major_axis", 0.0);
    sat->bstar = j.value("bstar", 0.0);
    sat->is_active = j.value("is_active", false);

    // parse nested data_meta
    copy_str(sat->data_meta.source_name, sizeof(sat->data_meta.source_name), data_meta.value("source_name", ""));
    std::string fmt_buf = data_meta.value("format", "UNKNOWN");
    sat->data_meta.format = StringToFormat(fmt_buf.c_str());
    sat->data_meta.fetch_time = (time_t)data_meta.value("fetch_time", 0L);
    sat->data_meta.epoch_time = (time_t)data_meta.value("epoch_time", 0L);

    return true;
}

// -- Public API -------------------------------------------------------------

bool SaveOrbitalData(const char *filename, Satellite *sats, int count)
{
    nlohmann::json root = nlohmann::json::object();
    root["version"] = 2;
    root["satellite_count"] = count;
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < count; i++)
        write_satellite(arr, &sats[i], i);
    root["satellites"] = arr;

    FILE *f = fopen(filename, "w");
    if (!f) {
        LOG_ERROR("Failed to save orbital data to %s", filename);
        return false;
    }
    LOG_INFO("Saving %d satellites to %s", count, filename);

    std::string out = root.dump(2);
    fwrite(out.c_str(), 1, out.size(), f);
    fwrite("\n", 1, 1, f);
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

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const std::exception &)
    {
        UnloadFileText(text);
        *count = 0;
        return false;
    }
    UnloadFileText(text);

    // Find the satellites array
    if (!root.is_object())
    {
        *count = 0;
        return false;
    }
    auto sats_arr = root.find("satellites");
    if (sats_arr == root.end() || !sats_arr->is_array())
    {
        *count = 0;
        return false;
    }

    int loaded = 0;
    for (size_t i = 0; i < sats_arr->size() && loaded < max; i++)
    {
        if (sats_arr->at(i).is_object())
        {
            const nlohmann::json &sat_node = sats_arr->at(i);
            // satellites array may contain the "index"/"name" of the wrapping
            // object; read the elements directly
            if (sat_node.contains("name") ||
                sat_node.contains("data_meta") ||
                sat_node.contains("norad_id"))
            {
                read_satellite_obj(sat_node, &sats[loaded]);
                loaded++;
            }
        }
    }

    *count = loaded;
    return loaded > 0;
}

// -- Source State Persistence ------------------------------------------------

bool SaveSourceState(const char *filename, DataSourceState *sources, int count)
{
    nlohmann::json root = nlohmann::json::object();
    root["version"] = 2;
    root["source_count"] = count;
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < count; i++)
    {
        arr.push_back(nlohmann::json{
            {"provider_name", sources[i].provider_name},
            {"group_name", sources[i].group_name},
            {"url", sources[i].url},
            {"preferred_format", FormatToString(sources[i].preferred_format)},
            {"selected", sources[i].selected},
            {"last_fetch", (long)sources[i].last_fetch}});
    }
    root["sources"] = arr;

    FILE *f = fopen(filename, "w");
    if (!f) {
        LOG_ERROR("Failed to save source state to %s", filename);
        return false;
    }
    LOG_INFO("Saving %d source states to %s", count, filename);

    std::string out = root.dump(2);
    fwrite(out.c_str(), 1, out.size(), f);
    fwrite("\n", 1, 1, f);
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

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const std::exception &)
    {
        UnloadFileText(text);
        *count = 0;
        return false;
    }
    UnloadFileText(text);

    if (!root.is_object())
    {
        *count = 0;
        return false;
    }
    auto srcs = root.find("sources");
    if (srcs == root.end() || !srcs->is_array())
    {
        *count = 0;
        return false;
    }

    int loaded = 0;
    for (size_t i = 0; i < srcs->size() && loaded < max; i++)
    {
        if (!srcs->at(i).is_object())
            continue;
        const nlohmann::json &o = srcs->at(i);
        DataSourceState *src = &sources[loaded];
        memset(src, 0, sizeof(DataSourceState));
        copy_str(src->provider_name, sizeof(src->provider_name), o.value("provider_name", ""));
        copy_str(src->group_name, sizeof(src->group_name), o.value("group_name", ""));
        copy_str(src->url, sizeof(src->url), o.value("url", ""));
        std::string fmt_buf = o.value("preferred_format", "UNKNOWN");
        src->preferred_format = StringToFormat(fmt_buf.c_str());
        src->selected = o.value("selected", false);
        src->last_fetch = (time_t)o.value("last_fetch", 0L);
        loaded++;
    }

    *count = loaded;
    return loaded > 0;
}

// -- Favorites Persistence ---------------------------------------------------
// In-memory set of favorite NORAD catalog numbers, loaded/saved as a JSON
// array of numeric ids (mirrors SaveSourceState/LoadSourceState style).

static uint32_t s_fav_ids[MAX_SATELLITES];
static int s_fav_count = 0;

int LoadFavorites(const char *filename)
{
    s_fav_count = 0;
    if (!FileExists(filename))
        return 0;

    char *text = LoadFileText(filename);
    if (!text)
        return 0;

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const std::exception &)
    {
        UnloadFileText(text);
        return 0;
    }
    UnloadFileText(text);

    if (!root.is_object())
        return 0;
    auto favs = root.find("favorites");
    if (favs == root.end() || !favs->is_array())
        return 0;

    for (size_t i = 0; i < favs->size() && s_fav_count < MAX_SATELLITES; i++)
    {
        if (favs->at(i).is_number_unsigned())
            s_fav_ids[s_fav_count++] = (uint32_t)favs->at(i).get<uint64_t>();
        else if (favs->at(i).is_number_integer())
            s_fav_ids[s_fav_count++] = (uint32_t)favs->at(i).get<int64_t>();
    }

    LOG_INFO("Loaded %d favorites from %s", s_fav_count, filename);
    return s_fav_count;
}

bool SaveFavorites(const char *filename)
{
    nlohmann::json root = nlohmann::json::object();
    root["version"] = 1;
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < s_fav_count; i++)
        arr.push_back(s_fav_ids[i]);
    root["favorites"] = arr;

    FILE *f = fopen(filename, "w");
    if (!f)
    {
        LOG_ERROR("Failed to save favorites to %s", filename);
        return false;
    }
    LOG_INFO("Saving %d favorites to %s", s_fav_count, filename);

    std::string out = root.dump(2);
    fwrite(out.c_str(), 1, out.size(), f);
    fwrite("\n", 1, 1, f);
    fclose(f);
    return true;
}

bool IsFavorite(uint32_t norad_id)
{
    for (int i = 0; i < s_fav_count; i++)
    {
        if (s_fav_ids[i] == norad_id)
            return true;
    }
    return false;
}

void SetFavorite(uint32_t norad_id, bool fav)
{
    for (int i = 0; i < s_fav_count; i++)
    {
        if (s_fav_ids[i] == norad_id)
        {
            if (!fav)
            {
                /* remove by shifting the tail into the gap */
                for (int j = i; j < s_fav_count - 1; j++)
                    s_fav_ids[j] = s_fav_ids[j + 1];
                s_fav_count--;
            }
            return;
        }
    }
    if (fav && s_fav_count < MAX_SATELLITES)
        s_fav_ids[s_fav_count++] = norad_id;
}

int FavoriteCount(void)
{
    return s_fav_count;
}

int GetFavoriteIds(uint32_t *out, int max)
{
    int n = (s_fav_count < max) ? s_fav_count : max;
    for (int i = 0; i < n; i++)
        out[i] = s_fav_ids[i];
    return n;
}