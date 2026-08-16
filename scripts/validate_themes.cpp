// Validate all themes/*/theme.json against the schema using the actual cJSON parser.
// Build: g++ -std=c++20 -Ilib/cjson scripts/validate_themes.cpp lib/cjson/cJSON.c -o build/validate_themes
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "cJSON.h"

static int g_failures = 0;

static bool is_color(const char *s)
{
    if (!s || s[0] != '#' || strlen(s) != 9)
        return false;
    for (int i = 1; s[i]; i++)
    {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex)
            return false;
    }
    return true;
}

static void check_keys(cJSON *root, const char *section, const std::vector<const char *> &keys)
{
    cJSON *sec = cJSON_GetObjectItem(root, section);
    if (!sec)
    {
        printf("  FAIL: missing section '%s'\n", section);
        g_failures++;
        return;
    }
    for (const char *k : keys)
    {
        cJSON *item = cJSON_GetObjectItem(sec, k);
        if (!item)
        {
            printf("  FAIL: missing '%s.%s'\n", section, k);
            g_failures++;
        }
        else if ((strcmp(section, "world") == 0 || strcmp(section, "ui") == 0) && cJSON_IsString(item))
        {
            if (!is_color(item->valuestring))
            {
                printf("  FAIL: '%s.%s' is not #RRGGBBAA: '%s'\n", section, k, item->valuestring);
                g_failures++;
            }
        }
    }
}

static void validate(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        printf("FAIL: cannot open %s\n", path);
        g_failures++;
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string text(sz, '\0');
    fread(&text[0], 1, (size_t)sz, f);
    fclose(f);

    cJSON *root = cJSON_Parse(text.c_str());
    if (!root)
    {
        printf("FAIL %s: parse error at %s\n", path, cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "?");
        g_failures++;
        return;
    }

    int before = g_failures;
    check_keys(root, "world", {
        "bg_color", "orbit_normal", "orbit_highlighted", "sat_normal",
        "sat_highlighted", "sat_selected", "periapsis", "apoapsis",
        "footprint_bg", "footprint_border", "scope_bg", "scope_horizon", "overlay_dim"});
    check_keys(root, "ui", {
        "text_main", "text_secondary", "ui_bg", "ui_primary", "ui_secondary",
        "ui_accent", "window_border", "window_border_focus", "window_bg",
        "titlebar", "titlebar_active", "titlebar_collapsed", "frame_bg",
        "frame_bg_hovered", "frame_bg_active", "button", "button_hovered",
        "button_active", "header", "header_hovered", "header_active", "tab",
        "tab_hovered", "tab_active", "tab_unfocused", "tab_unfocused_active",
        "scrollbar_bg", "scrollbar_grab", "scrollbar_grab_hovered",
        "scrollbar_grab_active", "separator", "separator_hovered",
        "separator_active", "check_mark", "slider_grab", "slider_grab_active",
        "text_selected_bg", "modal_dim", "plot_histogram", "plot_lines",
        "resize_grip", "docking_bg", "docking_preview"});
    check_keys(root, "style", {
        "window_rounding", "frame_rounding", "child_rounding", "popup_rounding",
        "grab_rounding", "scrollbar_rounding", "tab_rounding",
        "window_border_size", "frame_border_size", "popup_border_size",
        "window_padding_x", "window_padding_y", "frame_padding_x", "frame_padding_y",
        "item_spacing_x", "item_spacing_y", "item_inner_spacing_x", "item_inner_spacing_y",
        "scrollbar_size", "grab_min_size", "window_title_align_x",
        "button_text_align_x", "indent_spacing", "columns_min_spacing"});
    check_keys(root, "font", {"file", "size", "icon_size", "raylib_size"});
    check_keys(root, "textures", {
        "earth", "earth_night", "clouds", "skybox", "moon",
        "sat_icon", "marker_icon", "smallmark"});

    cJSON *meta = cJSON_GetObjectItem(root, "meta");
    if (!meta)
    {
        printf("  FAIL: missing section 'meta'\n");
        g_failures++;
    }

    printf("%s\n", g_failures == before ? "OK" : "FAIL");
    cJSON_Delete(root);
}

int main(int argc, char **argv)
{
    const char *files[] = {
        "themes/default/theme.json",
        "themes/girlypop/theme.json",
        "themes/trans-test/theme.json",
    };
    int count = 3;
    if (argc > 1)
    {
        count = argc - 1;
    }
    for (int i = 0; i < count; i++)
    {
        const char *path = (argc > 1) ? argv[i + 1] : files[i];
        printf("== %s\n", path);
        validate(path);
    }
    printf(g_failures ? "%d FAILURES\n" : "ALL THEMES VALID\n", g_failures);
    return g_failures ? 1 : 0;
}
