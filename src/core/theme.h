#ifndef THEME_H
#define THEME_H

#include "types.h"

/**
 * @file theme.h
 * @brief Theme definition and loading
 *
 * A theme is a self-contained visual identity for TLEscope. It is loaded
 * from `themes/<name>/theme.json` and covers:
 *   - world  : colors used for 3D/2D raylib scene drawing (Earth, orbits, sats)
 *   - ui     : colors used for the Dear ImGui windows and raylib UI overlays
 *   - style  : ImGui style variables (rounding, padding, border sizes, ...)
 *   - font   : the UI font file and its sizes
 *   - textures: per-theme texture file names (with fallback to default)
 *   - meta   : display metadata (name, author, description)
 *
 * The struct is intentionally flat and POD so it can be default-initialised
 * with `= {0}` and overlaid by the JSON parser.
 */

/** semantic + full ImGui color palette */
typedef struct
{
    /* semantic colors (used for raylib UI overlays and ImGui accents) */
    Color text_main;
    Color text_secondary;
    Color ui_bg;
    Color ui_primary;
    Color ui_secondary;
    Color ui_accent;
    Color window_border;
    Color window_border_focus;

    /* full ImGui palette (maps 1:1 to ImGuiCol_*) */
    Color window_bg;
    Color titlebar;
    Color titlebar_active;
    Color titlebar_collapsed;
    Color frame_bg;
    Color frame_bg_hovered;
    Color frame_bg_active;
    Color button;
    Color button_hovered;
    Color button_active;
    Color header;
    Color header_hovered;
    Color header_active;
    Color tab;
    Color tab_hovered;
    Color tab_active;
    Color tab_unfocused;
    Color tab_unfocused_active;
    Color scrollbar_bg;
    Color scrollbar_grab;
    Color scrollbar_grab_hovered;
    Color scrollbar_grab_active;
    Color separator;
    Color separator_hovered;
    Color separator_active;
    Color check_mark;
    Color slider_grab;
    Color slider_grab_active;
    Color text_selected_bg;
    Color modal_dim;
    Color plot_histogram;
    Color plot_lines;
    Color resize_grip;
    Color docking_bg;
    Color docking_preview;
} ThemeUIColors;

/** ImGui style variables */
typedef struct
{
    float window_rounding;
    float frame_rounding;
    float child_rounding;
    float popup_rounding;
    float grab_rounding;
    float scrollbar_rounding;
    float tab_rounding;
    float window_border_size;
    float frame_border_size;
    float popup_border_size;
    float window_padding_x;
    float window_padding_y;
    float frame_padding_x;
    float frame_padding_y;
    float item_spacing_x;
    float item_spacing_y;
    float item_inner_spacing_x;
    float item_inner_spacing_y;
    float scrollbar_size;
    float grab_min_size;
    float window_title_align_x;
    float button_text_align_x;
    float indent_spacing;
    float columns_min_spacing;
} ThemeStyle;

/** font configuration */
typedef struct
{
    char file[128];    /* relative to theme dir, e.g. "font.ttf" */
    float size;        /* ImGui base font size (px) */
    float icon_size;   /* FontAwesome icon size (px) */
    float raylib_size; /* raylib customFont size (px) */
} ThemeFont;

/** per-theme texture file names (relative to theme dir) */
typedef struct
{
    char earth[128];
    char earth_night[128];
    char clouds[128];
    char skybox[128];
    char moon[128];
    char sat_icon[128];
    char marker_icon[128];
    char smallmark[128];
} ThemeTextures;

/** the complete theme */
typedef struct
{
    /* metadata */
    char name[64];
    char display_name[64];
    char author[64];
    char description[128];

    /* world colors (3D/2D raylib scene) */
    struct
    {
        Color bg;
        Color orbit_normal;
        Color orbit_highlighted;
        Color sat_normal;
        Color sat_highlighted;
        Color sat_selected;
        Color periapsis;
        Color apoapsis;
        Color footprint_bg;
        Color footprint_border;
        Color scope_bg;
        Color scope_horizon;
        Color overlay_dim;
    } world;

    ThemeUIColors ui;
    ThemeStyle style;
    ThemeFont font;
    ThemeTextures textures;
} Theme;

/** result of scanning the themes/ directory */
typedef struct
{
    char names[2048]; /* \0-separated theme names, double-null terminated */
    int count;
} ThemeList;

/* global theme instance (populated by ThemeLoad) */
extern Theme g_theme;

/** fill the theme with hardcoded defaults (safety net before JSON load) */
void ThemeInitDefaults(Theme *t);

/**
 * Load `themes/<name>/theme.json` into `t`, overlaying on its current values.
 * Supports the nested schema and falls back to legacy flat keys.
 * Returns true on success (even if the file is missing, defaults remain).
 */
bool ThemeLoad(const char *theme_name, Theme *t);

/**
 * Resolve an asset path: `themes/<current>/<file>` if it exists, otherwise
 * `themes/default/<file>`. Returns a pointer to a static buffer.
 */
const char *ThemeAssetPath(const char *file);

/** scan themes/ for directories containing theme.json; fills `list` */
bool ThemeDiscover(ThemeList *list);

#endif /* THEME_H */