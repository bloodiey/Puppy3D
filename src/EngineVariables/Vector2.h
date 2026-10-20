#pragma once

namespace EngineVar
{
    class Vector2
    {
        public:
            float x;
            float y;

            Vector2(float x, float y);
            void add(Vector2 value);
            void subtract(Vector2 value);
            void multiply(float scalar);
            void multiplyByVector2(Vector2 value);
            void multplyByVector2(Vector2 value);
            void divide(float scalar);
            void divideByVector2(Vector2 value);
            float length() const;
            void normalize();
            Vector2 normalized() const;
            Vector2 perpendicular() const;
            Vector2 distanceTo(Vector2 value) const;
    };
}