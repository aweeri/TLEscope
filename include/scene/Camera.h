#include <raylib.h>

#ifndef TLE_SCOPE_CAMERA_H
#define TLE_SCOPE_CAMERA_H

class cCamera
{
public:
	cCamera();
	~cCamera();

	Camera3D& GetCamera3D() { return camera; }

	void UpdateCameraController();
	float GetMaxZoom() { return maxZoom; }

private:
	Camera3D camera;

	float distance;
	float angleX;
	float angleY;
	float sensitivity;
	float targetDistance;
	float targetAngleX;
	float targetAngleY;
	float maxZoom;
};

#endif