#include "imgui_theme.h"
#include "core/theme.h"
#include "util/log.h"

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>
#include "IconsFontAwesome6.h"
#include "FA6FreeSolidFontData.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static ImVec4 ColorToImVec4(Color c)
{
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

/* ------------------------------------------------------------------ */
/* apply colours + style                                               */
/* ------------------------------------------------------------------ */

void ThemeApplyToImGui(const Theme *t, float ui_scale)
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImGuiIO &io = ImGui::GetIO();

    /* ui_scale can change at runtime (settings slider / +/- keys) and
     * ThemeApplyToImGui may be re-invoked on theme switches, so scaling
     * must be idempotent: undo the previously applied scale first, then
     * re-apply with the new factor. Otherwise ScaleAllSizes() would
     * accumulate on every call. */
    static float applied_scale = 1.0f;
    style.ScaleAllSizes(1.0f / applied_scale);

    /* ── colour palette ──────────────────────────────────────────── */
    style.Colors[ImGuiCol_Text]                 = ColorToImVec4(t->ui.text_main);
    style.Colors[ImGuiCol_TextDisabled]         = ColorToImVec4(t->ui.text_secondary);
    style.Colors[ImGuiCol_WindowBg]             = ColorToImVec4(t->ui.window_bg);
    style.Colors[ImGuiCol_ChildBg]              = ColorToImVec4(t->ui.window_bg);  /* derive from window bg */
    style.Colors[ImGuiCol_PopupBg]              = ColorToImVec4(t->ui.window_bg);
    style.Colors[ImGuiCol_Border]               = ColorToImVec4(t->ui.window_border);
    style.Colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    style.Colors[ImGuiCol_FrameBg]              = ColorToImVec4(t->ui.frame_bg);
    style.Colors[ImGuiCol_FrameBgHovered]       = ColorToImVec4(t->ui.frame_bg_hovered);
    style.Colors[ImGuiCol_FrameBgActive]        = ColorToImVec4(t->ui.frame_bg_active);

    style.Colors[ImGuiCol_TitleBg]              = ColorToImVec4(t->ui.titlebar);
    style.Colors[ImGuiCol_TitleBgActive]        = ColorToImVec4(t->ui.titlebar_active);
    style.Colors[ImGuiCol_TitleBgCollapsed]     = ColorToImVec4(t->ui.titlebar_collapsed);

    style.Colors[ImGuiCol_MenuBarBg]            = ColorToImVec4(t->ui.ui_primary);

    style.Colors[ImGuiCol_ScrollbarBg]          = ColorToImVec4(t->ui.scrollbar_bg);
    style.Colors[ImGuiCol_ScrollbarGrab]        = ColorToImVec4(t->ui.scrollbar_grab);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ColorToImVec4(t->ui.scrollbar_grab_hovered);
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = ColorToImVec4(t->ui.scrollbar_grab_active);

    style.Colors[ImGuiCol_CheckMark]            = ColorToImVec4(t->ui.check_mark);
    style.Colors[ImGuiCol_SliderGrab]           = ColorToImVec4(t->ui.slider_grab);
    style.Colors[ImGuiCol_SliderGrabActive]     = ColorToImVec4(t->ui.slider_grab_active);

    style.Colors[ImGuiCol_Button]               = ColorToImVec4(t->ui.button);
    style.Colors[ImGuiCol_ButtonHovered]        = ColorToImVec4(t->ui.button_hovered);
    style.Colors[ImGuiCol_ButtonActive]         = ColorToImVec4(t->ui.button_active);

    style.Colors[ImGuiCol_Header]               = ColorToImVec4(t->ui.header);
    style.Colors[ImGuiCol_HeaderHovered]        = ColorToImVec4(t->ui.header_hovered);
    style.Colors[ImGuiCol_HeaderActive]         = ColorToImVec4(t->ui.header_active);

    style.Colors[ImGuiCol_Separator]            = ColorToImVec4(t->ui.separator);
    style.Colors[ImGuiCol_SeparatorHovered]     = ColorToImVec4(t->ui.separator_hovered);
    style.Colors[ImGuiCol_SeparatorActive]      = ColorToImVec4(t->ui.separator_active);

    style.Colors[ImGuiCol_ResizeGrip]           = ColorToImVec4(t->ui.resize_grip);
    style.Colors[ImGuiCol_ResizeGripHovered]    = ColorToImVec4(t->ui.ui_accent);
    style.Colors[ImGuiCol_ResizeGripActive]     = ColorToImVec4(t->ui.slider_grab_active);

    style.Colors[ImGuiCol_Tab]                  = ColorToImVec4(t->ui.tab);
    style.Colors[ImGuiCol_TabHovered]           = ColorToImVec4(t->ui.tab_hovered);
    style.Colors[ImGuiCol_TabActive]            = ColorToImVec4(t->ui.tab_active);
    style.Colors[ImGuiCol_TabUnfocused]         = ColorToImVec4(t->ui.tab_unfocused);
    style.Colors[ImGuiCol_TabUnfocusedActive]   = ColorToImVec4(t->ui.tab_unfocused_active);
#ifdef IMGUI_HAS_DOCK
style.Colors[ImGuiCol_DockingBg]            = ColorToImVec4(t->ui.docking_bg);
style.Colors[ImGuiCol_DockingPreview]       = ColorToImVec4(t->ui.docking_preview);
#endif


    style.Colors[ImGuiCol_PlotLines]            = ColorToImVec4(t->ui.plot_lines);
    style.Colors[ImGuiCol_PlotHistogram]        = ColorToImVec4(t->ui.plot_histogram);

    style.Colors[ImGuiCol_TextSelectedBg]       = ColorToImVec4(t->ui.text_selected_bg);
    style.Colors[ImGuiCol_ModalWindowDimBg]     = ColorToImVec4(t->ui.modal_dim);

    style.Colors[ImGuiCol_NavCursor]            = ColorToImVec4(t->ui.ui_accent);
    style.Colors[ImGuiCol_NavWindowingHighlight]= ColorToImVec4(t->ui.ui_accent);

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
    applied_scale = ui_scale;
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
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = true;
    font_cfg.MergeMode = false;
    font_cfg.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF(font_path, t->font.size * ui_scale, &font_cfg, NULL);

    /* merge FontAwesome icons */
    ImFontConfig icons_cfg;
    icons_cfg.MergeMode = true;
    icons_cfg.FontDataOwnedByAtlas = true;
    icons_cfg.PixelSnapH = true;
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    io.Fonts->AddFontFromMemoryCompressedTTF(
        fa_solid_900_compressed_data,
        fa_solid_900_compressed_size,
        t->font.icon_size * ui_scale,
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