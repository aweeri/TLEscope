// camera_controller.h
#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include "raylib.h"

/// Updates the camera position based on mouse movement and zoom.
void UpdateCameraController(Camera3D *camera, float *angleX, float *angleY, float *distance);

#endif // CAMERA_CONTROLLER_H
