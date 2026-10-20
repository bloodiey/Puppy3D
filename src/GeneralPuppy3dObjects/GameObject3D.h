#pragma once

#include "../EngineVariables/Vector3.h"

namespace GeneralPuppy3dObjects
{
    class GameObject3D
    {
        public:
            EngineVar::Vector3 position;
            EngineVar::Vector3 rotation;
            EngineVar::Vector3 scale;
            GameObject3D* Parent;

            GameObject3D(EngineVar::Vector3 position, EngineVar::Vector3 rotation,
                EngineVar::Vector3 scale, GameObject3D* parent);
            void scaleObject(EngineVar::Vector3 scaleFactor);
            void rotateObject(EngineVar::Vector3 rotationDelta);
            void translateObject(EngineVar::Vector3 offset);
    };
}