/*
 * hints.cpp - generic on-screen hint / coachmark overlay.
 *
 * Draws a floating hint box (icon + title + body) with an optional spotlight
 * hole and arrow pointing at a target rect. Everything is rendered into the
 * ImGui foreground draw list, so hints sit above every window, popup and toast.
 * Standalone hints are passive; sequences may opt in to input capture.
 */

#include "hints.h"

#include "core/theme.h"
#include "imgui_theme.h"
#include "tools/tools_common.h"

#include "imgui.h"
#include "IconsFontAwesome6.h"

#include <cmath>
#include <cstdio>

#define MAX_HINTS 16

/* -- State ----------------------------------------------------------------- */

static Hint s_steps[MAX_HINTS];
static int  s_count = 0;
static int  s_index = 0;
static bool s_active = false;
static bool s_sequence = false;      /* show N/M progress + click to continue */
static bool s_capture_input = false; /* opt-in input blocking                 */
static bool s_just_started = false;  /* swallow the click that launched us    */

/* -- Public API ------------------------------------------------------------ */

void HintShow(const HintContent *content)
{
    if (!content) return;
    s_steps[0].content = *content;
    s_steps[0].target = {0, 0, 0, 0, false};
    s_count = 1;
    s_index = 0;
    s_active = true;
    s_sequence = false;
    s_capture_input = false;
    s_just_started = true;
}

void HintShowAt(const Hint *hint)
{
    if (!hint) return;
    s_steps[0] = *hint;
    s_count = 1;
    s_index = 0;
    s_active = true;
    s_sequence = false;
    s_capture_input = false;
    s_just_started = true;
}

void HintStartSequence(const Hint *steps, int count, bool capture_input)
{
    if (!steps || count <= 0)
    {
        HintDismiss();
        return;
    }
    if (count > MAX_HINTS) count = MAX_HINTS;
    for (int i = 0; i < count; i++) s_steps[i] = steps[i];
    s_count = count;
    s_index = 0;
    s_active = true;
    s_sequence = true;
    s_capture_input = capture_input;
    s_just_started = true;
}

void HintNext(void)
{
    if (!s_active) return;
    if (s_index + 1 >= s_count)
    {
        s_active = false;
        s_index = 0;
    }
    else
    {
        s_index++;
    }
}

void HintPrev(void)
{
    if (!s_active) return;
    if (s_index > 0) s_index--;
}

void HintDismiss(void)
{
    s_active = false;
    s_index = 0;
}

void HintSetTarget(const UIRect *target)
{
    if (!s_active || s_index < 0 || s_index >= s_count) return;
    if (target) s_steps[s_index].target = *target;
    else s_steps[s_index].target = {0, 0, 0, 0, false};
}

bool HintIsActive(void) { return s_active; }
int  HintStepIndex(void) { return s_index; }
int  HintStepCount(void) { return s_count; }

/* -- Drawing --------------------------------------------------------------- */

/* draw the floating hint box + arrow; reports the box rect and the X hit rect */
static void DrawHintBox(ImDrawList *dl, const ImVec2 &screen, const UIRect *target,
                        const HintContent *c, int index, int count,
                        bool show_progress, bool show_click,
                        ImVec2 *out_bmin, ImVec2 *out_bmax, ImVec2 *out_xmin, ImVec2 *out_xmax)
{
    const float pad = UIPx(12.0f), pad_y = UIPx(10.0f), round = UIPx(8.0f);
    const float gap = UIPx(18.0f), max_w = UIPx(300.0f);
    const float icon_slot = UIPx(22.0f);
    const float row_gap   = UIPx(6.0f);
    const float foot_gap  = UIPx(8.0f);

    ImU32 accent    = IM_COL32(g_theme.ui.accent.r,   g_theme.ui.accent.g,   g_theme.ui.accent.b,   255);
    ImU32 title_col = IM_COL32(g_theme.ui.text.r,     g_theme.ui.text.g,     g_theme.ui.text.b,     255);
    ImU32 text_col  = IM_COL32(g_theme.ui.text_dim.r, g_theme.ui.text_dim.g, g_theme.ui.text_dim.b, 230);
    ImU32 foot_col  = IM_COL32(g_theme.ui.text_dim.r, g_theme.ui.text_dim.g, g_theme.ui.text_dim.b, 200);

    char prog[32];
    snprintf(prog, sizeof(prog), "%d/%d", index + 1, count);
    const char *click = "click to continue";

    const float line_h   = ImGui::GetTextLineHeight();
    const float text_max = max_w - 2.0f * pad;

    ImVec2 title_sz = ImGui::CalcTextSize(c->title);
    ImVec2 body_nat = ImGui::CalcTextSize(c->body);
    ImVec2 prog_sz  = ImGui::CalcTextSize(prog);
    ImVec2 click_sz = ImGui::CalcTextSize(click);
    float foot_w = prog_sz.x;
    if (show_click) foot_w += UIPx(16.0f) + click_sz.x;
    float text_w = fmaxf(fmaxf(title_sz.x + icon_slot, body_nat.x), foot_w);
    if (text_w > text_max) text_w = text_max;
    float w = text_w + 2.0f * pad;

    float wrap_w = w - 2.0f * pad;
    ImVec2 body_sz = ImGui::CalcTextSize(c->body, NULL, false, wrap_w);
    float title_h = line_h;
    float body_h  = body_sz.y;

    float h = pad_y + title_h + row_gap + body_h + foot_gap + line_h + pad_y;

    ImVec2 bmin;
    if (target)
    {
        ImVec2 tc((target->x0 + target->x1) * 0.5f, (target->y0 + target->y1) * 0.5f);
        bool to_right = tc.x < screen.x * 0.5f;
        bool to_below = tc.y < ImGui::GetFrameHeight() + UIPx(40.0f);
        if (to_below)      bmin = ImVec2(tc.x - w * 0.5f, target->y1 + gap);
        else if (to_right) bmin = ImVec2(target->x1 + gap, tc.y - h * 0.5f);
        else               bmin = ImVec2(target->x0 - gap - w, tc.y - h * 0.5f);
    }
    else
    {
        bmin = ImVec2((screen.x - w) * 0.5f, (screen.y - h) * 0.5f);
    }
    bmin.x = fmaxf(UIPx(10.0f), fminf(bmin.x, screen.x - w - UIPx(10.0f)));
    bmin.y = fmaxf(UIPx(10.0f), fminf(bmin.y, screen.y - h - UIPx(10.0f)));
    ImVec2 bmax(bmin.x + w, bmin.y + h);

    ImU32 fill = IM_COL32(g_theme.ui.surface.r, g_theme.ui.surface.g, g_theme.ui.surface.b, 245);
    ImU32 edge = IM_COL32(g_theme.ui.border.r, g_theme.ui.border.g, g_theme.ui.border.b, 200);

    /* arrow: a filled triangle on the side of the box facing the target */
    if (target)
    {
        const float aw = UIPx(7.0f);
        if (bmin.x >= target->x1)
        {
            float cy = fminf(fmaxf((target->y0 + target->y1) * 0.5f, bmin.y + aw), bmax.y - aw);
            dl->AddTriangleFilled(ImVec2(bmin.x, cy - aw), ImVec2(bmin.x, cy + aw), ImVec2(target->x1 + UIPx(2.0f), cy), fill);
        }
        else if (bmax.x <= target->x0)
        {
            float cy = fminf(fmaxf((target->y0 + target->y1) * 0.5f, bmin.y + aw), bmax.y - aw);
            dl->AddTriangleFilled(ImVec2(bmax.x, cy - aw), ImVec2(bmax.x, cy + aw), ImVec2(target->x0 - UIPx(2.0f), cy), fill);
        }
        else if (bmin.y >= target->y1)
        {
            float cx = fminf(fmaxf((target->x0 + target->x1) * 0.5f, bmin.x + aw), bmax.x - aw);
            dl->AddTriangleFilled(ImVec2(cx - aw, bmin.y), ImVec2(cx + aw, bmin.y), ImVec2(cx, target->y1 + UIPx(2.0f)), fill);
        }
    }

    dl->AddRectFilled(bmin, bmax, fill, round);
    dl->AddRect(bmin, bmax, edge, round, 0, UIPx(1.0f));

    float y = bmin.y + pad_y;
    dl->AddText(ImVec2(bmin.x + pad, y), accent, c->icon);
    dl->AddText(ImVec2(bmin.x + pad + icon_slot, y), title_col, c->title);
    y += title_h + row_gap;
    dl->AddText(NULL, 0.0f, ImVec2(bmin.x + pad, y), text_col, c->body, NULL, wrap_w);
    y += body_h + foot_gap;

    /* footer: N/M left-aligned, "click to continue" right-aligned, same baseline */
    if (show_progress) dl->AddText(ImVec2(bmin.x + pad, y), foot_col, prog);
    if (show_click)    dl->AddText(ImVec2(bmax.x - pad - click_sz.x, y), foot_col, click);

    /* X (dismiss) glyph at the box top-right */
    const float xb = ImGui::GetFrameHeight();
    ImVec2 xmin(bmax.x - xb - UIPx(4.0f), bmin.y + UIPx(4.0f));
    ImVec2 xmax(xmin.x + xb, xmin.y + xb);
    ImVec2 xg = ImGui::CalcTextSize(ICON_FA_XMARK);
    dl->AddText(ImVec2((xmin.x + xmax.x) * 0.5f - xg.x * 0.5f,
                       (xmin.y + xmax.y) * 0.5f - xg.y * 0.5f),
                foot_col, ICON_FA_XMARK);

    if (out_bmin) *out_bmin = bmin;
    if (out_bmax) *out_bmax = bmax;
    if (out_xmin) *out_xmin = xmin;
    if (out_xmax) *out_xmax = xmax;
}

void DrawHints(void)
{
    if (!s_active || s_count <= 0) return;

    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    ImDrawList *dl = ImGui::GetForegroundDrawList();

    const Hint *h = &s_steps[s_index];
    const UIRect *target = h->target.valid ? &h->target : NULL;

    /* input catcher: only for sequences that opt in to blocking */
    if (s_capture_input)
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(screen);
        ImGui::SetNextWindowFocus();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        if (ImGui::Begin("##hint_catcher", NULL,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::InvisibleButton("##hint_catcher_btn", screen);
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
    }

    /* --- dim + spotlight hole (only when the hint blocks input) ---------- */
    if (s_capture_input)
    {
        ImU32 dim = ImGui::GetColorU32(ThemeColor(ThemeAlpha(g_theme.ui.overlay, 0.62f)));
        ImU32 accent = IM_COL32(g_theme.ui.accent.r, g_theme.ui.accent.g, g_theme.ui.accent.b, 230);

        if (target)
        {
            const float pad = UIPx(6.0f);
            ImVec2 h0(target->x0 - pad, target->y0 - pad), h1(target->x1 + pad, target->y1 + pad);
            h0.x = fmaxf(h0.x, 0.0f); h0.y = fmaxf(h0.y, 0.0f);
            h1.x = fminf(h1.x, screen.x); h1.y = fminf(h1.y, screen.y);
            dl->AddRectFilled(ImVec2(0, 0),       ImVec2(screen.x, h0.y), dim);
            dl->AddRectFilled(ImVec2(0, h1.y),    ImVec2(screen.x, screen.y), dim);
            dl->AddRectFilled(ImVec2(0, h0.y),    ImVec2(h0.x, h1.y), dim);
            dl->AddRectFilled(ImVec2(h1.x, h0.y), ImVec2(screen.x, h1.y), dim);
            dl->AddRect(h0, h1, accent, UIPx(6.0f), 0, UIPx(2.0f));
        }
        else
        {
            dl->AddRectFilled(ImVec2(0, 0), screen, dim);
        }
    }

    /* --- hint box + arrow ------------------------------------------------ */
    ImVec2 box_min, box_max, x_min, x_max;
    DrawHintBox(dl, screen, target, &h->content, s_index, s_count,
                s_sequence, s_sequence && s_capture_input,
                &box_min, &box_max, &x_min, &x_max);

    /* --- input: X dismisses, background advances (capture sequences) ----- */
    bool over_x = io.MousePos.x >= x_min.x && io.MousePos.x <= x_max.x &&
                  io.MousePos.y >= x_min.y && io.MousePos.y <= x_max.y;
    if (over_x) dl->AddRectFilled(x_min, x_max, IM_COL32(255, 255, 255, 24), UIPx(4.0f));

    bool process_click = !s_just_started;
    s_just_started = false;
    if (process_click && io.MouseClicked[ImGuiMouseButton_Left])
    {
        if (over_x) HintDismiss();
        else if (s_sequence && s_capture_input) HintNext();
    }
}
