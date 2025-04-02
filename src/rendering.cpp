// rendering.cpp
#include "rendering.h"

void DrawScene(Model model, Camera3D camera) {
    BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
            Vector3 spherePosition = { 0.0f, 0.0f, 0.0f };
            DrawModelEx(
                model, 
                spherePosition, 
                Vector3{ 1.0f, 0.0f, 0.0f }, 
                0.0f, 
                Vector3{ 1.0f, 1.0f, 1.0f },
                WHITE
            );
            DrawGrid(10, 1.0f);
        EndMode3D();
    EndDrawing();
}
