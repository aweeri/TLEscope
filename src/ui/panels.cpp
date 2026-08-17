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

/* -- Shared state ---------------------------------------------------------- */

bool log_auto_scroll = true;
bool log_show_timestamps = false;  /* timestamps hidden by default (cleaner) */

/* -- Data Source Selection List (shopping-cart model) ---------------------- */

static DataSourceSelection g_data_selections[MAX_DATA_SOURCE_SELECTIONS];
static int g_data_selection_count = 0;

/** helper: check if a selection already exists (type + identifier match) */
static bool selection_exists(SourceType type, const char *identifier)
{
    for (int i = 0; i < g_data_selection_count; i++)
    {
        if (g_data_selections[i].type == type &&
            strcmp(g_data_selections[i].identifier, identifier) == 0)
            return true;
    }
    return false;
}

/** helper: add a selection, returns true if added */
static bool selection_add(SourceType type, const char *name, const char *identifier,
                          const char *paste_data, OrbitalDataFormat format)
{
    if (g_data_selection_count >= MAX_DATA_SOURCE_SELECTIONS) return false;
    if (selection_exists(type, identifier)) return false;

    DataSourceSelection *s = &g_data_selections[g_data_selection_count++];
    s->type = type;
    strncpy(s->name, name, sizeof(s->name) - 1);
    strncpy(s->identifier, identifier, sizeof(s->identifier) - 1);
    if (paste_data)
        strncpy(s->paste_data, paste_data, sizeof(s->paste_data) - 1);
    else
        s->paste_data[0] = '\0';
    s->format = format;
    return true;
}

/** helper: remove a selection by index */
static void selection_remove(int idx)
{
    if (idx < 0 || idx >= g_data_selection_count) return;
    for (int i = idx; i < g_data_selection_count - 1; i++)
        g_data_selections[i] = g_data_selections[i + 1];
    g_data_selection_count--;
}

/* -- Satellite Manager ----------------------------------------------------- */

void DrawPanelSatMgr(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    static char search_buf[64] = "";
    bool search_active = (search_buf[0] != '\0');

    /* empty state - point the user at the data puller */
    if (sat_count == 0)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "No satellites loaded yet.");
        ImGui::TextWrapped("Add data sources in the Data Sources tab, then pull to populate this list.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::SmallButton(ICON_FA_DATABASE " Open Data Sources"))
            LayoutOpenPanel(PANEL_DATA_SOURCES);
        return;
    }

    ImGui::PushTextWrapPos(0.0f);

    /* search box + icon buttons on the same line */
    float avail_w = ImGui::GetContentRegionAvail().x;
    float btn_w = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(avail_w - btn_w * 2.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f - 4.0f);
    ImGui::InputText("##sat_mgr_search", search_buf, sizeof(search_buf));
    ImGui::SameLine();

    /* Enable All (eye icon) */
    if (ImGui::Button(ICON_FA_EYE "##enable_all", ImVec2(btn_w, btn_w)))
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
    if (ImGui::Button(ICON_FA_EYE_SLASH "##disable_all", ImVec2(btn_w, btn_w)))
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

    /* cap the list at ~1/4 of the sidebar height so a huge catalogue
     * doesn't take over the whole sidebar */
    float sidebar_h = ImGui::GetIO().DisplaySize.y - ImGui::GetFrameHeight();
    float max_list_h = sidebar_h * 0.25f;
    float row_h = 20.0f + ImGui::GetStyle().ItemSpacing.y;
    float content_h = displayed * row_h + ImGui::GetStyle().ItemSpacing.y;
    float avail_h = ImGui::GetContentRegionAvail().y;
    float list_h = fminf(fminf(content_h, max_list_h), avail_h);
    ImGui::BeginChild("##SatList", ImVec2(0.0f, list_h));

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

    ImGui::PopTextWrapPos();
}

/* -- Data Sources ---------------------------------------------------------- */

void DrawPanelDataSources(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
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
        static std::thread s_fetch_thread;
        static int s_retlector_combo_idx = 0;

        if (ImGui::CollapsingHeader("Retlector"))
        {
            /* async fetch of retlector groups */
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
                /* dropdown + add button */
                if (s_retlector_combo_idx >= cfg->retlector_group_count)
                    s_retlector_combo_idx = 0;

                const char *combo_preview = cfg->retlector_groups[s_retlector_combo_idx].name;
                ImGui::SetNextItemWidth(avail_w * 0.65f);
                if (ImGui::BeginCombo("##retlector_group", combo_preview))
                {
                    for (int i = 0; i < cfg->retlector_group_count; i++)
                    {
                        bool is_selected = (i == s_retlector_combo_idx);
                        if (ImGui::Selectable(cfg->retlector_groups[i].name, is_selected))
                            s_retlector_combo_idx = i;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();

                /* Add to Selection button (plus icon) */
                if (ImGui::Button(ICON_FA_PLUS "##add_retlector"))
                {
                    const char *name = cfg->retlector_groups[s_retlector_combo_idx].name;
                    if (!selection_add(SOURCE_RETLECTOR, name, name, NULL, FORMAT_OMM_CSV))
                    {
                        LOG_DEBUG("Retlector group '%s' already in selection list", name);
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Add this group to the active selections list");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Failed to reach retlector.eu");
                if (ImGui::SmallButton("Retry"))
                    cfg->retlector_groups_fetched = false;
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

        const char *combo_preview = CELESTRAK_SOURCES[s_celestrak_combo_idx].name;
        ImGui::SetNextItemWidth(avail_w * 0.65f);
        if (ImGui::BeginCombo("##celestrak_group", combo_preview))
        {
            for (int i = 0; i < NUM_CELESTRAK_SOURCES; i++)
            {
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
            if (!selection_add(SOURCE_CELESTRAK, name, name, NULL, FORMAT_OMM_CSV))
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

            if (!selection_add(SOURCE_CUSTOM_URL, name_start, s_url_buf, NULL, FORMAT_UNKNOWN))
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
     *  Custom Paste Section
     * ==================================================================== */
    if (ImGui::CollapsingHeader("Custom Paste"))
    {
        static char s_paste_buf[4096] = "";

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "Accepted formats: TLE, JSON OMM, CSV OMM, KVN OMM, XML OMM");

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

        bool can_add = has_content && (paste_fmt != FORMAT_UNKNOWN);

        if (!can_add)
            ImGui::BeginDisabled();

        if (ImGui::Button(ICON_FA_PLUS "##add_custom_paste", ImVec2(paste_btn_w, paste_btn_w)))
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

            if (!selection_add(SOURCE_CUSTOM_PASTE, preview, preview, s_paste_buf, paste_fmt))
            {
                LOG_DEBUG("Paste entry already in selection list");
            }
            else
            {
                s_paste_buf[0] = '\0'; /* clear on success */
            }
        }

        if (!can_add)
            ImGui::EndDisabled();

        if (has_content)
        {
            if (paste_fmt != FORMAT_UNKNOWN)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Detected: %s",
                                   FormatToString(paste_fmt));
            else
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                   "Format unknown - not a valid orbital data format");
        }
    }

    /* ====================================================================
     *  Active Selections List
     * ==================================================================== */
    ImGui::Separator();
    ImGui::TextColored(ThemeColor(g_theme.ui.ui_accent), "%s Active Selections",
                       ICON_FA_LIST);
    ImGui::Separator();

    if (g_data_selection_count == 0)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "No sources selected. Add sources from the sections above.");
    }
    else
    {
        /* show count */
        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%d source(s) selected", g_data_selection_count);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "%s", count_str);

        /* scrollable list of selections */
        ImGui::BeginChild("##active_selections", ImVec2(0, fminf(g_data_selection_count * 28.0f, 200.0f)),
                          true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        for (int i = 0; i < g_data_selection_count; i++)
        {
            DataSourceSelection *s = &g_data_selections[i];
            ImGui::PushID(i);

            /* source badge with color */
            const char *badge = "";
            ImVec4 badge_col;
            switch (s->type)
            {
                case SOURCE_RETLECTOR:  badge = "R";  badge_col = ImVec4(0.3f, 0.6f, 1.0f, 1.0f); break;
                case SOURCE_CELESTRAK:  badge = "C";  badge_col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f); break;
                case SOURCE_CUSTOM_URL: badge = "URL"; badge_col = ImVec4(1.0f, 0.7f, 0.2f, 1.0f); break;
                case SOURCE_CUSTOM_PASTE: badge = "P"; badge_col = ImVec4(0.8f, 0.4f, 1.0f, 1.0f); break;
            }

            ImGui::TextColored(badge_col, "[%s]", badge);
            ImGui::SameLine();

            /* name */
            ImGui::TextUnformatted(s->name);

            /* red X remove button on the right */
            float x_pos = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 24.0f;
            ImGui::SameLine(x_pos);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(20, 20)))
            {
                selection_remove(i);
                ImGui::PopStyleColor();
                ImGui::PopID();
                break; /* list shifted, break to avoid invalid iteration */
            }
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    /* ====================================================================
     *  Pull All Selected Sources Button
     * ==================================================================== */
    ImGui::Separator();
    ImGui::PopTextWrapPos();

    if (ImGui::Button("Pull All Selected Sources", ImVec2(avail_w, 30)))
    {
        /* ---- Phase 1: clear the slate (purge all satellites) ---- */
        sat_count = 0;

        /* ---- Phase 2: fetch each selected source (if any) ---- */
        for (int i = 0; i < g_data_selection_count; i++)
        {
            DataSourceSelection *s = &g_data_selections[i];

            switch (s->type)
            {
                case SOURCE_RETLECTOR:
                {
                    char url[512];
                    snprintf(url, sizeof(url), "https://retlector.eu/%s/csv", s->identifier);
                    FetchResult result = FetchFromCustomURL(url);
                    if (result.success)
                    {
                        int before = sat_count;
                        ParseOMMCsv(result.data, result.size, satellites, &sat_count,
                                    MAX_SATELLITES, "", FORMAT_OMM_CSV);
                        char source_tag[80];
                        snprintf(source_tag, sizeof(source_tag), "retlector:%s", s->identifier);
                        for (int si = before; si < sat_count; si++)
                            strncpy(satellites[si].data_meta.source_name, source_tag,
                                    sizeof(satellites[si].data_meta.source_name) - 1);
                        LOG_INFO("Retlector %s: parsed %d satellites", s->identifier, sat_count - before);
                        FreeFetchResult(&result);
                    }
                    break;
                }

                case SOURCE_CELESTRAK:
                {
                    /* find the matching source index */
                    for (int ci = 0; ci < NUM_CELESTRAK_SOURCES; ci++)
                    {
                        if (strcmp(CELESTRAK_SOURCES[ci].name, s->identifier) == 0)
                        {
                            FetchResult result = FetchFromSource(&CELESTRAK_SOURCES[ci], FORMAT_OMM_CSV);
                            if (result.success)
                            {
                                int before = sat_count;
                                ParseOMMCsv(result.data, result.size, satellites, &sat_count,
                                            MAX_SATELLITES, "", FORMAT_OMM_CSV);
                                char source_tag[80];
                                snprintf(source_tag, sizeof(source_tag), "celestrak:%s", s->identifier);
                                for (int si = before; si < sat_count; si++)
                                    strncpy(satellites[si].data_meta.source_name, source_tag,
                                            sizeof(satellites[si].data_meta.source_name) - 1);
                                LOG_INFO("Celestrak %s: parsed %d satellites", s->identifier, sat_count - before);
                                FreeFetchResult(&result);
                            }
                            break;
                        }
                    }
                    break;
                }

                case SOURCE_CUSTOM_URL:
                {
                    FetchResult result = FetchFromCustomURL(s->identifier);
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
                                snprintf(meta.source_name, sizeof(meta.source_name), "custom:%.31s", s->name);
                                meta.format = result.format;
                                meta.fetch_time = time(NULL);
                                add_satellite_from_tle(l0, l1, l2, &meta);
                            }
                        }
                        else if (result.format == FORMAT_OMM_JSON)
                        {
                            ParseOMMJson(result.data, result.size, satellites, &sat_count,
                                         MAX_SATELLITES, s->name, result.format);
                        }
                        else if (result.format == FORMAT_OMM_CSV)
                        {
                            ParseOMMCsv(result.data, result.size, satellites, &sat_count,
                                        MAX_SATELLITES, s->name, result.format);
                        }
                        LOG_INFO("Custom URL %s: parsed %d satellites", s->identifier, sat_count - before);
                        FreeFetchResult(&result);
                    }
                    break;
                }

                case SOURCE_CUSTOM_PASTE:
                {
                    int before = sat_count;
                    if (s->format == FORMAT_TLE)
                    {
                        char *ptr = s->paste_data;
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
                            meta.format = s->format;
                            meta.fetch_time = time(NULL);
                            add_satellite_from_tle(l0, l1, l2, &meta);
                        }
                    }
                    else if (s->format == FORMAT_OMM_JSON)
                    {
                        ParseOMMJson(s->paste_data, strlen(s->paste_data), satellites,
                                     &sat_count, MAX_SATELLITES, "paste", s->format);
                    }
                    else if (s->format == FORMAT_OMM_CSV)
                    {
                        ParseOMMCsv(s->paste_data, strlen(s->paste_data), satellites,
                                    &sat_count, MAX_SATELLITES, "paste", s->format);
                    }
                    LOG_INFO("Custom paste %d: parsed %d satellites", i, sat_count - before);
                    break;
                }
            }
        }

        SaveOrbitalData("data.json", satellites, sat_count);
        LOG_INFO("Pull complete: %d satellites total", sat_count);
    }
}

/* -- Layers ----------------------------------------------------------------- */

void DrawPanelLayers(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;

    ImGui::PushTextWrapPos(0.0f);

    /* fixed icon width so all checkboxes align vertically */
    const float icon_w = 24.0f;

    auto DrawLayerCheckbox = [&](const char *label, bool *value, const char *icon, const char *tooltip) {
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(*value ? g_theme.ui.ui_accent : g_theme.ui.text_secondary));
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        /* pad to fixed width so next column aligns */
        float used = ImGui::GetItemRectSize().x;
        if (used < icon_w)
            ImGui::SameLine(0.0f, icon_w - used);
        else
            ImGui::SameLine();
        ImGui::Checkbox(label, value);
        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
    };

    DrawLayerCheckbox("Clouds", &cfg->show_clouds, ICON_FA_CLOUD, "Show cloud layer (C)");
    DrawLayerCheckbox("Night Lights", &cfg->show_night_lights, ICON_FA_MOON, "Show night-side city lights (N)");
    DrawLayerCheckbox("Markers", &cfg->show_markers, ICON_FA_MAP_PIN, "Show ground markers (L)");
    DrawLayerCheckbox("Scattering", &cfg->show_scattering, ICON_FA_SUN, "Atmospheric scattering effect");
    DrawLayerCheckbox("Skybox", &cfg->show_skybox, ICON_FA_STAR, "Show starfield skybox");
    DrawLayerCheckbox("Highlight Sunlit", &cfg->highlight_sunlit, ICON_FA_BOLT, "Highlight sunlit portions of orbits");
    DrawLayerCheckbox("Slant Range", &cfg->show_slant_range, ICON_FA_RULER, "Show slant range line to home");

    ImGui::PopTextWrapPos();
}

/* -- Time Control ---------------------------------------------------------- */

void DrawPanelTimeCtrl(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

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

    ImGui::PopTextWrapPos();
}

/* -- Scope ----------------------------------------------------------------- */

void DrawPanelScope(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

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

    ImGui::PopTextWrapPos();
}

/* -- Rotator Control ------------------------------------------------------- */

void DrawPanelRotator(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

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

    ImGui::PopTextWrapPos();
}

/* -- Satellite Info (inspector) -------------------------------------------- */

/** compute apogee altitude (km) from semi-major axis and eccentricity */
static double calc_apogee_km(const Satellite *sat)
{
    return sat->semi_major_axis * (1.0 + sat->eccentricity) - EARTH_RADIUS_KM;
}

/** compute perigee altitude (km) from semi-major axis and eccentricity */
static double calc_perigee_km(const Satellite *sat)
{
    return sat->semi_major_axis * (1.0 - sat->eccentricity) - EARTH_RADIUS_KM;
}

/** observer position in ECI, same axis convention as calculate_position() */
static Vector3 calc_observer_eci(const Marker *obs, double gmst_deg)
{
    double ox, oy, oz;
    geodetic_to_ecef(obs->lat, obs->lon + gmst_deg, obs->alt, &ox, &oy, &oz);
    Vector3 o = { (float)ox, (float)oz, (float)-oy };
    return o;
}

/** topocentric declination / right ascension of the satellite as seen from the observer */
static void calc_topocentric_radec(Vector3 eci_pos, Vector3 obs_eci,
                                   double *out_dec_deg, double *out_ra_deg)
{
    double rx = eci_pos.x - obs_eci.x;
    double ry = eci_pos.y - obs_eci.y;
    double rz = eci_pos.z - obs_eci.z;
    double r = sqrt(rx * rx + ry * ry + rz * rz);
    if (r < 0.001) { *out_dec_deg = 0; *out_ra_deg = 0; return; }

    *out_dec_deg = asin(ry / r) * RAD2DEG;

    double ra = atan2(-rz, rx) * RAD2DEG;
    while (ra < 0.0) ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;
    *out_ra_deg = ra;
}

/** apparent angular speed (deg/s) of the satellite across the sky from the observer */
static double calc_topocentric_ang_speed(Satellite *sat, double current_unix, Vector3 obs_eci)
{
    Vector3 p0 = calculate_position(sat, current_unix);
    Vector3 p1 = calculate_position(sat, current_unix + 1.0);

    double x0 = p0.x - obs_eci.x, y0 = p0.y - obs_eci.y, z0 = p0.z - obs_eci.z;
    double x1 = p1.x - obs_eci.x, y1 = p1.y - obs_eci.y, z1 = p1.z - obs_eci.z;

    double r0 = sqrt(x0 * x0 + y0 * y0 + z0 * z0);
    double r1 = sqrt(x1 * x1 + y1 * y1 + z1 * z1);
    if (r0 < 0.001 || r1 < 0.001) return 0.0;

    double cos_a = (x0 * x1 + y0 * y1 + z0 * z1) / (r0 * r1);
    if (cos_a > 1.0) cos_a = 1.0;
    if (cos_a < -1.0) cos_a = -1.0;

    return acos(cos_a) * RAD2DEG;   /* degrees per second */
}

void DrawPanelSatInfo(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    if (!*ctx->selected_sat)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "No satellite selected.\nClick a satellite in the 3D view or in the Satellite Manager.");
        ImGui::PopTextWrapPos();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);

    Satellite *sat = *ctx->selected_sat;
    ImGui::Text("NORAD: %s", sat->norad_id);
    ImGui::Text("Active: %s", sat->is_active ? "Yes" : "No");

    /* -- Observer geometry (home location) --------------------------------- */
    Vector3 obs_eci = calc_observer_eci(&home_location, ctx->gmst_deg);
    double current_unix = get_unix_from_epoch(*ctx->current_epoch);

    double dec = 0.0, ra = 0.0;
    calc_topocentric_radec(sat->current_pos, obs_eci, &dec, &ra);
    double ang_speed = calc_topocentric_ang_speed(sat, current_unix, obs_eci);

    /* -- Main orbital elements -------------------------------------------- */
    ImGui::Separator();
    ImGui::Text("Orbital Elements:");

    // inclination & eccentricity (most critical)
    ImGui::Text("  Inclination: %.4f deg", sat->inclination * RAD2DEG);
    ImGui::Text("  Eccentricity: %.6f", sat->eccentricity);

    // apogee & perigee altitude
    ImGui::Text("  Apogee: %.1f km", calc_apogee_km(sat));
    ImGui::Text("  Perigee: %.1f km", calc_perigee_km(sat));

    // topocentric sky position (computed from home location)
    ImGui::Text("  Declination: %.4f deg", dec);
    ImGui::Text("  Right Ascension: %.4f deg", ra);
    ImGui::Text("  Angular Speed: %.4f deg/s", ang_speed);

    /* -- Home-location relative info -------------------------------------- */
    ImGui::Separator();
    ImGui::Text("From Home Location:");

    double az = 0.0, el = 0.0;
    get_az_el(sat->current_pos, ctx->gmst_deg,
              home_location.lat, home_location.lon, home_location.alt,
              &az, &el);
    ImGui::Text("  Azimuth: %.2f deg", az);
    ImGui::Text("  Elevation: %.2f deg", el);

    double range = get_sat_range(sat, *ctx->current_epoch, home_location);
    ImGui::Text("  Range: %.1f km", range);

    /* -- Advanced (hidden by default) ------------------------------------- */
    ImGui::Separator();
    ImGui::PushID("sat_adv");
    if (ImGui::TreeNodeEx(ICON_FA_GEAR " Advanced Orbital Data", ImGuiTreeNodeFlags_Framed))
    {
        ImGui::Text("  RAAN: %.4f deg", sat->raan * RAD2DEG);
        ImGui::Text("  Arg of Perigee: %.4f deg", sat->arg_perigee * RAD2DEG);
        ImGui::Text("  Mean Anomaly: %.4f deg", sat->mean_anomaly * RAD2DEG);
        ImGui::Text("  Mean Motion: %.6f rev/day", sat->mean_motion * 86400.0 / (2.0 * PI));
        ImGui::Text("  Semi-major Axis: %.3f km", sat->semi_major_axis);
        ImGui::Text("  B* Drag: %.4e", sat->bstar);
        ImGui::Text("  Period: %.2f min", (2.0 * PI / sat->mean_motion) / 60.0);

        ImGui::Separator();
        ImGui::Text("ECI Position:");
        ImGui::Text("  X: %.2f km", sat->current_pos.x);
        ImGui::Text("  Y: %.2f km", sat->current_pos.y);
        ImGui::Text("  Z: %.2f km", sat->current_pos.z);

        ImGui::Separator();
        ImGui::Text("Epoch: %.4f", sat->epoch_days);
        char epoch_str[64];
        epoch_to_datetime_str(sat->epoch_days, epoch_str);
        ImGui::Text("  %s", epoch_str);

        if (sat->data_meta.format != FORMAT_UNKNOWN)
        {
            ImGui::Separator();
            ImGui::Text("Data Source: %s", sat->data_meta.source_name);
            const char *fmt_str = "Unknown";
            switch (sat->data_meta.format)
            {
                case FORMAT_TLE:      fmt_str = "TLE";       break;
                case FORMAT_OMM_JSON: fmt_str = "OMM JSON";  break;
                case FORMAT_OMM_CSV:  fmt_str = "OMM CSV";   break;
                case FORMAT_OMM_XML:  fmt_str = "OMM XML";   break;
                case FORMAT_OMM_KVN:  fmt_str = "OMM KVN";   break;
                default: break;
            }
            ImGui::Text("  Format: %s", fmt_str);
        }

        ImGui::TreePop();
    }
    ImGui::PopID();

    /* -- Activate / Deactivate -------------------------------------------- */
    ImGui::Separator();
    if (sat->is_active && ImGui::Button("Deactivate", ImVec2(avail_w, 0)))
        sat->is_active = false;
    else if (!sat->is_active && ImGui::Button("Activate", ImVec2(avail_w, 0)))
        sat->is_active = true;

    ImGui::PopTextWrapPos();
}

/* -- Satellite Passes ------------------------------------------------------ */

void DrawPanelPasses(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

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

    ImGui::PopTextWrapPos();
}

/* -- Polar Plot ------------------------------------------------------------ */

static void DrawPolarPlotGrid(ImDrawList *dl, ImVec2 center, float radius)
{
    /* concentric rings for 0°, 30°, 60°, 90° elevation */
    int rings[4] = { 90, 60, 30, 0 };
    ImU32 ring_col = IM_COL32(120, 120, 140, 80);
    ImU32 ring_col_bold = IM_COL32(120, 120, 140, 160);

    for (int r = 0; r < 4; r++)
    {
        float r_ratio = (90.0f - rings[r]) / 90.0f;
        float r_px = radius * r_ratio;
        ImU32 col = (r == 0) ? ring_col_bold : ring_col;
        dl->AddCircle(center, r_px, col, 64, 1.0f);
    }

    /* crosshairs (N-S, E-W) */
    dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), ring_col, 1.0f);
    dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), ring_col, 1.0f);

    /* cardinal labels */
    const char *labels[] = { "N", "E", "S", "W" };
    ImVec2 dirs[] = {
        ImVec2(0, -1),  /* N = up */
        ImVec2(1, 0),   /* E = right */
        ImVec2(0, 1),   /* S = down */
        ImVec2(-1, 0)   /* W = left */
    };
    ImU32 label_col = IM_COL32(180, 180, 200, 200);
    for (int i = 0; i < 4; i++)
    {
        ImVec2 pos = ImVec2(center.x + dirs[i].x * (radius + 12.0f),
                            center.y + dirs[i].y * (radius + 12.0f));
        dl->AddText(pos, label_col, labels[i]);
    }

    /* elevation labels on the 0° ring */
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius + 4.0f), IM_COL32(120, 120, 140, 120), "0°");
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius * 0.34f + 2.0f), IM_COL32(120, 120, 140, 100), "30°");
    dl->AddText(ImVec2(center.x + 4.0f, center.y + radius * 0.67f + 2.0f), IM_COL32(120, 120, 140, 80), "60°");
}

void DrawPanelPolarPlot(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    ImGui::Checkbox("Lunar Mode", &g_ui.polar_lunar_mode);

    /* ---- Polar plot canvas ---- */
    float plot_size = fminf(avail_w, 360.0f);
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImVec2(plot_size, plot_size);
    ImGui::InvisibleButton("##polar_canvas", canvas_sz);
    ImVec2 center = ImVec2(canvas_pos.x + plot_size * 0.5f, canvas_pos.y + plot_size * 0.5f);
    float radius = plot_size * 0.5f - 20.0f;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    /* background circle */
    dl->AddCircleFilled(center, radius + 4.0f, IM_COL32(10, 10, 16, 200), 64);
    DrawPolarPlotGrid(dl, center, radius);

    /* ---- Plot satellite position ---- */
    Satellite *sat = *ctx->selected_sat;
    if (sat && sat->is_active)
    {
        double az = 0.0, el = 0.0;
        get_az_el(sat->current_pos, ctx->gmst_deg,
                  home_location.lat, home_location.lon, home_location.alt,
                  &az, &el);

        /* convert azimuth (degrees from North, clockwise) to canvas angle
         * canvas: 0° = up (North), clockwise = positive screen angle */
        float angle_rad = (float)(az * DEG2RAD);
        float el_ratio = (90.0f - (float)el) / 90.0f;
        float dot_r = radius * el_ratio;

        ImVec2 dot_pos = ImVec2(
            center.x + dot_r * sinf(angle_rad),
            center.y - dot_r * cosf(angle_rad)
        );

        /* draw current position dot */
        dl->AddCircleFilled(dot_pos, 6.0f, IM_COL32(100, 200, 255, 255), 16);
        dl->AddCircle(dot_pos, 6.0f, IM_COL32(200, 230, 255, 200), 16, 2.0f);

        /* draw path trace: compute future positions over ~90 min */
        ImVec2 prev_pt = dot_pos;
        int trace_steps = 60;
        double time_step_s = 90.0; /* 90 seconds per step = 90 min total */
        for (int j = 1; j <= trace_steps; j++)
        {
            double future_unix = get_unix_from_epoch(*ctx->current_epoch) + j * time_step_s;
            Vector3 future_pos = calculate_position(sat, future_unix);
            double faz = 0.0, fel = 0.0;
            get_az_el(future_pos, ctx->gmst_deg,
                      home_location.lat, home_location.lon, home_location.alt,
                      &faz, &fel);

            if (fel < 0.0) continue; /* skip below horizon */

            float fa_rad = (float)(faz * DEG2RAD);
            float fe_ratio = (90.0f - (float)fel) / 90.0f;
            float f_r = radius * fe_ratio;
            ImVec2 f_pos = ImVec2(
                center.x + f_r * sinf(fa_rad),
                center.y - f_r * cosf(fa_rad)
            );

            dl->AddLine(prev_pt, f_pos, IM_COL32(100, 200, 255, 100), 1.5f);
            prev_pt = f_pos;
        }

        /* satellite name label */
        char label[128];
        snprintf(label, sizeof(label), "%s  AZ: %.1f°  EL: %.1f°", sat->name, az, el);
        ImVec2 label_sz = ImGui::CalcTextSize(label);
        ImVec2 label_pos = ImVec2(canvas_pos.x + 6.0f, canvas_pos.y + 4.0f);
        dl->AddRectFilled(label_pos, ImVec2(label_pos.x + label_sz.x + 8.0f, label_pos.y + label_sz.y + 6.0f),
                          IM_COL32(10, 10, 16, 180), 4.0f);
        dl->AddText(ImVec2(label_pos.x + 4.0f, label_pos.y + 3.0f), IM_COL32(100, 200, 255, 255), label);
    }
    else
    {
        const char *msg = "No satellite selected";
        ImVec2 msg_sz = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(center.x - msg_sz.x * 0.5f, center.y - msg_sz.y * 0.5f),
                    IM_COL32(120, 120, 140, 160), msg);
    }

    ImGui::Dummy(ImVec2(0, 6.0f));

    /* ---- Pass info ---- */
    if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
    {
        SatPass *pass = &passes[g_ui.selected_pass_idx];
        ImGui::Text("Max Elevation: %.1f", pass->max_el);
        ImGui::Text("AOS: %.2f  LOS: %.2f", pass->aos_epoch, pass->los_epoch);
    }

    ImGui::Separator();
    float half_w = (avail_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Jump to AOS", ImVec2(half_w, 0)))
    {
        if (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
        {
            *ctx->current_epoch = passes[g_ui.selected_pass_idx].aos_epoch;
        }
    
        ImGui::PopTextWrapPos();
    }
    ImGui::SameLine();
    if (ImGui::Button("Doppler Analysis", ImVec2(half_w, 0)))
    {
        LayoutOpenPanel(PANEL_DOPPLER);
    }

    ImGui::PopTextWrapPos();
}

/* -- Doppler Analysis ------------------------------------------------------ */

void DrawPanelDoppler(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

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

    ImGui::PopTextWrapPos();
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
    ImGui::PushTextWrapPos(0.0f);

    if (ImGui::Button("Clear", ImVec2(avail_w * 0.2f, 0)))
    {
        LogClear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &log_auto_scroll);
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();

    /* timestamp toggle (off by default) */
    ImGui::Checkbox("TS", &log_show_timestamps);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show/hide timestamps");
    ImGui::SameLine();

    static int log_level_filter = 0;
    ImGui::SetNextItemWidth(avail_w * 0.25f);
    ImGui::Combo("##filter", &log_level_filter, "ALL\0INFO+\0WARN+\0ERROR\0");
    LogLevel min_level = LogLevelFilterMinLevel(log_level_filter);

    ImGui::Separator();

    /* text-wrapped region (no horizontal scrollbar) so long lines wrap */
    ImGui::BeginChild("##LogEntries", ImVec2(0, 0), false);

    int head = LogGetHeadIndex();

    int count;
    const LogEntry *entries = LogLock(&count);

    int head_for_read = (head - count + LOG_RING_CAPACITY) % LOG_RING_CAPACITY;

    int display_idx = 0;
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

        /* optionally include the timestamp prefix */
        char line_buf[576];
        if (log_show_timestamps)
            snprintf(line_buf, sizeof(line_buf), "%s %s", e->timestamp, e->message);
        else
            snprintf(line_buf, sizeof(line_buf), "%s", e->message);

        /* alternating row background to visually separate log entries */
        if (display_idx % 2 == 0)
        {
            ImVec2 row_min = ImGui::GetCursorScreenPos();
            ImGui::TextUnformatted(""); /* advance cursor by one line height */
            ImVec2 row_max = ImGui::GetCursorScreenPos();
            row_max.x = row_min.x + ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(row_min, row_max,
                IM_COL32(255, 255, 255, 12));
            /* restore cursor to start of line */
            ImGui::SetCursorScreenPos(row_min);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(line_buf);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        display_idx++;
    }

    if (log_auto_scroll && count > 0)
    {
        ImGui::SetScrollHereY(1.0f);
    }

    LogUnlock();
    ImGui::EndChild();

    ImGui::PopTextWrapPos();
}