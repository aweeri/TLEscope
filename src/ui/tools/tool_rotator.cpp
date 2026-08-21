/*
 * tool_rotator.cpp - Rotator Control panel
 */

#include "tools.h"
#include "io/rotator.h"
#include "core/config.h"

#include <cstdio>

#include "imgui.h"

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
