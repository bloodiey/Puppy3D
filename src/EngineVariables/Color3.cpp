#include "Color3.h"

#include <stdexcept>

namespace EngineVar
{
    Color3::Color3(float r, float g, float b) : r(r), g(g), b(b) {}
    Color3 Color3::FromRgb(float r, float g, float b) { return Color3(r, g, b); }

    Color3 Color3::FromHex(const std::string& value)
    {
        const std::string hex = !value.empty() && value.front() == '#'
            ? value.substr(1) : value;
        if (hex.size() != 6) throw std::invalid_argument("Invalid hex color");
        try
        {
            std::size_t parsed = 0;
            const int red = std::stoi(hex.substr(0, 2), &parsed, 16);
            if (parsed != 2) throw std::invalid_argument("Invalid hex color");
            const int green = std::stoi(hex.substr(2, 2), &parsed, 16);
            if (parsed != 2) throw std::invalid_argument("Invalid hex color");
            const int blue = std::stoi(hex.substr(4, 2), &parsed, 16);
            if (parsed != 2) throw std::invalid_argument("Invalid hex color");
            return Color3(red / 255.0f, green / 255.0f, blue / 255.0f);
        }
        catch (const std::exception&)
        {
            throw std::invalid_argument("Invalid hex color");
        }
    }
}