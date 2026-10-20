#include "Edge.h"

namespace EngineVar
{
    Edge::Edge(Vertex v1, Vertex v2) : v1(v1), v2(v2) {}
    void Edge::traslate(Vector3 offset) { v1.traslate(offset); v2.traslate(offset); }
}