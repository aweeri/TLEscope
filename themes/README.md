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
└── girlypop/
    ├── theme.json
    └── font.ttf
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

The color model is deliberately **compact**: a theme declares only the base
colors, and TLEscope derives every Dear ImGui hover/active shade from them.
There are **21 colors** in total — 10 `world` + 11 `ui`.

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
        "bg":               "#101010FF",  // clear color
        "orbit":            "#D3D3D326",  // normal orbit path (alpha allowed)
        "orbit_active":     "#FFFFFFFF",  // active/hovered orbit path
        "sat":              "#FFFFFFAA",  // unselected satellite marker
        "sat_hover":        "#FFFF00FF",  // hovered satellite marker
        "sat_selected":     "#00FF00FF",  // selected satellite marker
        "periapsis":        "#87CEEBFF",  // periapsis marker
        "apoapsis":         "#FFA500FF",  // apoapsis marker
        "footprint_fill":   "#FFFFFF22",  // coverage footprint fill
        "footprint_border": "#FFFFFF88"   // coverage footprint outline
    },

    // ── UI colors: compact semantic palette ─────────────────────
    "ui": {
        "text":     "#FFFFFFFF",  // primary text
        "text_dim": "#D3D3D3FF",  // secondary / disabled text
        "bg":       "#1E1E1EFF",  // window, panel and overlay background
        "surface":  "#2E2E2EFF",  // inputs, buttons, tabs, headers, titlebar
        "border":   "#4A4A4AFF",  // window borders and separators
        "accent":   "#66FF66FF",  // selection, highlight, slider, check, plots
        "overlay":  "#00000080",  // modal + docking dimming (alpha matters)

        // notification toast accents
        "info":     "#66CCFFFF",
        "success":  "#66FF66FF",
        "warning":  "#FFAA00FF",
        "error":    "#FF5555FF"
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

## How the UI palette is expanded

Only the base `ui` colors are stored. When a theme is applied,
[`ThemeApplyToImGui()`](../src/ui/imgui_theme.cpp:24) derives the full
`ImGuiCol_*` set:

| Base color | Derived ImGui colors |
|------------|----------------------|
| `text` | `Text`, `Drawer`/navigation highlights |
| `text_dim` | `TextDisabled` |
| `bg` | `WindowBg`, `ChildBg`, `PopupBg`, `TitleBg`, `TitleBgCollapsed`, `ScrollbarBg`, `TabUnfocused` |
| `surface` | `FrameBg`, `Button`, `Header`, `MenuBarBg`, `TitleBgActive`, `TabHovered`, `TabActive` |
| `border` | `Border`, `Separator` |
| `accent` | `CheckMark`, `SliderGrab`, `PlotLines`, `PlotHistogram`, `NavCursor`, `ResizeGrip`, docking preview |
| `overlay` | `ModalWindowDimBg`, `DockingBg` |

Interactive states are mixed from the base color **toward the theme's `text`
color**, so the same rule works on dark *and* light themes:

- hovered = `mix(base, text, 0.10)`
- active  = `mix(base, text, 0.18)`

This is why light themes (like `daylight`) automatically get *darker* hover
states while dark themes get *lighter* ones — no hand-tuned grey per theme.

## Color format

Colors use 8-digit hex `#RRGGBBAA` (alpha in the last two digits).
All fields are optional — any key you omit inherits the **default theme's**
value, so a theme can override only a few colors.

## Asset fallback

Asset paths (`font.file`, `textures.*`) are resolved against the active
theme directory first, then `themes/default/`. This means a theme can ship
only a `font.ttf` and reuse every default texture, or override just the
`earth.png`.
