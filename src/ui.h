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

/*
 * UIState — encapsulates all UI-only state
 *
 * This struct holds window positions, dialog visibility flags, text buffers,
 * and other UI-specific state that was previously declared as static variables
 * in ui.c. Formalizing this struct makes the separation between core simulation
 * state and UI state explicit.
 */
typedef struct
{
    /* dialog visibility flags */
    bool show_help;
    bool show_settings;
    bool show_passes_dialog;
    bool show_polar_dialog;
    bool show_doppler_dialog;
    bool show_tle_warning;
    bool show_exit_dialog;
    bool show_sat_mgr_dialog;
    bool show_tle_mgr_dialog;
    bool show_time_dialog;
    bool show_scope_dialog;
    bool show_sat_info_dialog;

    /* window positions */
    float sm_x, sm_y;   /* satellite manager */
    float tm_x, tm_y;   /* TLE manager */
    float hw_x, hw_y;   /* help window */
    float sw_x, sw_y;   /* settings window */
    float pl_x, pl_y;   /* polar plot */
    float pd_x, pd_y;   /* polar doppler */
    float td_x, td_y;   /* time dialog */
    float dop_x, dop_y; /* doppler dialog */
    float sc_x, sc_y;   /* scope dialog */
    float si_x, si_y;   /* satellite info */
    float rot_x, rot_y; /* rotator window */

    /* drag state */
    bool drag_sat_mgr;
    Vector2 drag_sat_mgr_off;
    bool drag_tle_mgr;
    Vector2 drag_tle_mgr_off;
    bool drag_help;
    Vector2 drag_help_off;
    bool drag_settings;
    Vector2 drag_settings_off;
    bool drag_passes;
    Vector2 drag_passes_off;
    bool drag_polar;
    Vector2 drag_polar_off;
    bool drag_time_dialog;
    Vector2 drag_time_off;
    bool drag_doppler;
    Vector2 drag_doppler_off;
    bool drag_scope;
    Vector2 drag_scope_off;
    bool drag_sat_info;
    Vector2 drag_sat_info_off;
    bool rot_dragging;
    Vector2 rot_drag_off;

    /* scroll state */
    Vector2 sat_mgr_scroll;
    Vector2 tle_mgr_scroll;
    Vector2 help_scroll;
    Vector2 passes_scroll;

    /* text buffers */
    char sat_search_text[64];
    char new_tle_buf[512];
    char text_min_el[8];
    char text_fps[8];
    char text_hl_name[64];
    char text_hl_lat[32];
    char text_hl_lon[32];
    char text_hl_alt[32];
    char text_unix[64];
    char text_doppler_freq[32];
    char text_doppler_res[32];
    char text_doppler_file[128];
    char text_scope_az[16];
    char text_scope_el[16];
    char text_scope_beam[16];

    /* edit mode flags */
    bool edit_sat_search;
    bool edit_new_tle;
    bool edit_min_el;
    bool edit_fps;
    bool edit_hl_name, edit_hl_lat, edit_hl_lon, edit_hl_alt;
    bool edit_year, edit_month, edit_day;
    bool edit_hour, edit_min, edit_sec;
    bool edit_unix;
    bool edit_doppler_freq, edit_doppler_res, edit_doppler_file;
    bool edit_scope_az, edit_scope_el, edit_scope_beam;
    bool rot_edit_host, rot_edit_port;
    bool rot_edit_get_fmt, rot_edit_set_fmt;
    bool rot_edit_custom_cmd;
    bool rot_edit_park_az, rot_edit_park_el;
    bool rot_edit_lead_time;
    bool theme_dropdown_edit;

    /* scope state */
    bool scope_drag_active;
    Vector2 scope_drag_last;
    bool scope_drag_moved;
    bool scope_lock;
    bool scope_show_leo;
    bool scope_show_heo;
    bool scope_show_geo;
    bool scope_show_trails;

    /* polar state */
    bool polar_lunar_mode;
    double lunar_aos;
    double lunar_los;
    Vector2 lunar_path_pts[100];
    int lunar_num_pts;
    double last_lunar_calc_time;

    /* rotator state */
    bool rot_show_window;

    /* satellite info state */
    bool si_has_been_placed;
    bool si_rolled_up;
    Satellite *last_selected_sat;

    /* TLE manager state */
    bool celestrak_expanded;
    bool retlector_expanded;
    bool retlector_selected[25];
    bool other_expanded;
    bool manual_expanded;
    bool celestrak_selected[25];

    /* pass state */
    int selected_pass_idx;
    bool multi_pass_mode;
    Satellite *locked_pass_sat;
    double locked_pass_aos;
    double locked_pass_los;

    /* misc UI state */
    bool ui_initialized;
    int last_drawable_height;
    bool was_fullscreen;
    float tt_hover[19];
    char latest_version_str[64];
    bool update_available;
    char theme_names[1024];
    int active_theme_idx;
    float last_hl_lat, last_hl_lon, last_hl_alt;
    bool tb_select_all;
    void *active_tb_ptr;
} UIState;

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
