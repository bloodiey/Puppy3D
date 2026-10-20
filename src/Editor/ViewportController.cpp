#include "ViewportController.h"

#include <algorithm>
#include <cmath>

namespace EditorControls
{
    void moveCamera(EngineCamera& camera, float forward, float strafe,
        float vertical, float deltaTime)
    {
        const float yaw = camera.yaw * 0.017453292519943295f;
        const float speed = 6.0f * std::clamp(deltaTime, 0.0f, 0.1f);
        camera.position[0] += (std::cos(yaw) * forward - std::sin(yaw) * strafe) * speed;
        camera.position[1] += vertical * speed;
        camera.position[2] += (std::sin(yaw) * forward + std::cos(yaw) * strafe) * speed;
    }

    void orbitCamera(EngineCamera& camera, double deltaX, double deltaY)
    {
        camera.yaw += static_cast<float>(deltaX) * 0.25f;
        camera.pitch = std::clamp(camera.pitch - static_cast<float>(deltaY) * 0.25f,
            -89.0f, 89.0f);
    }
}