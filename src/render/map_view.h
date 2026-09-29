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

inline void BeginMapMode2D(Camera2D camera)
{
    Matrix screen_transform = rlGetMatrixModelview();
    BeginMode2D(camera);
    rlSetMatrixModelview(MatrixMultiply(rlGetMatrixModelview(), screen_transform));
}
