#ifndef TOUCH_GESTURE_H
#define TOUCH_GESTURE_H

/** Install the Windows touch observer (no-op on non-Windows). Call once after InitWindow(). */
void TouchGestureInit(void);

/** Returns true once when a 3-finger double-tap was detected; clears the flag. */
bool TouchGestureConsumeThreeFingerDoubleTap(void);

#endif
