#ifndef DEMO_DIRECTOR_H
#define DEMO_DIRECTOR_H

#include "core/config.h"
#include "core/types.h"
#include "ui/ui.h" /* TargetLock */
#include <raylib.h>

/* Demo Mode director: drives a scripted camera/selection/layer sequence. */

/* Live scene state the director drives; every pointer refers to a main-loop local. */
typedef struct
{
    AppConfig *cfg;

    /* view mode flags */
    bool *is_2d_view;
    bool *is_pov_mode;
    bool *is_ecliptic_frame;

    /* simulation time */
    double *current_epoch;
    double *time_multiplier;

    /* selection */
    Satellite **selected_sat;
    Satellite **hovered_sat;

    /* selection isolation */
    bool *hide_unselected;
    float *unselected_fade;

    /* 3D camera (written directly during demo) */
    Camera3D *camera3d;
    float *cam_distance;
    float *cam_angle_x;
    float *cam_angle_y;
    float *target_cam_distance;
    float *target_cam_angle_x;
    float *target_cam_angle_y;
    Vector3 *target_camera3d_target;

    /* 2D camera (written directly during demo) */
    Camera2D *camera2d;
    float *target_camera2d_zoom;
    Vector2 *target_camera2d_target;

    /* antenna scope beam (optional; the demo forces it off and restores it) */
    bool *show_scope;
    float *scope_az;
    float *scope_el;
    float *scope_beam;

    /* selected pass index (optional); drives the polar-plot AOS/LOS markers */
    int *selected_pass_idx;

    /* camera lock; the demo forces LOCK_NONE */
    TargetLock *active_lock;

    /* scene constants (values, not pointers) */
    float draw_earth_radius;
    float map_w;
    float map_h;
} DemoContext;

/** build the internal phase table (idempotent; call once at startup) */
void DemoDirectorInit(void);

/** true while demo mode is running */
bool DemoDirectorActive(void);

/** request entry into demo mode (applied on the next DemoDirectorUpdate) */
void DemoDirectorRequestStart(void);

/** request exit from demo mode (applied on the next DemoDirectorUpdate) */
void DemoDirectorRequestStop(void);

/** advance the demo state machine; call once per frame */
void DemoDirectorUpdate(DemoContext *ctx, float dt);

/** draw the demo overlay (Esc hint, logo, letterbox, fade); call after DrawGUI */
void DemoDirectorDrawOverlay(Texture2D logo, Font font, AppConfig *cfg);

#endif // DEMO_DIRECTOR_H
