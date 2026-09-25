#include "ui/touch_gesture.h"

#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#elif _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>

extern "C" void *GetWindowHandle(void);

static WNDPROC  g_prev_wndproc = nullptr;
static HWND     g_hwnd         = nullptr;
static std::atomic<int>  g_active_points{0};
static int      g_max_points_this_tap = 0;
static std::atomic<bool> g_double_tap{false};
static ULONGLONG g_last_tap_ms = 0;

static bool IsTouchPointer(WPARAM wparam)
{
    POINTER_INPUT_TYPE type = PT_POINTER;
    return GetPointerType(GET_POINTERID_WPARAM(wparam), &type) && type == PT_TOUCH;
}

static LRESULT CALLBACK TouchWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_POINTERDOWN:
            if (IsTouchPointer(wparam))
            {
                int n = ++g_active_points;
                if (n > g_max_points_this_tap) g_max_points_this_tap = n;
            }
            break;
        case WM_POINTERUP:
            if (IsTouchPointer(wparam))
            {
                int n = --g_active_points;
                if (n <= 0)
                {
                    g_active_points = 0;
                    if (g_max_points_this_tap >= 3)
                    {
                        ULONGLONG now = GetTickCount64();
                        if (now - g_last_tap_ms <= 400) g_double_tap = true; /* 2nd tap within 400 ms */
                        g_last_tap_ms = now;
                    }
                    g_max_points_this_tap = 0;
                }
            }
            break;
        case WM_POINTERCAPTURECHANGED:
        case WM_CANCELMODE:
            g_active_points = 0;
            g_max_points_this_tap = 0;
            break;
        default: break;
    }
    /* Always chain to GLFW's original proc so mouse promotion / normal input is untouched. */
    return CallWindowProcW(g_prev_wndproc, hwnd, msg, wparam, lparam);
}

void TouchGestureInit(void)
{
    g_hwnd = (HWND)GetWindowHandle();
    if (!g_hwnd) return;
    g_prev_wndproc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)TouchWndProc);
}

bool TouchGestureConsumeThreeFingerDoubleTap(void)
{
    return g_double_tap.exchange(false);
}

#else  /* non-Windows: feature disabled, no-op */

void TouchGestureInit(void) {}
bool TouchGestureConsumeThreeFingerDoubleTap(void) { return false; }

#endif
