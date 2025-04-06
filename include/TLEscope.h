#include "scene/Scene.h"
#include "scene/Camera.h"
#include "graphics/Renderer.h"

#ifndef TLE_SCOPE_BASE_H
#define TLE_SCOPE_BASE_H

class cTLEscope
{
public:
	cTLEscope();
	~cTLEscope();

	bool Init();
	void InitOver();

	cScene* GetScene() { return pScene; }
	cCamera* GetCamera() { return pCamera; }
	cRenderer* GetRenderer() { return pRenderer; }

private:
	int screenWidth;
	int screenHeight;

	cScene* pScene;
	cCamera* pCamera;
	cRenderer* pRenderer;
};

extern cTLEscope* gpBase;

#endif // TLE_SCOPE_BASE_H