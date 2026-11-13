#pragma once

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

/* The bundled raylib's BeginMode2D resets the modelview without preserving
 * the DPI transform from BeginDrawing. Keep camera coordinates in logical
 * pixels, matching GetWorldToScreen2D, mouse input, and the ImGui overlay. */

/* Smallest zoom at which the map still covers the whole window. */
inline float MapFillZoom(float map_w, float map_h)
{
    return fmaxf((float)GetScreenWidth() / map_w, (float)GetScreenHeight() / map_h);
}

/* Smallest zoom at which the map still covers an arbitrary viewport size. */
inline float MapFillZoomFor(float map_w, float map_h, float vp_w, float vp_h)
{
    return fmaxf(vp_w / map_w, vp_h / map_h);
}

inline void BeginMapMode2D(Camera2D camera)
{
    Matrix screen_transform = rlGetMatrixModelview();
    BeginMode2D(camera);
    rlSetMatrixModelview(MatrixMultiply(rlGetMatrixModelview(), screen_transform));
}

/* 3D viewport sub-rect (central canvas), published by main.cpp each frame */
inline float g_view3d_x = 0.0f;
inline float g_view3d_y = 0.0f;
inline float g_view3d_w = 0.0f;
inline float g_view3d_h = 0.0f;

inline void Viewport3DSetRect(float x, float y, float w, float h)
{
    g_view3d_x = x;
    g_view3d_y = y;
    g_view3d_w = w;
    g_view3d_h = h;
}

/* Inclusive range of horizontally repeated map copies intersecting the scene
 * viewport. This also handles deliberate zoom-out beyond one map width. */
inline void MapVisibleCopyRange(const Camera2D &cam, float map_w, int *first, int *last)
{
    if (map_w <= 0.0f)
    {
        if (first) *first = 0;
        if (last) *last = 0;
        return;
    }

    const float sx0 = g_view3d_w > 0.0f ? g_view3d_x : 0.0f;
    const float sx1 = g_view3d_w > 0.0f ? g_view3d_x + g_view3d_w : (float)GetScreenWidth();
    const float sy = g_view3d_h > 0.0f ? g_view3d_y + g_view3d_h * 0.5f : cam.offset.y;
    const float wx0 = GetScreenToWorld2D((Vector2){sx0, sy}, cam).x;
    const float wx1 = GetScreenToWorld2D((Vector2){sx1, sy}, cam).x;
    const float min_x = fminf(wx0, wx1);
    const float max_x = fmaxf(wx0, wx1);

    if (first) *first = (int)floorf((min_x + map_w * 0.5f) / map_w);
    if (last) *last = (int)floorf((max_x + map_w * 0.5f) / map_w);
}

/** ratio of framebuffer (render) pixels to logical (screen) pixels */
inline Vector2 Viewport3DPixelRatio(void)
{
    float sx = (GetScreenWidth()  > 0) ? (float)GetRenderWidth()  / (float)GetScreenWidth()  : 1.0f;
    float sy = (GetScreenHeight() > 0) ? (float)GetRenderHeight() / (float)GetScreenHeight() : 1.0f;
    return (Vector2){ sx, sy };
}

/** point glViewport at the sub-rect (framebuffer pixels, bottom-left origin) */
inline void Viewport3DApplyGL(void)
{
    Vector2 r = Viewport3DPixelRatio();
    int rx  = (int)(g_view3d_x * r.x);
    int rx2 = (int)((g_view3d_x + g_view3d_w) * r.x);
    int ry_top = (int)(g_view3d_y * r.y);
    int ry_bot = (int)((g_view3d_y + g_view3d_h) * r.y);
    int w = rx2 - rx;
    int h = ry_bot - ry_top;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    rlViewport(rx, GetRenderHeight() - ry_bot, w, h);
}

inline void Viewport3DResetGL(void)
{
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
}

// Zoom is unbounded but rlgl's clip planes are fixed (near 0.05, far 4000), so
// derive both from the camera distance: far = max(4000, dist*4, |cam|+700) and
// near = max(dist*1e-4, 0.05). Near must NOT be capped: a tiny near with an
// unbounded far blows the ratio past the 24-bit depth buffer and the whole scene
// (skybox included) goes black.
inline float g_scene_far_extra = 0.0f;

inline float Viewport3DCameraFar(const Camera &cam)
{
    const float dist   = Vector3Distance(cam.position, cam.target);
    const float origin = Vector3Length(cam.position);
    float far_plane = 4000.0f;
    if (isfinite(dist) && isfinite(origin))
        far_plane = fmaxf(fmaxf(4000.0f, dist * 4.0f), origin + 700.0f);
    if (isfinite(g_scene_far_extra) && g_scene_far_extra > far_plane)
        far_plane = g_scene_far_extra;
    return far_plane;
}

inline float Viewport3DCameraNear(const Camera &cam)
{
    const float dist = Vector3Distance(cam.position, cam.target);
    if (!isfinite(dist))
        return 0.05f;
    return fmaxf(dist * 1e-4f, 0.05f);
}

/** push the adaptive near/far into rlgl; must be called before BeginMode3D */
inline void Viewport3DApplyClipPlanes(const Camera &cam)
{
    rlSetClipPlanes((double)Viewport3DCameraNear(cam), (double)Viewport3DCameraFar(cam));
}

/** match the projection aspect to the sub-rect so the 3D content is not stretched */
inline void Viewport3DFixProjection(Camera cam)
{
    if (cam.projection != CAMERA_PERSPECTIVE) return;
    if (g_view3d_w <= 0.0f || g_view3d_h <= 0.0f) return;
    double nearz = rlGetCullDistanceNear();
    double farz  = rlGetCullDistanceFar();
    double top   = nearz * tan(cam.fovy * 0.5 * DEG2RAD);
    double right = top * ((double)g_view3d_w / (double)g_view3d_h);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlFrustum(-right, right, -top, top, nearz, farz);
    rlMatrixMode(RL_MODELVIEW);
}

// Fade markers out as the Earth shrinks to a speck: 1.0 up to 12x its drawn
// radius, smoothstep to 0.0 at 40x. Non-finite or non-positive inputs return 1.0.
inline float MarkerZoomAlpha(float cam_distance, float earth_radius)
{
    constexpr float FADE_START = 12.0f;
    constexpr float FADE_END   = 40.0f;

    if (!isfinite(cam_distance) || !isfinite(earth_radius) ||
        cam_distance <= 0.0f || earth_radius <= 0.0f)
        return 1.0f;

    const float d0 = FADE_START * earth_radius;
    const float d1 = FADE_END * earth_radius;
    if (cam_distance <= d0) return 1.0f;
    if (cam_distance >= d1) return 0.0f;

    float t = (cam_distance - d0) / (d1 - d0);
    t = t * t * (3.0f - 2.0f * t);
    const float a = 1.0f - t;
    if (a < 0.0f) return 0.0f;
    if (a > 1.0f) return 1.0f;
    return a;
}

/** world -> logical screen coords, offset for the sub-rect */
inline Vector2 WorldToScreenViewport3D(Vector3 pos, Camera cam)
{
    if (g_view3d_w <= 0.0f || g_view3d_h <= 0.0f) return GetWorldToScreen(pos, cam);
    Vector2 s = GetWorldToScreenEx(pos, cam, (int)g_view3d_w, (int)g_view3d_h);
    return (Vector2){ s.x + g_view3d_x, s.y + g_view3d_y };
}

/** logical mouse pos -> world ray, offset for the sub-rect */
inline Ray ScreenToWorldRayViewport3D(Vector2 mouse, Camera cam)
{
    if (g_view3d_w <= 0.0f || g_view3d_h <= 0.0f) return GetScreenToWorldRay(mouse, cam);
    return GetScreenToWorldRayEx((Vector2){ mouse.x - g_view3d_x, mouse.y - g_view3d_y },
                                 cam, (int)g_view3d_w, (int)g_view3d_h);
}
