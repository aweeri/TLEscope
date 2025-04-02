// camera_controller.cpp
#include "camera_controller.h"
#include <math.h>

void UpdateCameraController(Camera3D *camera, float *angleX, float *angleY, float *distance) {
    const float sensitivity = 0.01f;

    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        *angleX -= mouseDelta.x * sensitivity;
        *angleY += mouseDelta.y * sensitivity;

        // Clamp vertical rotation to prevent flipping
        if (*angleY > 1.5f) *angleY = 1.5f;
        if (*angleY < -1.5f) *angleY = -1.5f;

        // Update camera position using spherical coordinates
        camera->position.x = camera->target.x + *distance * cosf(*angleY) * sinf(*angleX);
        camera->position.y = camera->target.y + *distance * sinf(*angleY);
        camera->position.z = camera->target.z + *distance * cosf(*angleY) * cosf(*angleX);
    }

    // Zoom in/out using mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        *distance -= wheel;
        if (*distance < 2.0f) *distance = 2.0f;
        if (*distance > 100.0f) *distance = 100.0f;
    }
}
