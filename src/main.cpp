#include "raylib.h"
#include <math.h>
#include <raymath.h>

// A simple linear interpolation for floats
static float LerpFloat(float start, float end, float t) {
    return start + t * (end - start);
}

// ===================================================================================
// Camera Controller
// ===================================================================================

/// Updates the camera position based on mouse movement, zoom, and panning.
void UpdateCameraController(Camera3D* camera, float* angleX, float* angleY, float* distance) {
    const float sensitivity = 0.01f;
    // Using a static variable to hold our desired distance for smooth zooming.
    static float targetDistance = 10.0f;

    // Rotate with right mouse button
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        *angleX -= mouseDelta.x * sensitivity;
        *angleY += mouseDelta.y * sensitivity;

        // Clamp vertical rotation to prevent flipping
        if (*angleY > 1.5f)  *angleY = 1.5f;
        if (*angleY < -1.5f) *angleY = -1.5f;
    }

    // Zoom in/out using mouse wheel (works without holding any mouse button)
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        targetDistance -= wheel * 0.5f;  // adjust zoom speed for smoother feel
        if (targetDistance < 1.2f)   targetDistance = 1.2f;
        if (targetDistance > 100.0f) targetDistance = 100.0f;
    }
    // Smoothly interpolate the current distance toward the target distance
    *distance += (targetDistance - *distance) * 0.1f;

    // Pan (move the origin/target) with middle mouse button
    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        // Compute the forward vector from the camera's position to its target
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
        // Compute the right vector as perpendicular to forward and the global up
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
        // Compute a true up vector for the camera view
        Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
        // Pan speed factor can depend on the current distance to keep movement consistent
        float panSpeed = 0.005f * (*distance);
        // Update the camera target based on mouse movement
        camera->target = Vector3Add(camera->target, Vector3Scale(right, -mouseDelta.x * panSpeed));
        camera->target = Vector3Add(camera->target, Vector3Scale(up, mouseDelta.y * panSpeed));
    }

    // Always update camera position using spherical coordinates relative to the target.
    camera->position.x = camera->target.x + *distance * cosf(*angleY) * sinf(*angleX);
    camera->position.y = camera->target.y + *distance * sinf(*angleY);
    camera->position.z = camera->target.z + *distance * cosf(*angleY) * cosf(*angleX);
}

// Converts latitude and longitude (in degrees) to a 3D position on a sphere.
Vector3 LatLonToXYZ(float latitude, float longitude, float radius) {
    // Convert degrees to radians using Raylib's DEG2RAD constant
    float latRad = latitude * DEG2RAD;
    float lonRad = longitude * DEG2RAD;

    Vector3 pos;
    pos.x = radius * cosf(latRad) * cosf(lonRad);
    pos.y = radius * sinf(latRad);
    pos.z = radius * cosf(latRad) * sinf(lonRad);
    return pos;
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
    camera.position   = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target     = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy       = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    float distance = 10.0f;
    float angleX   = 0.0f;
    float angleY   = 0.0f;

    // Load Sphere Model and Texture
    Model earthModel   = LoadModel("resources/sphere.obj");  // Ensure the file exists
    Texture2D earthTexture   = LoadTexture("resources/daymap8k.png");  // Ensure the file exists
    earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;

    // Load another Sphere Model with Transparent Texture
    Model cloudsModel = LoadModel("resources/sphere.obj");  // Ensure the file exists
    Texture2D cloudsTexture = LoadTexture("resources/cloudcover8k.png");  // Ensure the file exists
    cloudsModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloudsTexture;

    // Main Application Loop
    while (!WindowShouldClose()) {
        UpdateCameraController(&camera, &angleX, &angleY, &distance);
        
        BeginDrawing();
            ClearBackground(BLACK);

            BeginMode3D(camera);
                DrawModelEx(earthModel, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }, 0.0f, (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);
                DrawModelEx(cloudsModel, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }, 0.0f, (Vector3){ 1.001f, 1.001f, 1.001f }, WHITE);
                //DrawGrid(10, 1.0f);
            EndMode3D();
        EndDrawing();
    }

    // Cleanup
    UnloadTexture(earthTexture);
    UnloadTexture(cloudsTexture);
    UnloadModel(earthModel);
    UnloadModel(cloudsModel);
    CloseWindow();

    return 0;
}
