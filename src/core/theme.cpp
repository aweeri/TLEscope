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
/* color helpers                                                       */
/* ------------------------------------------------------------------ */

Color ThemeMix(Color a, Color b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (unsigned char)(a.r + ((float)b.r - (float)a.r) * t),
        (unsigned char)(a.g + ((float)b.g - (float)a.g) * t),
        (unsigned char)(a.b + ((float)b.b - (float)a.b) * t),
        (unsigned char)(a.a + ((float)b.a - (float)a.a) * t)};
}

Color ThemeAlpha(Color c, float a)
{
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    c.a = (unsigned char)(a * 255.0f);
    return c;
}

/* ------------------------------------------------------------------ */
/* parsing helpers                                                     */
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

/** parse a color key from a JSON section, falling back to `fallback` */
static void ParseColorField(cJSON *section, const char *key, Color *dst, Color fallback)
{
    cJSON *item = section ? cJSON_GetObjectItem(section, key) : NULL;
    if (cJSON_IsString(item) && item->valuestring)
        *dst = ParseHexColor(item->valuestring, fallback);
    else
        *dst = fallback;
}

/** parse a float key from a JSON section */
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
    t->world.bg               = ParseHexColor("#101010FF", BLACK);
    t->world.orbit            = ParseHexColor("#D3D3D326", WHITE);
    t->world.orbit_active     = ParseHexColor("#FFFFFFFF", WHITE);
    t->world.sat              = ParseHexColor("#FFFFFFAA", WHITE);
    t->world.sat_hover        = ParseHexColor("#FFFF00FF", YELLOW);
    t->world.sat_selected     = ParseHexColor("#00FF00FF", GREEN);
    t->world.periapsis        = ParseHexColor("#87CEEBFF", SKYBLUE);
    t->world.apoapsis         = ParseHexColor("#FFA500FF", ORANGE);
    t->world.footprint_fill   = ParseHexColor("#FFFFFF22", WHITE);
    t->world.footprint_border = ParseHexColor("#FFFFFF88", WHITE);

    /* compact semantic UI palette (match themes/default/theme.json) */
    t->ui.text    = ParseHexColor("#FFFFFFFF", WHITE);
    t->ui.text_dim = ParseHexColor("#D3D3D3FF", LIGHTGRAY);
    t->ui.bg      = ParseHexColor("#1E1E1EFF", DARKGRAY);
    t->ui.surface = ParseHexColor("#2E2E2EFF", DARKGRAY);
    t->ui.border  = ParseHexColor("#4A4A4AFF", GRAY);
    t->ui.accent  = ParseHexColor("#66FF66FF", GREEN);
    t->ui.overlay = ParseHexColor("#00000080", BLACK);

    /* notification toast accents */
    t->ui.info    = ParseHexColor("#66CCFFFF", SKYBLUE);
    t->ui.success = ParseHexColor("#66FF66FF", GREEN);
    t->ui.warning = ParseHexColor("#FFAA00FF", ORANGE);
    t->ui.error   = ParseHexColor("#FF5555FF", RED);

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
    ParseColorField(world, "bg", &t->world.bg, t->world.bg);
    ParseColorField(world, "orbit", &t->world.orbit, t->world.orbit);
    ParseColorField(world, "orbit_active", &t->world.orbit_active, t->world.orbit_active);
    ParseColorField(world, "sat", &t->world.sat, t->world.sat);
    ParseColorField(world, "sat_hover", &t->world.sat_hover, t->world.sat_hover);
    ParseColorField(world, "sat_selected", &t->world.sat_selected, t->world.sat_selected);
    ParseColorField(world, "periapsis", &t->world.periapsis, t->world.periapsis);
    ParseColorField(world, "apoapsis", &t->world.apoapsis, t->world.apoapsis);
    ParseColorField(world, "footprint_fill", &t->world.footprint_fill, t->world.footprint_fill);
    ParseColorField(world, "footprint_border", &t->world.footprint_border, t->world.footprint_border);

    /* compact UI palette */
    ParseColorField(ui, "text", &t->ui.text, t->ui.text);
    ParseColorField(ui, "text_dim", &t->ui.text_dim, t->ui.text_dim);
    ParseColorField(ui, "bg", &t->ui.bg, t->ui.bg);
    ParseColorField(ui, "surface", &t->ui.surface, t->ui.surface);
    ParseColorField(ui, "border", &t->ui.border, t->ui.border);
    ParseColorField(ui, "accent", &t->ui.accent, t->ui.accent);
    ParseColorField(ui, "overlay", &t->ui.overlay, t->ui.overlay);
    ParseColorField(ui, "info", &t->ui.info, t->ui.info);
    ParseColorField(ui, "success", &t->ui.success, t->ui.success);
    ParseColorField(ui, "warning", &t->ui.warning, t->ui.warning);
    ParseColorField(ui, "error", &t->ui.error, t->ui.error);

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
