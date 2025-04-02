// main.cpp
#include "raylib.h"
#include "camera_controller.h"
#include "rendering.h"
#include <math.h>

int main(void) {
    // Initialization
    const int screenWidth = 1280;
    const int screenHeight = 720;
    InitWindow(screenWidth, screenHeight, "TLEscope");
    SetWindowState(FLAG_WINDOW_RESIZABLE);

    // Set up the camera
    Camera3D camera = { 0 };
    camera.position = Vector3{ 10.0f, 10.0f, 10.0f };
    camera.target   = Vector3{ 0.0f, 0.0f, 0.0f };
    camera.up       = Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy     = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Orbital camera parameters
    float distance = 10.0f;
    float angleX = 0.0f;
    float angleY = 0.0f;

    // Create sphere model with texture
    Mesh sphereMesh = GenMeshSphere(1.0f, 32, 32);
    Model sphereModel = LoadModelFromMesh(sphereMesh);
    Texture2D texture = LoadTexture("resources/8k_earth_daymap.jpg");  // Ensure the texture file exists
    sphereModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;

    SetTargetFPS(144);

    // Main game loop
    while (!WindowShouldClose()) {
        UpdateCameraController(&camera, &angleX, &angleY, &distance);
        DrawScene(sphereModel, camera);
    }

    // Unload resources and close window
    UnloadTexture(texture);
    UnloadModel(sphereModel);
    CloseWindow();

    return 0;
}
