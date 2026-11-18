#ifndef LABELS_H
#define LABELS_H

#include "core/config.h"
#include "core/types.h"
#include "ui/ui.h"

/* persisted config keys (ToolSettings store, see tools_settings.h) */
#define LABELS_KEY_ENABLED   "labels.enabled"
#define LABELS_KEY_MODE      "labels.mode"
#define LABELS_KEY_ALTITUDE  "labels.show_altitude"
#define LABELS_KEY_SIZE      "labels.size"
#define LABELS_KEY_BG        "labels.background"
#define LABELS_KEY_MAX_COUNT "labels.max_count"

/* label scope values for LABELS_KEY_MODE */
#define LABELS_MODE_SELECTED_ONLY 0  /* Sel: only the selected satellite gets a label */
#define LABELS_MODE_ALL           1  /* All: every active satellite gets a label */
#define LABELS_MODE_FAV           2  /* fav: only favorite satellites get a label */

/** collect + render all scene labels for the current frame */
void DrawSceneLabels(UIContext *ctx, AppConfig *cfg);

#endif /* LABELS_H */
