#pragma once

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

/* The bundled raylib's BeginMode2D resets the modelview without preserving
 * the DPI transform from BeginDrawing. Keep camera coordinates in logical
 * pixels, matching GetWorldToScreen2D, mouse input, and the ImGui overlay. */
inline void BeginMapMode2D(Camera2D camera)
{
    Matrix screen_transform = rlGetMatrixModelview();
    BeginMode2D(camera);
    rlSetMatrixModelview(MatrixMultiply(rlGetMatrixModelview(), screen_transform));
}
