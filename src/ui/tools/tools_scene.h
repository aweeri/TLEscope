#ifndef TOOLS_SCENE_H
#define TOOLS_SCENE_H

#include "core/config.h"
#include "core/types.h"
#include "ui/ui.h"

/* read-only scene state passed to draw_scene hooks; persist tool state via tools_settings.h */
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