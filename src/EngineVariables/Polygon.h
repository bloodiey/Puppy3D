#pragma once

#include "Color3.h"
#include "Edge.h"

#include <vector>

namespace EngineVar
{
    class Polygon
    {
        public:
            using Color3 = EngineVar::Color3;
            std::vector<Edge> edges;

            explicit Polygon(std::vector<Edge> edges);
            void traslate(Vector3 offset);
            void moveUV(Vector2 offset);
    };
}