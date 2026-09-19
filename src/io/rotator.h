#ifndef ROTATOR_H
#define ROTATOR_H

/**
 * @file rotator.h
 * @brief Antenna rotator control via TCP (rotctld protocol)
 */

#include "ui/ui.h"

#define ROTATOR_STEER_POLAR 0
#define ROTATOR_STEER_SCOPE 1

void RotatorShutdown(void);

/* persist/restore rotator settings to/from an AppConfig (section 11) */
void RotatorSaveSettings(AppConfig *cfg);
void RotatorLoadSettings(const AppConfig *cfg);

int RotatorGetHostBufferSize(void);
int RotatorGetPortBufferSize(void);
int RotatorGetGetFmtBufferSize(void);
int RotatorGetSetFmtBufferSize(void);
int RotatorGetCustomCmdBufferSize(void);
int RotatorGetParkAzBufferSize(void);
int RotatorGetParkElBufferSize(void);
int RotatorGetLeadTimeBufferSize(void);
bool RotatorGetAutoSteer(void);
void RotatorSetAutoSteer(bool enabled);
int RotatorGetLeadTimeSec(void);
void RotatorConnect(void);
void RotatorDisconnect(void);
void RotatorPollNow(void);
void RotatorSendCustomNow(void);

void RotatorUpdateControl(UIContext *ctx, bool show_scope_dialog, bool show_polar_dialog, bool polar_lunar_mode, int selected_pass_idx);

bool RotatorIsConnected(void);
float RotatorGetAz(void);
float RotatorGetEl(void);

#endif
