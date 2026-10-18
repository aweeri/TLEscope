#ifndef TUTORIAL_H
#define TUTORIAL_H
#include "core/config.h"
#include "ui.h"

#define TUTORIAL_STEP_COUNT 8

/* true while the first-boot tutorial should be shown and capture input */
bool TutorialActive(const AppConfig *cfg);

/* start (or restart) the tutorial sequence and persist the pending flag */
void TutorialLaunch(AppConfig *cfg);

/* drive the tutorial hint sequence; call once per frame from DrawGUI */
void DrawTutorialOverlay(UIContext *ctx, AppConfig *cfg);
#endif
