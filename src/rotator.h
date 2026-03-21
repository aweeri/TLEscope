#ifndef ROTATOR_H
#define ROTATOR_H

#include "ui.h"

#define ROTATOR_STEER_POLAR 0
#define ROTATOR_STEER_SCOPE 1

void RotatorShutdown(void);

char *RotatorGetHostBuffer(void);
int RotatorGetHostBufferSize(void);
char *RotatorGetPortBuffer(void);
int RotatorGetPortBufferSize(void);
char *RotatorGetGetFmtBuffer(void);
int RotatorGetGetFmtBufferSize(void);
char *RotatorGetSetFmtBuffer(void);
int RotatorGetSetFmtBufferSize(void);
char *RotatorGetCustomCmdBuffer(void);
int RotatorGetCustomCmdBufferSize(void);
char *RotatorGetParkAzBuffer(void);
int RotatorGetParkAzBufferSize(void);
char *RotatorGetParkElBuffer(void);
int RotatorGetParkElBufferSize(void);
const char *RotatorGetStatus(void);
bool RotatorGetAutoSteer(void);
void RotatorSetAutoSteer(bool enabled);
int RotatorGetSteerMode(void);
void RotatorSetSteerMode(int mode);
int RotatorGetLeadTimeSec(void);
void RotatorSetLeadTimeSec(int sec);
char *RotatorGetLeadTimeBuffer(void);
int RotatorGetLeadTimeBufferSize(void);
void RotatorConnect(void);
void RotatorDisconnect(void);
void RotatorPollNow(void);
void RotatorSendCustomNow(void);
void RotatorSetParkNow(float az, float el);

bool RotatorIsWindowVisible(void);
void RotatorToggleWindow(AppConfig *cfg);
Rectangle RotatorGetWindowRect(AppConfig *cfg);
bool RotatorIsPointInWindow(Vector2 point, AppConfig *cfg);
bool RotatorBeginDrag(Vector2 point, AppConfig *cfg);
void RotatorUpdateDrag(AppConfig *cfg);
void RotatorEndDrag(void);

void RotatorUpdateControl(UIContext *ctx, bool show_scope_dialog, bool show_polar_dialog, bool polar_lunar_mode, int selected_pass_idx);
void RotatorDrawWindow(AppConfig *cfg, Font customFont, bool interactive);

bool RotatorIsConnected(void);
bool RotatorHasPosition(void);
float RotatorGetAz(void);
float RotatorGetEl(void);

float RotatorConnectedItemWidth(AppConfig *cfg, Font customFont);
void RotatorDrawConnectedItem(AppConfig *cfg, Font customFont, float x, float y);
void RotatorDrawPolarOverlay(AppConfig *cfg, Font customFont, float cx, float cy, float r_max, float pl_x, float pl_y);
void RotatorDrawScopeOverlay(
    AppConfig *cfg, Font customFont, float center_x, float center_y, float scope_radius, float scope_az, float scope_el, float scope_beam, float sc_x, float ctrl_y
);

#endif
