#include "scene/Scene.h"
#include <raylib.h>
#include <raymath.h>

cScene::cScene()
{
    earthTexture = { 0 };
    cloudTexture = { 0 };
}

cScene::~cScene()
{
    tModelNameMapIt it;
    for (it = modelsbyName.begin(); it != modelsbyName.end(); it++)
    {
        UnloadModel(it->second);
    }
    modelsbyName.clear();

    if (earthTexture.id > 0)
        UnloadTexture(earthTexture);

    if (cloudTexture.id > 0)
        UnloadTexture(cloudTexture);
}

void cScene::GenEarth()
{
    // Create mesh and models, then rotate the models by -90 degrees on the X axis.
    Mesh earthMesh = GenMeshSphere(5, 128, 256);
    Mesh cloudMesh = GenMeshSphere(5, 128, 256);

    // Rotate UVs 90 degrees counterclockwise
    for (int k = 0; k < earthMesh.vertexCount; k++)
    {
        // Get existing UVs
        float u = earthMesh.texcoords[k * 2];
        float v = earthMesh.texcoords[k * 2 + 1];

        // Translate UVs to center
        float centeredU = u - 0.5f;
        float centeredV = v - 0.5f;

        // Rotate
        float rotatedU = -centeredV;
        float rotatedV = centeredU;

        // Translate back
        earthMesh.texcoords[k * 2] = rotatedU + 0.5f;
        earthMesh.texcoords[k * 2 + 1] = rotatedV + 0.5f;
        cloudMesh.texcoords[k * 2] = rotatedU + 0.5f;
        cloudMesh.texcoords[k * 2 + 1] = rotatedV + 0.5f;
    }

    // Update mesh buffer
    UpdateMeshBuffer(earthMesh, 1, earthMesh.texcoords, earthMesh.vertexCount * 2 * sizeof(float), 0);
    UpdateMeshBuffer(cloudMesh, 1, cloudMesh.texcoords, cloudMesh.vertexCount * 2 * sizeof(float), 0);

    Model earthModel = LoadModelFromMesh(earthMesh);
    earthModel.transform = MatrixMultiply(MatrixIdentity(), MatrixRotate({ 1, 0, 0 }, -90.0f * (PI / 180.0f)));
    Model cloudModel = LoadModelFromMesh(cloudMesh);
    cloudModel.transform = MatrixMultiply(MatrixIdentity(), MatrixRotate({ 1, 0, 0 }, -90.0f * (PI / 180.0f)));

    // Load textures, generate mipmaps, set trilinear filtering, and assign materials to the appropriate models.
    earthTexture = LoadTexture("resources/daymap8k.png");
    GenTextureMipmaps(&earthTexture);
    SetTextureFilter(earthTexture, TEXTURE_FILTER_TRILINEAR);
    cloudTexture = LoadTexture("resources/cloudcover8k.png");
    GenTextureMipmaps(&cloudTexture);
    SetTextureFilter(cloudTexture, TEXTURE_FILTER_TRILINEAR);

    SetMaterialTexture(earthModel.materials, MATERIAL_MAP_DIFFUSE, earthTexture);
    SetMaterialTexture(cloudModel.materials, MATERIAL_MAP_DIFFUSE, cloudTexture);

    modelsbyName.insert(tModelNameMap::value_type(std::string("earth"), earthModel));
    modelsbyName.insert(tModelNameMap::value_type(std::string("cloud"), cloudModel));
}

Model& cScene::GetModelByName(const std::string& asName)
{
    tModelNameMapIt it = modelsbyName.find(asName);
    if (it == modelsbyName.end()) {
        static Model defaultModel = { 0 };
        return defaultModel;
    }
    return it->second;
}
