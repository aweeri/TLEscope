# TLEscope Tools

This directory contains all of TLEscope's **tool panels** — the sidebar panels
you see in the app (Satellite Manager, Data Sources, Layers, Scope, Rotator,
Satellite Info, Passes, Polar Plot, Doppler, Log).

Each tool is a self-contained `.cpp` file. Adding a new tool is easy and
requires no changes to the core layout engine.

A tool can be more than a sidebar panel. Beyond rendering a panel, a tool may
also:

- **Draw into the 3D world / 2D map** via an optional `draw_scene` hook.
- **Persist its own settings** via a generic key-value store, with no new
  `AppConfig` field and no `config.cpp` edit.

Both mechanisms are described below.

## How to add a new tool

Adding a tool takes **3 small steps**:

### 1. Write the draw function

Create a new file, e.g. `tool_my_tool.cpp`:

```cpp
// tool_my_tool.cpp
#include "tools.h"
#include "core/config.h"

#include "imgui.h"

void DrawPanelMyTool(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    (void)cfg;
    ImGui::Text("Hello from my tool!");
    // ... any ImGui content you like ...
}
```

### 2. Declare it

Add the declaration to `tools.h`:

```cpp
void DrawPanelMyTool(UIContext *ctx, AppConfig *cfg);
```

### 3. Register it

Add a `PanelId` entry in `core/types.h` (the enum lives there so the panel
count can be derived automatically):

```cpp
PANEL_MY_TOOL,   /* add before PANEL_COUNT */
```

Then add one row to the registry table in `tools_registry.cpp`:

```cpp
{ PANEL_MY_TOOL, "My Tool", ICON_FA_WRENCH, PANEL_CAT_EXTRA, SIDEBAR_RIGHT, false, false, DrawPanelMyTool },
```

New tools are **disabled by default** (the `default_enabled` field is `false`),
so they stay hidden from the sidebar until the user enables them in the Tools
modal. Only the essential core/inspector tools ship enabled on first run.

Finally, add your new file to the `SRC` list in the root `Makefile`.

That's it! The Tools modal, the enable/disable persistence, the left/right
placement, and the sidebar renderer all pick your tool up automatically
because they iterate the `g_panel_defs` registry.

> **No capacity bump needed.** `MAX_PANELS` is derived from `PANEL_COUNT`, so
> adding a `PanelId` entry automatically grows the layout arrays. You never
> touch a hardcoded panel limit.

## Reordering panels (drag)

Panels can be reordered by dragging the **grip handle** (the vertical-dots icon
on the right edge of a panel header). While dragging:

- A blue insertion line shows where the panel will land.
- You can drag a panel **across sidebars** (left ↔ right) by moving the mouse
  into the other sidebar before releasing.
- The new arrangement is **saved to `settings.json` immediately on drop**, so
  it survives a crash or a forced quit — you don't need to open Settings and
  press Save.

The reorder logic lives in `ui_layout.cpp` (`DrawAccordionHeader` +
`FinishReorder`). It maps the mouse position to a *visible* slot index (only
enabled panels are rendered) and translates that back into a real position in
the full order array, so disabled panels never throw the drop position off.

## Registry fields

Each row in `g_panel_defs` (in `tools_registry.cpp`) is a `PanelDef` with these
fields, in order:

| Field          | Meaning                                                        |
|----------------|----------------------------------------------------------------|
| `id`           | The `PanelId` enum value (must be unique, matches `core/types.h`). |
| `title`        | Display name shown in the sidebar header and Tools modal.      |
| `icon`         | A FontAwesome icon constant, e.g. `ICON_FA_WRENCH`. Pick one that matches the tool. |
| `category`     | `PANEL_CAT_CORE`, `PANEL_CAT_EXTRA`, or `PANEL_CAT_DEBUG`. Controls which section the tool appears under in the Tools modal (Core Functions / Extra Tools / Debug). |
| `default_side` | `SIDEBAR_LEFT` or `SIDEBAR_RIGHT` — where the panel lives on first run. The user can move it later (drag the grip, or the Tools modal arrows). |
| `default_open` | `true` = panel is expanded (content visible) on first run; `false` = collapsed to just its header. |
| `default_enabled` | `true` = panel is shown in the sidebar on first run; `false` = hidden until the user enables it in the Tools modal. **New tools should set this to `false`** so they don't crowd the sidebar until the user opts in. |
| `draw_content` | Your panel body renderer: `void f(UIContext*, AppConfig*)`.    |
| `draw_scene`   | Optional scene hook (see below). `NULL` if the tool has none.  |

## Drawing into the 3D world / 2D map (scene hooks)

A tool that affects the scene (a 3D overlay, a marker, a line, etc.) can
register an optional `draw_scene` callback. The render loop in `main.cpp`
calls every registered `draw_scene` hook each frame, inside `BeginMode3D` /
`BeginMode2D`, so your overlay draws on top of the scene.

### 1. Write the scene callback

Add a second function to your `tool_my_tool.cpp`:

```cpp
#include "tools_scene.h"

void DrawSceneMyTool(SceneContext *sctx, AppConfig *cfg)
{
    // Draw a cube in the 3D world when the tool's setting is enabled.
    if (!ToolSettingGetBool(cfg, "mytool.show_cube", false))
        return;

    Vector3 pos = { 0.0f, 0.0f, 0.0f };   // world space (draw units)
    float size = 0.5f;
    DrawCube(pos, size, RED);             // raylib 3D draw call
}
```

`SceneContext` (defined in `tools_scene.h`) gives you read-only access to the
scene state:

| Field              | Meaning                                              |
|--------------------|------------------------------------------------------|
| `is_2d_view`       | `true` = 2D map, `false` = 3D globe.                 |
| `camera2d`         | 2D camera (valid when `is_2d_view`).                 |
| `camera3d`         | 3D camera (valid when `!is_2d_view`).                |
| `current_epoch`    | Current simulation epoch.                            |
| `gmst_deg`         | Greenwich mean sidereal time (degrees).              |
| `earth_rotation_offset` | `cfg.earth_rotation_offset`.                    |
| `draw_earth_radius`| `EARTH_RADIUS_KM / DRAW_SCALE` (draw units).         |
| `map_w`, `map_h`   | 2D map dimensions.                                   |
| `sun_dir_world`    | Unit vector toward the sun.                          |
| `moon_pos_world`   | Moon position in draw space.                         |
| `active_sat`       | Hovered or selected satellite (may be `NULL`).       |
| `selected_sat`     | Currently selected satellite (may be `NULL`).        |
| `is_pov_mode`      | Point-of-view camera mode.                           |

### 2. Register the scene hook

Add the `draw_scene` callback to your registry row in `tools_registry.cpp`:

```cpp
{ PANEL_MY_TOOL, "My Tool", ICON_FA_WRENCH, PANEL_CAT_EXTRA, SIDEBAR_RIGHT, false, DrawPanelMyTool, DrawSceneMyTool },
```

Declare it in `tools.h`:

```cpp
void DrawSceneMyTool(SceneContext *s, AppConfig *cfg);
```

That's it. The render loop picks up your scene hook automatically because it
iterates `g_panel_defs`. **No edit to `main.cpp` is required.**

> **Keep `draw_scene` cheap.** It runs every frame. Cache expensive
> calculations; don't allocate or load resources inside it.

## Persisting tool settings (generic key-value store)

Instead of adding a new `AppConfig` field for every toggle, tools can read and
write their own namespaced keys via `tools_settings.h`. The whole map is
serialized to `settings.json` generically, so `types.h` and `config.cpp` never
change when you add a setting.

```cpp
#include "tools_settings.h"

// read a bool (default false if absent)
bool show = ToolSettingGetBool(cfg, "mytool.show_cube", false);

// write it back (persisted on next save)
ToolSettingSetBool(cfg, "mytool.show_cube", show);
```

Available accessors:

| Function | Signature |
|----------|-----------|
| `ToolSettingGetBool` / `ToolSettingSetBool` | `(AppConfig*, key, bool)` |
| `ToolSettingGetInt` / `ToolSettingSetInt` | `(AppConfig*, key, int)` |
| `ToolSettingGetFloat` / `ToolSettingSetFloat` | `(AppConfig*, key, float)` |
| `ToolSettingGetString` / `ToolSettingSetString` | `(AppConfig*, key, const char*)` |

**Key convention:** namespace keys by the tool, e.g. `"mytool.show_cube"`,
`"mytool.color"`. This avoids collisions between tools.

## Worked example: a 3D overlay tool

Here is a complete, self-contained tool that draws a cube in the 3D world and
persists its own toggle — with **no changes to `main.cpp`, `types.h`, or
`config.cpp`**.

```cpp
// tool_cubeifier.cpp
#include "tools.h"
#include "tools_scene.h"
#include "tools_settings.h"
#include "core/config.h"
#include "imgui.h"

void DrawPanelCubeifier(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    bool show = ToolSettingGetBool(cfg, "cubeifier.enabled", false);
    if (ImGui::Checkbox("Show cube", &show))
        ToolSettingSetBool(cfg, "cubeifier.enabled", show);
}

void DrawSceneCubeifier(SceneContext *s, AppConfig *cfg)
{
    if (!ToolSettingGetBool(cfg, "cubeifier.enabled", false))
        return;
    DrawCube((Vector3){ 0.0f, 0.0f, 0.0f }, 0.5f, RED);
}
```

Register it in `tools_registry.cpp`:

```cpp
{ PANEL_CUBEIFIER, "Cubeifier", ICON_FA_CUBE, PANEL_CAT_EXTRA, SIDEBAR_RIGHT, false, DrawPanelCubeifier, DrawSceneCubeifier },
```

Add `PANEL_CUBEIFIER` to the `PanelId` enum in `core/types.h`, declare both
functions in `tools.h`, and add `tool_cubeifier.cpp` to the Makefile `SRC`.

## File layout

- `tools_registry.h` / `tools_registry.cpp` — the single registry of all tools.
- `tools_common.h` / `tools_common.cpp` — shared helpers (data-source
  selection persistence, `InfoRow`, RA/Dec formatting, case-insensitive search).
- `tools_scene.h` / `tools_scene.cpp` — the scene render-hook registry
  (`SceneContext` + `DrawSceneHooks`).
- `tools_settings.h` / `tools_settings.cpp` — the generic persisted key-value
  settings store.
- `tools.h` — declarations of all built-in tool draw functions.
- `tool_*.cpp` — one file per tool.