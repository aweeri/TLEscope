#pragma once

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

/* The bundled raylib's BeginMode2D resets the modelview without preserving
 * the DPI transform from BeginDrawing. Keep camera coordinates in logical
 * pixels, matching GetWorldToScreen2D, mouse input, and the ImGui overlay. */
/* Smallest zoom at which the map still covers the whole window. The map wraps
 * horizontally, so overshooting the width is fine; only the height must fit. */
inline float MapFillZoom(float map_w, float map_h)
{
    return fmaxf((float)GetScreenWidth() / map_w, (float)GetScreenHeight() / map_h);
}

/* Does horizontal wrap copy k (-1, 0, +1) of the map intersect the viewport? */
inline bool MapCopyVisible(const Camera2D &cam, float map_w, int k)
{
    float min_x = cam.target.x - cam.offset.x / cam.zoom;
    float max_x = cam.target.x + ((float)GetScreenWidth() - cam.offset.x) / cam.zoom;
    return k * map_w + map_w * 0.5f > min_x && k * map_w - map_w * 0.5f < max_x;
}

inline void BeginMapMode2D(Camera2D camera)
{
    Matrix screen_transform = rlGetMatrixModelview();
    BeginMode2D(camera);
    rlSetMatrixModelview(MatrixMultiply(rlGetMatrixModelview(), screen_transform));
}
