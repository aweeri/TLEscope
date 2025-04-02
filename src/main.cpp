#include "raylib.h"
#include <math.h>

// ===================================================================================
// Camera Controller
// ===================================================================================

/// Updates the camera position based on mouse movement and zoom.
void UpdateCameraController(Camera3D* camera, float* angleX, float* angleY, float* distance) {
    const float sensitivity = 0.01f;

    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        *angleX -= mouseDelta.x * sensitivity;
        *angleY += mouseDelta.y * sensitivity;

        // Clamp vertical rotation to prevent flipping
        if (*angleY > 1.5f)  *angleY = 1.5f;
        if (*angleY < -1.5f) *angleY = -1.5f;

        // Update camera position using spherical coordinates
        camera->position.x = camera->target.x + *distance * cosf(*angleY) * sinf(*angleX);
        camera->position.y = camera->target.y + *distance * sinf(*angleY);
        camera->position.z = camera->target.z + *distance * cosf(*angleY) * cosf(*angleX);
    }

    // Zoom in/out using mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        *distance -= wheel;
        if (*distance < 2.0f)   *distance = 2.0f;
        if (*distance > 100.0f) *distance = 100.0f;
    }
}

// ===================================================================================
// Scene Rendering
// ===================================================================================

/// Draws the 3D scene including the model and grid.
void DrawScene(Model model, Camera3D camera) {
    BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);
            Vector3 modelPosition = { 0.0f, 0.0f, 0.0f };
            DrawModelEx(model, modelPosition, Vector3{ 1.0f, 0.0f, 0.0f }, 0.0f, Vector3{ 1.0f, 1.0f, 1.0f }, WHITE);
            DrawGrid(10, 1.0f);
        EndMode3D();

    EndDrawing();
}

// ===================================================================================
// Main Entry Point
// ===================================================================================

int main() {
    // Window Initialization
    const int screenWidth  = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "TLEscope");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(144);

    // Camera Setup
    Camera3D camera = { 0 };
    camera.position   = Vector3{ 10.0f, 10.0f, 10.0f };
    camera.target     = Vector3{ 0.0f, 0.0f, 0.0f };
    camera.up         = Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy       = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    float distance = 10.0f;
    float angleX   = 0.0f;
    float angleY   = 0.0f;

    // Load Sphere Model and Texture
    Mesh sphereMesh     = GenMeshSphere(1.0f, 32, 32);
    Model sphereModel   = LoadModelFromMesh(sphereMesh);
    Texture2D texture   = LoadTexture("resources/8k_earth_daymap.jpg");  // Ensure the file exists
    sphereModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;

    // Main Application Loop
    while (!WindowShouldClose()) {
        UpdateCameraController(&camera, &angleX, &angleY, &distance);
        DrawScene(sphereModel, camera);
    }

    // Cleanup
    UnloadTexture(texture);
    UnloadModel(sphereModel);
    CloseWindow();

    return 0;
}
