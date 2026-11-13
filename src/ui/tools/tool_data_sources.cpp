/*
 * tool_data_sources.cpp - Data Sources panel
 */

#include "tools.h"
#include "tools_common.h"
#include "tools_registry.h"
#include "ui/ui_layout.h"
#include "ui/imgui_theme.h"
#include "ui/notifications.h"
#include "core/astro.h"
#include "core/theme.h"
#include "core/config.h"
#include "data/file_dialog.h"
#include "data/provider.h"
#include "data/storage.h"
#include "data/omm_parser.h"
#include "data/async_fetch.h"
#include "data/ephem.h"
#include "data/ephem_parse.h"
#include "data/horizons_api.h"
#include "util/log.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <thread>
#include <atomic>
#include <utility>
#include <vector>

#include <raylib.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

/* case-insensitive alphabetical comparison for sorting source lists */
static int CiStrCmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        unsigned char ca = (unsigned char)tolower((unsigned char)*a);
        unsigned char cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb) return (int)ca - (int)cb;
        a++;
        b++;
    }
    return (*a) ? 1 : ((*b) ? -1 : 0);
}

/* insertion sort the (mutable) Retlector groups alphabetically by name */
static void SortRetlectorGroups(RetlectorGroup *groups, int count)
{
    for (int i = 1; i < count; i++)
    {
        RetlectorGroup key = groups[i];
        int j = i - 1;
        while (j >= 0 && CiStrCmp(groups[j].name, key.name) > 0)
        {
            groups[j + 1] = groups[j];
            j--;
        }
        groups[j + 1] = key;
    }
}

static int *GetCelestrakSortedIndex(void)
{
    static int *s_idx = NULL;
    static bool s_built = false;
    if (!s_built)
    {
        int n = NUM_CELESTRAK_SOURCES;
        s_idx = (int *)malloc((size_t)n * sizeof(int));
        for (int i = 0; i < n; i++) s_idx[i] = i;
        for (int i = 1; i < n; i++)
        {
            int key = s_idx[i];
            int j = i - 1;
            while (j >= 0 && CiStrCmp(CELESTRAK_SOURCES[s_idx[j]].name,
                                      CELESTRAK_SOURCES[key].name) > 0)
            {
                s_idx[j + 1] = s_idx[j];
                j--;
            }
            s_idx[j + 1] = key;
        }
        s_built = true;
    }
    return s_idx;
}

// parsed mission awaiting the "Add Mission" modal; the id is assigned by EphemAdd()
static EphemMission g_ephem_pending;
static bool g_ephem_pending_valid = false;

// false when the file cannot be opened or is empty
static bool ReadWholeFile(const char *path, std::vector<char> *out)
{
    out->clear();
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out->insert(out->end(), buf, buf + n);
    const bool ok = (ferror(f) == 0);
    fclose(f);
    return ok && !out->empty();
}

// filename without directory or extension, truncated to the target buffer
static void FileStem(const char *path, char *out, size_t sz)
{
    const char *base = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    const char *dot = strrchr(base, '.');
    const size_t len = dot ? (size_t)(dot - base) : strlen(base);
    const size_t n = (len < sz - 1) ? len : sz - 1;
    if (n > 0)
        memcpy(out, base, n);
    out[n] = '\0';
}

// unix seconds -> "YYYY-MM-DD HH:MM UTC" (mission windows are read as UTC)
static void FormatUnixUtc(double t_unix, char *buf, size_t sz)
{
    const time_t t = (time_t)t_unix;
    struct tm *tm_info = gmtime(&t);
    if (!tm_info)
    {
        snprintf(buf, sz, "----/--/-- --:--");
        return;
    }
    snprintf(buf, sz, "%04d-%02d-%02d %02d:%02d UTC",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min);
}

// Commit the pending mission and its Satellite row; returns the new id or 0.
static int EphemCommitPending(UIContext *ctx, const char *name, const char *target,
                              char *err, size_t err_sz)
{
    (void)ctx;

    if (err && err_sz > 0)
        err[0] = '\0';
    if (!g_ephem_pending_valid)
        return 0;

    EphemMission m = g_ephem_pending;
    snprintf(m.name, sizeof(m.name), "%s", name ? name : "");
    snprintf(m.target, sizeof(m.target), "%s", target ? target : "");
    m.norad_id_num = 0;

    // re-adding a loaded target refreshes in place: EphemReplace() keeps the id
    const int existing_id = EphemFindByTarget(m.target);
    if (existing_id != 0)
    {
        m.norad_id_num = 900000u + (uint32_t)existing_id;
        if (!EphemReplace(existing_id, m))
        {
            if (err && err_sz > 0)
                snprintf(err, err_sz, "Mission vanished while refreshing.");
            return 0;
        }

        for (int i = 0; i < sat_count; i++)
        {
            if (satellites[i].kind == SAT_KIND_EPHEM && satellites[i].ephem_id == existing_id)
            {
                satellites[i].norad_id_num = 900000u + (uint32_t)existing_id;
                satellites[i].epoch_unix = m.t0_unix;
                satellites[i].data_meta.fetch_time = time(NULL);
                satellites[i].data_meta.epoch_time = (time_t)m.t0_unix;
                snprintf(satellites[i].data_meta.source_name,
                         sizeof(satellites[i].data_meta.source_name),
                         "Horizons %s", m.target[0] ? m.target : m.name);
                break;
            }
        }

        SaveOrbitalData("data.json", satellites, sat_count);
        NotifyPush(NOTIFY_SUCCESS, ICON_FA_ARROWS_ROTATE,
                   "Refreshed mission '%s' (%d samples)", m.name, (int)m.s.size());
        LOG_INFO("Refreshed ephemeris mission '%s' in place (id %d, %d samples)",
                 m.name, existing_id, (int)m.s.size());

        g_ephem_pending_valid = false;
        return existing_id;
    }

    const int id = EphemAdd(m);
    if (id == 0)
    {
        if (err && err_sz > 0)
            snprintf(err, err_sz, "Mission registry full (max %d missions).",
                     EPHEM_MAX_MISSIONS);
        return 0;
    }

    if (sat_count >= GetMaxSatellites() || sat_count >= MAX_SATELLITES_HARD_CAP)
    {
        EphemRemove(id);
        if (err && err_sz > 0)
            snprintf(err, err_sz, "Satellite limit reached; mission was not added.");
        return 0;
    }

    Satellite *sat = &satellites[sat_count++];
    memset(sat, 0, sizeof(*sat));

    snprintf(sat->name, sizeof(sat->name), "%s", m.name);
    /* display-only synthetic catalogue id: "M%05d" fits norad_id[10] */
    snprintf(sat->norad_id, sizeof(sat->norad_id), "M%05d", id);
    sat->kind = SAT_KIND_EPHEM;
    sat->ephem_id = id;
    // dedicated 900000+ block: real NORAD ids stay below 700000, 32 missions max
    sat->norad_id_num = 900000u + (uint32_t)id;
    sat->epoch_unix = m.t0_unix;
    sat->data_meta.format = FORMAT_HORIZONS_VECTORS;
    sat->data_meta.fetch_time = time(NULL);
    sat->data_meta.epoch_time = (time_t)m.t0_unix;
    snprintf(sat->data_meta.source_name, sizeof(sat->data_meta.source_name),
             "Horizons %s", m.target[0] ? m.target : m.name);
    sat->is_active = false;
    sat->ephem_window_disabled = false;

    SaveOrbitalData("data.json", satellites, sat_count);
    NotifyPush(NOTIFY_SUCCESS, ICON_FA_SATELLITE,
               "Imported mission '%s' (%d samples)", m.name, (int)m.s.size());
    LOG_INFO("Imported ephemeris mission '%s' (id %d, %d samples)",
             m.name, id, (int)m.s.size());

    g_ephem_pending_valid = false;
    return id;
}

// Compact display label; the full name and target id stay in the hover tooltip.
static const char *HorizonsDisplayName(const char *name)
{
    if (!name)
        return "";

    struct Alias
    {
        const char *full;
        const char *shorter;
    };
    static const Alias kAliases[] = {
        { "James Webb Space Telescope", "JWST" },
        { "Herschel Space Observatory", "Herschel" },
        { "Planck Space Observatory", "Planck" },
        { "Infrared Space Observatory", "ISO" },
        { "International Space Station", "ISS" },
        { "Chinese Space Station (CSS)", "CSS" },
        { "Mars Reconnaissance Orbiter", "MRO" },
        { "Hubble Space Telescope", "HST" },
        { "Chandrayaan-2 (ORBITER)", "Chandrayaan-2 Orbiter" },
        { "Chandrayaan-2 (LANDER)", "Chandrayaan-2 Lander" },
        { "Chandrayaan-3 (LANDER)", "Chandrayaan-3 Lander" },
        { "Chandrayaan-3P (ORBITER)", "Chandrayaan-3P Orbiter" },
        { "Lunar Prospector (LP)", "Lunar Prospector" },
        { "Lucy Centaur RB Booster", "Lucy Centaur Booster" },
        { "LCROSS Centaur Impactor", "LCROSS Impactor" },
        { "GRAIL-SS Second Stage", "GRAIL-SS 2nd Stage" },
        { "Clementine Extended", "Clementine Ext." },
    };

    for (const Alias &a : kAliases)
        if (strcmp(a.full, name) == 0)
            return a.shorter;
    return name;
}

// Draw the error as one clipped line; the full message stays on hover.
static void HorizonsDrawErrorLine(const char *err)
{
    if (!err || err[0] == '\0')
        return;

    char one[256];
    size_t n = 0;
    for (const char *p = err; *p && n < sizeof(one) - 1; ++p)
        one[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p;
    one[n] = '\0';

    const float kNoWrap = 1.0e9f;
    const float max_w = ImGui::GetContentRegionAvail().x;
    char disp[260];

    if (ImGui::CalcTextSize(one, NULL, false, kNoWrap).x <= max_w)
    {
        snprintf(disp, sizeof(disp), "%s", one);
    }
    else
    {
        // binary-search the longest prefix that fits alongside an ellipsis
        size_t lo = 0, hi = n;
        while (lo < hi)
        {
            const size_t mid = (lo + hi + 1) / 2;
            char probe[264];
            snprintf(probe, sizeof(probe), "%.*s...", (int)mid, one);
            if (ImGui::CalcTextSize(probe, NULL, false, kNoWrap).x <= max_w)
                lo = mid;
            else
                hi = mid - 1;
        }
        snprintf(disp, sizeof(disp), "%.*s...", (int)lo, one);
    }

    ImGui::TextColored(ThemeColor(g_theme.ui.error),
                       ICON_FA_TRIANGLE_EXCLAMATION " %s", disp);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", one);
}

// The worker does not copy these strings, so they must outlive the request.
static char s_hz_name[64] = "";
static char s_hz_command[32] = "";
static char s_hz_start[32] = "";
static char s_hz_stop[32] = "";
static char s_hz_step[16] = "";
static HorizonsMissionEntry s_hz_entry = {};
static bool s_hz_entry_valid = false;

// true when ephem_error came from a fetch; drives the "import a file instead" button
static bool s_horizons_error_was_fetch = false;

// copy the five string fields into the file-static backing store
static void HorizonsSnapshotEntry(const HorizonsMissionEntry *e)
{
    snprintf(s_hz_name, sizeof(s_hz_name), "%s", (e && e->name) ? e->name : "");
    snprintf(s_hz_command, sizeof(s_hz_command), "%s", (e && e->command) ? e->command : "");
    snprintf(s_hz_start, sizeof(s_hz_start), "%s", (e && e->start_time) ? e->start_time : "");
    snprintf(s_hz_stop, sizeof(s_hz_stop), "%s", (e && e->stop_time) ? e->stop_time : "");
    snprintf(s_hz_step, sizeof(s_hz_step), "%s", (e && e->step_size) ? e->step_size : "");
    s_hz_entry.name = s_hz_name;
    s_hz_entry.command = s_hz_command;
    s_hz_entry.start_time = s_hz_start;
    s_hz_entry.stop_time = s_hz_stop;
    s_hz_entry.step_size = s_hz_step;
    s_hz_entry_valid = true;
}

// clear stale error state, then start a fetch from a catalog entry
static bool HorizonsStartCatalogEntry(const HorizonsCatalogEntry *e)
{
    if (!e)
        return false;

    HorizonsMissionEntry local = {};
    local.name = e->name;
    local.command = e->command;
    local.start_time = e->start_time;
    local.stop_time = e->stop_time;
    local.step_size = e->step_size;
    HorizonsSnapshotEntry(&local);

    g_ui.ephem_error[0] = '\0';
    s_horizons_error_was_fetch = false;
    if (!HorizonsFetchStart(&s_hz_entry))
    {
        snprintf(g_ui.ephem_error, sizeof(g_ui.ephem_error),
                 "A Horizons request is already in flight.");
        return false;
    }
    return true;
}

// typed target not in the snapshot; the client widens then clamp-retries the window
static bool HorizonsStartAdHoc(const char *command)
{
    HorizonsMissionEntry local = {};
    if (!HorizonsMakeAdHocEntry(command, "", "", "", &local))
        return false;
    HorizonsSnapshotEntry(&local);

    g_ui.ephem_error[0] = '\0';
    s_horizons_error_was_fetch = false;
    if (!HorizonsFetchStart(&s_hz_entry))
    {
        snprintf(g_ui.ephem_error, sizeof(g_ui.ephem_error),
                 "A Horizons request is already in flight.");
        return false;
    }
    return true;
}

void DrawPanelDataSources(UIContext *ctx, AppConfig *cfg)
{
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    /* ====================================================================
     *  Retlector Section
     * ==================================================================== */
    {
        static std::atomic<bool> s_fetch_running{false};
        static std::atomic<bool> s_fetch_done{false};
        static RetlectorGroup s_pending[MAX_RETLECTOR_GROUPS];
        static int s_pending_count = 0;
        static long s_pending_http_code = 0;
        static bool s_fetch_attempted = false;
        static int s_retlector_combo_idx = 0;

        if (ImGui::CollapsingHeader("Retlector"))
        {
            if (!cfg->retlector_groups_fetched &&
                !s_fetch_running.load() &&
                !s_fetch_attempted)
            {
                s_fetch_attempted = true;
                s_fetch_running = true;
                s_fetch_done = false;
                s_pending_http_code = 0;

                std::thread([]() {
                    long http_code = 0;
                    int count = FetchRetlectorGroups(
                        s_pending, MAX_RETLECTOR_GROUPS, &http_code);
                    s_pending_http_code = http_code;
                    s_pending_count = count;
                    s_fetch_done = true;
                }).detach();
            }

            if (s_fetch_done.load())
            {
                if (s_pending_count > 0)
                {
                    for (int i = 0; i < s_pending_count && i < MAX_RETLECTOR_GROUPS; i++)
                        cfg->retlector_groups[i] = s_pending[i];
                    cfg->retlector_group_count = s_pending_count;
                    SortRetlectorGroups(cfg->retlector_groups, cfg->retlector_group_count);
                    cfg->retlector_groups_fetched = true;
                    LOG_INFO("Loaded %d retlector groups", s_pending_count);
                    NotifyPush(NOTIFY_INFO, ICON_FA_SATELLITE_DISH,
                               "Discovered %d new data sources", s_pending_count);
                }
                else if (s_pending_http_code == 403)
                {
                    LOG_WARN("Retlector group discovery blocked by network (HTTP 403)");
                    NotifyPush(NOTIFY_WARNING, ICON_FA_TRIANGLE_EXCLAMATION,
                               "Retlector blocked by network (HTTP 403)");
                }
                else if (s_pending_http_code > 0)
                {
                    LOG_ERROR("Retlector group discovery failed (HTTP %ld)",
                              s_pending_http_code);
                    NotifyPush(NOTIFY_WARNING, ICON_FA_TRIANGLE_EXCLAMATION,
                               "Retlector unavailable (HTTP %ld)", s_pending_http_code);
                }
                else
                {
                    LOG_ERROR("Retlector group discovery failed");
                    NotifyPush(NOTIFY_WARNING, ICON_FA_TRIANGLE_EXCLAMATION,
                               "Retlector network request failed");
                }

                s_fetch_done = false;
                s_fetch_running = false;
            }

            if (s_fetch_running.load())
            {
                ImGui::Text("Discovering available data sources...");
                ImGui::SameLine();
                static float spinner_angle = 0.0f;
                spinner_angle += ImGui::GetIO().DeltaTime * 180.0f;
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "%c",
                                   "|/-\\"[(int)(spinner_angle / 45.0f) % 4]);
            }
            else if (cfg->retlector_group_count > 0)
            {
                if (s_retlector_combo_idx >= cfg->retlector_group_count)
                    s_retlector_combo_idx = 0;

                const char *combo_preview = cfg->retlector_groups[s_retlector_combo_idx].name;
                ImGui::SetNextItemWidth(avail_w * 0.65f);
                if (ImGui::BeginCombo("##retlector_group", combo_preview))
                {
                    static char search_buf[64] = "";
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    ImGui::InputTextWithHint("##retlector_search",
                                             ICON_FA_MAGNIFYING_GLASS " Search groups...",
                                             search_buf, sizeof(search_buf));
                    ImGui::Separator();

                    for (int i = 0; i < cfg->retlector_group_count; i++)
                    {
                        if (search_buf[0] != '\0' &&
                            !str_contains_ic(cfg->retlector_groups[i].name, search_buf))
                            continue;

                        bool is_selected = (i == s_retlector_combo_idx);
                        if (ImGui::Selectable(cfg->retlector_groups[i].name, is_selected))
                            s_retlector_combo_idx = i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_PLUS "##add_retlector"))
                {
                    const char *name = cfg->retlector_groups[s_retlector_combo_idx].name;
                    if (!DataSelectionAdd(SOURCE_RETLECTOR, name, name, NULL, FORMAT_OMM_CSV))
                        LOG_DEBUG("Retlector group '%s' already in selection list", name);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Add this group to the active selections list");
            }
            else if (s_fetch_attempted)
            {
                if (s_pending_http_code == 403)
                    ImGui::TextColored(ThemeColor(g_theme.ui.warning),
                                       "Retlector blocked by network (HTTP 403)");
                else if (s_pending_http_code > 0)
                    ImGui::TextColored(ThemeColor(g_theme.ui.warning),
                                       "Retlector unavailable (HTTP %ld)",
                                       s_pending_http_code);
                else
                    ImGui::TextColored(ThemeColor(g_theme.ui.warning),
                                       "Failed to reach retlector.eu");

                if (ImGui::SmallButton("Retry"))
                {
                    s_pending_count = 0;
                    s_pending_http_code = 0;
                    s_fetch_attempted = false;
                    cfg->retlector_groups_fetched = false;
                }
            }
        }
    }

    /* ====================================================================
     *  Celestrak Section
     * ==================================================================== */
    if (ImGui::CollapsingHeader("Celestrak"))
    {
        static int s_celestrak_combo_idx = 0;

        if (s_celestrak_combo_idx >= NUM_CELESTRAK_SOURCES)
            s_celestrak_combo_idx = 0;

        int *celestrak_idx = GetCelestrakSortedIndex();
        const char *combo_preview = CELESTRAK_SOURCES[s_celestrak_combo_idx].name;
        ImGui::SetNextItemWidth(avail_w * 0.65f);
        if (ImGui::BeginCombo("##celestrak_group", combo_preview))
        {
            static char search_buf[64] = "";
            bool search_active = (search_buf[0] != '\0');
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##celestrak_search", ICON_FA_MAGNIFYING_GLASS " Search sources...",
                                     search_buf, sizeof(search_buf));
            ImGui::Separator();

            for (int k = 0; k < NUM_CELESTRAK_SOURCES; k++)
            {
                int i = celestrak_idx[k];
                if (search_active && !str_contains_ic(CELESTRAK_SOURCES[i].name, search_buf))
                    continue;
                bool is_selected = (i == s_celestrak_combo_idx);
                if (ImGui::Selectable(CELESTRAK_SOURCES[i].name, is_selected))
                    s_celestrak_combo_idx = i;
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_PLUS "##add_celestrak"))
        {
            const char *name = CELESTRAK_SOURCES[s_celestrak_combo_idx].name;
            if (!DataSelectionAdd(SOURCE_CELESTRAK, name, name, NULL, FORMAT_OMM_CSV))
            {
                LOG_DEBUG("Celestrak group '%s' already in selection list", name);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add this group to the active selections list");
    }

    /* ====================================================================
     *  Custom URL Section
     * ==================================================================== */
    if (ImGui::CollapsingHeader("Custom URL"))
    {
        static char s_url_buf[512] = "";

        /* URL input + Add button on the same line */
        float url_btn_w = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(avail_w - url_btn_w - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##custom_url", s_url_buf, sizeof(s_url_buf));
        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_PLUS "##add_custom_url", ImVec2(url_btn_w, url_btn_w)) && s_url_buf[0])
        {
            /* extract a short name from the URL for display */
            const char *name_start = strrchr(s_url_buf, '/');
            if (name_start) name_start++; else name_start = s_url_buf;

            if (!DataSelectionAdd(SOURCE_CUSTOM_URL, name_start, s_url_buf, NULL, FORMAT_UNKNOWN))
            {
                LOG_DEBUG("URL '%s' already in selection list", s_url_buf);
            }
            else
            {
                s_url_buf[0] = '\0'; /* clear on success */
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add this URL to the active selections (fetched on pull)");
    }

    /* ====================================================================
     *  Custom File Section
     * ==================================================================== */
    if (ImGui::CollapsingHeader("Custom File"))
    {
        /* file input + file dialog + add button on the same line */
        float file_btn_w = ImGui::GetFrameHeight();
        static char s_file_buf[512] = "";

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Accepted formats: 3LE, JSON OMM, CSV OMM, KVN OMM, XML OMM");

        ImGui::SetNextItemWidth(avail_w - 2*file_btn_w - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##custom_file", s_file_buf, sizeof(s_file_buf));
        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FOLDER "##select_custom_file",ImVec2(file_btn_w, file_btn_w)))
        {
            // Default to "All Files"
            const std::vector<std::string> filter = { "All Files", "*", "GP", "*.3le *.json *.csv *.kvn *.xml" };
            g_ui.file_dialog_tle.open("Open TLE file","",filter);
        }
        if (g_ui.file_dialog_tle.isOpen() && g_ui.file_dialog_tle.draw())
        {
            std::snprintf(s_file_buf, sizeof(s_file_buf), "%s",
                          g_ui.file_dialog_tle.fileName().c_str());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select file");
        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_PLUS "##add_custom_file", ImVec2(file_btn_w, file_btn_w)) && s_file_buf[0])
        {
            /* extract a short name from the file for display */
            const char *name_start = strrchr(s_file_buf, '/');
            if (name_start) name_start++; else name_start = s_file_buf;

            if (!DataSelectionAdd(SOURCE_CUSTOM_FILE, name_start, s_file_buf, NULL, FORMAT_UNKNOWN))
            {
                LOG_DEBUG("File '%s' already in selection list", s_file_buf);
            }
            else
            {
                s_file_buf[0] = '\0'; /* clear on success */
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add this file to the active selections (fetched on pull)");
    }

    /* ====================================================================
     *  Custom Paste Section
     * ==================================================================== */
    if (ImGui::CollapsingHeader("Custom Paste"))
    {
        static char s_paste_buf[4096] = "";

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Accepted formats: 3LE, JSON OMM, CSV OMM, KVN OMM, XML OMM");

        /* thin by default (1 line), expands as content is pasted */
        float paste_h = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
        if (s_paste_buf[0] != '\0')
        {
            int line_count = 1;
            for (const char *p = s_paste_buf; *p; p++)
                if (*p == '\n') line_count++;
            paste_h = fminf(line_count * ImGui::GetTextLineHeightWithSpacing() + 8.0f, 200.0f);
        }

        /* paste textbox + add button on the same line (mirrors the Custom URL row) */
        float paste_btn_w = ImGui::GetFrameHeight();
        float paste_box_w = avail_w - paste_btn_w - ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(paste_box_w);
        ImGui::InputTextMultiline("##paste", s_paste_buf, sizeof(s_paste_buf),
                                  ImVec2(0, paste_h));
        ImGui::SameLine();

        OrbitalDataFormat paste_fmt = FORMAT_UNKNOWN;
        bool has_content = (s_paste_buf[0] != '\0');
        if (has_content)
            paste_fmt = DetectDataFormat(s_paste_buf, strlen(s_paste_buf));

        /* 3LE requires a name line; element-only input is rejected */
        bool tle_missing_name = has_content && (paste_fmt == FORMAT_TLE) &&
                                !tleHasNameLine(s_paste_buf);

        bool can_add = has_content && (paste_fmt != FORMAT_UNKNOWN);

        if (!can_add)
            ImGui::BeginDisabled();

        if (ImGui::Button(ICON_FA_PLUS "##add_custom_paste", ImVec2(paste_btn_w, paste_btn_w)))
        {
            if (tle_missing_name)
            {
                /* reject element-only 3LE input: a name line is mandatory */
                NotifyPush(NOTIFY_ERROR, ICON_FA_TRIANGLE_EXCLAMATION,
                           "Invalid 3LE: a name line is required "
                           "(name, then line 1 and line 2)");
                LOG_DEBUG("Rejected 3LE paste: missing name line");
            }
            else
            {
                /* build a short preview for the name */
                char preview[64];
                const char *nl = strchr(s_paste_buf, '\n');
                if (nl)
                {
                    int len = (int)(nl - s_paste_buf);
                    if (len > 55) len = 55;
                    strncpy(preview, s_paste_buf, len);
                    preview[len] = '\0';
                }
                else
                {
                    strncpy(preview, s_paste_buf, 55);
                    preview[55] = '\0';
                }

                if (!DataSelectionAdd(SOURCE_CUSTOM_PASTE, preview, preview, s_paste_buf, paste_fmt))
                {
                    LOG_DEBUG("Paste entry already in selection list");
                }
                else
                {
                    s_paste_buf[0] = '\0'; /* clear on success */
                }
            }
        }

        if (!can_add)
            ImGui::EndDisabled();

        if (has_content)
        {
            if (paste_fmt != FORMAT_UNKNOWN)
            {
                const char *fmt_label = (paste_fmt == FORMAT_TLE) ? "3LE"
                                                                  : FormatToString(paste_fmt);
                if (tle_missing_name)
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                       "Detected: %s - missing name line "
                                       "(name, line 1, line 2 required)", fmt_label);
                else
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Detected: %s",
                                       fmt_label);
            }
            else
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                   "Format unknown - not a valid orbital data format");
        }
    }

    /* Horizons Section (JPL Horizons VECTORS ephemerides) */
    if (ImGui::CollapsingHeader("Horizons"))
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
                           ICON_FA_SATELLITE " JPL Horizons VECTORS");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Fetch mission trajectories directly from JPL Horizons.\n"
                              "Accepts VECTORS exports (.txt / .csv).\n"
                              "Ephemeris courtesy NASA/JPL Horizons.");
        }
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("Open on JPL", "https://ssd.jpl.nasa.gov/api/horizons.api");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open the JPL Horizons API page in your browser");

        static int s_horizons_combo_idx = 0;
        const int hz_cat_count = HorizonsCatalogCount();
        if (s_horizons_combo_idx < 0 || s_horizons_combo_idx >= hz_cat_count)
            s_horizons_combo_idx = 0;
        const HorizonsCatalogEntry *sel_c =
            (hz_cat_count > 0) ? HorizonsCatalogAt(s_horizons_combo_idx) : NULL;

        const float add_btn_w = ImGui::GetFrameHeight();

        ImGui::SetNextItemWidth(avail_w * 0.65f);
        if (ImGui::BeginCombo("##horizons_mission",
                              sel_c ? HorizonsDisplayName(sel_c->name) : ""))
        {
            const ImGuiStyle &hstyle = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2(hstyle.ItemSpacing.x,
                                       hstyle.ItemSpacing.y * 0.35f));

            static char search_buf[64] = "";
            const bool search_active = (search_buf[0] != '\0');
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##horizons_search",
                                     ICON_FA_MAGNIFYING_GLASS " Search missions...",
                                     search_buf, sizeof(search_buf));
            ImGui::Separator();

            // group headers appear in first-appearance order, entries beneath each
            const char *groups[8];
            int n_groups = 0;
            for (int i = 0; i < hz_cat_count && n_groups < 8; i++)
            {
                const HorizonsCatalogEntry *e = HorizonsCatalogAt(i);
                if (!e || !e->group)
                    continue;
                bool seen = false;
                for (int g = 0; g < n_groups; g++)
                {
                    if (strcmp(groups[g], e->group) == 0)
                    {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                    groups[n_groups++] = e->group;
            }

            for (int gi = 0; gi < n_groups; gi++)
            {
                bool header_done = false;
                for (int i = 0; i < hz_cat_count; i++)
                {
                    const HorizonsCatalogEntry *e = HorizonsCatalogAt(i);
                    if (!e || !e->group || strcmp(e->group, groups[gi]) != 0)
                        continue;
                    if (search_active &&
                        !str_contains_ic(e->name, search_buf) &&
                        !str_contains_ic(HorizonsDisplayName(e->name), search_buf) &&
                        !str_contains_ic(e->command, search_buf))
                        continue;

                    if (!header_done)
                    {
                        ImGui::TextDisabled("%s", groups[gi]);
                        header_done = true;
                    }

                    bool is_selected = (i == s_horizons_combo_idx);
                    if (ImGui::Selectable(HorizonsDisplayName(e->name), is_selected))
                        s_horizons_combo_idx = i;
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s\nTarget: %s\nWindow: %s .. %s\nStep: %s",
                                          e->name ? e->name : "",
                                          e->command ? e->command : "",
                                          e->start_time ? e->start_time : "",
                                          e->stop_time ? e->stop_time : "",
                                          e->step_size ? e->step_size : "");
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::PopStyleVar();
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered() && sel_c)
        {
            ImGui::SetTooltip("%s\nTarget: %s\nWindow: %s .. %s\nStep: %s\nGroup: %s",
                              sel_c->name ? sel_c->name : "",
                              sel_c->command ? sel_c->command : "",
                              sel_c->start_time ? sel_c->start_time : "",
                              sel_c->stop_time ? sel_c->stop_time : "",
                              sel_c->step_size ? sel_c->step_size : "",
                              sel_c->group ? sel_c->group : "");
        }

        ImGui::SameLine();
        float hz_btn_x = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - add_btn_w;
        ImGui::SameLine(hz_btn_x);
        /* an already-loaded target refreshes in place, so the button says so */
        const bool hz_already_loaded =
            (sel_c != NULL) && (EphemFindByTarget(sel_c->command) != 0);
        const char *hz_btn_label = hz_already_loaded
                                       ? ICON_FA_ARROWS_ROTATE "##add_horizons"
                                       : ICON_FA_PLUS "##add_horizons";
        if (ImGui::Button(hz_btn_label, ImVec2(add_btn_w, add_btn_w)))
        {
            HorizonsStartCatalogEntry(sel_c);
        }
        if (ImGui::IsItemHovered())
        {
            if (hz_already_loaded)
                ImGui::SetTooltip("Already loaded - adding will refresh this mission");
            else
                ImGui::SetTooltip("Fetch this mission from JPL Horizons");
        }

        /* typed target fallback: need not be in the bundled snapshot */
        static char s_horizons_target_buf[32] = "";
        ImGui::SetNextItemWidth(avail_w * 0.5f);
        ImGui::InputTextWithHint("##horizons_target",
                                 "Target id or name, e.g. -1024",
                                 s_horizons_target_buf, sizeof(s_horizons_target_buf));
        ImGui::SameLine();
        if (ImGui::SmallButton("Add by ID"))
        {
            if (s_horizons_target_buf[0] == '\0')
            {
                snprintf(g_ui.ephem_error, sizeof(g_ui.ephem_error),
                         "Enter a Horizons target id, e.g. -1024");
                s_horizons_error_was_fetch = false;
            }
            else
            {
                const int found = HorizonsCatalogFind(s_horizons_target_buf);
                if (found >= 0)
                {
                    s_horizons_combo_idx = found;
                    HorizonsStartCatalogEntry(HorizonsCatalogAt(found));
                }
                else if (!HorizonsStartAdHoc(s_horizons_target_buf))
                {
                    snprintf(g_ui.ephem_error, sizeof(g_ui.ephem_error),
                             "Enter a Horizons target id, e.g. -1024");
                    s_horizons_error_was_fetch = false;
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Fetch any JPL Horizons target by id or name");

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN "##import_horizons_file"))
        {
            const std::vector<std::string> filter = { "Ephemeris", "*.txt *.csv", "All Files", "*" };
            g_ui.file_dialog_ephem.open("Import JPL Horizons VECTORS", "", filter);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Import a saved JPL Horizons VECTORS file (.txt / .csv)");

        HorizonsFetchState hz_state = HorizonsFetchPoll();
        if (hz_state == HZ_RUNNING)
        {
            static float spinner_angle = 0.0f;
            spinner_angle += ImGui::GetIO().DeltaTime * 180.0f;
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
                               "Fetching from JPL Horizons...");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "%c",
                               "|/-\\"[(int)(spinner_angle / 45.0f) % 4]);
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel"))
                HorizonsFetchCancel();
        }
        else if (hz_state == HZ_DONE || hz_state == HZ_FAILED)
        {
            // s_hz_entry is the fetched snapshot, so a later combo change cannot mislabel
            EphemMission parsed = {};
            char hz_err[256] = "";
            const bool taken = HorizonsFetchTake(&parsed, hz_err, sizeof hz_err);
            if (!taken || hz_state == HZ_FAILED)
            {
                snprintf(g_ui.ephem_error, sizeof(g_ui.ephem_error), "%s",
                         (hz_err[0] != '\0') ? hz_err
                                             : "JPL Horizons request failed.");
                s_horizons_error_was_fetch = true;
            }
            else if (s_hz_entry_valid)
            {
                g_ui.ephem_error[0] = '\0';
                s_horizons_error_was_fetch = false;
                g_ephem_pending = std::move(parsed);
                g_ephem_pending_valid = true;
                snprintf(g_ui.ephem_name_buf,   sizeof(g_ui.ephem_name_buf),   "%s", s_hz_name);
                snprintf(g_ui.ephem_target_buf, sizeof(g_ui.ephem_target_buf), "%s", s_hz_command);
                snprintf(g_ui.ephem_path_buf,   sizeof(g_ui.ephem_path_buf),   "Horizons API: %s", s_hz_command);
                g_ui.ephem_modal_open = true;
            }
        }
        else if (hz_state == HZ_CANCELLED)
        {
            EphemMission discard = {};
            char cancel_err[64] = "";
            HorizonsFetchTake(&discard, cancel_err, sizeof cancel_err);
            g_ui.ephem_error[0] = '\0';
            s_horizons_error_was_fetch = false;
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
                               "Horizons fetch cancelled.");
        }

        if (g_ui.file_dialog_ephem.isOpen() && g_ui.file_dialog_ephem.draw())
        {
            const std::string picked = g_ui.file_dialog_ephem.fileName();
            snprintf(g_ui.ephem_path_buf, sizeof(g_ui.ephem_path_buf), "%s", picked.c_str());
            g_ui.ephem_error[0] = '\0';
            s_horizons_error_was_fetch = false;

            std::vector<char> text;
            if (!ReadWholeFile(picked.c_str(), &text))
            {
                snprintf(g_ui.ephem_error, sizeof(g_ui.ephem_error),
                         "Cannot read the selected file (missing or empty).");
            }
            else
            {
                EphemMission parsed = {};
                char err[256] = "";
                if (!EphemParseHorizons(text.data(), text.size(), &parsed, err, sizeof(err)))
                {
                    snprintf(g_ui.ephem_error, sizeof(g_ui.ephem_error), "%s",
                             (err[0] != '\0') ? err : "File is not a usable Horizons VECTORS export.");
                }
                else
                {
                    g_ephem_pending = std::move(parsed);
                    g_ephem_pending_valid = true;

                    char stem[48];
                    FileStem(picked.c_str(), stem, sizeof(stem));
                    snprintf(g_ui.ephem_name_buf, sizeof(g_ui.ephem_name_buf), "%s", stem);
                    g_ui.ephem_target_buf[0] = '\0';
                    g_ui.ephem_modal_open = true;
                }
            }
        }

        if (g_ui.ephem_error[0] != '\0')
        {
            HorizonsDrawErrorLine(g_ui.ephem_error);

            /* only fetch failures offer the file-import escape hatch */
            if (s_horizons_error_was_fetch)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN "##import_instead"))
                {
                    const std::vector<std::string> filter = { "Ephemeris", "*.txt *.csv", "All Files", "*" };
                    g_ui.file_dialog_ephem.open("Import JPL Horizons VECTORS", "", filter);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Import a saved JPL Horizons VECTORS export instead");
            }
        }

        if (g_ui.ephem_modal_open && g_ephem_pending_valid &&
            !ImGui::IsPopupOpen("Add Mission"))
            ImGui::OpenPopup("Add Mission");

        bool *modal_open = &g_ui.ephem_modal_open;
        ImGui::SetNextWindowSize(ImVec2(UIPx(420.0f), 0), ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Add Mission", modal_open, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!g_ephem_pending_valid)
            {
                /* defensive: the pending copy vanished, close cleanly */
                g_ui.ephem_modal_open = false;
                ImGui::CloseCurrentPopup();
            }
            else
            {
                ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "%s", g_ui.ephem_path_buf);

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##ephem_name", "Mission name", g_ui.ephem_name_buf,
                                         sizeof(g_ui.ephem_name_buf));
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##ephem_target", "Target (optional)", g_ui.ephem_target_buf,
                                         sizeof(g_ui.ephem_target_buf));

                char t0_str[40], t1_str[40];
                FormatUnixUtc(g_ephem_pending.t0_unix, t0_str, sizeof(t0_str));
                FormatUnixUtc(g_ephem_pending.t1_unix, t1_str, sizeof(t1_str));
                ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "Samples: %d",
                                   (int)g_ephem_pending.s.size());
                ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "Window: %s -> %s",
                                   t0_str, t1_str);

                ImGui::Separator();

                const bool name_ok = (g_ui.ephem_name_buf[0] != '\0');
                const bool refreshing = (EphemFindByTarget(g_ui.ephem_target_buf) != 0);
                if (refreshing)
                    ImGui::TextColored(ThemeColor(g_theme.ui.accent),
                                       ICON_FA_ARROWS_ROTATE
                                       " This target is already loaded; confirming will refresh it in place.");
                if (!name_ok)
                    ImGui::BeginDisabled();
                if (ImGui::Button(refreshing ? "Refresh" : "Add", ImVec2(UIPx(120.0f), 0)))
                {
                    char commit_err[256] = "";
                    const int id = EphemCommitPending(ctx, g_ui.ephem_name_buf,
                                                      g_ui.ephem_target_buf,
                                                      commit_err, sizeof(commit_err));
                    if (id == 0)
                    {
                        snprintf(g_ui.ephem_error, sizeof(g_ui.ephem_error), "%s",
                                 (commit_err[0] != '\0') ? commit_err
                                                         : "Mission could not be added.");
                        g_ui.ephem_modal_open = false;
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        g_ui.ephem_modal_open = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!name_ok)
                    ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(UIPx(120.0f), 0)))
                {
                    g_ui.ephem_modal_open = false;
                    g_ephem_pending_valid = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        const int mission_count = EphemCount();
        if (mission_count == 0)
        {
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "No missions imported.");
        }
        else
        {
            int remove_id = 0;
            for (int i = 0; i < EphemCount(); i++)
            {
                const EphemMission *m = EphemGet(i);
                if (!m)
                    break;

                ImGui::PushID(m->id);

                char t0_str[40], t1_str[40];
                FormatUnixUtc(m->t0_unix, t0_str, sizeof(t0_str));
                FormatUnixUtc(m->t1_unix, t1_str, sizeof(t1_str));

                ImGui::TextUnformatted(m->name);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%d samples\n%s -> %s",
                                      (int)m->s.size(), t0_str, t1_str);

                float x_pos = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f;
                ImGui::SameLine(x_pos);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button(ICON_FA_XMARK "##ephem_remove", ImVec2(20, 20)))
                    remove_id = m->id;
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove this mission");

                ImGui::PopID();

                /* the registry shifts after a removal; stop iterating */
                if (remove_id != 0)
                    break;
            }

            if (remove_id != 0)
            {
                int rem_idx = -1;
                for (int i = 0; i < sat_count; i++)
                {
                    if (satellites[i].kind == SAT_KIND_EPHEM && satellites[i].ephem_id == remove_id)
                    {
                        rem_idx = i;
                        break;
                    }
                }

                if (rem_idx >= 0)
                {
                    // every pointer is &satellites[i]; recover its index before the shift
                    Satellite *sel_ptr = (ctx && ctx->selected_sat) ? *ctx->selected_sat : NULL;
                    int sel_i = sel_ptr ? (int)(sel_ptr - satellites) : -1;
                    if (sel_i < 0 || sel_i >= sat_count)
                        sel_i = -1;

                    int last_i = g_ui.last_selected_sat
                                     ? (int)(g_ui.last_selected_sat - satellites)
                                     : -1;
                    if (last_i < 0 || last_i >= sat_count)
                        last_i = -1;

                    int pass_i = g_ui.locked_pass_sat
                                     ? (int)(g_ui.locked_pass_sat - satellites)
                                     : -1;
                    if (pass_i < 0 || pass_i >= sat_count)
                        pass_i = -1;

                    if (sel_i == rem_idx)
                        *ctx->selected_sat = NULL;
                    else if (sel_i > rem_idx)
                        *ctx->selected_sat = &satellites[sel_i - 1];

                    if (last_i == rem_idx)
                        g_ui.last_selected_sat = NULL;
                    else if (last_i > rem_idx)
                        g_ui.last_selected_sat = &satellites[last_i - 1];

                    if (pass_i == rem_idx)
                        g_ui.locked_pass_sat = NULL;
                    else if (pass_i > rem_idx)
                        g_ui.locked_pass_sat = &satellites[pass_i - 1];

                    // main.cpp's tracker holds a raw pointer we cannot remap; drop the lock
                    if (ctx && ctx->active_lock && *ctx->active_lock == LOCK_SAT)
                        *ctx->active_lock = LOCK_NONE;

                    for (int i = rem_idx; i < sat_count - 1; i++)
                        satellites[i] = satellites[i + 1];
                    sat_count--;
                }

                EphemRemove(remove_id);
                SaveOrbitalData("data.json", satellites, sat_count);
                NotifyPush(NOTIFY_INFO, ICON_FA_TRASH_CAN, "Removed mission");
                LOG_INFO("Removed ephemeris mission id %d", remove_id);
            }
        }
    }

    /* Active Selections List */
    ImGui::Separator();
    ImGui::TextColored(ThemeColor(g_theme.ui.accent), "%s Active Selections",
                       ICON_FA_LIST);
    ImGui::Separator();

    int sel_count = DataSelectionCount();
    if (sel_count == 0)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
                           "No sources selected. Add sources from the sections above.");
    }
    else
    {
        /* show count */
        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%d source(s) selected", sel_count);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "%s", count_str);

        // render the selections directly into the tool panel so they scroll with the panel itself.
        for (int i = 0; i < sel_count; i++)
        {
            DataSourceSelection *s = DataSelectionAt(i);
            if (!s) break;
            ImGui::PushID(i);

            /* source badge with color */
            const char *badge = "";
            ImVec4 badge_col;
            switch (s->type)
            {
                case SOURCE_RETLECTOR:    badge = "R";    badge_col = ImVec4(0.3f, 0.6f, 1.0f, 1.0f); break;
                case SOURCE_CELESTRAK:    badge = "C";    badge_col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f); break;
                case SOURCE_CUSTOM_URL:   badge = "URL";  badge_col = ImVec4(1.0f, 0.7f, 0.2f, 1.0f); break;
                case SOURCE_CUSTOM_FILE:  badge = "FILE"; badge_col = ImVec4(0.5f, 0.2f, 0.8f, 1.0f); break;
                case SOURCE_CUSTOM_PASTE: badge = "P";    badge_col = ImVec4(0.8f, 0.4f, 1.0f, 1.0f); break;
            }

            ImGui::TextColored(badge_col, "[%s]", badge);
            ImGui::SameLine();

            char obj_name[64];
            const char *display = s->name;
            if (s->type == SOURCE_CUSTOM_URL || s->type == SOURCE_CUSTOM_FILE)
            {
                /* prefer the full URL or filename, which is the identifier */
                display = (s->identifier[0] != '\0') ? s->identifier : s->name;
            }
            else if (s->type == SOURCE_CUSTOM_PASTE)
            {
                if (ExtractOMMObjectName(s->paste_data, strlen(s->paste_data), s->format,
                                         obj_name, sizeof(obj_name)))
                    display = obj_name;
            }

            ImGui::TextUnformatted(display);

            /* red X remove button on the right */
            float x_pos = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f;
            ImGui::SameLine(x_pos);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(20, 20)))
            {
                DataSelectionRemove(i);
                ImGui::PopStyleColor();
                ImGui::PopID();
                break; /* list shifted, break to avoid invalid iteration */
            }
            ImGui::PopStyleColor();

            ImGui::PopID();
        }
    }

    /* ====================================================================
     *  Pull All Selected Sources Button
     * ==================================================================== */
    ImGui::Separator();
    ImGui::PopTextWrapPos();

    // async pull state lives in tools_common so completion is handled even when this panel isn't drawn
    bool pull_running = DataSourcesPullInProgress();
    int pull_total = DataSourcesPullTotal();

    // only emit EndDisabled() when BeginDisabled() was called this frame
    bool pull_disabled = false;

    if (pull_running)
    {
        /* show progress while the worker thread fetches/parses in the background */
        ImGui::TextColored(ThemeColor(g_theme.ui.accent), "%s Pulling data...",
                           ICON_FA_SPINNER);
        ImGui::SameLine();
        static float spinner_angle = 0.0f;
        spinner_angle += ImGui::GetIO().DeltaTime * 180.0f;
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "%c",
                           "|/-\\"[(int)(spinner_angle / 45.0f) % 4]);

        int done = pull_total - AsyncFetchPendingCount();
        if (pull_total > 0)
        {
            ImGui::ProgressBar((float)done / (float)pull_total,
                               ImVec2(avail_w, 0.0f), "");
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
                               "%d / %d sources", done, pull_total);
        }

        /* disable the button while a pull is in flight */
        ImGui::BeginDisabled();
        pull_disabled = true;
    }

    if (ImGui::Button("Pull All Selected Sources", ImVec2(avail_w, 30)))
    {
        DataSourcesStartPull(cfg, ctx);
    }

    if (pull_disabled)
        ImGui::EndDisabled();
}
