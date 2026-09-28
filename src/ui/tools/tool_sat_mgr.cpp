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
#include "data/storage.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>
#include <vector>

#include <raylib.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

/* -- Favorites star rendering ---------------------------------------------- */

static constexpr float kPi = 3.14159265358979323846f;

static void DrawStarHollow(ImDrawList *dl, ImVec2 c, float R, ImU32 col)
{
    float r = R * 0.382f;
    for (int k = 0; k < 10; k++)
    {
        float ang = -kPi / 2.0f + (float)k * kPi / 5.0f;
        float rad = (k % 2 == 0) ? R : r;
        dl->PathLineTo(ImVec2(c.x + cosf(ang) * rad, c.y + sinf(ang) * rad));
    }
    dl->PathStroke(col, ImDrawFlags_Closed, 1.5f);
}

static void DrawStarFilled(ImDrawList *dl, ImVec2 c, float R, ImU32 col)
{
    float r = R * 0.382f;
    for (int k = 0; k < 10; k++)
    {
        float ang = -kPi / 2.0f + (float)k * kPi / 5.0f;
        float rad = (k % 2 == 0) ? R : r;
        dl->PathLineTo(ImVec2(c.x + cosf(ang) * rad, c.y + sinf(ang) * rad));
    }
    dl->PathFillConcave(col);
}

void DrawPanelSatMgr(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    static char search_buf[64] = "";
    static bool active_only = false;
    bool search_active = (search_buf[0] != '\0');

    /* empty state - point the user at the data puller */
    if (sat_count == 0)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
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
    ImGui::SetNextItemWidth(avail_w - btn_w * 3.0f - ImGui::GetStyle().ItemSpacing.x * 3.0f - 4.0f);
    ImGui::InputText("##sat_mgr_search", search_buf, sizeof(search_buf));
    ImGui::SameLine();

    /* Enable All (eye icon) */
    if (ImGui::Button(ICON_FA_EYE "##enable_all", ImVec2(btn_w, btn_w)))
    {
        for (int i = 0; i < sat_count; i++)
        {
            if ((!search_active || str_contains_ic(satellites[i].name, search_buf)) &&
                (!active_only || satellites[i].is_active))
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
            if ((!search_active || str_contains_ic(satellites[i].name, search_buf)) &&
                (!active_only || satellites[i].is_active))
                satellites[i].is_active = false;
        }
        SaveSatSelection(cfg);
        SaveAppConfig("settings.json", cfg);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Disable all visible satellites");
    ImGui::SameLine();

    /* Active-only filter (funnel icon) */
    ImGui::PushStyleColor(ImGuiCol_Text, active_only ? ThemeColor(g_theme.ui.accent) : ThemeColor(g_theme.ui.text_dim));
    if (ImGui::Button(ICON_FA_FILTER "##active_only", ImVec2(btn_w, btn_w)))
        active_only = !active_only;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only show already active satellites");

    /* show count of displayed satellites */
    int displayed = 0;
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].name[0] == '\0' || satellites[i].norad_id[0] == '\0')
            continue;
        if (active_only && !satellites[i].is_active)
            continue;
        if (!search_active || str_contains_ic(satellites[i].name, search_buf))
            displayed++;
    }
    if (search_active || active_only)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
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

    /* build the display order: favorites first, preserving insertion order
     * within each group, so starred sats always sit at the top of the list */
    std::vector<int> order;
    order.reserve(sat_count);
    for (int pass = 0; pass < 2; pass++)
    {
        for (int i = 0; i < sat_count; i++)
        {
            if (satellites[i].name[0] == '\0' || satellites[i].norad_id[0] == '\0')
                continue;
            if (active_only && !satellites[i].is_active)
                continue;
            if (search_active && !str_contains_ic(satellites[i].name, search_buf))
                continue;
            bool fav = IsFavorite(satellites[i].norad_id_num);
            if ((pass == 0 && fav) || (pass == 1 && !fav))
                order.push_back(i);
        }
    }

    const ImU32 dim = ImGui::GetColorU32(ThemeColor(g_theme.ui.text_dim));
    const ImU32 fav_yellow = ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.1f, 1.0f));

    for (size_t oi = 0; oi < order.size(); oi++)
    {
        int i = order[oi];
        bool active = satellites[i].is_active;
        bool fav = IsFavorite(satellites[i].norad_id_num);

        ImGui::PushID(i);

        /* always show the favorite control so it is discoverable */
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 frame = ImVec2(20.0f, 20.0f);
        ImVec2 star_center = ImVec2(p.x + frame.x * 0.5f, p.y + frame.y * 0.5f);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (fav)
            DrawStarFilled(dl, star_center, 7.0f, fav_yellow);
        else
            DrawStarHollow(dl, star_center, 7.0f, dim);

        ImGui::InvisibleButton("##fav", frame);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            bool now_fav = !fav;
            SetFavorite(satellites[i].norad_id_num, now_fav);
            SaveFavorites("favorites.json");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(fav ? "Remove from favorites" : "Add to favorites");
        ImGui::SameLine();

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
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(g_theme.ui.text_dim));
        }

        char label[128];
        snprintf(label, sizeof(label), "%s##%d", satellites[i].name, i);

        float row_avail = ImGui::GetContentRegionAvail().x;
        ImVec2 selectable_size = ImVec2(row_avail, 20);
        if (ImGui::Selectable(label, *ctx->selected_sat == &satellites[i],
                              ImGuiSelectableFlags_None, selectable_size))
        {
            /* clicking the already-selected row toggles the selection off */
            *ctx->selected_sat = (*ctx->selected_sat == &satellites[i]) ? NULL : &satellites[i];
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

    /* drag-to-scroll: dragging on empty list space scrolls the list */
    if (ImGui::IsWindowHovered() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemActive())
    {
        ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
    }

    ImGui::EndChild();

    ImGui::PopTextWrapPos();
}
