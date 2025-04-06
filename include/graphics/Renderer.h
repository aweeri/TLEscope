#ifndef TLE_SCOPE_RENDERER_H
#define TLE_SCOPE_RENDERER_H

#include "raylib.h"
#include <map>

class cRenderer
{
public:
	cRenderer(int alScreenWidth, int alScreenHeight);
	~cRenderer();

	void SetSceneSpecific();
	void Render();

private:
	Shader shader;
	RenderTexture2D renderTarget;
	Font fontRoboto;
	//Font fontConsola;
	Vector3 earthCenter;
	float cloudSphereAngle;
	float cloudRotationMul;
	Matrix baseCloudTransform;
};

#endif