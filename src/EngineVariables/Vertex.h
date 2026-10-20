#pragma once

#include "Vector2.h"
#include "Vector3.h"

namespace EngineVar
{
    class Vertex
    {
        public:
            Vector3 position;
            Vector3 normal;
            Vector2 uv;

            Vertex(Vector3 position, Vector3 normal, Vector2 uv);
            void traslate(Vector3 offset);
            void moveUV(Vector2 offset);
    };
}