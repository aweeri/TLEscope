#ifndef UI_H
#define UI_H

#include "config.h"
#include "types.h"
#include <raylib.h>

typedef enum
{
    LOCK_NONE,
    LOCK_EARTH,
    LOCK_MOON
} TargetLock;

/* this context struct passes necessary simulation state to the UI */
typedef struct
{
    double *current_epoch;
    double *time_multiplier;
    double *saved_multiplier;
    bool *is_auto_warping;
    double *auto_warp_target;
    double *auto_warp_initial_diff;
    bool *is_2d_view;
    bool *hide_unselected;
    bool *picking_home;
    bool *exit_app;
    bool *is_ecliptic_frame;
    bool *is_pov_mode;
    bool *show_scope;
    float *scope_az;
    float *scope_el;
    float *scope_beam;
    Satellite **selected_sat;
    Satellite *hovered_sat;
    Satellite *active_sat;
    TargetLock *active_lock;
    char *datetime_str;
    double gmst_deg;
    float map_w;
    float map_h;
    Camera2D *camera2d;
    Camera3D *camera3d;
} UIContext;

/* core UI Methods */
void SaveSatSelection(void);
void LoadSatSelection(void);
bool IsUITyping(void);
void ToggleTLEWarning(void);
bool IsMouseOverUI(AppConfig *cfg);
void DrawGUI(UIContext *ctx, AppConfig *cfg, Font customFont);

/* shared Helpers */
Color ApplyAlpha(Color c, float alpha);
void DrawUIText(Font font, const char *text, float x, float y, float size, Color color);
double StepTimeMultiplier(double current, bool increase);
double unix_to_epoch(double target_unix);
bool IsOccludedByEarth(Vector3 camPos, Vector3 targetPos, float earthRadius);

#endif // UI_H
