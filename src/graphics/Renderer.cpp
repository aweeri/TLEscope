#include "graphics/Renderer.h"
#include "scene/BillboardHelper.h"
#include "TLEscope.h"
#include <raylib.h>
#include <raymath.h>
#include <string>

#if defined(PLATFORM_DESKTOP)
#define GLSL_VERSION            330
#else   // PLATFORM_ANDROID, PLATFORM_WEB
#define GLSL_VERSION            100
#endif

cRenderer::cRenderer(int argScreenWidth, int argScreenHeight)
{
    shader = LoadShader(TextFormat("resources/shaders/glsl%i/base.vs", GLSL_VERSION), TextFormat("resources/shaders/glsl%i/base.fs", GLSL_VERSION));
    renderTarget = LoadRenderTexture(argScreenWidth, argScreenHeight);
    fontRoboto = LoadFontEx("resources/fonts/RobotoFont.ttf", 16, 0, 0);

    earthCenter = { 0.0f, 0.0f, 0.0f };
    cloudSphereAngle = 0.0f;
    cloudRotationMul = 0.001f;
    baseCloudTransform = { 0 };
}

cRenderer::~cRenderer()
{
    UnloadShader(shader);
    UnloadRenderTexture(renderTarget);
    UnloadFont(fontRoboto);
}

void cRenderer::SetSceneSpecific()
{
    cScene* pScene = gpBase->GetScene();
    baseCloudTransform = pScene->GetModelByName("cloud").transform;

    Image locationImage = LoadImage("resources/mylocation.png");
    Texture2D locationTexture = LoadTextureFromImage(locationImage);
    UnloadImage(locationImage);
    GenTextureMipmaps(&locationTexture);
    gpBase->GetBillboardHelper()->CreateBillboard(std::string("MyLocation"), locationTexture, { 0.0f, 6.0f, 0.0f }, 1.0f, WHITE);
}

void cRenderer::Render()
{
    if (IsWindowResized())
    {
        UnloadRenderTexture(renderTarget);
        renderTarget = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    }

    cCamera* pCam = gpBase->GetCamera();
    pCam->UpdateCameraController();

    BeginTextureMode(renderTarget);
        ClearBackground(BLACK);

        BeginMode3D(pCam->GetCamera3D());
            cScene* pScene = gpBase->GetScene();
            Model& earthModel = pScene->GetModelByName(std::string("earth"));
            DrawModel(earthModel, { 0, 0, 0 }, 1.0f, WHITE);

            Model& cloudModel = pScene->GetModelByName(std::string("cloud"));
            cloudSphereAngle = fmodf(cloudSphereAngle + cloudRotationMul * GetFrameTime(), 2 * PI);
            cloudModel.transform = MatrixMultiply(MatrixRotate({ 0, 0, 1 }, cloudSphereAngle), baseCloudTransform);

            float earthCamDistance = Vector3Distance(pCam->GetCamera3D().position, earthCenter);
            // Define constants for the fade range near Earth
            const float fadeStart = 7.5f; // Start fading when closer than 1.5 units from Earth's center
            const float fadeEnd   = 6.5f; // Fully transparent when closer than 1.0 unit
            
            float alpha;
            if (earthCamDistance >= fadeStart) {
                alpha = 1.0f;   // Fully opaque if farther than fadeStart
            } else if (earthCamDistance <= fadeEnd) {
                alpha = 0.0f;   // Fully transparent if closer than fadeEnd
            } else {
                // Linearly interpolate alpha between fadeEnd and fadeStart distances
                alpha = (earthCamDistance - fadeEnd) / (fadeStart - fadeEnd);
            }
            

            unsigned char preMultiplied = (unsigned char)(255 * alpha);
            Color cloudColor = { preMultiplied, preMultiplied, preMultiplied, (unsigned char)(255 * alpha) };
            cloudModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = cloudColor;
            BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
                DrawModel(cloudModel, { 0, 0, 0 }, 1.01f, WHITE);
            EndBlendMode();

            DrawBillboard(gpBase->GetScene()->GetBillboardByName(std::string("MyLocation")));
        EndMode3D();

    EndTextureMode();


    BeginDrawing();
        ClearBackground(BLACK);

        BeginShaderMode(shader);
            DrawTextureRec(renderTarget.texture, { 0, 0, (float)renderTarget.texture.width, (float)-renderTarget.texture.height }, { 0, 0 }, WHITE);
        EndShaderMode();

        // Draw text at the very end
        char fpsText[20];
        sprintf(fpsText, "FPS: %i", GetFPS());
        DrawTextEx(fontRoboto, fpsText, { 10, 10 }, (float)fontRoboto.baseSize, 2, WHITE);
    EndDrawing();
}


void cRenderer::DrawBillboard(Billboard* argBillboard)
{
    cCamera* pCam = gpBase->GetCamera();

    int Type = argBillboard->GetType();

    if (Type == BILLBOARD)
        ::DrawBillboard(pCam->GetCamera3D(),
            argBillboard->GetTexture(),
            argBillboard->GetPosition(),
            argBillboard->GetScale(),
            argBillboard->GetTint());
    else if (Type == BILLBOARD_REC)
        ::DrawBillboardRec(pCam->GetCamera3D(),
            argBillboard->GetTexture(),
            argBillboard->GetSource(),
            argBillboard->GetPosition(),
            argBillboard->GetSize(),
            argBillboard->GetTint());
    else if (Type == BILLBOARD_PRO)
        ::DrawBillboardPro(pCam->GetCamera3D(),
            argBillboard->GetTexture(),
            argBillboard->GetSource(),
            argBillboard->GetPosition(),
            argBillboard->GetUp(),
            argBillboard->GetSize(),
            argBillboard->GetOrigin(),
            argBillboard->GetRotation(),
            argBillboard->GetTint());
}