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
 *   - ui     : a compact semantic palette; the full Dear ImGui color set is
 *              derived from it at apply time (hover/active states are mixed
 *              from the base surface toward the theme's text color)
 *   - style  : ImGui style variables (rounding, padding, border sizes, ...)
 *   - font   : the UI font file and its sizes
 *   - textures: per-theme texture file names (with fallback to default)
 *   - meta   : display metadata (name, author, description)
 *
 * The struct is intentionally flat and POD so it can be default-initialised
 * with `= {0}` and overlaid by the JSON parser.
 */

/** mix ratio applied to derive a hovered state from a base color */
#define THEME_STATE_HOVER 0.10f
/** mix ratio applied to derive an active/pressed state from a base color */
#define THEME_STATE_ACTIVE 0.18f

/** number of colors available for multi-satellite ground tracks */
#define GROUND_TRACK_PALETTE_SIZE 8

/** compact semantic UI palette (11 colors) */
typedef struct
{
    Color text;     /* primary text */
    Color text_dim; /* secondary / disabled text */
    Color bg;       /* window, panel and overlay background */
    Color surface;  /* inputs, buttons, tabs, headers, titlebar */
    Color border;   /* window borders and separators */
    Color accent;   /* selection, highlight, slider, check mark, plots */
    Color overlay;  /* modal + docking dimming (alpha matters) */

    /* notification toast accents */
    Color info;
    Color success;
    Color warning;
    Color error;
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
        Color bg;               /* scene clear color */
        Color orbit;            /* normal orbit path (alpha allowed) */
        Color orbit_active;     /* highlighted/active orbit path */
        Color sat;              /* normal satellite marker */
        Color sat_hover;        /* hovered satellite marker */
        Color sat_selected;     /* selected satellite marker */
        Color periapsis;        /* periapsis marker */
        Color apoapsis;         /* apoapsis marker */
        Color footprint_fill;   /* coverage footprint fill (alpha allowed) */
        Color footprint_border; /* coverage footprint outline */
    } world;

    struct
    {
        Color palette[GROUND_TRACK_PALETTE_SIZE]; /* Multi track colors */
    } ground_tracks;

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

/** linear interpolation between two colors (t clamped to [0,1]) */
Color ThemeMix(Color a, Color b, float t);

/** return `c` with its alpha replaced by `a` (a clamped to [0,1]) */
Color ThemeAlpha(Color c, float a);

/** hover state of `base`: mixed toward the text color (works on dark & light) */
static inline Color ThemeHoverOf(Color base, Color text)
{
    return ThemeMix(base, text, THEME_STATE_HOVER);
}

/** active/pressed state of `base`: mixed further toward the text color */
static inline Color ThemeActiveOf(Color base, Color text)
{
    return ThemeMix(base, text, THEME_STATE_ACTIVE);
}

/** fill the theme with hardcoded defaults (safety net before JSON load) */
void ThemeInitDefaults(Theme *t);

/**
 * Load `themes/<name>/theme.json` into `t`, overlaying on its current values.
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
