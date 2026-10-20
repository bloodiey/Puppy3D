#include "Polygon.h"

#include <utility>

namespace EngineVar
{
    Polygon::Polygon(std::vector<Edge> edges) : edges(std::move(edges)) {}
    void Polygon::traslate(Vector3 offset)
    {
        for (Edge& edge : edges) edge.traslate(offset);
    }
    void Polygon::moveUV(Vector2 offset)
    {
        for (Edge& edge : edges)
        {
            edge.v1.moveUV(offset);
            edge.v2.moveUV(offset);
        }
    }
}