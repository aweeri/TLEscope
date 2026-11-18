#include "log_window.h"
#include "core/theme.h"
#include "imgui_theme.h"
#include "tools/tools_common.h"
#include "util/log.h"

#include <cstdio>

#include "imgui.h"
#include "IconsFontAwesome6.h"

static bool log_auto_scroll = true;
static bool log_show_timestamps = false;
static int log_level_filter = 0;
static float log_font_scale = 0.8f;
static bool log_follow_tail = false;

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

void DrawLogWindow(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;

    if (!g_ui.show_log_window)
        log_follow_tail = false;

    if (g_ui.show_log_window)
        ImGui::OpenPopup(ICON_FA_LIST "  Log");

    ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float def_w = UIPx(720.0f);
    const float def_h = UIPx(420.0f);
    float max_w = (UIPx(1100.0f) < screen.x * 0.62f) ? UIPx(1100.0f) : screen.x * 0.62f;
    if (max_w < UIPx(420.0f)) max_w = UIPx(420.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(UIPx(420.0f), UIPx(240.0f)),
                                        ImVec2(max_w, screen.y - UIPx(48.0f)));
    ImGui::SetNextWindowSize(ImVec2(def_w, def_h), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    /* p_open draws a native close button; clicking it clears show_log_window */
    bool *p_open = &g_ui.show_log_window;
    if (ImGui::BeginPopupModal(ICON_FA_LIST "  Log", p_open, ImGuiWindowFlags_NoCollapse))
    {
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

        ImGui::SetNextItemWidth(avail_w * 0.25f);
        ImGui::Combo("##filter", &log_level_filter, "ALL\0INFO+\0WARN+\0ERROR\0");
        LogLevel min_level = LogLevelFilterMinLevel(log_level_filter);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(avail_w * 0.2f);
        ImGui::SliderFloat("##scale", &log_font_scale, 0.5f, 1.2f, "%.2fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scale");

        ImGui::Separator();

        /* text-wrapped region (no horizontal scrollbar) so long lines wrap */
        ImGui::BeginChild("##LogEntries", ImVec2(0, 0), false);
        ImGui::SetWindowFontScale(log_font_scale);

        /* sticky auto-scroll: follow the tail only while the user is at the bottom,
           so the welcome banner at the top stays visible until they scroll down */
        const float scroll_max = ImGui::GetScrollMaxY();
        if (scroll_max > 0.0f)
            log_follow_tail = (ImGui::GetScrollY() >= scroll_max - 1.0f);

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
                case LOG_LEVEL_DEBUG: color = ThemeColor(g_theme.ui.text_dim); break;
                case LOG_LEVEL_INFO:  color = ThemeColor(g_theme.ui.text); break;
                case LOG_LEVEL_WARN:  color = ImVec4(1.0f, 0.9f, 0.4f, 1.0f); break;
                case LOG_LEVEL_ERROR: color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break;
                default:              color = ThemeColor(g_theme.ui.text);
            }

            /* optionally include the timestamp prefix */
            char line_buf[576];
            if (log_show_timestamps)
                snprintf(line_buf, sizeof(line_buf), "%s %s", e->timestamp, e->message);
            else
                snprintf(line_buf, sizeof(line_buf), "%s", e->message);

            ImGui::PushID(idx);

            /* alternating row background to visually separate log entries */
            if (display_idx % 2 == 0)
            {
                ImVec2 row_min = ImGui::GetCursorScreenPos();
                ImGui::TextUnformatted(""); /* advance cursor by one line height */
                ImVec2 row_max = ImGui::GetCursorScreenPos();
                row_max.x = row_min.x + ImGui::GetContentRegionAvail().x;
                Color row_theme = g_theme.ui.surface;
                ImGui::GetWindowDrawList()->AddRectFilled(row_min, row_max,
                    IM_COL32(row_theme.r, row_theme.g, row_theme.b, 40));
                /* restore cursor to start of line */
                ImGui::SetCursorScreenPos(row_min);
            }

            /* hover highlight drawn behind the row */
            ImVec2 hov_min = ImGui::GetCursorScreenPos();
            float wrap_w = ImGui::GetContentRegionAvail().x;
            ImVec2 text_sz = ImGui::CalcTextSize(line_buf, NULL, false, wrap_w);
            ImVec2 hov_max = ImVec2(hov_min.x + wrap_w, hov_min.y + text_sz.y);
            if (ImGui::IsMouseHoveringRect(hov_min, hov_max))
                ImGui::GetWindowDrawList()->AddRectFilled(hov_min, hov_max,
                    ImGui::GetColorU32(ImGuiCol_HeaderHovered));

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(line_buf);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Right-click to copy");

            if (ImGui::BeginPopupContextItem("##logcopy"))
            {
                if (ImGui::MenuItem("Copy line"))
                    ImGui::SetClipboardText(line_buf);
                ImGui::EndPopup();
            }

            ImGui::PopID();

            display_idx++;
        }

        if (log_auto_scroll && count > 0 && log_follow_tail)
        {
            ImGui::SetScrollHereY(1.0f);
        }

        LogUnlock();

        /* drag-to-scroll: dragging on empty log space scrolls the list */
        if (ImGui::IsWindowHovered() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemActive())
        {
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        }

        ImGui::EndChild();

        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

void DrawPanelLog(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;

    if (ImGui::Button("Open Log Window", ImVec2(-1, 0)))
        g_ui.show_log_window = true;
}
