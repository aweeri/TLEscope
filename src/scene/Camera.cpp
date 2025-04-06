#include "scene/Camera.h"
#include <raylib.h>
#include <raymath.h>

cCamera::cCamera()
{
    camera = { 0 };
    camera.position = { 16.0f, 16.0f, 16.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    distance = 16.0f;
    angleX = 0.0f;
    angleY = 0.0f;
    sensitivity = 0.01f;
    targetDistance = 16.0f;
    targetAngleX = 0.0f;
    targetAngleY = 0.0f;
    maxZoom = 0.5f;
}

cCamera::~cCamera()
{

}

void cCamera::UpdateCameraController()
{
    // --- Rotation ---
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        targetAngleX -= mouseDelta.x * sensitivity;
        targetAngleY += mouseDelta.y * sensitivity;
        targetAngleY = Clamp(targetAngleY, -1.5f, 1.5f);
    }
    float smoothFactor = 0.1f;
    angleX += (targetAngleX - angleX) * smoothFactor;
    angleY += (targetAngleY - angleY) * smoothFactor;


    // --- Zoom ---
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        float zoomSpeed = fminf(1.5f, 0.15f * distance);  // cap zoom speed at 0.8
        targetDistance -= wheel * zoomSpeed;
        targetDistance = Clamp(targetDistance, maxZoom, 50.0f);
    }
    distance += (targetDistance - distance) * 0.1f;

    // --- Panning ---
    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON)) {
        Vector2 mouseDelta = GetMouseDelta();
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
        Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));

        // Update the camera target
        float panSpeed = 0.001f * distance;
        camera.target = Vector3Subtract(camera.target, Vector3Scale(right, mouseDelta.x * panSpeed));
        camera.target = Vector3Add(camera.target, Vector3Scale(up, mouseDelta.y * panSpeed));
    }

    camera.position.x = camera.target.x + distance * cosf(angleY) * sinf(angleX);
    camera.position.y = camera.target.y + distance * sinf(angleY);
    camera.position.z = camera.target.z + distance * cosf(angleY) * cosf(angleX);
}
