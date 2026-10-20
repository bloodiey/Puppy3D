#pragma once

#include "Engine.h"

namespace EditorControls
{
    void moveCamera(EngineCamera& camera, float forward, float strafe,
        float vertical, float deltaTime);
    void orbitCamera(EngineCamera& camera, double deltaX, double deltaY);
}