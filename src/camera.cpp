#include "raylib.h"
#include <raymath.h>
#include <point.h>
#include "camera.h"

void UpdateCameraController(Camera3D* camera, float* angleX, float* angleY, float* distance) {
    const float sensitivity = 0.01f;
    // Using a static variable to hold our desired distance for smooth zooming.
    static float targetDistance = 10.0f;

    // Rotate with right mouse button
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        *angleX -= mouseDelta.x * sensitivity;
        *angleY += mouseDelta.y * sensitivity;

        // Clamp vertical rotation to prevent flipping
        if (*angleY > 1.5f)  *angleY = 1.5f;
        if (*angleY < -1.5f) *angleY = -1.5f;
    }

    // Zoom in/out using mouse wheel (works without holding any mouse button)
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        targetDistance -= wheel * 0.5f;  // adjust zoom speed for smoother feel
        if (targetDistance < 0.01f)   targetDistance = 0.01f;
        if (targetDistance > 100.0f) targetDistance = 100.0f;
    }
    // Smoothly interpolate the current distance toward the target distance
    *distance += (targetDistance - *distance) * 0.1f;

    // Pan (move the origin/target) with middle mouse button
    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        // Compute the forward vector from the camera's position to its target
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
        // Compute the right vector as perpendicular to forward and the global up
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
        // Compute a true up vector for the camera view
        Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
        // Pan speed factor can depend on the current distance to keep movement consistent
        float panSpeed = 0.001f * (*distance);
        // Update the camera target based on mouse movement
        camera->target = Vector3Add(camera->target, Vector3Scale(right, -mouseDelta.x * panSpeed));
        camera->target = Vector3Add(camera->target, Vector3Scale(up, mouseDelta.y * panSpeed));
    }

    // Always update camera position using spherical coordinates relative to the target.
    camera->position.x = camera->target.x + *distance * cosf(*angleY) * sinf(*angleX);
    camera->position.y = camera->target.y + *distance * sinf(*angleY);
    camera->position.z = camera->target.z + *distance * cosf(*angleY) * cosf(*angleX);
}
