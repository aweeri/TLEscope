/*
 * tool_data_sources.cpp - Data Sources panel
 */

#include "tools.h"
#include "tools_common.h"
#include "tools_registry.h"
#include "ui/ui_layout.h"
#include "core/astro.h"
#include "core/theme.h"
#include "core/config.h"
#include "data/provider.h"
#include "data/storage.h"
#include "data/omm_parser.h"
#include "data/async_fetch.h"
#include "util/log.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <atomic>

#include <raylib.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

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
                    if (!DataSelectionAdd(SOURCE_RETLECTOR, name, name, NULL, FORMAT_OMM_CSV))
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

            if (!DataSelectionAdd(SOURCE_CUSTOM_PASTE, preview, preview, s_paste_buf, paste_fmt))
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

    int sel_count = DataSelectionCount();
    if (sel_count == 0)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "No sources selected. Add sources from the sections above.");
    }
    else
    {
        /* show count */
        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%d source(s) selected", sel_count);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "%s", count_str);

        /* scrollable list of selections */
        ImGui::BeginChild("##active_selections", ImVec2(0, fminf(sel_count * 28.0f, 200.0f)),
                          true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

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
                DataSelectionRemove(i);
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

    /* async pull state (persists across frames) */
    static bool s_pull_running = false;
    static int s_pull_total = 0;

    /* track whether BeginDisabled() was called this frame so the matching
     * EndDisabled() is only emitted when it was because imgui be crashy if not*/
    bool pull_disabled = false;

    if (s_pull_running)
    {
        /* once all jobs have been drained, the pull is complete */
        if (!AsyncFetchBusy())
        {
            s_pull_running = false;
            s_pull_total = 0;
        }
        else
        {
            /* show progress while the worker thread fetches/parses in the background */
            ImGui::TextColored(ThemeColor(g_theme.ui.ui_accent), "%s Pulling data...",
                               ICON_FA_SPINNER);
            ImGui::SameLine();
            static float spinner_angle = 0.0f;
            spinner_angle += ImGui::GetIO().DeltaTime * 180.0f;
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "%c",
                               "|/-\\"[(int)(spinner_angle / 45.0f) % 4]);

            int done = s_pull_total - AsyncFetchPendingCount();
            if (s_pull_total > 0)
            {
                ImGui::ProgressBar((float)done / (float)s_pull_total,
                                   ImVec2(avail_w, 0.0f), "");
                ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                                   "%d / %d sources", done, s_pull_total);
            }

            /* disable the button while a pull is in flight */
            ImGui::BeginDisabled();
            pull_disabled = true;
        }
    }

    if (ImGui::Button("Pull All Selected Sources", ImVec2(avail_w, 30)))
    {
        /* clear the slate and queue every selected source as an async job */
        sat_count = 0;
        s_pull_total = DataSelectionCount();
        s_pull_running = (s_pull_total > 0);

        for (int i = 0; i < s_pull_total; i++)
        {
            DataSourceSelection *s = DataSelectionAt(i);
            if (!s) break;

            AsyncFetchJob job;
            memset(&job, 0, sizeof(job));
            job.type = (AsyncJobType)s->type;
            strncpy(job.name, s->name, sizeof(job.name) - 1);
            strncpy(job.identifier, s->identifier, sizeof(job.identifier) - 1);
            strncpy(job.paste_data, s->paste_data, sizeof(job.paste_data) - 1);
            job.format = s->format;

            AsyncFetchSubmit(&job);
        }

        if (s_pull_total == 0)
            LOG_INFO("No sources selected to pull");
    }

    if (pull_disabled)
        ImGui::EndDisabled();
}
