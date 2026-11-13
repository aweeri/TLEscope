#include "storage.h"
#include "core/types.h"
#include "core/astro.h"
#include "core/config.h"
#include "data/ephem.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <raylib.h>
#include <raymath.h>

#include <map>
#include <string>
#include <vector>

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
        case FORMAT_HORIZONS_VECTORS: return "horizons_vectors";
        default:              return "UNKNOWN";
    }
}

OrbitalDataFormat StringToFormat(const char *str)
{
    // unknown/missing strings fall back to FORMAT_TLE so old data.json still loads
    if (!str) return FORMAT_TLE;
    if (strcmp(str, "TLE") == 0)      return FORMAT_TLE;
    if (strcmp(str, "OMM_JSON") == 0) return FORMAT_OMM_JSON;
    if (strcmp(str, "OMM_CSV") == 0)  return FORMAT_OMM_CSV;
    if (strcmp(str, "OMM_XML") == 0)  return FORMAT_OMM_XML;
    if (strcmp(str, "OMM_KVN") == 0)  return FORMAT_OMM_KVN;
    if (strcmp(str, "horizons_vectors") == 0) return FORMAT_HORIZONS_VECTORS;
    if (strcmp(str, "HORIZONS_VECTORS") == 0) return FORMAT_HORIZONS_VECTORS;
    return FORMAT_TLE;
}

static void copy_str(char *dst, size_t dst_size, const std::string &src)
{
    snprintf(dst, dst_size, "%s", src.c_str());
}

// -- Ephemeris mission serialization ----------------------------------------
// Missions are persisted inline in data.json under a top-level "ephem" array.
// Each mission stores its samples as one compact string of
//   t|px|py|pz|vx|vy|vz|lat|lon|sunlit
// tuples joined by ';'. Older builds simply ignore the unknown key; newer
// builds rebuild the registry from it on load.

static std::string ephem_samples_to_string(const EphemMission *m)
{
    std::string out;
    if (!m)
        return out;

    size_t n = m->s.size();
    if (n > (size_t)EPHEM_MAX_SAMPLES)
        n = (size_t)EPHEM_MAX_SAMPLES;
    out.reserve(n * 96);

    char buf[256];
    for (size_t i = 0; i < n; ++i)
    {
        const EphemSample &s = m->s[i];
        snprintf(buf, sizeof(buf), "%s%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%d",
                 (i ? ";" : ""), s.t_unix, (double)s.p.x, (double)s.p.y, (double)s.p.z,
                 (double)s.v.x, (double)s.v.y, (double)s.v.z, (double)s.lat, (double)s.lon,
                 s.sunlit ? 1 : 0);
        out += buf;
    }
    return out;
}

/* parse the compact sample string; malformed tuples are skipped, never fatal */
static void ephem_samples_from_string(const std::string &text, EphemMission *m)
{
    m->s.clear();
    size_t pos = 0;
    while (pos <= text.size())
    {
        const size_t end = text.find(';', pos);
        const std::string item =
            text.substr(pos, (end == std::string::npos) ? std::string::npos : end - pos);
        if (!item.empty())
        {
            double t = 0.0, px = 0.0, py = 0.0, pz = 0.0, vx = 0.0, vy = 0.0, vz = 0.0;
            double lat = 0.0, lon = 0.0;
            int sunlit = 0;
            if (sscanf(item.c_str(), "%lf|%lf|%lf|%lf|%lf|%lf|%lf|%lf|%lf|%d",
                       &t, &px, &py, &pz, &vx, &vy, &vz, &lat, &lon, &sunlit) == 10)
            {
                EphemSample s = {};
                s.t_unix = t;
                s.p.x = (float)px;
                s.p.y = (float)py;
                s.p.z = (float)pz;
                s.v.x = (float)vx;
                s.v.y = (float)vy;
                s.v.z = (float)vz;
                s.lat = (float)lat;
                s.lon = (float)lon;
                s.gmst = 0.0; /* not persisted; recomputed on demand */
                s.sunlit = (sunlit != 0);
                m->s.push_back(s);
                if (m->s.size() >= (size_t)EPHEM_MAX_SAMPLES)
                    break;
            }
        }
        if (end == std::string::npos)
            break;
        pos = end + 1;
    }

    if (!m->s.empty())
    {
        m->t0_unix = m->s.front().t_unix;
        m->t1_unix = m->s.back().t_unix;
    }
}

static void write_ephem_missions(nlohmann::json &arr)
{
    const int n = EphemCount();
    for (int i = 0; i < n; ++i)
    {
        const EphemMission *m = EphemGet(i);
        if (!m)
            continue;
        arr.push_back(nlohmann::json{
            {"id", m->id},
            {"name", m->name},
            {"target", m->target},
            {"norad", m->norad_id_num},
            {"t0", m->t0_unix},
            {"t1", m->t1_unix},
            {"samples", ephem_samples_to_string(m)}});
    }
}

// rebuilds the ephemeris registry; EphemClear() runs first so loads never accumulate
static std::map<int, int> load_ephem_missions(const nlohmann::json &root)
{
    std::map<int, int> id_map;

    EphemClear();

    auto it = root.find("ephem");
    if (it == root.end() || !it->is_array())
        return id_map;

    size_t limit = it->size();
    if (limit > (size_t)EPHEM_MAX_MISSIONS)
        limit = (size_t)EPHEM_MAX_MISSIONS;

    for (size_t i = 0; i < limit; ++i)
    {
        const nlohmann::json &o = it->at(i);
        if (!o.is_object())
            continue;

        const std::string samples = o.value("samples", std::string());
        if (samples.empty())
            continue;

        EphemMission m;
        m.id = 0;
        copy_str(m.name, sizeof(m.name), o.value("name", std::string()));
        copy_str(m.target, sizeof(m.target), o.value("target", std::string()));
        m.norad_id_num = (uint32_t)o.value("norad", 0u);
        m.t0_unix = o.value("t0", 0.0);
        m.t1_unix = o.value("t1", 0.0);

        ephem_samples_from_string(samples, &m);
        if (m.s.size() < 2)
        {
            LOG_WARN("Skipping ephemeris mission '%s': unusable sample data", m.name);
            continue;
        }

        const int old_id = (int)o.value("id", 0);
        const int new_id = EphemAdd(m);
        if (new_id == 0)
        {
            LOG_WARN("Ephemeris registry full while restoring mission '%s'", m.name);
            break;
        }
        if (old_id != 0)
            id_map[old_id] = new_id;
    }

    return id_map;
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
    /* persisted explicitly for forward compatibility; "sgp4" is the default */
    j["kind"] = (sat->kind == SAT_KIND_EPHEM) ? "ephem" : "sgp4";
    if (sat->kind == SAT_KIND_EPHEM)
        j["ephem_id"] = sat->ephem_id;
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
    // kind defaults to SAT_KIND_SGP4 (zero value); ephem_id is -1 when unassigned
    sat->ephem_id = -1;

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

    // "kind" is absent in pre-ephemeris files, and unknown values, mean SGP4
    const std::string kind_str = j.value("kind", std::string("sgp4"));
    sat->kind = (kind_str == "ephem") ? SAT_KIND_EPHEM : SAT_KIND_SGP4;
    if (sat->kind == SAT_KIND_EPHEM)
        sat->ephem_id = (int)j.value("ephem_id", -1);

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
    filename = AppConfigPath(filename);

    nlohmann::json root = nlohmann::json::object();
    root["version"] = 2;
    root["satellite_count"] = count;
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < count; i++)
        write_satellite(arr, &sats[i], i);
    root["satellites"] = arr;

    // omit the key when no mission exists so clean files stay byte-identical
    if (EphemCount() > 0)
    {
        nlohmann::json ephem_arr = nlohmann::json::array();
        write_ephem_missions(ephem_arr);
        if (!ephem_arr.empty())
            root["ephem"] = ephem_arr;
    }

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


static bool parse_orbital_json(const char *text, Satellite *sats, int *count, int max)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const std::exception &)
    {
        *count = 0;
        return false;
    }

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

    // rebuild missions first; the map translates persisted ids to this run's ids
    const std::map<int, int> ephem_id_map = load_ephem_missions(root);

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

    // remap persisted ephem ids; an unrestorable mission degrades to inactive SGP4
    for (int i = 0; i < loaded; i++)
    {
        Satellite *sat = &sats[i];
        if (sat->kind != SAT_KIND_EPHEM)
            continue;
        const auto it = ephem_id_map.find(sat->ephem_id);
        if (it != ephem_id_map.end() && EphemGetById(it->second) != NULL)
        {
            sat->ephem_id = it->second;
        }
        else
        {
            LOG_WARN("Ephemeris mission for satellite '%s' unavailable; deactivating", sat->name);
            sat->kind = SAT_KIND_SGP4;
            sat->ephem_id = -1;
            sat->is_active = false;
        }
    }

    *count = loaded;
    return loaded > 0;
}

bool LoadOrbitalData(const char *filename, Satellite *sats, int *count, int max)
{
    filename = AppConfigPath(filename);

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

    bool ok = parse_orbital_json(text, sats, count, max);
    UnloadFileText(text);
    return ok;
}

bool LoadOrbitalDataFromString(const char *json, Satellite *sats, int *count, int max)
{
    if (!json)
    {
        *count = 0;
        return false;
    }
    return parse_orbital_json(json, sats, count, max);
}

// -- Source State Persistence ------------------------------------------------

bool SaveSourceState(const char *filename, DataSourceState *sources, int count)
{
    filename = AppConfigPath(filename);

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
    filename = AppConfigPath(filename);

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

static uint32_t s_fav_ids[MAX_SATELLITES_HARD_CAP];
static int s_fav_count = 0;

int LoadFavorites(const char *filename)
{
    filename = AppConfigPath(filename);

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

    for (size_t i = 0; i < favs->size() && s_fav_count < GetMaxSatellites(); i++)
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
    filename = AppConfigPath(filename);

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
    if (fav && s_fav_count < GetMaxSatellites())
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