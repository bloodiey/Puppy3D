#pragma once

#include "Vertex.h"

namespace EngineVar
{
    class Edge
    {
        public:
            Vertex v1;
            Vertex v2;

            Edge(Vertex v1, Vertex v2);
            void traslate(Vector3 offset);
    };
}