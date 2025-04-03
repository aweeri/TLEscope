#include "raylib.h"
#include <math.h>
#include <raymath.h>
#include <point.h>
#include <camera.h>

// A simple linear interpolation for floats
static float LerpFloat(float start, float end, float t) {
    return start + t * (end - start);
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
