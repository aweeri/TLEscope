/*
 * tool_trxdb.cpp - TRXDB NORAD TEST panel
 *
 * Queries the TRXDB API (https://trxdb.mrtalon.eu) for the currently selected
 * satellite's NORAD number:
 *   - object info:   GET /api/v1/object/{norad}
 *   - image:         GET /api/v1/object/{norad}/image
 *   - transponders:  GET /api/v1/object/{norad}/transponders
 *
 * Network I/O runs on a detached worker thread (never the UI thread). The raw
 * bytes are handed back to the UI thread, which does all JSON parsing and GL
 * texture upload (LoadTextureFromImage requires the GL context).
 */

#include "tools.h"
#include "tools_common.h"
#include "core/theme.h"
#include "data/provider.h"
#include "util/log.h"

#include "cJSON.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <mutex>
#include <thread>

#include <raylib.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

/* -- Constants ------------------------------------------------------------- */

#define TRXDB_BASE "https://trxdb.mrtalon.eu/api/v1/object"
#define MAX_TRXDB_TRANSPONDERS 32

/* -- Parsed data ----------------------------------------------------------- */

typedef struct {
    char id[16];
    char name[64];
    char description[512];
    char image_url[256];
    char launched[32];
    char status[32];
    char short_description[128];
    char operator_name[64];
    char launch_vehicle[64];
    char orbit[16];
    char created[32];
    char updated[32];
} TrxdbObjectInfo;

typedef struct {
    char id[32];
    char object[16];
    char type[16];
    char status[16];
    char description[128];
    char mode[16];
    char uplink[32];
    char symrate[32];
    char bandwidth[32];
    char modulation[32];
    char fec[64];
    bool encrypted;
    char created[32];
    char updated[32];
} TrxdbTransponder;

/* -- Async fetch state ---------------------------------------------------- */

typedef struct {
    int gen;                 /* generation this result belongs to            */
    bool ok;                 /* object JSON fetch succeeded                  */
    char *object_json;       /* heap buffer (owned)                          */
    size_t object_json_size;
    bool image_ok;
    char *image_data;        /* heap buffer (owned)                          */
    size_t image_size;
    bool transponders_ok;
    char *transponders_json; /* heap buffer (owned)                          */
    size_t transponders_size;
} TrxdbResult;

static std::mutex s_mutex;
static char s_target_norad[16] = {0};
static int s_target_gen = 0;
static bool s_fetching = false;
static bool s_result_applied = false;
static TrxdbResult s_result = {0};

/* UI-thread-owned parsed state (only touched on the UI thread) */
static TrxdbObjectInfo s_info = {0};
static bool s_info_valid = false;
static TrxdbTransponder s_transponders[MAX_TRXDB_TRANSPONDERS];
static int s_transponder_count = 0;
static Texture2D s_tex = {0};
static bool s_tex_valid = false;

/* -- Helpers --------------------------------------------------------------- */

static void trxdb_free_result(TrxdbResult *res)
{
    if (!res) return;
    free(res->object_json);
    free(res->image_data);
    free(res->transponders_json);
    memset(res, 0, sizeof(*res));
}

/** strip HTML tags from a string (e.g. "<p>text</p>" -> "text") */
static void strip_html(const char *src, char *dst, size_t dst_size)
{
    if (!src) { if (dst_size) dst[0] = '\0'; return; }
    size_t di = 0;
    bool in_tag = false;
    for (const char *p = src; *p && di < dst_size - 1; p++)
    {
        if (*p == '<') { in_tag = true; continue; }
        if (*p == '>') { in_tag = false; continue; }
        if (!in_tag) dst[di++] = *p;
    }
    dst[di] = '\0';
}

/** copy a cJSON string field into a fixed buffer */
static void get_str_field(cJSON *obj, const char *key, char *dst, size_t size)
{
    dst[0] = '\0';
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(item) && item->valuestring)
        snprintf(dst, size, "%s", item->valuestring);
}

/** format a cJSON number-or-string field into a fixed buffer */
static void get_num_or_str_field(cJSON *obj, const char *key, char *dst, size_t size)
{
    dst[0] = '\0';
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item))
    {
        double v = item->valuedouble;
        if (v == (double)(long long)v)
            snprintf(dst, size, "%lld", (long long)v);
        else
            snprintf(dst, size, "%g", v);
    }
    else if (cJSON_IsString(item) && item->valuestring)
    {
        snprintf(dst, size, "%s", item->valuestring);
    }
}

/** detect the image format from magic bytes; returns raylib extension (with dot) or NULL */
static const char *detect_image_format(const char *data, size_t size)
{
    if (!data || size < 4) return NULL;
    const unsigned char *b = (const unsigned char *)data;
    if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return ".png";
    if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return ".jpg";
    if (b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8') return ".gif";
    if (b[0] == 'B' && b[1] == 'M') return ".bmp";
    return NULL;
}

/* -- JSON parsing ---------------------------------------------------------- */

static void parse_object_json(const char *json, TrxdbObjectInfo *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    get_str_field(root, "id", out->id, sizeof(out->id));
    get_str_field(root, "name", out->name, sizeof(out->name));
    get_str_field(root, "image", out->image_url, sizeof(out->image_url));
    get_str_field(root, "launched", out->launched, sizeof(out->launched));
    get_str_field(root, "status", out->status, sizeof(out->status));
    get_str_field(root, "shortDescription", out->short_description, sizeof(out->short_description));
    get_str_field(root, "operator", out->operator_name, sizeof(out->operator_name));
    get_str_field(root, "launchVehicle", out->launch_vehicle, sizeof(out->launch_vehicle));
    get_str_field(root, "orbit", out->orbit, sizeof(out->orbit));
    get_str_field(root, "created", out->created, sizeof(out->created));
    get_str_field(root, "updated", out->updated, sizeof(out->updated));

    /* description may contain HTML; strip tags for display */
    cJSON *desc = cJSON_GetObjectItem(root, "description");
    if (cJSON_IsString(desc) && desc->valuestring)
        strip_html(desc->valuestring, out->description, sizeof(out->description));

    cJSON_Delete(root);
}

static int parse_transponders_json(const char *json, TrxdbTransponder *out, int max)
{
    int count = 0;
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;

    cJSON *found = cJSON_GetObjectItem(root, "found");
    if (cJSON_IsArray(found))
    {
        int n = cJSON_GetArraySize(found);
        if (n > max) n = max;
        for (int i = 0; i < n; i++)
        {
            cJSON *t = cJSON_GetArrayItem(found, i);
            TrxdbTransponder *tp = &out[count];
            memset(tp, 0, sizeof(*tp));

            get_str_field(t, "id", tp->id, sizeof(tp->id));
            get_str_field(t, "object", tp->object, sizeof(tp->object));
            get_str_field(t, "type", tp->type, sizeof(tp->type));
            get_str_field(t, "status", tp->status, sizeof(tp->status));
            get_str_field(t, "description", tp->description, sizeof(tp->description));
            get_str_field(t, "mode", tp->mode, sizeof(tp->mode));
            get_str_field(t, "modulation", tp->modulation, sizeof(tp->modulation));
            get_str_field(t, "fec", tp->fec, sizeof(tp->fec));
            get_str_field(t, "created", tp->created, sizeof(tp->created));
            get_str_field(t, "updated", tp->updated, sizeof(tp->updated));

            get_num_or_str_field(t, "uplink", tp->uplink, sizeof(tp->uplink));
            get_num_or_str_field(t, "symrate", tp->symrate, sizeof(tp->symrate));
            get_num_or_str_field(t, "bandwidth", tp->bandwidth, sizeof(tp->bandwidth));

            cJSON *enc = cJSON_GetObjectItem(t, "encrypted");
            tp->encrypted = cJSON_IsTrue(enc);

            count++;
        }
    }

    cJSON_Delete(root);
    return count;
}

/* -- Worker thread --------------------------------------------------------- */

static void trxdb_worker_fetch(int gen, std::string norad)
{
    TrxdbResult res;
    memset(&res, 0, sizeof(res));
    res.gen = gen;

    char url[512];

    /* object info */
    snprintf(url, sizeof(url), "%s/%s", TRXDB_BASE, norad.c_str());
    FetchResult obj = FetchFromCustomURL(url);
    if (obj.success)
    {
        res.object_json = obj.data;
        res.object_json_size = obj.size;
        res.ok = true;
    }
    else
    {
        FreeFetchResult(&obj);
    }

    /* image */
    snprintf(url, sizeof(url), "%s/%s/image", TRXDB_BASE, norad.c_str());
    FetchResult img = FetchFromCustomURL(url);
    if (img.success)
    {
        res.image_data = img.data;
        res.image_size = img.size;
        res.image_ok = true;
    }
    else
    {
        FreeFetchResult(&img);
    }

    /* transponders */
    snprintf(url, sizeof(url), "%s/%s/transponders", TRXDB_BASE, norad.c_str());
    FetchResult trx = FetchFromCustomURL(url);
    if (trx.success)
    {
        res.transponders_json = trx.data;
        res.transponders_size = trx.size;
        res.transponders_ok = true;
    }
    else
    {
        FreeFetchResult(&trx);
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (res.gen == s_target_gen)
        {
            /* this is still the latest request - hand the result to the UI */
            trxdb_free_result(&s_result);
            s_result = res;
            s_fetching = false;
        }
        else
        {
            /* a newer request superseded this one - discard */
            trxdb_free_result(&res);
        }
    }
}

/* -- Panel ----------------------------------------------------------------- */

void DrawPanelTrxdb(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    /* current selection */
    const char *norad = NULL;
    if (*ctx->selected_sat)
        norad = (*ctx->selected_sat)->norad_id;

    if (!norad || !*norad)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "No satellite selected.\nSelect a satellite to query TRXDB.");
        ImGui::PopTextWrapPos();
        return;
    }

    /* request a fetch when the selected NORAD changes */
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (strcmp(norad, s_target_norad) != 0)
        {
            snprintf(s_target_norad, sizeof(s_target_norad), "%s", norad);
            s_target_gen++;
            s_fetching = true;
            s_result_applied = false;
            trxdb_free_result(&s_result);
            s_info_valid = false;          /* drop stale data from the previous sat */
            s_transponder_count = 0;

            int gen = s_target_gen;
            std::thread(trxdb_worker_fetch, gen, std::string(norad)).detach();
        }
    }

    /* apply a completed fetch on the UI thread (GL + parsing live here) */
    TrxdbResult pending;
    bool have_pending = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_fetching && s_result.gen == s_target_gen && !s_result_applied)
        {
            pending = s_result;
            memset(&s_result, 0, sizeof(s_result));
            s_result_applied = true;
            have_pending = true;
        }
    }

    if (have_pending)
    {
        /* release the previous texture (GL context is current here) */
        if (s_tex_valid)
        {
            UnloadTexture(s_tex);
            s_tex_valid = false;
        }

        s_info_valid = false;
        if (pending.ok && pending.object_json)
        {
            parse_object_json(pending.object_json, &s_info);
            s_info_valid = true;
        }

        s_transponder_count = 0;
        if (pending.transponders_ok && pending.transponders_json)
        {
            s_transponder_count = parse_transponders_json(
                pending.transponders_json, s_transponders, MAX_TRXDB_TRANSPONDERS);
        }

        if (pending.image_ok && pending.image_data && pending.image_size > 0)
        {
            const char *fmt = detect_image_format(pending.image_data, pending.image_size);
            if (fmt)
            {
                Image img = LoadImageFromMemory(fmt, (const unsigned char *)pending.image_data,
                                                (int)pending.image_size);
                if (img.data)
                {
                    s_tex = LoadTextureFromImage(img);
                    s_tex_valid = true;
                    UnloadImage(img);
                    LOG_INFO("TRXDB: loaded image (%dx%d, %s)", s_tex.width, s_tex.height, fmt);
                }
                else
                {
                    LOG_WARN("TRXDB: LoadImageFromMemory failed for %s (%zu bytes)", fmt, pending.image_size);
                }
            }
            else
            {
                LOG_WARN("TRXDB: unrecognized image format (%zu bytes)", pending.image_size);
            }
        }
        else
        {
            LOG_WARN("TRXDB: image fetch failed or empty (%d, %zu bytes)",
                     pending.image_ok ? 1 : 0, pending.image_size);
        }

        trxdb_free_result(&pending);
    }

    /* header */
    ImGui::TextColored(ThemeColor(g_theme.ui.ui_accent), "%s TRXDB", ICON_FA_SATELLITE_DISH);
    ImGui::SameLine();
    ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "NORAD %s", norad);

    bool fetching = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        fetching = s_fetching;
    }

    if (ImGui::Button(fetching ? ICON_FA_SPINNER " Fetching..." : ICON_FA_ROTATE " Refresh",
                      ImVec2(avail_w, 0)))
    {
        if (!fetching)
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_target_gen++;
            s_fetching = true;
            s_result_applied = false;
            trxdb_free_result(&s_result);
            s_info_valid = false;
            s_transponder_count = 0;
            int gen = s_target_gen;
            std::thread(trxdb_worker_fetch, gen, std::string(norad)).detach();
        }
    }
    ImGui::Separator();

    if (fetching)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "%s Fetching TRXDB data...", ICON_FA_SPINNER);
        ImGui::PopTextWrapPos();
        return;
    }

    if (!s_info_valid)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.notif_error),
                           "%s Failed to fetch object data from TRXDB.", ICON_FA_CIRCLE_INFO);
        ImGui::PopTextWrapPos();
        return;
    }

    /* -- image ------------------------------------------------------------ */
    if (s_tex_valid && s_tex.width > 0 && s_tex.height > 0)
    {
        float img_w = avail_w;
        float img_h = img_w * (float)s_tex.height / (float)s_tex.width;
        const float max_h = 300.0f;
        if (img_h > max_h)
        {
            img_h = max_h;
            img_w = img_h * (float)s_tex.width / (float)s_tex.height;
        }
        ImGui::Image((ImTextureID)(intptr_t)s_tex.id, ImVec2(img_w, img_h));
        ImGui::Separator();
    }

    /* -- object info ------------------------------------------------------ */
    if (ImGui::BeginTable("##trxdb_info", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        InfoRow("Name", "%s", s_info.name);
        InfoRow("NORAD", "%s", s_info.id);
        InfoRow("Status", "%s", s_info.status);
        InfoRow("Short Desc", "%s", s_info.short_description);
        InfoRow("Operator", "%s", s_info.operator_name);
        InfoRow("Launch Vehicle", "%s", s_info.launch_vehicle);
        InfoRow("Orbit", "%s", s_info.orbit);
        InfoRow("Launched", "%s", s_info.launched);
        InfoRow("Created", "%s", s_info.created);
        InfoRow("Updated", "%s", s_info.updated);
        ImGui::EndTable();
    }

    /* -- description ------------------------------------------------------ */
    if (s_info.description[0])
    {
        ImGui::Separator();
        ImGui::Text("%s Description", ICON_FA_FILE_LINES);
        ImGui::TextWrapped("%s", s_info.description);
    }

    /* -- transponders ----------------------------------------------------- */
    ImGui::Separator();
    ImGui::Text("%s Transponders (%d)", ICON_FA_TOWER_BROADCAST, s_transponder_count);
    for (int i = 0; i < s_transponder_count; i++)
    {
        TrxdbTransponder *t = &s_transponders[i];
        char header[160];
        snprintf(header, sizeof(header), "%s %s %s##trxdb_tp_%d",
                 ICON_FA_RADIO, t->mode, t->id, i);
        if (ImGui::CollapsingHeader(header))
        {
            if (ImGui::BeginTable("##trxdb_tp_table", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                InfoRow("ID", "%s", t->id);
                InfoRow("Type", "%s", t->type);
                InfoRow("Status", "%s", t->status);
                InfoRow("Mode", "%s", t->mode);
                InfoRow("Uplink", "%s", t->uplink);
                InfoRow("Symrate", "%s", t->symrate);
                InfoRow("Bandwidth", "%s", t->bandwidth);
                InfoRow("Modulation", "%s", t->modulation);
                InfoRow("FEC", "%s", t->fec);
                InfoRow("Encrypted", "%s", t->encrypted ? "Yes" : "No");
                if (t->description[0])
                    InfoRow("Description", "%s", t->description);
                ImGui::EndTable();
            }
        }
    }

    ImGui::PopTextWrapPos();
}