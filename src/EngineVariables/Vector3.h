#pragma once

namespace EngineVar
{
    class Vector3
    {
        public:
            float x;
            float y;
            float z;

            Vector3(float x, float y, float z);
            void add(Vector3 value);
            void subtract(Vector3 value);
            void multiply(float scalar);
            void multiplyByVector3(Vector3 value);
            void multplyByVector3(Vector3 value);
            void divide(float scalar);
            void divideByVector3(Vector3 value);
            float length() const;
            void normalize();
            float dot(Vector3 value) const;
            Vector3 cross(Vector3 value) const;
            Vector3 product(Vector3 value) const;
            float distance(Vector3 value) const;
    };
}