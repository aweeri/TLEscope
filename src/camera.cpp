#include "raylib.h"
#include <raymath.h>
#include <point.h>
#include "camera.h"

void UpdateCameraController(Camera3D* camera, float* angleX, float* angleY, float* distance, float* maxZoom) {
    const float sensitivity = 0.005f;
    const float smoothFactor = 0.1f;
    static float targetAngleX = 0.0f;
    static float targetAngleY = 0.0f;
    static float targetDistance = 10.0f;

    // Rotate with right mouse button
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        targetAngleX -= mouseDelta.x * sensitivity;
        targetAngleY += mouseDelta.y * sensitivity;

        // Clamp vertical rotation to prevent flipping
        if (targetAngleY > 1.5f)  targetAngleY = 1.5f;
        if (targetAngleY < -1.5f) targetAngleY = -1.5f;
    }

    // interpolate angles
    *angleX += (targetAngleX - *angleX) * smoothFactor;
    *angleY += (targetAngleY - *angleY) * smoothFactor;

    // Zoom in/out
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        float zoomSpeed = fminf(1.5f, 0.15f * (*distance));  // cap zoom speed at 0.8
        targetDistance -= wheel * zoomSpeed;
        if (targetDistance < *maxZoom)   targetDistance = *maxZoom;
        if (targetDistance > 100.0f) targetDistance = 100.0f;
    }
    // Smoothly interpolate the current distance toward the target distance
    *distance += (targetDistance - *distance) * 0.1f;

    // Pan with middle mouse button
    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        // Compute the forward vector
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
        // Compute a true up vector
        Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
        // Pan speed factor
        float panSpeed = 0.001f * (*distance);
        // Update the camera target
        camera->target = Vector3Add(camera->target, Vector3Scale(right, -mouseDelta.x * panSpeed));
        camera->target = Vector3Add(camera->target, Vector3Scale(up, mouseDelta.y * panSpeed));
    }

    // Always update camera position using spherical coordinates relative to the target.
    camera->position.x = camera->target.x + *distance * cosf(*angleY) * sinf(*angleX);
    camera->position.y = camera->target.y + *distance * sinf(*angleY);
    camera->position.z = camera->target.z + *distance * cosf(*angleY) * cosf(*angleX);
}
