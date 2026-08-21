#ifndef TOOLS_SCENE_H
#define TOOLS_SCENE_H

#include "core/config.h"
#include "core/types.h"
#include "ui/ui.h"

/**
 * @file tools_scene.h
 * @brief Scene render-hook registry for tools.
 *
 * A tool may register an optional draw_scene callback (see PanelDef in
 * tools_registry.h) to draw into the 3D world or 2D map. The render loop in
 * main.cpp constructs a SceneContext and calls DrawSceneHooks() each frame
 * inside BeginMode3D / BeginMode2D, so tool overlays draw on top of the scene.
 *
 * This lets a feature like a 3D overlay live entirely inside its own
 * tool_*.cpp — no edit to main.cpp required.
 */

/**
 * @brief read-only snapshot of the scene state passed to draw_scene hooks.
 *
 * Tools should treat this as read-only input. To persist their own state they
 * use the generic settings store (tools_settings.h) via AppConfig.
 */
typedef struct
{
    bool is_2d_view;              /* true = 2D map, false = 3D globe */
    Camera2D *camera2d;           /* 2D camera (valid when is_2d)    */
    Camera3D *camera3d;           /* 3D camera (valid when !is_2d)   */
    double current_epoch;         /* current simulation epoch        */
    double gmst_deg;              /* Greenwich mean sidereal time    */
    float earth_rotation_offset;  /* cfg.earth_rotation_offset       */
    float draw_earth_radius;      /* EARTH_RADIUS_KM / DRAW_SCALE    */
    float map_w, map_h;           /* 2D map dimensions               */
    Vector3 sun_dir_world;        /* unit vector toward the sun      */
    Vector3 moon_pos_world;       /* moon position in draw space     */
    Satellite *active_sat;        /* hovered or selected satellite   */
    Satellite *selected_sat;      /* currently selected satellite    */
    bool is_pov_mode;             /* point-of-view camera mode       */
} SceneContext;

/** call every registered draw_scene hook (defined in tools_scene.cpp) */
void DrawSceneHooks(SceneContext *sctx, AppConfig *cfg);

#endif /* TOOLS_SCENE_H */