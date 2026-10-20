#pragma once

#include "GameObject3D.h"
#include "../EngineVariables/Polygon.h"

#include <vector>

namespace GeneralPuppy3dObjects
{
    class ModelMesh3D : public GameObject3D
    {
        public:
            std::vector<EngineVar::Polygon> polygons;

            ModelMesh3D(EngineVar::Vector3 position, EngineVar::Vector3 rotation,
                EngineVar::Vector3 scale, GameObject3D* parent,
                std::vector<EngineVar::Polygon> polygons);
    };
}