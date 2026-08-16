#!/usr/bin/env python3
"""Validate all themes/*/theme.json files against the expected schema."""
import json
import sys

REQUIRED_SECTIONS = ["meta", "world", "ui", "style", "font", "textures"]

REQUIRED_KEYS = {
    "world": ["bg_color", "orbit_normal", "orbit_highlighted", "sat_normal",
              "sat_highlighted", "sat_selected", "periapsis", "apoapsis",
              "footprint_bg", "footprint_border", "scope_bg", "scope_horizon",
              "overlay_dim"],
    "ui": ["text_main", "text_secondary", "ui_bg", "ui_primary", "ui_secondary",
           "ui_accent", "window_border", "window_border_focus", "window_bg",
           "titlebar", "titlebar_active", "titlebar_collapsed", "frame_bg",
           "frame_bg_hovered", "frame_bg_active", "button", "button_hovered",
           "button_active", "header", "header_hovered", "header_active", "tab",
           "tab_hovered", "tab_active", "tab_unfocused", "tab_unfocused_active",
           "scrollbar_bg", "scrollbar_grab", "scrollbar_grab_hovered",
           "scrollbar_grab_active", "separator", "separator_hovered",
           "separator_active", "check_mark", "slider_grab", "slider_grab_active",
           "text_selected_bg", "modal_dim", "plot_histogram", "plot_lines",
           "resize_grip", "docking_bg", "docking_preview"],
    "style": ["window_rounding", "frame_rounding", "child_rounding",
              "popup_rounding", "grab_rounding", "scrollbar_rounding",
              "tab_rounding", "window_border_size", "frame_border_size",
              "popup_border_size", "window_padding_x", "window_padding_y",
              "frame_padding_x", "frame_padding_y", "item_spacing_x",
              "item_spacing_y", "item_inner_spacing_x", "item_inner_spacing_y",
              "scrollbar_size", "grab_min_size", "window_title_align_x",
              "button_text_align_x", "indent_spacing", "columns_min_spacing"],
    "font": ["file", "size", "icon_size", "raylib_size"],
    "textures": ["earth", "earth_night", "clouds", "skybox", "moon",
                 "sat_icon", "marker_icon", "smallmark"],
}

HEX_COLOR = "0123456789abcdefABCDEF"


def is_color(value):
    if not isinstance(value, str):
        return False
    if not value.startswith("#") or len(value) != 9:
        return False
    return all(c in HEX_COLOR for c in value[1:])


def main():
    import glob
    files = sorted(glob.glob("themes/*/theme.json"))
    if not files:
        print("No theme files found")
        return 1

    ok = True
    for path in files:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)

        problems = []

        for section in REQUIRED_SECTIONS:
            if section not in data:
                problems.append(f"missing section '{section}'")
                continue
            for key in REQUIRED_KEYS[section]:
                if key not in data[section]:
                    problems.append(f"missing '{section}.{key}'")

        # color format validation
        for section, keys in REQUIRED_KEYS.items():
            sec = data.get(section)
            if not sec:
                continue
            if section in ("world", "ui"):
                for key in keys:
                    if key in sec and not is_color(sec[key]):
                        problems.append(f"'{section}.{key}' is not a #RRGGBBAA color: {sec[key]!r}")

        if "meta" in data:
            meta = data["meta"]
            for key in ("name", "display_name", "author", "description"):
                if key not in meta:
                    problems.append(f"missing 'meta.{key}'")

        if problems:
            ok = False
            print(f"FAIL {path}:")
            for p in problems:
                print(f"  - {p}")
        else:
            print(f"OK   {path}")

    print("ALL THEMES VALID" if ok else "ERRORS FOUND")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
