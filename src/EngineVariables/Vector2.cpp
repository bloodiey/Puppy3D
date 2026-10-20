#include "Vector2.h"

#include <cmath>

namespace EngineVar
{
    Vector2::Vector2(float x, float y) : x(x), y(y) {}
    void Vector2::add(Vector2 value) { x += value.x; y += value.y; }
    void Vector2::subtract(Vector2 value) { x -= value.x; y -= value.y; }
    void Vector2::multiply(float scalar) { x *= scalar; y *= scalar; }
    void Vector2::multiplyByVector2(Vector2 value) { x *= value.x; y *= value.y; }
    void Vector2::multplyByVector2(Vector2 value) { multiplyByVector2(value); }
    void Vector2::divide(float scalar) { x /= scalar; y /= scalar; }
    void Vector2::divideByVector2(Vector2 value) { x /= value.x; y /= value.y; }
    float Vector2::length() const { return std::sqrt(x * x + y * y); }
    void Vector2::normalize() { const float magnitude = length(); if (magnitude != 0.0f) divide(magnitude); }
    Vector2 Vector2::normalized() const
    {
        const float magnitude = length();
        return magnitude == 0.0f ? Vector2(0.0f, 0.0f) : Vector2(x / magnitude, y / magnitude);
    }
    Vector2 Vector2::perpendicular() const { return Vector2(-y, x); }
    Vector2 Vector2::distanceTo(Vector2 value) const { return Vector2(x - value.x, y - value.y); }
}