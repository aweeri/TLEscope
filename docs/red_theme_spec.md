# TLEscope Theme Specification — "Monochromatic Single-Channel Red" (Night / Astronomy Red-Light Mode)

Status: **Specification only** — no theme files or source code have been created or modified.
Author: Architect investigation
Scope: Add a new built-in theme that is strictly monochromatic red (only the red channel is
non-zero), very dim, and intended for dark-adapted nighttime use.

---

## 1. How themes work in TLEscope (findings from the code)

### 1.1 Loading

- A theme is loaded from `themes/<name>/theme.json` by
  [`ThemeLoad()`](src/core/theme.cpp:201). The path is built as
  `snprintf(theme_path, ..., "themes/%s/theme.json", t->name)` at
  [`theme.cpp:206`](src/core/theme.cpp:206).
- The JSON is parsed with cJSON. The parser reads six top-level sections:
  `meta`, `world`, `ui`, `style`, `font`, `textures`
  ([`theme.cpp:230-235`](src/core/theme.cpp:230)).
- Colors are parsed by [`ParseHexColor()`](src/core/theme.cpp:18), which accepts
  `#RRGGBB` (7 chars) **or** `#RRGGBBAA` (9 chars). The validator, however,
  **requires the 9-character `#RRGGBBAA` form** (see §6), so the new theme must
  use 8 hex digits for every color.
- Missing keys fall back to the hardcoded defaults in
  [`ThemeInitDefaults()`](src/core/theme.cpp:75), which mirror
  `themes/default/theme.json`.

### 1.2 Discovery / registration — **no code changes required**

- Themes are **auto-discovered by folder scan**, not registered in code.
  [`ThemeDiscover()`](src/core/theme.cpp:385) calls `LoadDirectoryFiles("themes")`
  and keeps every **directory that contains a `theme.json`**
  ([`theme.cpp:415-421`](src/core/theme.cpp:415)). The folder name is used as the
  theme id via `GetFileName(path)` ([`theme.cpp:423`](src/core/theme.cpp:423)).
- The Settings UI populates its combo box from this scan
  ([`ui_layout.cpp:1078-1101`](src/ui/ui_layout.cpp:1078)). Selecting an entry
  writes the folder name into `cfg->theme` and sets `cfg->reload_theme = true`
  ([`ui_layout.cpp:1102-1110`](src/ui/ui_layout.cpp:1102)).
- `main.cpp` reacts to `reload_theme` by re-running `ThemeLoad`, rebuilding fonts
  and reloading textures ([`main.cpp:631-695`](src/main.cpp:631)).
- The persisted setting is the plain string `"theme"` in `settings.json`
  (written at [`config.cpp:862`](src/core/config.cpp:862), read at
  [`config.cpp:153-165`](src/core/config.cpp:153)).

> **Consequence:** adding `themes/red/theme.json` is sufficient. It will appear in
> the Theme dropdown automatically. **No enum, no registry, no config default, and
> no UI list change is needed.**

> **Note on display name:** the dropdown shows the **folder name** (e.g. `red`),
> not `meta.display_name`. `display_name` is only used in log output
> ([`theme.cpp:355`](src/core/theme.cpp:355)). Keep the folder name short and
> lowercase.

### 1.3 Asset inheritance — **theme.json alone is sufficient**

- [`ThemeAssetPath()`](src/core/theme.cpp:363) resolves every asset as
  `themes/<current>/<file>` and, if that file does not exist, falls back to
  `themes/default/<file>` ([`theme.cpp:368-373`](src/core/theme.cpp:368)).
- This applies to the font (`t->font.file`) and all textures
  (`earth`, `earth_night`, `clouds`, `skybox`, `moon`, `sat_icon`, `marker_icon`,
  `smallmark`), which are loaded through `ThemeAssetPath(...)` in
  [`main.cpp:411`](src/main.cpp:411) and [`main.cpp:430-551`](src/main.cpp:430).
- This is confirmed empirically: `themes/amber/` and `themes/daylight/` contain
  **only `theme.json`** and inherit all PNGs and `font.ttf` from `themes/default/`.

> **Consequence:** the red theme needs **only** `themes/red/theme.json`. No image,
> font, or skybox files must be created or copied.

---

## 2. Required folder path, filename, and identity

| Item | Value |
|------|-------|
| Folder | `themes/red/` |
| File | `themes/red/theme.json` |
| `meta.name` | `red` (must match the folder name) |
| `meta.display_name` | `Red (Night)` |
| `meta.author` | `TLEscope` |
| `meta.description` | `Monochromatic single-channel red theme for dark-adapted nighttime use` |

The folder name `red` is what appears in the Settings → Theme dropdown and what is
stored in `settings.json` as `"theme": "red"`.

---

## 3. theme.json schema (field-by-field)

All six sections are **required** by the validator. Every color must be a
`#RRGGBBAA` string (exactly 9 characters). Floats are JSON numbers. Strings are
JSON strings.

### 3.1 `meta` (object, required)

| Field | Type | Notes |
|-------|------|-------|
| `name` | string | Theme id; should equal the folder name. |
| `display_name` | string | Human-readable name (log only). |
| `author` | string | Author string. |
| `description` | string | Short description. |

### 3.2 `world` (object, required) — 3D/2D raylib scene colors

| Field | Type | Meaning |
|-------|------|---------|
| `bg_color` | color | Scene clear color. |
| `orbit_normal` | color | Normal orbit line (alpha used). |
| `orbit_highlighted` | color | Highlighted/active orbit. |
| `sat_normal` | color | Normal satellite marker. |
| `sat_highlighted` | color | Hovered satellite. |
| `sat_selected` | color | Selected satellite. |
| `periapsis` | color | Periapsis marker. |
| `apoapsis` | color | Apoapsis marker. |
| `footprint_bg` | color | Ground footprint fill (alpha used). |
| `footprint_border` | color | Ground footprint border. |
| `scope_bg` | color | Polar/scope plot background. |
| `scope_horizon` | color | Scope horizon line. |
| `overlay_dim` | color | Full-screen dim overlay. |

### 3.3 `ui` (object, required) — ImGui + raylib UI colors

Semantic colors:

| Field | Type | Meaning |
|-------|------|---------|
| `text_main` | color | Primary text. |
| `text_secondary` | color | Secondary/disabled text. |
| `ui_bg` | color | UI overlay background. |
| `ui_primary` | color | Primary panel/menu bar. |
| `ui_secondary` | color | Secondary panel. |
| `ui_accent` | color | Accent (nav cursor, highlights). |
| `window_border` | color | Window border. |
| `window_border_focus` | color | Focused window border. |

Full ImGui palette (maps 1:1 to `ImGuiCol_*` in
[`imgui_theme.cpp:40-101`](src/ui/imgui_theme.cpp:40)):

`window_bg`, `titlebar`, `titlebar_active`, `titlebar_collapsed`, `frame_bg`,
`frame_bg_hovered`, `frame_bg_active`, `button`, `button_hovered`, `button_active`,
`header`, `header_hovered`, `header_active`, `tab`, `tab_hovered`, `tab_active`,
`tab_unfocused`, `tab_unfocused_active`, `scrollbar_bg`, `scrollbar_grab`,
`scrollbar_grab_hovered`, `scrollbar_grab_active`, `separator`, `separator_hovered`,
`separator_active`, `check_mark`, `slider_grab`, `slider_grab_active`,
`text_selected_bg`, `modal_dim`, `plot_histogram`, `plot_lines`, `resize_grip`,
`docking_bg`, `docking_preview`.

Notification toasts (ROADMAP 8.1):

| Field | Type | Meaning |
|-------|------|---------|
| `notif_info` | color | Info toast accent. |
| `notif_success` | color | Success toast accent. |
| `notif_warning` | color | Warning toast accent. |
| `notif_error` | color | Error toast accent. |
| `notif_bg` | color | Toast background. |
| `notif_border` | color | Toast border. |

### 3.4 `style` (object, required) — ImGui style floats

`window_rounding`, `frame_rounding`, `child_rounding`, `popup_rounding`,
`grab_rounding`, `scrollbar_rounding`, `tab_rounding`, `window_border_size`,
`frame_border_size`, `popup_border_size`, `window_padding_x`, `window_padding_y`,
`frame_padding_x`, `frame_padding_y`, `item_spacing_x`, `item_spacing_y`,
`item_inner_spacing_x`, `item_inner_spacing_y`, `scrollbar_size`, `grab_min_size`,
`window_title_align_x`, `button_text_align_x`, `indent_spacing`,
`columns_min_spacing`.

### 3.5 `font` (object, required)

| Field | Type | Notes |
|-------|------|-------|
| `file` | string | `font.ttf` — inherited from `themes/default/`. |
| `size` | float | ImGui base font size (px). |
| `icon_size` | float | FontAwesome icon size (px). |
| `raylib_size` | float | raylib custom font size (px). |

### 3.6 `textures` (object, required)

`earth`, `earth_night`, `clouds`, `skybox`, `moon`, `sat_icon`, `marker_icon`,
`smallmark` — all strings. Keep the default filenames so they resolve to
`themes/default/*.png` via `ThemeAssetPath()`.

---

## 4. Proposed monochromatic red palette

### 4.1 Mapping rule (luminance → red)

Every color is `#RR0000AA` (or `#RR0000` with alpha `FF`): **G = 0 and B = 0 for
every field**, so the entire UI and scene render on the red channel only. The
perceived brightness is the red value `RR` (0–255). A single luminance ramp is
used so that relative contrast relationships from the default theme are preserved:

| Ramp step | Hex | R value | Intended use |
|-----------|-----|---------|--------------|
| L0 | `#080000` | 8 | Canvas / deepest background |
| L1 | `#1A0000` | 26 | Panels, titlebars |
| L2 | `#2B0000` | 43 | Frames, buttons, headers |
| L3 | `#330000` | 51 | Separators, borders |
| L4 | `#470000` | 71 | Active/hovered frames |
| L5 | `#660000` | 102 | Dim markers, info |
| L6 | `#800000` | 128 | Secondary text, periapsis |
| L7 | `#990000` | 153 | Accent, primary markers |
| L8 | `#B30000` | 179 | Primary text |
| L9 | `#CC0000` | 204 | Brightest allowed (selected/error) |

**Intensity justification:** the brightest value is capped at `#CC0000` (R = 204,
~80 % of full red) and is reserved for only a few elements (selected satellite,
slider active, error toast). The bulk of the UI sits at R ≤ 153, and backgrounds at
R ≤ 43. This keeps total emitted light low, preserves dark adaptation, and avoids
the harsh glare of `#FF0000`. Pure black (`#000000`) is used only for the modal dim
overlay, which is still monochromatic (R = G = B = 0).

### 4.2 Complete `world` palette

| Field | Value |
|-------|-------|
| `bg_color` | `#080000FF` |
| `orbit_normal` | `#66000026` |
| `orbit_highlighted` | `#CC0000FF` |
| `sat_normal` | `#660000CC` |
| `sat_highlighted` | `#990000FF` |
| `sat_selected` | `#CC0000FF` |
| `periapsis` | `#800000FF` |
| `apoapsis` | `#990000FF` |
| `footprint_bg` | `#66000022` |
| `footprint_border` | `#99000088` |
| `scope_bg` | `#080000FF` |
| `scope_horizon` | `#330000FF` |
| `overlay_dim` | `#000000B4` |

### 4.3 Complete `ui` palette

| Field | Value |
|-------|-------|
| `text_main` | `#B30000FF` |
| `text_secondary` | `#800000FF` |
| `ui_bg` | `#0A0000CC` |
| `ui_primary` | `#1A0000FF` |
| `ui_secondary` | `#2B0000FF` |
| `ui_accent` | `#990000FF` |
| `window_border` | `#3A0000FF` |
| `window_border_focus` | `#990000FF` |
| `window_bg` | `#120000FF` |
| `titlebar` | `#1A0000FF` |
| `titlebar_active` | `#220000FF` |
| `titlebar_collapsed` | `#120000FF` |
| `frame_bg` | `#2B0000FF` |
| `frame_bg_hovered` | `#3A0000FF` |
| `frame_bg_active` | `#470000FF` |
| `button` | `#2B0000FF` |
| `button_hovered` | `#3A0000FF` |
| `button_active` | `#470000FF` |
| `header` | `#2B0000FF` |
| `header_hovered` | `#3A0000FF` |
| `header_active` | `#470000FF` |
| `tab` | `#1A0000FF` |
| `tab_hovered` | `#330000FF` |
| `tab_active` | `#2B0000FF` |
| `tab_unfocused` | `#140000FF` |
| `tab_unfocused_active` | `#220000FF` |
| `scrollbar_bg` | `#120000FF` |
| `scrollbar_grab` | `#3A0000FF` |
| `scrollbar_grab_hovered` | `#4A0000FF` |
| `scrollbar_grab_active` | `#5A0000FF` |
| `separator` | `#330000FF` |
| `separator_hovered` | `#4A0000FF` |
| `separator_active` | `#5A0000FF` |
| `check_mark` | `#990000FF` |
| `slider_grab` | `#990000FF` |
| `slider_grab_active` | `#CC0000FF` |
| `text_selected_bg` | `#99000033` |
| `modal_dim` | `#00000080` |
| `plot_histogram` | `#990000FF` |
| `plot_lines` | `#990000FF` |
| `resize_grip` | `#66000033` |
| `docking_bg` | `#0A0000BB` |
| `docking_preview` | `#99000088` |
| `notif_info` | `#660000FF` |
| `notif_success` | `#800000FF` |
| `notif_warning` | `#990000FF` |
| `notif_error` | `#CC0000FF` |
| `notif_bg` | `#120000CC` |
| `notif_border` | `#3A0000FF` |

> **Semantic limitation:** in a strictly monochromatic theme the four notification
> categories cannot be distinguished by hue. They are differentiated by brightness
> only (info dimmest → error brightest). This is an accepted trade-off of
> single-channel red mode.

### 4.4 `style`, `font`, `textures`

Copy the values from `themes/default/theme.json` unchanged:

- `style`: identical floats to `themes/default/theme.json` lines 74–99.
- `font`: `file = "font.ttf"`, `size = 16.0`, `icon_size = 14.0`,
  `raylib_size = 64.0`.
- `textures`: the eight default filenames (`earth.png`, `earth_night.png`,
  `clouds.png`, `skybox.png`, `moon.png`, `sat_icon.png`, `marker_icon.png`,
  `smallmark.png`).

---

## 5. Required files and code changes

### 5.1 Files to create

| File | Required? | Notes |
|------|-----------|-------|
| `themes/red/theme.json` | **Yes** | The only file that must be created. |
| `themes/red/font.ttf` | No | Inherited from `themes/default/`. |
| `themes/red/*.png` | No | All textures inherited from `themes/default/`. |

### 5.2 Code changes

**None.** Themes are auto-discovered by folder scan
([`ThemeDiscover()`](src/core/theme.cpp:385)), the dropdown is populated from that
scan ([`ui_layout.cpp:1078`](src/ui/ui_layout.cpp:1078)), and assets fall back to
`themes/default/` ([`ThemeAssetPath()`](src/core/theme.cpp:363)). No enum, registry,
config default, or UI list edit is required.

---

## 6. Validation

### 6.1 Python validator (recommended — auto-globs all themes)

From the workspace root:

```powershell
python scripts/validate_themes.py
```

The script globs `themes/*/theme.json` ([`validate_themes.py:50`](scripts/validate_themes.py:50)),
checks all six sections and every required key, and enforces the `#RRGGBBAA`
format ([`validate_themes.py:40-45`](scripts/validate_themes.py:40)).

**Passing result** includes a line for the new theme and a final success banner:

```
OK   themes/red/theme.json
...
ALL THEMES VALID
```

Exit code `0`. Any missing key or malformed color prints `FAIL themes/red/theme.json`
with the offending field and the script exits `1`.

### 6.2 C++ validator (optional, uses the real cJSON parser)

Build and run (Windows PowerShell):

```powershell
g++ -std=c++20 -Ilib/cjson scripts/validate_themes.cpp lib/cjson/cJSON.c -o build/validate_themes.exe
./build/validate_themes.exe themes/red/theme.json
```

**Passing result:**

```
== themes/red/theme.json
OK
ALL THEMES VALID
```

Exit code `0`. (The C++ validator accepts explicit file arguments; without
arguments it uses a hardcoded list that does not include `red`.)

---

## 7. Implementation checklist (for the Code mode step)

1. Create `themes/red/theme.json` with the six sections and the palette in §4.
2. Ensure every `world`/`ui` color is exactly 9 characters (`#RRGGBBAA`).
3. Run `python scripts/validate_themes.py` and confirm `ALL THEMES VALID`.
4. Launch TLEscope, open Settings → Theme, select `red`, and confirm the UI and
   scene render in monochrome red.
5. (Optional) Set `"theme": "red"` in `settings.json` to make it the default.