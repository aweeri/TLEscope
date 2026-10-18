#ifndef HINTS_H
#define HINTS_H

#include "ui_layout.h" /* UIRect */

/**
 * @file hints.h
 * @brief Generic, reusable on-screen hint / coachmark overlay.
 *
 * A hint is a small floating box (icon + title + body) that may point at an
 * on-screen target rect (spotlight + arrow) or float centered when it has no
 * target. Hints are drawn into the ImGui foreground draw list, so they render
 * over every window, popup and toast. Standalone hints are passive (they never
 * block input); a hint sequence may opt in to input capture (the tutorial does).
 *
 * The module is self-contained: call HintShow()/HintStartSequence() from any
 * event point, drive the sequence with HintNext()/HintPrev()/HintDismiss(), and
 * call DrawHints() once per frame, last, from the UI render loop.
 */

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
