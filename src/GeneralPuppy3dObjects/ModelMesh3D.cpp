#include "ModelMesh3D.h"

#include <utility>

namespace GeneralPuppy3dObjects
{
    ModelMesh3D::ModelMesh3D(EngineVar::Vector3 position, EngineVar::Vector3 rotation,
        EngineVar::Vector3 scale, GameObject3D* parent,
        std::vector<EngineVar::Polygon> polygons)
        : GameObject3D(position, rotation, scale, parent), polygons(std::move(polygons)) {}
}