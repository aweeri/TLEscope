# TLEscope Tools

This directory contains all of TLEscope's **tool panels** — the sidebar panels
you see in the app (Satellite Manager, Data Sources, Layers, Scope, Rotator,
Satellite Info, Passes, Polar Plot, Doppler, Log).

Each tool is a self-contained `.cpp` file with a single draw function. Adding a
new tool is easy and requires no changes to the core layout engine.

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

Add a `PanelId` entry in `tools_registry.h`:

```cpp
PANEL_MY_TOOL,   /* add before PANEL_COUNT */
```

Then add one row to the registry table in `tools_registry.cpp`:

```cpp
{ PANEL_MY_TOOL, "My Tool", ICON_FA_WRENCH, PANEL_CAT_SCIENTIFIC, SIDEBAR_RIGHT, false, DrawPanelMyTool },
```

Finally, add your new file to the `SRC` list in the root `Makefile`.

That's it! The Tools modal, the enable/disable persistence, the left/right
placement, and the sidebar renderer all pick your tool up automatically
because they iterate the `g_panel_defs` registry.

## Registry fields

| Field          | Meaning                                                        |
|----------------|----------------------------------------------------------------|
| `id`           | The `PanelId` enum value (must be unique).                     |
| `title`        | Display name shown in the sidebar and Tools modal.             |
| `icon`         | A FontAwesome icon constant, e.g. `ICON_FA_WRENCH`.            |
| `category`     | `PANEL_CAT_CORE`, `PANEL_CAT_SCIENTIFIC`, or `PANEL_CAT_INSPECTOR`. |
| `default_side` | `SIDEBAR_LEFT` or `SIDEBAR_RIGHT` (default placement).         |
| `default_open` | Whether the panel is open on first run.                        |
| `draw_content` | Your draw function.                                            |

## File layout

- `tools_registry.h` / `tools_registry.cpp` — the single registry of all tools.
- `tools_common.h` / `tools_common.cpp` — shared helpers (data-source
  selection persistence, `InfoRow`, RA/Dec formatting, case-insensitive search).
- `tools.h` — declarations of all built-in tool draw functions.
- `tool_*.cpp` — one file per tool.
