#include "raylib.h"
#include <math.h>
#include <raymath.h>
#include <point.h>
#include <camera.h>
#include <conversions.h>
#include <stdio.h>

#if defined(PLATFORM_DESKTOP)
#define GLSL_VERSION            330
#else   // PLATFORM_ANDROID, PLATFORM_WEB
#define GLSL_VERSION            100
#endif

int main() {
    // Window Initialization
    const int screenWidth  = 1280;
    const int screenHeight = 720;

    // Let there be VSYNC
    SetConfigFlags(FLAG_VSYNC_HINT);

    InitWindow(screenWidth, screenHeight, "TLEscope");
    Image icon = LoadImage("resources/icon/tlescopeico_512.png");
    Font font = LoadFontEx("resources/RobotoFont.ttf", 16, 0, 0);
    SetWindowIcon(icon);    
    SetWindowState(FLAG_WINDOW_RESIZABLE);

    // Camera Setup
    Camera3D camera = { 0 };
    camera.position = { 10.0f, 10.0f, 10.0f };
    camera.target     = { 0.0f, 0.0f, 0.0f };
    camera.up         = { 0.0f, 1.0f, 0.0f };
    camera.fovy       = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    float distance = 10.0f;
    float angleX   = 0.0f;
    float angleY   = 0.0f;

    // Load Earth model
    Model earthModel   = LoadModel("resources/earth.obj");

    // Load textures, generate mipmaps, set trilinear filtering, and assign to the appropriate meshes.
    Texture2D earthTexture = LoadTexture("resources/daymap8k.png");
    GenTextureMipmaps(&earthTexture);
    SetTextureFilter(earthTexture, TEXTURE_FILTER_TRILINEAR);
    Texture2D cloudTexture = LoadTexture("resources/cloudcover8k.png");
    GenTextureMipmaps(&cloudTexture);
    SetTextureFilter(cloudTexture, TEXTURE_FILTER_TRILINEAR);

    earthModel.materials[1].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;
    earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloudTexture;

    static float cloudSphereAngle = 0.0f;
    static float cloudRotationMul = 0.001f;

    // Load shaders and create render target
    Shader shader = LoadShader(TextFormat("resources/shaders/glsl%i/base.vs", GLSL_VERSION), TextFormat("resources/shaders/glsl%i/base.fs", GLSL_VERSION));
    RenderTexture2D renderTarget = LoadRenderTexture(screenWidth, screenHeight);

    //Vector3 QNH = LatLonToXYZ(52.22f, 21.01f, 1.0f); //Poland
    Vector3 QNH = LatLonToXYZ(33.89f, 134.185f, 1.0f);
    //Vector3 QNH = LatLonToXYZ(-41.2865f, 174.7762f, 1.0f); //NZ

    // Set max zoom level based on Earth's size.
    BoundingBox bbEarth = GetModelBoundingBox(earthModel);
    float maxZoom = bbEarth.max.x + 0.1f;

    // Main Application Loop
    while (!WindowShouldClose()) {
        UpdateCameraController(&camera, &angleX, &angleY, &distance, &maxZoom);

        BeginTextureMode(renderTarget);
            ClearBackground(BLACK);

            BeginMode3D(camera);
                // Use fmodf to avoid accumulating a huge value and make the rotation frame rate independent
                cloudSphereAngle = fmodf(cloudSphereAngle + cloudRotationMul * GetFrameTime(), 2 * PI);
                // Cloud layer rotation matrix (cloudSphereAngle represents the rotation in radians)
                Matrix cloudRotation = MatrixRotate({ 0, 1, 0 }, cloudSphereAngle);

                // Draw Earth and cloud meshes. For some bizzare reason, Raylib appears to be splitting each mesh into two.
                // Earth
                DrawMesh(earthModel.meshes[0], earthModel.materials[1], MatrixIdentity());
                DrawMesh(earthModel.meshes[1], earthModel.materials[1], MatrixIdentity());
                // Clouds
                DrawMesh(earthModel.meshes[2], earthModel.materials[0], MatrixMultiply(MatrixIdentity(), cloudRotation));
                DrawMesh(earthModel.meshes[3], earthModel.materials[0], MatrixMultiply(MatrixIdentity(), cloudRotation));
            EndMode3D();

        EndTextureMode();


        BeginDrawing();
            ClearBackground(BLACK);

            BeginShaderMode(shader);
                DrawTextureRec(renderTarget.texture, { 0, 0, (float)renderTarget.texture.width, (float)-renderTarget.texture.height }, { 0, 0 }, WHITE);
            EndShaderMode();

            // Draw text at the very end
            char fpsText[20];
            sprintf_s(fpsText, "FPS: %i", GetFPS());
            DrawTextEx(font, fpsText, { 10, 10 }, (float)font.baseSize, 2, WHITE);

        EndDrawing();
    }
    
    // Cleanup
    UnloadModel(earthModel);
    UnloadTexture(earthTexture);
    UnloadTexture(cloudTexture);
    UnloadShader(shader);
    UnloadRenderTexture(renderTarget);
    CloseWindow();

    return 0;
}
