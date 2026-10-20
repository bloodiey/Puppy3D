#pragma once

#include <string>

namespace EngineVar
{
    class Color3
    {
        public:
            float r;
            float g;
            float b;

            Color3(float r, float g, float b);
            static Color3 FromRgb(float r, float g, float b);
            static Color3 FromHex(const std::string& hex);
    };
}