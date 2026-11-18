#ifndef NOTIFICATIONS_H
#define NOTIFICATIONS_H

#include "core/config.h"
#include "core/theme.h"

/** severity level; drives the accent color and the per-category toggle */
typedef enum
{
    NOTIFY_INFO = 0,
    NOTIFY_SUCCESS,
    NOTIFY_WARNING,
    NOTIFY_ERROR,
    NOTIFY_LEVEL_COUNT
} NotifyLevel;

/* ring buffer capacity and timing (seconds) */
#define MAX_NOTIFICATIONS 8
#define NOTIFY_DURATION 4.0f   /* how long a toast stays fully visible */
#define NOTIFY_FADE 0.5f       /* fade-out duration after NOTIFY_DURATION */

void NotifyPush(NotifyLevel level, const char *icon, const char *fmt, ...);

/** advance fade timers; call once per frame with the frame delta time */
void NotifyUpdate(float dt);

/** render the stacked toasts in the top-right corner; call inside the ImGui frame */
void DrawNotifications(void);

/* -- settings toggles (persisted via tool_settings) ------------------------ */

/** load enabled/category toggles from the generic tool_settings store */
void NotifyLoadSettings(AppConfig *cfg);

/** persist the current toggles into the generic tool_settings store */
void NotifySaveSettings(AppConfig *cfg);

/** master switch: when false, no toasts are pushed or drawn */
bool NotifyEnabled(void);

int NotifyCount(void);

void NotifyClearAll(void);

/** per-category switch for a given level */
bool NotifyCategoryEnabled(NotifyLevel level);

/** set the master switch (and persist via NotifySaveSettings) */
void NotifySetEnabled(bool enabled);

/** set a per-category switch (and persist via NotifySaveSettings) */
void NotifySetCategoryEnabled(NotifyLevel level, bool enabled);

#endif /* NOTIFICATIONS_H */