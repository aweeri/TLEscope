/*
 * panels.cpp - Sidebar panel body renderers
 *
 * These functions render the *body* of each accordion panel in the new
 * sidebar workspace. They were extracted from the old floating-window
 * dialog functions in ui.cpp. The accordion chrome (header, collapse,
 * drag handle) is owned by ui_layout.cpp.
 */

#include "panels.h"
#include "ui_layout.h"
#include "core/astro.h"
#include "io/rotator.h"
#include "core/config.h"
#include "core/theme.h"
#include "imgui_theme.h"
#include "data/provider.h"
#include "data/cache.h"
#include "data/storage.h"
#include "data/omm_parser.h"
#include "util/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <cmath>
#include <thread>
#include <atomic>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"
#include "rlImGui.h"
#include "IconsFontAwesome6.h"

/* -- Helpers --------------------------------------------------------------- */

static ImVec4 ThemeColor(const Color &c)
{
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

/** case-insensitive substring search; returns true if substr is found in str */
static bool str_contains_ic(const char *str, const char *substr)
{
    if (!str || !substr) return false;
    if (*substr == '\0') return true;
    while (*str)
    {
        const char *a = str;
        const char *b = substr;
        while (*a && *b && (tolower((unsigned char)*a) == tolower((unsigned char)*b)))
        { a++; b++; }
        if (*b == '\0') return true;
        str++;
    }
    return false;
}

/* -- Shared state (moved from ui.cpp) -------------------------------------- */

bool celestrak_sel[25] = {false};
bool log_auto_scroll = true;

/* -- Satellite Manager ----------------------------------------------------- */

void DrawPanelSatMgr(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    static char search_buf[64] = "";
    bool search_active = (search_buf[0] != '\0');

    /* search box + icon buttons on the same line */
    float avail_w = ImGui::GetContentRegionAvail().x;
    float btn_w = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(avail_w - btn_w * 2.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f - 4.0f);
    ImGui::InputText("##search", search_buf, sizeof(search_buf));
    ImGui::SameLine();

    /* Enable All (eye icon) */
    if (ImGui::Button(ICON_FA_EYE, ImVec2(btn_w, btn_w)))
    {
        for (int i = 0; i < sat_count; i++)
        {
            if (!search_active || str_contains_ic(satellites[i].name, search_buf))
                satellites[i].is_active = true;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable all visible satellites");
    ImGui::SameLine();

    /* Disable All (eye-slash icon) */
    if (ImGui::Button(ICON_FA_EYE_SLASH, ImVec2(btn_w, btn_w)))
    {
        for (int i = 0; i < sat_count; i++)
        {
            if (!search_active || str_contains_ic(satellites[i].name, search_buf))
                satellites[i].is_active = false;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Disable all visible satellites");

    /* show count of displayed satellites */
    int displayed = 0;
    for (int i = 0; i < sat_count; i++)
    {
        if (!search_active || str_contains_ic(satellites[i].name, search_buf))
            displayed++;
    }
    if (search_active)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "%d / %d satellites", displayed, sat_count);
    }

    ImGui::Separator();
    ImGui::BeginChild("SatList");

    for (int i = 0; i < sat_count; i++)
    {
        /* skip empty/invalid entries (name must be non-empty and have a valid NORAD ID) */
        if (satellites[i].name[0] == '\0' || satellites[i].norad_id[0] == '\0')
            continue;

        /* case-insensitive search matching */
        if (search_active && !str_contains_ic(satellites[i].name, search_buf))
            continue;

        bool active = satellites[i].is_active;
        ImGui::PushID(i);

        /* checkbox for active state */
        ImGui::Checkbox("##active", &satellites[i].is_active);
        ImGui::SameLine();

        if (!active)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_secondary));
        }

        char label[128];
        snprintf(label, sizeof(label), "%s##%d", satellites[i].name, i);

        float row_avail = ImGui::GetContentRegionAvail().x;
        ImVec2 selectable_size = ImVec2(row_avail, 20);
        if (ImGui::Selectable(label, *ctx->selected_sat == &satellites[i],
                              ImGuiSelectableFlags_None, selectable_size))
        {
            *ctx->selected_sat = &satellites[i];
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            LayoutOpenPanel(PANEL_SAT_INFO);
        }

        if (!active)
        {
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

/* -- Data Sources ---------------------------------------------------------- */

void DrawPanelDataSources(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    /* -- Retlector Section (baseline) ------------------------------------- */
    {
        static std::atomic<bool> s_fetch_running{false};
        static std::atomic<bool> s_fetch_done{false};
        static RetlectorGroup s_pending[MAX_RETLECTOR_GROUPS];
        static int s_pending_count = 0;
        static std::thread s_fetch_thread;

        if (ImGui::CollapsingHeader("Retlector (baseline)"))
        {
            if (!cfg->retlector_groups_fetched && !s_fetch_running.load())
            {
                s_fetch_running = true;
                s_fetch_done = false;
                s_fetch_thread = std::thread([]() {
                    s_pending_count = FetchRetlectorGroups(s_pending, MAX_RETLECTOR_GROUPS);
                    s_fetch_done = true;
                });
                s_fetch_thread.detach();
            }

            if (s_fetch_done.load())
            {
                if (s_pending_count > 0)
                {
                    for (int i = 0; i < s_pending_count && i < MAX_RETLECTOR_GROUPS; i++)
                        cfg->retlector_groups[i] = s_pending[i];
                    cfg->retlector_group_count = s_pending_count;
                    cfg->retlector_groups_fetched = true;
                    LOG_INFO("Loaded %d retlector groups", s_pending_count);
                }
                else
                {
                    LOG_ERROR("Failed to fetch retlector groups");
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
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%d sources available (CSV format)",
                                   cfg->retlector_group_count);
                ImGui::Separator();
                for (int i = 0; i < cfg->retlector_group_count; i++)
                {
                    RetlectorGroup *g = &cfg->retlector_groups[i];
                    ImGui::PushID(i);
                    ImGui::Checkbox(g->name, &g->selected);
                    ImGui::PopID();
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Failed to reach retlector.eu");
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Use Celestrak fallback below.");
                if (ImGui::SmallButton("Retry"))
                {
                    cfg->retlector_groups_fetched = false;
                }
            }
        }
    }

    /* -- Celestrak Section (fallback) ------------------------------------- */
    if (ImGui::CollapsingHeader("Celestrak (fallback)"))
    {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "CSV format (default)");
        ImGui::Separator();
        for (int i = 0; i < NUM_CELESTRAK_SOURCES && i < 25; i++)
        {
            ImGui::PushID(i + 1000);
            ImGui::Checkbox(CELESTRAK_SOURCES[i].name, &celestrak_sel[i]);
            ImGui::PopID();
        }
    }

    /* -- Custom URL Section ------------------------------------------------ */
    if (ImGui::CollapsingHeader("Custom URL"))
    {
        static char custom_url_buf[512] = "";
        ImGui::InputText("##custom_url", custom_url_buf, sizeof(custom_url_buf));
        ImGui::SameLine();
        if (ImGui::Button("Fetch") && custom_url_buf[0])
        {
            FetchResult result = FetchFromCustomURL(custom_url_buf);
            if (result.success)
            {
                const char *fmt_name = FormatToString(result.format);
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Detected: %s", fmt_name);
                LOG_INFO("Custom URL fetched: %s, format: %s", custom_url_buf, fmt_name);

                const char *name_start = strrchr(custom_url_buf, '/');
                if (name_start) name_start++; else name_start = custom_url_buf;

                if (cfg->custom_data_source_count < MAX_CUSTOM_DATA_SOURCES)
                {
                    CustomDataSource *ds = &cfg->custom_data_sources[cfg->custom_data_source_count];
                    snprintf(ds->name, sizeof(ds->name), "%.63s", name_start);
                    snprintf(ds->url, sizeof(ds->url), "%s", custom_url_buf);
                    ds->preferred_format = result.format;
                    ds->selected = true;
                    cfg->custom_data_source_count++;
                }

                int before = sat_count;
                if (result.format == FORMAT_TLE)
                {
                    char *ptr = result.data;
                    char l0[256], l1[256], l2[256];
                    while (*ptr && sat_count < MAX_SATELLITES)
                    {
                        while (*ptr == '\r' || *ptr == '\n') ptr++;
                        if (!*ptr) break;
                        if (*ptr == '#') { while (*ptr && *ptr != '\n') ptr++; continue; }
                        int j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l0[j++] = *ptr++;
                        l0[j] = '\0'; if (*ptr == '\n') ptr++;
                        j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l1[j++] = *ptr++;
                        l1[j] = '\0'; if (*ptr == '\n') ptr++;
                        j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l2[j++] = *ptr++;
                        l2[j] = '\0'; if (*ptr == '\n') ptr++;
                        OrbitalDataMeta meta = {0};
                        snprintf(meta.source_name, sizeof(meta.source_name), "custom:%.31s", name_start);
                        meta.format = result.format;
                        meta.fetch_time = time(NULL);
                        add_satellite_from_tle(l0, l1, l2, &meta);
                    }
                }
                else if (result.format == FORMAT_OMM_JSON)
                {
                    ParseOMMJson(result.data, result.size, satellites, &sat_count, MAX_SATELLITES,
                                 "custom_url", result.format);
                }
                else if (result.format == FORMAT_OMM_CSV)
                {
                    ParseOMMCsv(result.data, result.size, satellites, &sat_count, MAX_SATELLITES,
                                "custom_url", result.format);
                }
                if (sat_count > before)
                    SaveOrbitalData("data.json", satellites, sat_count);

                FreeFetchResult(&result);
                custom_url_buf[0] = '\0';
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Fetch failed (HTTP %ld)", result.http_code);
                FreeFetchResult(&result);
            }
        }

        if (custom_url_buf[0])
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Enter URL and press Fetch to auto-detect format");
        }

        if (cfg->custom_data_source_count > 0)
        {
            ImGui::Separator();
            for (int i = 0; i < cfg->custom_data_source_count; i++)
            {
                CustomDataSource *ds = &cfg->custom_data_sources[i];
                ImGui::PushID(i + 3000);

                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]",
                                   FormatToString(ds->preferred_format));
                ImGui::SameLine();
                ImGui::Checkbox(ds->name, &ds->selected);

                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 30);
                if (ImGui::SmallButton("X"))
                {
                    for (int j = i; j < cfg->custom_data_source_count - 1; j++)
                        cfg->custom_data_sources[j] = cfg->custom_data_sources[j + 1];
                    cfg->custom_data_source_count--;
                    ImGui::PopID();
                    break;
                }

                ImGui::PopID();
            }
        }
    }

    /* -- Paste Entry Section ---------------------------------------------- */
    if (ImGui::CollapsingHeader("Paste Entry"))
    {
        static char custom_paste_buf[4096] = "";
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Accepted formats: TLE, JSON OMM, CSV OMM, KVN OMM, XML OMM");
        ImGui::InputTextMultiline("##paste", custom_paste_buf, sizeof(custom_paste_buf),
                                  ImVec2(0, 100));

        OrbitalDataFormat paste_fmt = FORMAT_UNKNOWN;
        bool has_content = (custom_paste_buf[0] != '\0');
        if (has_content)
        {
            paste_fmt = DetectDataFormat(custom_paste_buf, strlen(custom_paste_buf));
        }

        bool can_add = has_content && (paste_fmt != FORMAT_UNKNOWN) &&
                       (cfg->custom_entry_count < MAX_CUSTOM_ENTRIES);

        if (!can_add)
            ImGui::BeginDisabled();

        if (ImGui::Button("Add Entry"))
        {
            const char *fmt_name = FormatToString(paste_fmt);
            LOG_INFO("Pasted entry detected format: %s", fmt_name);

            CustomEntry *e = &cfg->custom_entries[cfg->custom_entry_count];
            strncpy(e->data, custom_paste_buf, sizeof(e->data) - 1);
            e->detected_format = paste_fmt;
            e->selected = true;
            cfg->custom_entry_count++;
            custom_paste_buf[0] = '\0';
        }

        if (!can_add)
            ImGui::EndDisabled();

        if (has_content)
        {
            ImGui::SameLine();
            if (paste_fmt != FORMAT_UNKNOWN)
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Detected: %s", FormatToString(paste_fmt));
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Format unknown - not a valid orbital data format");
            }
        }

        if (cfg->custom_entry_count > 0)
        {
            ImGui::Separator();
            for (int i = 0; i < cfg->custom_entry_count; i++)
            {
                CustomEntry *e = &cfg->custom_entries[i];
                ImGui::PushID(i + 2000);

                char preview[64];
                const char *nl = strchr(e->data, '\n');
                if (nl)
                {
                    int len = (int)(nl - e->data);
                    if (len > 60) len = 60;
                    strncpy(preview, e->data, len);
                    preview[len] = '\0';
                }
                else
                {
                    strncpy(preview, e->data, 60);
                    preview[60] = '\0';
                }

                bool was_selected = e->selected;
                ImGui::Checkbox(preview, &e->selected);

                if (was_selected && !e->selected)
                {
                    for (int j = i; j < cfg->custom_entry_count - 1; j++)
                        cfg->custom_entries[j] = cfg->custom_entries[j + 1];
                    cfg->custom_entry_count--;
                    ImGui::PopID();
                    break;
                }

                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]", FormatToString(e->detected_format));
                ImGui::PopID();
            }
        }
    }

    /* -- Pull Button ------------------------------------------------------- */
    ImGui::Separator();
    if (ImGui::Button("Pull Selected Sources", ImVec2(ImGui::GetContentRegionAvail().x, 30)))
    {
        int new_count = 0;
        for (int i = 0; i < sat_count; i++)
        {
            bool keep = false;

            for (int j = 0; j < cfg->retlector_group_count; j++)
            {
                if (cfg->retlector_groups[j].selected)
                {
                    char expected[80];
                    snprintf(expected, sizeof(expected), "retlector:%s", cfg->retlector_groups[j].name);
                    if (strcmp(satellites[i].data_meta.source_name, expected) == 0)
                    {
                        keep = true;
                        break;
                    }
                }
            }

            if (!keep)
            {
                for (int j = 0; j < NUM_CELESTRAK_SOURCES && j < 25; j++)
                {
                    if (celestrak_sel[j])
                    {
                        char expected[80];
                        snprintf(expected, sizeof(expected), "celestrak:%s", CELESTRAK_SOURCES[j].name);
                        if (strcmp(satellites[i].data_meta.source_name, expected) == 0)
                        {
                            keep = true;
                            break;
                        }
                    }
                }
            }

            if (!keep)
            {
                for (int j = 0; j < cfg->custom_data_source_count; j++)
                {
                    if (cfg->custom_data_sources[j].selected)
                    {
                        char expected[80];
                        snprintf(expected, sizeof(expected), "custom:%.31s", cfg->custom_data_sources[j].name);
                        if (strcmp(satellites[i].data_meta.source_name, expected) == 0)
                        {
                            keep = true;
                            break;
                        }
                    }
                }
            }

            if (!keep)
            {
                for (int j = 0; j < cfg->custom_entry_count; j++)
                {
                    if (cfg->custom_entries[j].selected)
                    {
                        char expected[80];
                        snprintf(expected, sizeof(expected), "paste:%d", j);
                        if (strcmp(satellites[i].data_meta.source_name, expected) == 0)
                        {
                            keep = true;
                            break;
                        }
                    }
                }
            }

            if (keep)
            {
                if (new_count != i)
                    satellites[new_count] = satellites[i];
                new_count++;
            }
        }
        sat_count = new_count;

        for (int i = 0; i < cfg->retlector_group_count; i++)
        {
            if (!cfg->retlector_groups[i].selected) continue;

            char url[512];
            snprintf(url, sizeof(url), "https://retlector.eu/%s/csv", cfg->retlector_groups[i].name);
            FetchResult result = FetchFromCustomURL(url);
            if (result.success)
            {
                int before = sat_count;
                int parsed = ParseOMMCsv(result.data, result.size, satellites, &sat_count,
                                          MAX_SATELLITES, "", FORMAT_OMM_CSV);
                char source_tag[80];
                snprintf(source_tag, sizeof(source_tag), "retlector:%s", cfg->retlector_groups[i].name);
                for (int s = before; s < sat_count; s++)
                    strncpy(satellites[s].data_meta.source_name, source_tag,
                            sizeof(satellites[s].data_meta.source_name) - 1);
                LOG_INFO("Retlector %s: parsed %d satellites", cfg->retlector_groups[i].name, sat_count - before);
                FreeFetchResult(&result);
            }
        }

        {
            for (int i = 0; i < NUM_CELESTRAK_SOURCES && i < 25; i++)
            {
                if (!celestrak_sel[i]) continue;

                FetchResult result = FetchFromSource(&CELESTRAK_SOURCES[i], FORMAT_OMM_CSV);
                if (result.success)
                {
                    int before = sat_count;
                    int parsed = ParseOMMCsv(result.data, result.size, satellites, &sat_count,
                                              MAX_SATELLITES, "", FORMAT_OMM_CSV);
                    char source_tag[80];
                    snprintf(source_tag, sizeof(source_tag), "celestrak:%s", CELESTRAK_SOURCES[i].name);
                    for (int s = before; s < sat_count; s++)
                        strncpy(satellites[s].data_meta.source_name, source_tag,
                                sizeof(satellites[s].data_meta.source_name) - 1);
                    LOG_INFO("Celestrak %s: parsed %d satellites", CELESTRAK_SOURCES[i].name, sat_count - before);
                    FreeFetchResult(&result);
                }
            }
        }

        for (int i = 0; i < cfg->custom_data_source_count; i++)
        {
            if (!cfg->custom_data_sources[i].selected) continue;

            FetchResult result = FetchFromCustomURL(cfg->custom_data_sources[i].url);
            if (result.success)
            {
                int before = sat_count;
                if (result.format == FORMAT_TLE)
                {
                    char *ptr = result.data;
                    char l0[256], l1[256], l2[256];
                    while (*ptr && sat_count < MAX_SATELLITES)
                    {
                        while (*ptr == '\r' || *ptr == '\n') ptr++;
                        if (!*ptr) break;
                        if (*ptr == '#') { while (*ptr && *ptr != '\n') ptr++; continue; }
                        int j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l0[j++] = *ptr++;
                        l0[j] = '\0'; if (*ptr == '\n') ptr++;
                        j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l1[j++] = *ptr++;
                        l1[j] = '\0'; if (*ptr == '\n') ptr++;
                        j = 0;
                        while (*ptr && *ptr != '\n' && j < 255) l2[j++] = *ptr++;
                        l2[j] = '\0'; if (*ptr == '\n') ptr++;
                        OrbitalDataMeta meta = {0};
                        snprintf(meta.source_name, sizeof(meta.source_name), "custom:%.31s",
                                 cfg->custom_data_sources[i].name);
                        meta.format = result.format;
                        meta.fetch_time = time(NULL);
                        add_satellite_from_tle(l0, l1, l2, &meta);
                    }
                }
                else if (result.format == FORMAT_OMM_JSON)
                {
                    ParseOMMJson(result.data, result.size, satellites, &sat_count, MAX_SATELLITES,
                                 cfg->custom_data_sources[i].name, result.format);
                }
                else if (result.format == FORMAT_OMM_CSV)
                {
                    ParseOMMCsv(result.data, result.size, satellites, &sat_count, MAX_SATELLITES,
                                cfg->custom_data_sources[i].name, result.format);
                }
                LOG_INFO("Custom URL %s: parsed %d satellites", cfg->custom_data_sources[i].url,
                         sat_count - before);
                FreeFetchResult(&result);
            }
        }

        for (int i = 0; i < cfg->custom_entry_count; i++)
        {
            if (!cfg->custom_entries[i].selected) continue;

            CustomEntry *e = &cfg->custom_entries[i];
            int before = sat_count;

            if (e->detected_format == FORMAT_TLE)
            {
                char *ptr = e->data;
                char l0[256], l1[256], l2[256];
                while (*ptr && sat_count < MAX_SATELLITES)
                {
                    while (*ptr == '\r' || *ptr == '\n') ptr++;
                    if (!*ptr) break;
                    if (*ptr == '#') { while (*ptr && *ptr != '\n') ptr++; continue; }
                    int j = 0;
                    while (*ptr && *ptr != '\n' && j < 255) l0[j++] = *ptr++;
                    l0[j] = '\0'; if (*ptr == '\n') ptr++;
                    j = 0;
                    while (*ptr && *ptr != '\n' && j < 255) l1[j++] = *ptr++;
                    l1[j] = '\0'; if (*ptr == '\n') ptr++;
                    j = 0;
                    while (*ptr && *ptr != '\n' && j < 255) l2[j++] = *ptr++;
                    l2[j] = '\0'; if (*ptr == '\n') ptr++;
                    OrbitalDataMeta meta = {0};
                    snprintf(meta.source_name, sizeof(meta.source_name), "paste:%d", i);
                    meta.format = e->detected_format;
                    meta.fetch_time = time(NULL);
                    add_satellite_from_tle(l0, l1, l2, &meta);
                }
            }
            else if (e->detected_format == FORMAT_OMM_JSON)
            {
                ParseOMMJson(e->data, strlen(e->data), satellites, &sat_count, MAX_SATELLITES,
                             "paste", e->detected_format);
            }
            else if (e->detected_format == FORMAT_OMM_CSV)
            {
                ParseOMMCsv(e->data, strlen(e->data), satellites, &sat_count, MAX_SATELLITES,
                            "paste", e->detected_format);
            }

            LOG_INFO("Custom entry %d: parsed %d satellites", i, sat_count - before);
        }

        SaveOrbitalData("data.json", satellites, sat_count);
        LOG_INFO("Pull complete: %d satellites total", sat_count);
    }
}

/* -- Time Control ---------------------------------------------------------- */

void DrawPanelTimeCtrl(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    ImGui::Text("Current: %s", ctx->datetime_str);
    ImGui::SetNextItemWidth(avail_w);
    { double v_min = 0.0, v_max = 3600.0; ImGui::SliderScalar("Speed", ImGuiDataType_Double, ctx->time_multiplier, &v_min, &v_max, "%.1fx"); }

    float btn_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Pause/Resume", ImVec2(btn_w, 0)))
    {
        if (*ctx->time_multiplier != 0.0)
        {
            *ctx->saved_multiplier = *ctx->time_multiplier;
            *ctx->time_multiplier = 0.0;
        }
        else
        {
            *ctx->time_multiplier = (*ctx->saved_multiplier != 0.0) ? *ctx->saved_multiplier : 1.0;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset to Now", ImVec2(btn_w, 0)))
    {
        *ctx->current_epoch = get_current_real_time_epoch();
        *ctx->time_multiplier = 1.0;
    }
}

/* -- Scope ----------------------------------------------------------------- */

void DrawPanelScope(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    ImGui::SetNextItemWidth(avail_w);
    ImGui::SliderFloat("Azimuth", ctx->scope_az, 0.0f, 360.0f, "%.1f");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::SliderFloat("Elevation", ctx->scope_el, -90.0f, 90.0f, "%.1f");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::SliderFloat("Beam Width", ctx->scope_beam, 1.0f, 120.0f, "%.1f");

    ImGui::Separator();
    /* Use a wrapping layout for checkboxes so they adapt to sidebar width */
    float cb_w = ImGui::CalcTextSize("Show HEO/MEO").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetFrameHeight();
    float avail_for_cb = avail_w;
    int items_per_row = (int)(avail_for_cb / (cb_w + ImGui::GetStyle().ItemSpacing.x));
    if (items_per_row < 1) items_per_row = 1;

    ImGui::Checkbox("Show LEO", &g_ui.scope_show_leo);
    if (items_per_row >= 2) { ImGui::SameLine(); }
    ImGui::Checkbox("Show HEO/MEO", &g_ui.scope_show_heo);
    if (items_per_row >= 3) { ImGui::SameLine(); }
    ImGui::Checkbox("Show GEO", &g_ui.scope_show_geo);
    if (items_per_row >= 4) { ImGui::SameLine(); }
    ImGui::Checkbox("Show Trails", &g_ui.scope_show_trails);
}

/* -- Rotator Control ------------------------------------------------------- */

void DrawPanelRotator(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    if (ImGui::Button(RotatorIsConnected() ? "Disconnect" : "Connect", ImVec2(avail_w, 0)))
    {
        if (RotatorIsConnected())
            RotatorDisconnect();
        else
            RotatorConnect();
    }

    ImGui::Separator();
    if (RotatorIsConnected())
    {
        ImGui::Text("Status: Connected");
        ImGui::Text("Az: %.1f  El: %.1f", RotatorGetAz(), RotatorGetEl());

        float half_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("Auto Steer", ImVec2(half_w, 0)))
        {
            RotatorSetAutoSteer(!RotatorGetAutoSteer());
        }

        ImGui::SameLine();
        if (ImGui::Button("Poll", ImVec2(half_w, 0)))
        {
            RotatorPollNow();
        }

        ImGui::Separator();
        ImGui::Text("Raw Commands:");
        static char cmd_buf[64] = "";
        ImGui::SetNextItemWidth(avail_w);
        ImGui::InputText("##cmd", cmd_buf, sizeof(cmd_buf));
        if (ImGui::Button("Send", ImVec2(avail_w, 0)))
        {
            RotatorSendCustomNow();
        }
    }
    else
    {
        ImGui::Text("Status: Disconnected");
    }
}

/* -- Satellite Info (inspector) -------------------------------------------- */

void DrawPanelSatInfo(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    if (!*ctx->selected_sat)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "No satellite selected.\nClick a satellite in the 3D view or in the Satellite Manager.");
        return;
    }

    Satellite *sat = *ctx->selected_sat;
    ImGui::Text("NORAD: %s", sat->norad_id);
    ImGui::Text("Active: %s", sat->is_active ? "Yes" : "No");

    ImGui::Separator();
    ImGui::Text("Position:");
    ImGui::Text("  X: %.2f km", sat->current_pos.x);
    ImGui::Text("  Y: %.2f km", sat->current_pos.y);
    ImGui::Text("  Z: %.2f km", sat->current_pos.z);

    ImGui::Separator();
    ImGui::Text("Orbital Elements:");
    ImGui::Text("  Inclination: %.4f deg", sat->inclination);
    ImGui::Text("  RAAN: %.4f deg", sat->raan);
    ImGui::Text("  Eccentricity: %.6f", sat->eccentricity);
    ImGui::Text("  Arg of Perigee: %.4f deg", sat->arg_perigee);
    ImGui::Text("  Mean Anomaly: %.4f deg", sat->mean_anomaly);
    ImGui::Text("  Mean Motion: %.6f rev/day", sat->mean_motion);

    ImGui::Separator();
    if (sat->is_active && ImGui::Button("Deactivate", ImVec2(avail_w, 0)))
        sat->is_active = false;
    else if (!sat->is_active && ImGui::Button("Activate", ImVec2(avail_w, 0)))
        sat->is_active = true;
}

/* -- Satellite Passes ------------------------------------------------------ */

void DrawPanelPasses(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    ImGui::SetNextItemWidth(avail_w);
    static char min_el_buf[8] = "0";
    ImGui::InputText("Min Elevation", min_el_buf, sizeof(min_el_buf));

    if (ImGui::Button("Calculate Passes", ImVec2(avail_w, 0)))
    {
        if (*ctx->selected_sat)
            CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
        else
            CalculatePasses(NULL, *ctx->current_epoch);
    }

    ImGui::Separator();
    ImGui::BeginChild("PassList");

    for (int i = 0; i < num_passes; i++)
    {
        char label[128];
        snprintf(label, sizeof(label), "%s - El: %.1f",
                 passes[i].sat ? passes[i].sat->name : "Unknown",
                 passes[i].max_el);

        if (ImGui::Selectable(label, i == g_ui.selected_pass_idx))
        {
            g_ui.selected_pass_idx = i;
        }
    }

    ImGui::EndChild();
}

/* -- Polar Plot ------------------------------------------------------------ */

void DrawPanelPolarPlot(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    ImGui::Checkbox("Lunar Mode", &g_ui.polar_lunar_mode);

    if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
    {
        SatPass *pass = &passes[g_ui.selected_pass_idx];
        ImGui::Text("Satellite: %s", pass->sat ? pass->sat->name : "N/A");
        ImGui::Text("Max Elevation: %.1f", pass->max_el);
        ImGui::Text("AOS: %.2f", pass->aos_epoch);
        ImGui::Text("LOS: %.2f", pass->los_epoch);
    }
    else
    {
        ImGui::Text("Select a pass from the Passes panel");
    }

    ImGui::Separator();
    float half_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Jump to AOS", ImVec2(half_w, 0)))
    {
        if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
        {
            *ctx->current_epoch = passes[g_ui.selected_pass_idx].aos_epoch;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Doppler Analysis", ImVec2(half_w, 0)))
    {
        LayoutOpenPanel(PANEL_DOPPLER);
    }
}

/* -- Doppler Analysis ------------------------------------------------------ */

void DrawPanelDoppler(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    static float freq = 145800000.0f; /* default: 2m band */
    static float csv_res = 1.0f;
    static char csv_path[128] = "doppler_export.csv";

    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputFloat("Frequency (Hz)", &freq, 1000.0f, 1000000.0f, "%.0f");
    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputFloat("CSV Resolution (s)", &csv_res, 0.1f, 10.0f);
    ImGui::SetNextItemWidth(avail_w);
    ImGui::InputText("Export Path", csv_path, sizeof(csv_path));

    if (ImGui::Button("Export CSV", ImVec2(avail_w, 0)))
    {
        /* TODO: Implement CSV export */
    }
}

/* -- Log ------------------------------------------------------------------- */

static const char *LogLevelFilterLabel(int idx)
{
    switch (idx)
    {
        case 0:  return "ALL";
        case 1:  return "INFO+";
        case 2:  return "WARN+";
        case 3:  return "ERROR";
        default: return "ALL";
    }
}

static LogLevel LogLevelFilterMinLevel(int idx)
{
    switch (idx)
    {
        case 0:  return LOG_LEVEL_DEBUG;
        case 1:  return LOG_LEVEL_INFO;
        case 2:  return LOG_LEVEL_WARN;
        case 3:  return LOG_LEVEL_ERROR;
        default: return LOG_LEVEL_DEBUG;
    }
}

void DrawPanelLog(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    if (ImGui::Button("Clear", ImVec2(avail_w * 0.2f, 0)))
    {
        LogClear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &log_auto_scroll);
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();

    static int log_level_filter = 0;
    ImGui::SetNextItemWidth(avail_w * 0.35f);
    ImGui::Combo("##filter", &log_level_filter, "ALL\0INFO+\0WARN+\0ERROR\0");
    LogLevel min_level = LogLevelFilterMinLevel(log_level_filter);

    ImGui::Separator();

    ImGui::BeginChild("LogEntries", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    int head = LogGetHeadIndex();

    int count;
    const LogEntry *entries = LogLock(&count);

    int head_for_read = (head - count + LOG_RING_CAPACITY) % LOG_RING_CAPACITY;

    for (int i = 0; i < count; i++)
    {
        int idx = (head_for_read + i) % LOG_RING_CAPACITY;
        const LogEntry *e = &entries[idx];

        if (e->level < min_level)
            continue;

        ImVec4 color;
        switch (e->level)
        {
            case LOG_LEVEL_DEBUG: color = ThemeColor(g_theme.ui.text_secondary); break;
            case LOG_LEVEL_INFO:  color = ThemeColor(g_theme.ui.text_main); break;
            case LOG_LEVEL_WARN:  color = ImVec4(1.0f, 0.9f, 0.4f, 1.0f); break;
            case LOG_LEVEL_ERROR: color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break;
            default:              color = ThemeColor(g_theme.ui.text_main);
        }

        char line_buf[576];
        snprintf(line_buf, sizeof(line_buf), "%s %s", e->timestamp, e->message);

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Selectable(line_buf);
        ImGui::PopStyleColor();
    }

    if (log_auto_scroll && count > 0)
    {
        ImGui::SetScrollHereY(1.0f);
    }

    LogUnlock();
    ImGui::EndChild();
}