# TLEscope Themes

TLEscope themes are self-contained visual identities stored in `themes/<name>/`.
Each theme is a directory containing a `theme.json` descriptor plus optional
asset files (fonts, textures).

The active theme is selected in **Settings → Theme** and persisted in
`settings.json`. The app discovers available themes by scanning the `themes/`
directory, so **adding a theme is as simple as dropping a new folder**.

```
themes/
├── default/          # built-in fallback theme (always present)
│   ├── theme.json
│   ├── font.ttf
│   ├── earth.png
│   └── ...
├── girlypop/
│   ├── theme.json
│   └── font.ttf
└── trans-test/
    └── theme.json
```

---

## Creating a theme

1. Create a directory under `themes/` named after your theme (lowercase, no spaces).
2. Add a `theme.json` file — the only required file.
3. Optionally add a custom `font.ttf` and/or any of the texture files (see below).
4. Any asset you do not provide falls back to the `default` theme's copy.

> The theme folder name is the theme ID shown in the UI (unless `meta.display_name` is set).

---

## theme.json format

```jsonc
{
    // ── Metadata ────────────────────────────────────────────────
    "meta": {
        "name": "default",          // theme id (matches folder name)
        "display_name": "Default",  // label shown in the settings combo
        "author": "TLEscope",
        "description": "Default TLEscope dark theme"
    },

    // ── World colors: 3D/2D scene drawing (Earth, orbits, sats) ──
    "world": {
        "bg_color":           "#101010FF",  // clear color
        "orbit_normal":       "#D3D3D326",  // normal orbit path (alpha allowed)
        "orbit_highlighted":  "#FFFFFFFF",  // active/hovered orbit path
        "sat_normal":         "#FFFFFFAA",  // unselected satellite marker
        "sat_highlighted":    "#FFFF00FF",  // hovered satellite marker
        "sat_selected":       "#00FF00FF",  // selected satellite marker
        "periapsis":          "#87CEEBFF",  // periapsis marker
        "apoapsis":           "#FFA500FF",  // apoapsis marker
        "footprint_bg":       "#FFFFFF22",  // coverage footprint fill
        "footprint_border":   "#FFFFFF88",  // coverage footprint outline
        "scope_bg":           "#0A0F19FF",  // scope window background
        "scope_horizon":      "#2D1E14FF",  // scope horizon line
        "overlay_dim":        "#000000B4"   // modal overlay dimming
    },

    // ── UI colors: semantic + Dear ImGui palette ────────────────
    "ui": {
        "text_main":            "#FFFFFFFF",  // primary text
        "text_secondary":       "#D3D3D3FF",  // secondary/dimmed text
        "ui_bg":                "#000000CC",  // UI overlay background
        "ui_primary":           "#202020FF",  // primary surface
        "ui_secondary":         "#404040FF",  // secondary surface
        "ui_accent":            "#66FF66FF",  // accent (buttons, highlights)
        "window_border":        "#4A4A4AFF",  // unfocused window border
        "window_border_focus":  "#66FF66FF",  // focused window border

        // full ImGui palette (maps to ImGuiCol_*)
        "window_bg":            "#1E1E1EFF",  // window background
        "titlebar":             "#202020FF",  // unfocused title bar
        "titlebar_active":      "#252525FF",  // focused title bar
        "titlebar_collapsed":   "#1A1A1AFF",
        "frame_bg":             "#2E2E2EFF",  // input frame background
        "frame_bg_hovered":     "#383838FF",
        "frame_bg_active":      "#404040FF",
        "button":               "#303030FF",
        "button_hovered":       "#3A3A3AFF",
        "button_active":        "#404040FF",
        "header":               "#2E2E2EFF",  // collapsing headers, menu
        "header_hovered":       "#383838FF",
        "header_active":        "#404040FF",
        "tab":                  "#282828FF",
        "tab_hovered":          "#323232FF",
        "tab_active":           "#353535FF",
        "tab_unfocused":        "#222222FF",
        "tab_unfocused_active": "#2A2A2AFF",
        "scrollbar_bg":         "#1A1A1AFF",
        "scrollbar_grab":       "#4A4A4AFF",
        "scrollbar_grab_hovered": "#555555FF",
        "scrollbar_grab_active":  "#606060FF",
        "separator":            "#3A3A3AFF",
        "separator_hovered":    "#4A4A4AFF",
        "separator_active":     "#5A5A5AFF",
        "check_mark":           "#66FF66FF",  // checkbox tick
        "slider_grab":          "#66FF66FF",  // slider handle
        "slider_grab_active":   "#88FF88FF",
        "text_selected_bg":     "#66FF6633",  // text selection highlight
        "modal_dim":            "#00000080",  // modal window dimming
        "plot_histogram":       "#66FF66FF",  // plots
        "plot_lines":           "#66FF66FF",
        "resize_grip":          "#66FF6633",  // window resize corner
        "docking_bg":           "#000000BB",  // dock space background
        "docking_preview":      "#66FF6688"   // dock preview highlight
    },

    // ── Style: ImGui style variables ────────────────────────────
    "style": {
        "window_rounding":    6.0,  // rounded corners: windows
        "frame_rounding":     4.0,  // rounded corners: buttons/inputs
        "child_rounding":     4.0,  // rounded corners: child windows
        "popup_rounding":     4.0,  // rounded corners: popups/tooltips
        "grab_rounding":      3.0,  // rounded corners: slider grabs
        "scrollbar_rounding": 3.0,  // rounded corners: scrollbars
        "tab_rounding":       3.0,  // rounded corners: tabs

        "window_border_size": 1.0,  // window border thickness
        "frame_border_size":  0.0,  // frame border thickness
        "popup_border_size":  1.0,  // popup border thickness

        "window_padding_x":   10.0, // window inner padding
        "window_padding_y":   10.0,
        "frame_padding_x":    6.0,  // button/input inner padding
        "frame_padding_y":    4.0,
        "item_spacing_x":     8.0,  // spacing between widgets
        "item_spacing_y":     6.0,
        "item_inner_spacing_x": 6.0, // spacing inside composite widgets
        "item_inner_spacing_y": 6.0,
        "scrollbar_size":     14.0, // scrollbar thickness
        "grab_min_size":      10.0, // minimum slider grab size

        "window_title_align_x": 0.5, // 0.0 left, 0.5 center, 1.0 right
        "button_text_align_x":  0.5,
        "indent_spacing":       20.0,
        "columns_min_spacing":  6.0
    },

    // ── Fonts ────────────────────────────────────────────────────
    "font": {
        "file":        "font.ttf",  // TTF/OTF in the theme dir (falls back to default)
        "size":        16.0,        // ImGui base font size in pixels
        "icon_size":   14.0,        // FontAwesome icon size in pixels
        "raylib_size": 64.0         // raylib customFont size (3D label rendering)
    },

    // ── Textures: per-theme asset overrides (fall back to default) ──
    "textures": {
        "earth":        "earth.png",        // day Earth texture
        "earth_night":  "earth_night.png",  // night Earth texture
        "clouds":       "clouds.png",       // cloud layer texture
        "skybox":       "skybox.png",       // skybox texture
        "moon":         "moon.png",         // Moon texture
        "sat_icon":     "sat_icon.png",     // satellite icon
        "marker_icon":  "marker_icon.png",  // ground marker icon
        "smallmark":    "smallmark.png"     // periapsis/apoapsis marker
    }
}
```

---

## Color format

Colors use 8-digit hex `#RRGGBBAA` (alpha in the last two digits).
All fields are optional — any key you omit inherits the **default theme's**
value, so minimal themes like the transparency test can override only a few
colors.

## Legacy (flat) format

For backward compatibility, old flat `theme.json` files that put the color
keys at the root level (e.g. `"bg_color": "#101010FF"` instead of inside
`world`) are still parsed. New themes should use the nested format above.

## Asset fallback

Asset paths (`font.file`, `textures.*`) are resolved against the active
theme directory first, then `themes/default/`. This means a theme can ship
only a `font.ttf` and reuse every default texture, or override just the
`earth.png`.
