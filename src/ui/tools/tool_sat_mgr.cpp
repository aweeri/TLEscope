/*
 * tool_sat_mgr.cpp - Satellite Manager panel
 */

#include "tools.h"
#include "tools_common.h"
#include "tools_registry.h"
#include "ui/ui_layout.h"
#include "core/astro.h"
#include "core/theme.h"
#include "core/config.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>

#include <raylib.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

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
        SaveSatSelection(cfg);
        SaveAppConfig("settings.json", cfg);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable all visible satellites");
    ImGui::SameLine();

    /* Disable all (eye-slash icon) */
    if (ImGui::Button(ICON_FA_EYE_SLASH "##disable_all", ImVec2(btn_w, btn_w)))
    {
        for (int i = 0; i < sat_count; i++)
        {
            if (!search_active || str_contains_ic(satellites[i].name, search_buf))
                satellites[i].is_active = false;
        }
        SaveSatSelection(cfg);
        SaveAppConfig("settings.json", cfg);
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
        if (ImGui::Checkbox("##active", &satellites[i].is_active))
        {
            /* persist the selection immediately so it survives a crash/kill */
            SaveSatSelection(cfg);
            SaveAppConfig("settings.json", cfg);
        }
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
