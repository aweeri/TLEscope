/*
 * tool_time_ctrl.cpp - Time Control panel
 */

#include "tools.h"
#include "core/astro.h"
#include "core/config.h"

#include "imgui.h"

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
