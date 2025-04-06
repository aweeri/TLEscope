#ifndef TLE_SCOPE_RENDERER_H
#define TLE_SCOPE_RENDERER_H

#include <raylib.h>

class cRenderer
{
public:
	cRenderer(int screenWidth, int screenHeight);
	~cRenderer();

	void SetSceneSpecific();
	void Render();

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