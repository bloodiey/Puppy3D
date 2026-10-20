#include "GameObject3D.h"

namespace GeneralPuppy3dObjects
{
    GameObject3D::GameObject3D(EngineVar::Vector3 position, EngineVar::Vector3 rotation,
        EngineVar::Vector3 scale, GameObject3D* parent)
        : position(position), rotation(rotation), scale(scale), Parent(parent) {}

    void GameObject3D::scaleObject(EngineVar::Vector3 scaleFactor)
    {
        scale.multiplyByVector3(scaleFactor);
    }
    void GameObject3D::rotateObject(EngineVar::Vector3 rotationDelta)
    {
        rotation.add(rotationDelta);
    }
    void GameObject3D::translateObject(EngineVar::Vector3 offset)
    {
        position.add(offset);
    }
}