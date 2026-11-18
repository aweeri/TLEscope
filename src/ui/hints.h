#ifndef HINTS_H
#define HINTS_H

#include "ui_layout.h" /* UIRect */

/** text content of a hint; icon and title may be "" */
typedef struct
{
    const char *icon;  /* FontAwesome glyph */
    const char *title; /* heading           */
    const char *body;  /* wrapped paragraph */
} HintContent;

/** a hint plus an optional spotlight target (valid=false => centered, no spotlight) */
typedef struct
{
    HintContent content;
    UIRect      target;
} Hint;

/* -- standalone hint (minor hints, one-offs); passive, never blocks input -- */
void HintShow(const HintContent *content);
void HintShowAt(const Hint *hint);

/* -- sequence support (used by the tutorial) ------------------------------- */
void HintStartSequence(const Hint *steps, int count, bool capture_input);
void HintNext(void);
void HintPrev(void);
void HintDismiss(void);

/** update the current step's spotlight target (call each frame while active) */
void HintSetTarget(const UIRect *target);

/* -- state queries --------------------------------------------------------- */
bool HintIsActive(void);
int  HintStepIndex(void);
int  HintStepCount(void);

/* -- per-frame ------------------------------------------------------------- */
/** render the active hint into the foreground draw list; call LAST in DrawGUI */
void DrawHints(void);

#endif /* HINTS_H */
