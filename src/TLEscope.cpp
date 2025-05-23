#include "TLEscope.h"
#include <raylib.h>

cTLEscope* gpBase = nullptr;

cTLEscope::cTLEscope()
{
	screenWidth = 1280;
	screenHeight = 720;

	pScene = NULL;
	pCamera = NULL;
	pRenderer = NULL;
	pBillboardHelper = NULL;
}

cTLEscope::~cTLEscope()
{
	delete pScene;
	delete pCamera;
	delete pRenderer;
	delete pBillboardHelper;
}

bool cTLEscope::Init()
{
	SetConfigFlags(FLAG_VSYNC_HINT);
	InitWindow(screenWidth, screenHeight, "Loading...");
	Image windowIcon = LoadImage("resources/icon/tlescopeico_512.png");
	SetWindowIcon(windowIcon);
	UnloadImage(windowIcon);
	SetWindowState(FLAG_WINDOW_RESIZABLE);

	pScene = new cScene;
	pCamera = new cCamera;
	pRenderer = new cRenderer(screenWidth, screenHeight);
	pBillboardHelper = new cBillboardHelper;

	if (!pScene || !pRenderer || !pCamera || !pBillboardHelper)
		return false;

	//The init is over
	InitOver();

	return true;
}

void cTLEscope::InitOver()
{
	//Set proper caption when loading is done.
	SetWindowTitle("TLEscope");
}
