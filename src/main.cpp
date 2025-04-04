#include "raylib.h"
#include <math.h>
#include <raymath.h>
#include <point.h>
#include <camera.h>
#include <conversions.h>
#include <stdio.h>

int main() {
    // Window Initialization
    const int screenWidth  = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "TLEscope");
    Image icon = LoadImage("resources/icon/tlescopeico_512.png");
    Font font = LoadFontEx("resources/RobotoFont.ttf", 16, 0, 0);
    SetWindowIcon(icon);    
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);

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
    static float cloudSphereAngle = 0.0f;

    //Vector3 QNH = LatLonToXYZ(52.22f, 21.01f, 1.0f); //Poland
    Vector3 QNH = LatLonToXYZ(33.89f, 134.185f, 1.0f);
    //Vector3 QNH = LatLonToXYZ(-41.2865f, 174.7762f, 1.0f); //NZ

    // Main Application Loop
    while (!WindowShouldClose()) {
        UpdateCameraController(&camera, &angleX, &angleY, &distance);
        
        BeginDrawing();
            ClearBackground(BLACK);
            char fpsText[20];
            sprintf(fpsText, "FPS: %i", GetFPS());
            DrawTextEx(font, fpsText, (Vector2){ 10, 10 }, font.baseSize, 2, WHITE);
            BeginMode3D(camera);
                DrawCube(QNH, 0.01f, 0.01f, 0.01f, BLUE);
                DrawModelEx(earthModel, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f, (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);
                cloudSphereAngle += 0.002f; // Adjust rotation speed as needed
                DrawModelEx(cloudsModel, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, cloudSphereAngle, (Vector3){ 1.001f, 1.001f, 1.001f }, WHITE);
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
