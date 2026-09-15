/*
 * tool_layers.cpp - Layers panel
 */

#include "tools.h"
#include "tools_common.h"
#include "core/theme.h"
#include "core/config.h"
#include "ui/labels.h"
#include "ui/tools/tools_settings.h"

#include <raylib.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

void DrawPanelLayers(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;

    ImGui::PushTextWrapPos(0.0f);

    /* fixed icon width so all checkboxes align vertically */
    const float icon_w = 24.0f;

    auto DrawLayerCheckbox = [&](const char *label, bool *value, const char *icon, const char *tooltip) {
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColor(*value ? g_theme.ui.ui_accent : g_theme.ui.text_secondary));
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        /* center the icon within a fixed-width cell so all rows align */
        ImVec2 icon_sz = ImGui::CalcTextSize(icon);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + (icon_w - icon_sz.x) * 0.5f, pos.y), col, icon);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(icon_w, ImGui::GetFrameHeight()));
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
    DrawLayerCheckbox("Ground Coverage", &cfg->show_ground_coverage, ICON_FA_ROUTE, "Show the line-of-sight ground coverage footprint");
    DrawLayerCheckbox("Apsides", &cfg->show_apsides, ICON_FA_CIRCLE_DOT, "Show perigee/apogee markers and altitude labels");

    ImGui::Separator();

    /* label overlay controls (see labels.h / labels.cpp) */
    bool labels_enabled = ToolSettingGetBool(cfg, LABELS_KEY_ENABLED, true);
    if (ImGui::Checkbox("Labels", &labels_enabled))
        ToolSettingSetBool(cfg, LABELS_KEY_ENABLED, labels_enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show name labels for satellites and markers");

    int label_mode = ToolSettingGetInt(cfg, LABELS_KEY_MODE, LABELS_MODE_ACTIVE_ONLY);
    const char *modes[] = { "Active only", "All", "None" };
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##label_mode", &label_mode, modes, 3))
        ToolSettingSetInt(cfg, LABELS_KEY_MODE, label_mode);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Which satellites get name labels");

    bool show_alt = ToolSettingGetBool(cfg, LABELS_KEY_ALTITUDE, false);
    if (ImGui::Checkbox("Label Altitude", &show_alt))
        ToolSettingSetBool(cfg, LABELS_KEY_ALTITUDE, show_alt);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Append current altitude (km) to satellite labels");

    float label_size = ToolSettingGetFloat(cfg, LABELS_KEY_SIZE, 1.0f);
    if (ImGui::SliderFloat("Label Size", &label_size, 0.5f, 1.5f, "%.2fx"))
        ToolSettingSetFloat(cfg, LABELS_KEY_SIZE, label_size);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scale multiplier on the UI font (1.0x = crisp native size)");

    bool use_bg = ToolSettingGetBool(cfg, LABELS_KEY_BG, true);
    if (ImGui::Checkbox("Label Background", &use_bg))
        ToolSettingSetBool(cfg, LABELS_KEY_BG, use_bg);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draw a dark rounded box behind label text for contrast");

    int max_count = ToolSettingGetInt(cfg, LABELS_KEY_MAX_COUNT, 200);
    if (ImGui::SliderInt("Max Labels", &max_count, 10, 1000))
        ToolSettingSetInt(cfg, LABELS_KEY_MAX_COUNT, max_count);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Declutter cap: how many labels are drawn before overlap rejection kicks in");

    ImGui::PopTextWrapPos();
}
