#include "Vertex.h"

namespace EngineVar
{
    Vertex::Vertex(Vector3 position, Vector3 normal, Vector2 uv)
        : position(position), normal(normal), uv(uv) {}
    void Vertex::traslate(Vector3 offset) { position.add(offset); }
    void Vertex::moveUV(Vector2 offset) { uv.add(offset); }
}