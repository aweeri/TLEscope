#include "imgui_theme.h"
#include "core/theme.h"
#include "util/log.h"
#include "tools/tools_common.h"

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>
#include "IconsFontAwesome6.h"
#include "FA6FreeSolidFontData.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* apply colours + style                                               */
/* ------------------------------------------------------------------ */

void ThemeApplyToImGui(const Theme *t, float ui_scale)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImGuiIO &io = ImGui::GetIO();

    /* ui_scale can change at runtime (settings slider / +/- keys) and
     * ThemeApplyToImGui may be re-invoked on theme switches, so scaling
     * must be idempotent. Reset to pristine defaults first (the theme
     * re-applies every colour/style var below), then scale once with the
     * new factor. This avoids ScaleAllSizes() accumulating across calls:
     * its truncation would otherwise ratchet sizes downward on every
     * undo/redo round-trip, eventually driving WindowMinSize below 1.0
     * and tripping ImGui's NewFrame() assertion. */
    style = ImGuiStyle();

    /* ── colour palette ──────────────────────────────────────────────
     * The full ImGuiCol_* set is derived from the compact theme palette.
     * Interactive states are mixed from a base color toward the theme's
     * text color, so a single rule produces readable hover/active shades
     * on both dark and light themes. */
    const Color text = t->ui.text;
    const Color bg = t->ui.bg;
    const Color surface = t->ui.surface;
    const Color border = t->ui.border;
    const Color accent = t->ui.accent;

    /* mid-tone used for scrollbar grabs, between surface and text */
    const Color grab = ThemeMix(surface, text, 0.25f);

    style.Colors[ImGuiCol_Text]                 = ThemeColor(text);
    style.Colors[ImGuiCol_TextDisabled]         = ThemeColor(t->ui.text_dim);
    style.Colors[ImGuiCol_WindowBg]             = ThemeColor(bg);
    style.Colors[ImGuiCol_ChildBg]              = ThemeColor(bg);
    style.Colors[ImGuiCol_PopupBg]              = ThemeColor(bg);
    style.Colors[ImGuiCol_Border]               = ThemeColor(border);
    style.Colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    style.Colors[ImGuiCol_FrameBg]              = ThemeColor(surface);
    style.Colors[ImGuiCol_FrameBgHovered]       = ThemeColor(ThemeHoverOf(surface, text));
    style.Colors[ImGuiCol_FrameBgActive]        = ThemeColor(ThemeActiveOf(surface, text));

    style.Colors[ImGuiCol_TitleBg]              = ThemeColor(bg);
    style.Colors[ImGuiCol_TitleBgActive]        = ThemeColor(surface);
    style.Colors[ImGuiCol_TitleBgCollapsed]     = ThemeColor(bg);

    style.Colors[ImGuiCol_MenuBarBg]            = ThemeColor(surface);

    style.Colors[ImGuiCol_ScrollbarBg]          = ThemeColor(bg);
    style.Colors[ImGuiCol_ScrollbarGrab]        = ThemeColor(grab);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ThemeColor(ThemeMix(surface, text, 0.35f));
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = ThemeColor(ThemeMix(surface, text, 0.45f));

    style.Colors[ImGuiCol_CheckMark]            = ThemeColor(accent);
    style.Colors[ImGuiCol_SliderGrab]           = ThemeColor(accent);
    style.Colors[ImGuiCol_SliderGrabActive]     = ThemeColor(ThemeMix(accent, text, 0.25f));

    style.Colors[ImGuiCol_Button]               = ThemeColor(surface);
    style.Colors[ImGuiCol_ButtonHovered]        = ThemeColor(ThemeHoverOf(surface, text));
    style.Colors[ImGuiCol_ButtonActive]         = ThemeColor(ThemeActiveOf(surface, text));

    style.Colors[ImGuiCol_Header]               = ThemeColor(surface);
    style.Colors[ImGuiCol_HeaderHovered]        = ThemeColor(ThemeHoverOf(surface, text));
    style.Colors[ImGuiCol_HeaderActive]         = ThemeColor(ThemeActiveOf(surface, text));

    style.Colors[ImGuiCol_Separator]            = ThemeColor(border);
    style.Colors[ImGuiCol_SeparatorHovered]     = ThemeColor(ThemeMix(border, text, 0.25f));
    style.Colors[ImGuiCol_SeparatorActive]      = ThemeColor(ThemeMix(border, text, 0.35f));

    style.Colors[ImGuiCol_ResizeGrip]           = ThemeColor(ThemeAlpha(accent, 0.20f));
    style.Colors[ImGuiCol_ResizeGripHovered]    = ThemeColor(ThemeAlpha(accent, 0.60f));
    style.Colors[ImGuiCol_ResizeGripActive]     = ThemeColor(accent);

    style.Colors[ImGuiCol_Tab]                  = ThemeColor(ThemeMix(bg, surface, 0.5f));
    style.Colors[ImGuiCol_TabHovered]           = ThemeColor(surface);
    style.Colors[ImGuiCol_TabActive]            = ThemeColor(surface);
    style.Colors[ImGuiCol_TabUnfocused]         = ThemeColor(bg);
    style.Colors[ImGuiCol_TabUnfocusedActive]   = ThemeColor(ThemeMix(bg, surface, 0.5f));
#ifdef IMGUI_HAS_DOCK
    style.Colors[ImGuiCol_DockingBg]            = ThemeColor(t->ui.overlay);
    style.Colors[ImGuiCol_DockingPreview]       = ThemeColor(ThemeAlpha(accent, 0.50f));
#endif

    style.Colors[ImGuiCol_PlotLines]            = ThemeColor(accent);
    style.Colors[ImGuiCol_PlotHistogram]        = ThemeColor(accent);

    style.Colors[ImGuiCol_TextSelectedBg]       = ThemeColor(ThemeAlpha(accent, 0.25f));
    style.Colors[ImGuiCol_ModalWindowDimBg]     = ThemeColor(t->ui.overlay);

    style.Colors[ImGuiCol_NavCursor]            = ThemeColor(accent);
    style.Colors[ImGuiCol_NavWindowingHighlight]= ThemeColor(accent);

    /* ── style variables ──────────────────────────────────────────── */
    style.WindowRounding          = t->style.window_rounding;
    style.ChildRounding           = t->style.child_rounding;
    style.PopupRounding           = t->style.popup_rounding;
    style.FrameRounding           = t->style.frame_rounding;
    style.GrabRounding            = t->style.grab_rounding;
    style.ScrollbarRounding       = t->style.scrollbar_rounding;
    style.TabRounding             = t->style.tab_rounding;

    style.WindowBorderSize        = t->style.window_border_size;
    style.ChildBorderSize         = t->style.window_border_size;  /* derive from window */
    style.PopupBorderSize         = t->style.popup_border_size;
    style.FrameBorderSize         = t->style.frame_border_size;
    style.TabBorderSize           = t->style.frame_border_size;

    style.WindowPadding           = ImVec2(t->style.window_padding_x,  t->style.window_padding_y);
    style.FramePadding            = ImVec2(t->style.frame_padding_x,   t->style.frame_padding_y);
    style.ItemSpacing             = ImVec2(t->style.item_spacing_x,    t->style.item_spacing_y);
    style.ItemInnerSpacing        = ImVec2(t->style.item_inner_spacing_x, t->style.item_inner_spacing_y);
    style.ScrollbarSize           = t->style.scrollbar_size;
    style.GrabMinSize             = t->style.grab_min_size;

    style.WindowTitleAlign        = ImVec2(t->style.window_title_align_x, 0.5f);
    style.ButtonTextAlign         = ImVec2(t->style.button_text_align_x,  0.5f);
    style.IndentSpacing           = t->style.indent_spacing;
    style.ColumnsMinSpacing       = t->style.columns_min_spacing;

    /* ── scale ────────────────────────────────────────────────────── */
    style.ScaleAllSizes(ui_scale);
    /* FontGlobalScale is kept at 1.0: the UI scale is baked into the font
     * atlas size in ThemeRebuildImGuiFonts() instead. Scaling glyphs at
     * render time (FontGlobalScale != 1.0) places them on non-pixel-aligned
     * positions and makes text look blurry/antialiased. */
    io.FontGlobalScale = 1.0f;

    LOG_DEBUG("ImGui theme applied (scale=%.2f)", (double)ui_scale);
}

/* ------------------------------------------------------------------ */
/* font atlas rebuild                                                  */
/* ------------------------------------------------------------------ */

float ThemeDevicePixelScale(void)
{
    float sw = (float)GetScreenWidth();
    float rw = (float)GetRenderWidth();
    if (sw > 0.0f && rw > 0.0f)
        return rw / sw;

    Vector2 dpi = GetWindowScaleDPI();
    return (dpi.y > 0.0f) ? dpi.y : 1.0f;
}

void ThemeRebuildImGuiFonts(const Theme *t, float ui_scale)
{
    ImGuiIO &io = ImGui::GetIO();

    /* unload the previous font texture if we have one */
    static Texture s_font_tex = {0};
    if (s_font_tex.id != 0)
    {
        UnloadTexture(s_font_tex);
        s_font_tex = {0};
    }

    /* clear existing fonts */
    io.Fonts->Clear();

    /* load the theme's font file (fallback to default via ThemeAssetPath).
     * The UI scale is baked into the atlas size so glyphs are rasterized at
     * the final pixel size (crisp text) rather than scaled at render time. */
    const char *font_path = ThemeAssetPath(t->font.file);

    /* Round the logical font size to a whole pixel (Dear ImGui's DPI guidance)
     * so glyph metrics line up with the integer-truncated style sizes that
     * ImGuiStyle::ScaleAllSizes() produces. A fractional font size leaves text
     * baselines between pixels and is a common source of misaligned rows on
     * HiDPI displays. */
    float font_px  = (float)(int)(t->font.size * ui_scale + 0.5f);
    float icon_px  = (float)(int)(t->font.icon_size * ui_scale + 0.5f);
    if (font_px < 1.0f) font_px = 1.0f;
    if (icon_px < 1.0f) icon_px = 1.0f;

    /* Single source of truth for the device scale: the exact ratio raylib uses
     * for its DPI transform, so the atlas is never resampled. */
    const float density = ThemeDevicePixelScale();

    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = true;
    font_cfg.MergeMode = false;
    font_cfg.PixelSnapH = true;
    /* Keep logical font metrics unchanged; rasterize for the framebuffer's
     * pixel density. raylib/rlImGui handle window and input scaling. */
    font_cfg.RasterizerDensity = density;
    io.Fonts->AddFontFromFileTTF(font_path, font_px, &font_cfg, NULL);

    /* merge FontAwesome icons */

    ImFontConfig icons_cfg;
    icons_cfg.MergeMode = true;
    icons_cfg.FontDataOwnedByAtlas = true;
    icons_cfg.PixelSnapH = true;
    icons_cfg.RasterizerDensity = density;
    /* ~2px down at the 14px default icon size, clamped to stay 1-4px */
    float icon_y_off = icon_px * 0.14f + 0.04f;
    if (icon_y_off < 1.0f) icon_y_off = 1.0f;
    if (icon_y_off > 4.0f) icon_y_off = 4.0f;
    icons_cfg.GlyphOffset.y = icon_y_off;
    icons_cfg.GlyphMinAdvanceX = icon_px; /* uniform 1em icon cell is stable horizontal centring */
    icons_cfg.GlyphMaxAdvanceX = icon_px;
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        fa_solid_900_compressed_data,
        fa_solid_900_compressed_size,
        icon_px,
        &icons_cfg,
        icon_ranges);

    /* build atlas and upload to raylib */
    io.Fonts->Build();
    unsigned char *pixels = NULL;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    Image img = { pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    s_font_tex = LoadTextureFromImage(img);
    /* img pixels are owned by ImGui atlas — do NOT unload */

    io.Fonts->SetTexID((ImTextureID)(intptr_t)s_font_tex.id);

    LOG_INFO("ImGui fonts rebuilt: %s @ %.0fpx + FA @ %.0fpx (scale=%.2f)",
             t->font.file, (double)t->font.size, (double)t->font.icon_size, (double)ui_scale);
}
