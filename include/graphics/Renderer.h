#ifndef TLE_SCOPE_RENDERER_H
#define TLE_SCOPE_RENDERER_H

#include "scene/BillboardHelper.h"
#include <raylib.h>

class cRenderer
{
public:
	cRenderer(int argScreenWidth, int argScreenHeight);
	~cRenderer();

	void SetSceneSpecific();
	void Render();

	void DrawBillboard(Billboard* argBillboard);

private:
	Shader shader;
	RenderTexture2D renderTarget;
	Font fontRoboto;
	Vector3 earthCenter;
	float cloudSphereAngle;
	float cloudRotationMul;
	Matrix baseCloudTransform;
};

#endif