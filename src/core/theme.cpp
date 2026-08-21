#include "theme.h"
#include "util/log.h"
#include "cJSON.h"

#include <raylib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* global theme instance */
Theme g_theme = {0};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/** parse "#RRGGBB" or "#RRGGBBAA" into a raylib Color, else fallback */
static Color ParseHexColor(const char *hexStr, Color fallback)
{
    if (!hexStr || hexStr[0] != '#')
        return fallback;
    unsigned int r = 0, g = 0, b = 0, a = 255;
    int len = (int)strlen(hexStr);
    if (len == 7)
        sscanf(hexStr, "#%02x%02x%02x", &r, &g, &b);
    else if (len >= 9)
        sscanf(hexStr, "#%02x%02x%02x%02x", &r, &g, &b, &a);
    else
        return fallback;
    return (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}

/**
 * Parse a color key from a JSON section, falling back to the same key at the
 * JSON root (legacy flat theme files) and finally to `fallback`.
 */
static void ParseColorField(cJSON *section, cJSON *root, const char *key,
                            Color *dst, Color fallback)
{
    cJSON *item = section ? cJSON_GetObjectItem(section, key) : NULL;
    if (!item && root)
        item = cJSON_GetObjectItem(root, key); /* legacy flat key fallback */
    if (cJSON_IsString(item) && item->valuestring)
        *dst = ParseHexColor(item->valuestring, fallback);
    else
        *dst = fallback;
}

/** parse a float key from a JSON section (no legacy fallback) */
static void ParseFloatField(cJSON *section, const char *key, float *dst, float fallback)
{
    cJSON *item = section ? cJSON_GetObjectItem(section, key) : NULL;
    if (cJSON_IsNumber(item))
        *dst = (float)item->valuedouble;
    else
        *dst = fallback;
}

/** parse a string key from a JSON section into a fixed buffer */
static void ParseStringField(cJSON *section, const char *key, char *dst, size_t dstSize,
                             const char *fallback)
{
    cJSON *item = section ? cJSON_GetObjectItem(section, key) : NULL;
    const char *value = cJSON_IsString(item) ? item->valuestring : fallback;
    if (value)
    {
        snprintf(dst, dstSize, "%s", value);
    }
}

/* ------------------------------------------------------------------ */
/* defaults                                                            */
/* ------------------------------------------------------------------ */

void ThemeInitDefaults(Theme *t)
{
    memset(t, 0, sizeof(Theme));

    snprintf(t->name, sizeof(t->name), "default");
    snprintf(t->display_name, sizeof(t->display_name), "Default");
    snprintf(t->author, sizeof(t->author), "TLEscope");
    snprintf(t->description, sizeof(t->description), "Default TLEscope dark theme");

    /* world colors (match themes/default/theme.json) */
    t->world.bg              = ParseHexColor("#101010FF", BLACK);
    t->world.orbit_normal    = ParseHexColor("#D3D3D326", WHITE);
    t->world.orbit_highlighted = ParseHexColor("#FFFFFFFF", WHITE);
    t->world.sat_normal      = ParseHexColor("#FFFFFFAA", WHITE);
    t->world.sat_highlighted = ParseHexColor("#FFFF00FF", YELLOW);
    t->world.sat_selected    = ParseHexColor("#00FF00FF", GREEN);
    t->world.periapsis       = ParseHexColor("#87CEEBFF", SKYBLUE);
    t->world.apoapsis        = ParseHexColor("#FFA500FF", ORANGE);
    t->world.footprint_bg    = ParseHexColor("#FFFFFF22", WHITE);
    t->world.footprint_border = ParseHexColor("#FFFFFF88", WHITE);
    t->world.scope_bg        = ParseHexColor("#0A0F19FF", BLACK);
    t->world.scope_horizon   = ParseHexColor("#2D1E14FF", BROWN);
    t->world.overlay_dim     = ParseHexColor("#000000B4", BLACK);

    /* semantic UI colors */
    t->ui.text_main          = ParseHexColor("#FFFFFFFF", WHITE);
    t->ui.text_secondary     = ParseHexColor("#D3D3D3FF", LIGHTGRAY);
    t->ui.ui_bg              = ParseHexColor("#000000CC", BLACK);
    t->ui.ui_primary         = ParseHexColor("#202020FF", DARKGRAY);
    t->ui.ui_secondary       = ParseHexColor("#404040FF", GRAY);
    t->ui.ui_accent          = ParseHexColor("#66FF66FF", GREEN);
    t->ui.window_border      = ParseHexColor("#4A4A4AFF", GRAY);
    t->ui.window_border_focus = ParseHexColor("#66FF66FF", GREEN);

    /* full ImGui palette (dark theme derived from the defaults above) */
    t->ui.window_bg           = ParseHexColor("#1E1E1EFF", DARKGRAY);
    t->ui.titlebar            = ParseHexColor("#202020FF", DARKGRAY);
    t->ui.titlebar_active     = ParseHexColor("#252525FF", DARKGRAY);
    t->ui.titlebar_collapsed  = ParseHexColor("#1A1A1AFF", DARKGRAY);
    t->ui.frame_bg            = ParseHexColor("#2E2E2EFF", DARKGRAY);
    t->ui.frame_bg_hovered    = ParseHexColor("#383838FF", DARKGRAY);
    t->ui.frame_bg_active     = ParseHexColor("#404040FF", GRAY);
    t->ui.button              = ParseHexColor("#303030FF", DARKGRAY);
    t->ui.button_hovered      = ParseHexColor("#3A3A3AFF", DARKGRAY);
    t->ui.button_active       = ParseHexColor("#404040FF", GRAY);
    t->ui.header              = ParseHexColor("#2E2E2EFF", DARKGRAY);
    t->ui.header_hovered      = ParseHexColor("#383838FF", DARKGRAY);
    t->ui.header_active       = ParseHexColor("#404040FF", GRAY);
    t->ui.tab                 = ParseHexColor("#282828FF", DARKGRAY);
    t->ui.tab_hovered         = ParseHexColor("#323232FF", DARKGRAY);
    t->ui.tab_active          = ParseHexColor("#353535FF", DARKGRAY);
    t->ui.tab_unfocused       = ParseHexColor("#222222FF", DARKGRAY);
    t->ui.tab_unfocused_active = ParseHexColor("#2A2A2AFF", DARKGRAY);
    t->ui.scrollbar_bg        = ParseHexColor("#1A1A1AFF", DARKGRAY);
    t->ui.scrollbar_grab      = ParseHexColor("#4A4A4AFF", GRAY);
    t->ui.scrollbar_grab_hovered = ParseHexColor("#555555FF", GRAY);
    t->ui.scrollbar_grab_active  = ParseHexColor("#606060FF", GRAY);
    t->ui.separator           = ParseHexColor("#3A3A3AFF", GRAY);
    t->ui.separator_hovered   = ParseHexColor("#4A4A4AFF", GRAY);
    t->ui.separator_active    = ParseHexColor("#5A5A5AFF", GRAY);
    t->ui.check_mark          = ParseHexColor("#66FF66FF", GREEN);
    t->ui.slider_grab         = ParseHexColor("#66FF66FF", GREEN);
    t->ui.slider_grab_active  = ParseHexColor("#88FF88FF", GREEN);
    t->ui.text_selected_bg    = ParseHexColor("#66FF6633", GREEN);
    t->ui.modal_dim           = ParseHexColor("#00000080", BLACK);
    t->ui.plot_histogram      = ParseHexColor("#66FF66FF", GREEN);
    t->ui.plot_lines          = ParseHexColor("#66FF66FF", GREEN);
    t->ui.resize_grip         = ParseHexColor("#66FF6633", GREEN);
    t->ui.docking_bg          = ParseHexColor("#000000BB", BLACK);
    t->ui.docking_preview     = ParseHexColor("#66FF6688", GREEN);

    /* style (mirrors the previous hardcoded ImGui style) */
    t->style.window_rounding    = 3.0f;
    t->style.frame_rounding     = 2.0f;
    t->style.child_rounding     = 3.0f;
    t->style.popup_rounding     = 3.0f;
    t->style.grab_rounding      = 2.0f;
    t->style.scrollbar_rounding = 2.0f;
    t->style.tab_rounding       = 2.0f;
    t->style.window_border_size = 1.0f;
    t->style.frame_border_size  = 0.0f;
    t->style.popup_border_size  = 1.0f;
    t->style.window_padding_x   = 10.0f;
    t->style.window_padding_y   = 10.0f;
    t->style.frame_padding_x    = 6.0f;
    t->style.frame_padding_y    = 4.0f;
    t->style.item_spacing_x     = 8.0f;
    t->style.item_spacing_y     = 6.0f;
    t->style.item_inner_spacing_x = 6.0f;
    t->style.item_inner_spacing_y = 6.0f;
    t->style.scrollbar_size     = 14.0f;
    t->style.grab_min_size      = 10.0f;
    t->style.window_title_align_x = 0.5f;
    t->style.button_text_align_x  = 0.5f;
    t->style.indent_spacing     = 20.0f;
    t->style.columns_min_spacing = 6.0f;

    /* font */
    snprintf(t->font.file, sizeof(t->font.file), "font.ttf");
    t->font.size       = 16.0f;
    t->font.icon_size  = 14.0f;
    t->font.raylib_size = 64.0f;

    /* textures */
    snprintf(t->textures.earth,        sizeof(t->textures.earth),        "earth.png");
    snprintf(t->textures.earth_night,  sizeof(t->textures.earth_night),  "earth_night.png");
    snprintf(t->textures.clouds,       sizeof(t->textures.clouds),       "clouds.png");
    snprintf(t->textures.skybox,       sizeof(t->textures.skybox),       "skybox.png");
    snprintf(t->textures.moon,         sizeof(t->textures.moon),         "moon.png");
    snprintf(t->textures.sat_icon,     sizeof(t->textures.sat_icon),     "sat_icon.png");
    snprintf(t->textures.marker_icon,  sizeof(t->textures.marker_icon),  "marker_icon.png");
    snprintf(t->textures.smallmark,    sizeof(t->textures.smallmark),    "smallmark.png");
}

/* ------------------------------------------------------------------ */
/* loading                                                             */
/* ------------------------------------------------------------------ */

bool ThemeLoad(const char *theme_name, Theme *t)
{
    snprintf(t->name, sizeof(t->name), "%s", theme_name ? theme_name : "default");

    char theme_path[256];
    snprintf(theme_path, sizeof(theme_path), "themes/%s/theme.json", t->name);

    if (!FileExists(theme_path))
    {
        LOG_WARN("Theme file not found: %s (using defaults)", theme_path);
        return true;
    }

    char *text = LoadFileText(theme_path);
    if (!text)
    {
        LOG_ERROR("Failed to read theme file: %s", theme_path);
        return true;
    }

    cJSON *root = cJSON_Parse(text);
    UnloadFileText(text);

    if (!root)
    {
        LOG_ERROR("Failed to parse theme JSON: %s", theme_path);
        return true;
    }

    cJSON *meta = cJSON_GetObjectItem(root, "meta");
    cJSON *world = cJSON_GetObjectItem(root, "world");
    cJSON *ui = cJSON_GetObjectItem(root, "ui");
    cJSON *style = cJSON_GetObjectItem(root, "style");
    cJSON *font = cJSON_GetObjectItem(root, "font");
    cJSON *textures = cJSON_GetObjectItem(root, "textures");

    /* meta */
    ParseStringField(meta, "name", t->name, sizeof(t->name), t->name);
    ParseStringField(meta, "display_name", t->display_name, sizeof(t->display_name), t->name);
    ParseStringField(meta, "author", t->author, sizeof(t->author), t->author);
    ParseStringField(meta, "description", t->description, sizeof(t->description), t->description);

    /* world colors */
    ParseColorField(world, root, "bg_color", &t->world.bg, t->world.bg);
    ParseColorField(world, root, "orbit_normal", &t->world.orbit_normal, t->world.orbit_normal);
    ParseColorField(world, root, "orbit_highlighted", &t->world.orbit_highlighted, t->world.orbit_highlighted);
    ParseColorField(world, root, "sat_normal", &t->world.sat_normal, t->world.sat_normal);
    ParseColorField(world, root, "sat_highlighted", &t->world.sat_highlighted, t->world.sat_highlighted);
    ParseColorField(world, root, "sat_selected", &t->world.sat_selected, t->world.sat_selected);
    ParseColorField(world, root, "periapsis", &t->world.periapsis, t->world.periapsis);
    ParseColorField(world, root, "apoapsis", &t->world.apoapsis, t->world.apoapsis);
    ParseColorField(world, root, "footprint_bg", &t->world.footprint_bg, t->world.footprint_bg);
    ParseColorField(world, root, "footprint_border", &t->world.footprint_border, t->world.footprint_border);
    ParseColorField(world, root, "scope_bg", &t->world.scope_bg, t->world.scope_bg);
    ParseColorField(world, root, "scope_horizon", &t->world.scope_horizon, t->world.scope_horizon);
    ParseColorField(world, root, "overlay_dim", &t->world.overlay_dim, t->world.overlay_dim);

    /* semantic + ImGui UI colors */
    ParseColorField(ui, root, "text_main", &t->ui.text_main, t->ui.text_main);
    ParseColorField(ui, root, "text_secondary", &t->ui.text_secondary, t->ui.text_secondary);
    ParseColorField(ui, root, "ui_bg", &t->ui.ui_bg, t->ui.ui_bg);
    ParseColorField(ui, root, "ui_primary", &t->ui.ui_primary, t->ui.ui_primary);
    ParseColorField(ui, root, "ui_secondary", &t->ui.ui_secondary, t->ui.ui_secondary);
    ParseColorField(ui, root, "ui_accent", &t->ui.ui_accent, t->ui.ui_accent);
    ParseColorField(ui, root, "window_border", &t->ui.window_border, t->ui.window_border);
    ParseColorField(ui, root, "window_border_focus", &t->ui.window_border_focus, t->ui.window_border_focus);

    ParseColorField(ui, NULL, "window_bg", &t->ui.window_bg, t->ui.window_bg);
    ParseColorField(ui, NULL, "titlebar", &t->ui.titlebar, t->ui.titlebar);
    ParseColorField(ui, NULL, "titlebar_active", &t->ui.titlebar_active, t->ui.titlebar_active);
    ParseColorField(ui, NULL, "titlebar_collapsed", &t->ui.titlebar_collapsed, t->ui.titlebar_collapsed);
    ParseColorField(ui, NULL, "frame_bg", &t->ui.frame_bg, t->ui.frame_bg);
    ParseColorField(ui, NULL, "frame_bg_hovered", &t->ui.frame_bg_hovered, t->ui.frame_bg_hovered);
    ParseColorField(ui, NULL, "frame_bg_active", &t->ui.frame_bg_active, t->ui.frame_bg_active);
    ParseColorField(ui, NULL, "button", &t->ui.button, t->ui.button);
    ParseColorField(ui, NULL, "button_hovered", &t->ui.button_hovered, t->ui.button_hovered);
    ParseColorField(ui, NULL, "button_active", &t->ui.button_active, t->ui.button_active);
    ParseColorField(ui, NULL, "header", &t->ui.header, t->ui.header);
    ParseColorField(ui, NULL, "header_hovered", &t->ui.header_hovered, t->ui.header_hovered);
    ParseColorField(ui, NULL, "header_active", &t->ui.header_active, t->ui.header_active);
    ParseColorField(ui, NULL, "tab", &t->ui.tab, t->ui.tab);
    ParseColorField(ui, NULL, "tab_hovered", &t->ui.tab_hovered, t->ui.tab_hovered);
    ParseColorField(ui, NULL, "tab_active", &t->ui.tab_active, t->ui.tab_active);
    ParseColorField(ui, NULL, "tab_unfocused", &t->ui.tab_unfocused, t->ui.tab_unfocused);
    ParseColorField(ui, NULL, "tab_unfocused_active", &t->ui.tab_unfocused_active, t->ui.tab_unfocused_active);
    ParseColorField(ui, NULL, "scrollbar_bg", &t->ui.scrollbar_bg, t->ui.scrollbar_bg);
    ParseColorField(ui, NULL, "scrollbar_grab", &t->ui.scrollbar_grab, t->ui.scrollbar_grab);
    ParseColorField(ui, NULL, "scrollbar_grab_hovered", &t->ui.scrollbar_grab_hovered, t->ui.scrollbar_grab_hovered);
    ParseColorField(ui, NULL, "scrollbar_grab_active", &t->ui.scrollbar_grab_active, t->ui.scrollbar_grab_active);
    ParseColorField(ui, NULL, "separator", &t->ui.separator, t->ui.separator);
    ParseColorField(ui, NULL, "separator_hovered", &t->ui.separator_hovered, t->ui.separator_hovered);
    ParseColorField(ui, NULL, "separator_active", &t->ui.separator_active, t->ui.separator_active);
    ParseColorField(ui, NULL, "check_mark", &t->ui.check_mark, t->ui.check_mark);
    ParseColorField(ui, NULL, "slider_grab", &t->ui.slider_grab, t->ui.slider_grab);
    ParseColorField(ui, NULL, "slider_grab_active", &t->ui.slider_grab_active, t->ui.slider_grab_active);
    ParseColorField(ui, NULL, "text_selected_bg", &t->ui.text_selected_bg, t->ui.text_selected_bg);
    ParseColorField(ui, NULL, "modal_dim", &t->ui.modal_dim, t->ui.modal_dim);
    ParseColorField(ui, NULL, "plot_histogram", &t->ui.plot_histogram, t->ui.plot_histogram);
    ParseColorField(ui, NULL, "plot_lines", &t->ui.plot_lines, t->ui.plot_lines);
    ParseColorField(ui, NULL, "resize_grip", &t->ui.resize_grip, t->ui.resize_grip);
    ParseColorField(ui, NULL, "docking_bg", &t->ui.docking_bg, t->ui.docking_bg);
    ParseColorField(ui, NULL, "docking_preview", &t->ui.docking_preview, t->ui.docking_preview);

    /* style */
    ParseFloatField(style, "window_rounding", &t->style.window_rounding, t->style.window_rounding);
    ParseFloatField(style, "frame_rounding", &t->style.frame_rounding, t->style.frame_rounding);
    ParseFloatField(style, "child_rounding", &t->style.child_rounding, t->style.child_rounding);
    ParseFloatField(style, "popup_rounding", &t->style.popup_rounding, t->style.popup_rounding);
    ParseFloatField(style, "grab_rounding", &t->style.grab_rounding, t->style.grab_rounding);
    ParseFloatField(style, "scrollbar_rounding", &t->style.scrollbar_rounding, t->style.scrollbar_rounding);
    ParseFloatField(style, "tab_rounding", &t->style.tab_rounding, t->style.tab_rounding);
    ParseFloatField(style, "window_border_size", &t->style.window_border_size, t->style.window_border_size);
    ParseFloatField(style, "frame_border_size", &t->style.frame_border_size, t->style.frame_border_size);
    ParseFloatField(style, "popup_border_size", &t->style.popup_border_size, t->style.popup_border_size);
    ParseFloatField(style, "window_padding_x", &t->style.window_padding_x, t->style.window_padding_x);
    ParseFloatField(style, "window_padding_y", &t->style.window_padding_y, t->style.window_padding_y);
    ParseFloatField(style, "frame_padding_x", &t->style.frame_padding_x, t->style.frame_padding_x);
    ParseFloatField(style, "frame_padding_y", &t->style.frame_padding_y, t->style.frame_padding_y);
    ParseFloatField(style, "item_spacing_x", &t->style.item_spacing_x, t->style.item_spacing_x);
    ParseFloatField(style, "item_spacing_y", &t->style.item_spacing_y, t->style.item_spacing_y);
    ParseFloatField(style, "item_inner_spacing_x", &t->style.item_inner_spacing_x, t->style.item_inner_spacing_x);
    ParseFloatField(style, "item_inner_spacing_y", &t->style.item_inner_spacing_y, t->style.item_inner_spacing_y);
    ParseFloatField(style, "scrollbar_size", &t->style.scrollbar_size, t->style.scrollbar_size);
    ParseFloatField(style, "grab_min_size", &t->style.grab_min_size, t->style.grab_min_size);
    ParseFloatField(style, "window_title_align_x", &t->style.window_title_align_x, t->style.window_title_align_x);
    ParseFloatField(style, "button_text_align_x", &t->style.button_text_align_x, t->style.button_text_align_x);
    ParseFloatField(style, "indent_spacing", &t->style.indent_spacing, t->style.indent_spacing);
    ParseFloatField(style, "columns_min_spacing", &t->style.columns_min_spacing, t->style.columns_min_spacing);

    /* font */
    ParseStringField(font, "file", t->font.file, sizeof(t->font.file), t->font.file);
    ParseFloatField(font, "size", &t->font.size, t->font.size);
    ParseFloatField(font, "icon_size", &t->font.icon_size, t->font.icon_size);
    ParseFloatField(font, "raylib_size", &t->font.raylib_size, t->font.raylib_size);

    /* textures */
    ParseStringField(textures, "earth", t->textures.earth, sizeof(t->textures.earth), t->textures.earth);
    ParseStringField(textures, "earth_night", t->textures.earth_night, sizeof(t->textures.earth_night), t->textures.earth_night);
    ParseStringField(textures, "clouds", t->textures.clouds, sizeof(t->textures.clouds), t->textures.clouds);
    ParseStringField(textures, "skybox", t->textures.skybox, sizeof(t->textures.skybox), t->textures.skybox);
    ParseStringField(textures, "moon", t->textures.moon, sizeof(t->textures.moon), t->textures.moon);
    ParseStringField(textures, "sat_icon", t->textures.sat_icon, sizeof(t->textures.sat_icon), t->textures.sat_icon);
    ParseStringField(textures, "marker_icon", t->textures.marker_icon, sizeof(t->textures.marker_icon), t->textures.marker_icon);
    ParseStringField(textures, "smallmark", t->textures.smallmark, sizeof(t->textures.smallmark), t->textures.smallmark);

    cJSON_Delete(root);
    LOG_INFO("Theme loaded: %s (%s)", t->name, t->display_name);
    return true;
}

/* ------------------------------------------------------------------ */
/* asset resolution                                                    */
/* ------------------------------------------------------------------ */

const char *ThemeAssetPath(const char *file)
{
    static char path[256];
    const char *theme = g_theme.name[0] ? g_theme.name : "default";

    snprintf(path, sizeof(path), "themes/%s/%s", theme, file);
    if (FileExists(path))
        return path;

    snprintf(path, sizeof(path), "themes/default/%s", file);
    return path;
}

/* ------------------------------------------------------------------ */
/* discovery                                                           */
/* ------------------------------------------------------------------ */

static int ThemeNameCompare(const char *a, const char *b)
{
    return strcmp(a, b);
}

bool ThemeDiscover(ThemeList *list)
{
    if (!list)
        return false;

    list->count = 0;
    list->names[0] = '\0';
    list->names[1] = '\0';

    /* LoadDirectoryFiles returns both files and directories (no filter).
     * NOTE: LoadDirectoryFilesEx's filter is a file-extension filter, not a
     * directory-type filter, so it cannot be used to select directories. */
    FilePathList dirs = LoadDirectoryFiles("themes");
    if (dirs.count == 0)
    {
        UnloadDirectoryFiles(dirs);
        /* fall back to at least "default" so the UI is usable */
        snprintf(list->names, sizeof(list->names), "default");
        list->count = 1;
        return true;
    }

    char sorted[64][64];
    int n = 0;

    for (unsigned int i = 0; i < dirs.count && n < 64; i++)
    {
        const char *path = dirs.paths[i];
        if (!path)
            continue;
        /* only directories that contain a theme.json are themes */
        if (!DirectoryExists(path))
            continue;
        char check[256];
        snprintf(check, sizeof(check), "%s/theme.json", path);
        if (!FileExists(check))
            continue;

        const char *name = GetFileName(path);
        if (!name || name[0] == '\0')
            continue;
        snprintf(sorted[n], sizeof(sorted[n]), "%s", name);
        n++;
    }

    UnloadDirectoryFiles(dirs);

    /* simple insertion sort (stable, few entries) */
    for (int i = 1; i < n; i++)
    {
        char key[64];
        snprintf(key, sizeof(key), "%s", sorted[i]);
        int j = i - 1;
        while (j >= 0 && ThemeNameCompare(sorted[j], key) > 0)
        {
            snprintf(sorted[j + 1], sizeof(sorted[j + 1]), "%s", sorted[j]);
            j--;
        }
        snprintf(sorted[j + 1], sizeof(sorted[j + 1]), "%s", key);
    }

    /* build \0-separated list */
    int offset = 0;
    for (int i = 0; i < n; i++)
    {
        int len = (int)strlen(sorted[i]) + 1;
        if (offset + len >= (int)sizeof(list->names))
            break;
        memcpy(list->names + offset, sorted[i], (size_t)len);
        offset += len;
        list->count++;
    }
    if (offset + 1 < (int)sizeof(list->names))
        list->names[offset] = '\0'; /* double-null terminate */

    return list->count > 0;
}