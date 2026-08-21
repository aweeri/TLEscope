/*
 * tool_log.cpp - Log panel
 */

#include "tools.h"
#include "tools_common.h"
#include "core/theme.h"
#include "core/config.h"
#include "util/log.h"

#include <cstdio>

#include "imgui.h"

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
            Color row_theme = g_theme.ui.frame_bg;
            ImGui::GetWindowDrawList()->AddRectFilled(row_min, row_max,
                IM_COL32(row_theme.r, row_theme.g, row_theme.b, 40));
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
