#include "Vector3.h"

#include <cmath>

namespace EngineVar
{
    Vector3::Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    void Vector3::add(Vector3 value) { x += value.x; y += value.y; z += value.z; }
    void Vector3::subtract(Vector3 value) { x -= value.x; y -= value.y; z -= value.z; }
    void Vector3::multiply(float scalar) { x *= scalar; y *= scalar; z *= scalar; }
    void Vector3::multiplyByVector3(Vector3 value) { x *= value.x; y *= value.y; z *= value.z; }
    void Vector3::multplyByVector3(Vector3 value) { multiplyByVector3(value); }
    void Vector3::divide(float scalar) { x /= scalar; y /= scalar; z /= scalar; }
    void Vector3::divideByVector3(Vector3 value) { x /= value.x; y /= value.y; z /= value.z; }
    float Vector3::length() const { return std::sqrt(x * x + y * y + z * z); }
    void Vector3::normalize() { const float magnitude = length(); if (magnitude != 0.0f) divide(magnitude); }
    float Vector3::dot(Vector3 value) const { return x * value.x + y * value.y + z * value.z; }
    Vector3 Vector3::cross(Vector3 value) const
    {
        return Vector3(y * value.z - z * value.y,
            z * value.x - x * value.z, x * value.y - y * value.x);
    }
    Vector3 Vector3::product(Vector3 value) const
    {
        return Vector3(x * value.x, y * value.y, z * value.z);
    }
    float Vector3::distance(Vector3 value) const
    {
        return Vector3(x - value.x, y - value.y, z - value.z).length();
    }
}