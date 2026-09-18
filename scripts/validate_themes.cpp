// Validate all themes/*/theme.json against the schema using nlohmann/json.
// Build: g++ -std=c++20 -Ilib -Ilib/nlohmann/include scripts/validate_themes.cpp -o build/validate_themes
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

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

static void check_keys(const nlohmann::json &root, const char *section, const std::vector<const char *> &keys)
{
    auto sec_it = root.find(section);
    if (sec_it == root.end() || !sec_it->is_object())
    {
        printf("  FAIL: missing section '%s'\n", section);
        g_failures++;
        return;
    }
    const nlohmann::json &sec = *sec_it;
    for (const char *k : keys)
    {
        auto item = sec.find(k);
        if (item == sec.end())
        {
            printf("  FAIL: missing '%s.%s'\n", section, k);
            g_failures++;
        }
        else if ((strcmp(section, "world") == 0 || strcmp(section, "ui") == 0) && item->is_string())
        {
            const char *val = item->get_ref<const std::string &>().c_str();
            if (!is_color(val))
            {
                printf("  FAIL: '%s.%s' is not #RRGGBBAA: '%s'\n", section, k, val);
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

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const std::exception &e)
    {
        printf("FAIL %s: parse error: %s\n", path, e.what());
        g_failures++;
        return;
    }

    int before = g_failures;
    check_keys(root, "world", {
        "bg", "orbit", "orbit_active", "sat", "sat_hover", "sat_selected",
        "periapsis", "apoapsis", "footprint_fill", "footprint_border"});
    check_keys(root, "ui", {
        "text", "text_dim", "bg", "surface", "border", "accent", "overlay",
        "info", "success", "warning", "error"});
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

    auto meta_it = root.find("meta");
    if (meta_it == root.end())
    {
        printf("  FAIL: missing section 'meta'\n");
        g_failures++;
    }

    printf("%s\n", g_failures == before ? "OK" : "FAIL");
}

int main(int argc, char **argv)
{
    const char *files[] = {
        "themes/default/theme.json",
        "themes/midnight/theme.json",
        "themes/amber/theme.json",
        "themes/daylight/theme.json",
        "themes/paper/theme.json",
        "themes/girlypop/theme.json",
    };
    int count = 6;
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