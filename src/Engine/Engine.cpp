#include "Engine.h"
#include "GlbImporter.h"
#include "Rendering/EngineVarMeshRenderer.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Matrix = std::array<float, 16>;

struct RenderVertex
{
    float position[3];
    float normal[3];
    float uv[2];
};

constexpr const char* vertexShaderSource = R"GLSL(#version 150
in vec3 aPosition;
in vec3 aNormal;
in vec2 aUv;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vNormal;
out vec2 vUv;
void main()
{
    gl_Position = uMvp * vec4(aPosition, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vUv = aUv;
}
)GLSL";

constexpr const char* fragmentShaderSource = R"GLSL(#version 150
in vec3 vNormal;
in vec2 vUv;
uniform vec4 uBaseColor;
uniform sampler2D uTexture;
uniform bool uHasTexture;
uniform bool uUnlit;
out vec4 fragmentColor;
void main()
{
    vec4 texel = uHasTexture ? texture(uTexture, vUv) : vec4(1.0);
    float diffuse = max(dot(normalize(vNormal), normalize(vec3(-0.4, 0.8, 0.6))), 0.0);
    float illumination = uUnlit ? 1.0 : (0.3 + 0.7 * diffuse);
    fragmentColor = vec4(uBaseColor.rgb * texel.rgb * illumination,
                         uBaseColor.a * texel.a);
}
)GLSL";

Matrix identityMatrix()
{
    return {1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
}

Matrix multiply(const Matrix& left, const Matrix& right)
{
    Matrix result{};
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int index = 0; index < 4; ++index)
            {
                result[static_cast<std::size_t>(column * 4 + row)] +=
                    left[static_cast<std::size_t>(index * 4 + row)] *
                    right[static_cast<std::size_t>(column * 4 + index)];
            }
        }
    }
    return result;
}

float dot3(const std::array<float, 3>& left, const std::array<float, 3>& right)
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

std::array<float, 3> normalize3(const std::array<float, 3>& value)
{
    const float length = std::sqrt(dot3(value, value));
    if (length <= 0.000001f)
    {
        return {0.0f, 0.0f, 1.0f};
    }
    return {value[0] / length, value[1] / length, value[2] / length};
}
std::array<float, 3> cameraForward(const EngineCamera& camera)
{
    constexpr float degreesToRadians = 0.017453292519943295f;
    const float yaw = camera.yaw * degreesToRadians;
    const float pitch = camera.pitch * degreesToRadians;
    return normalize3({std::cos(yaw) * std::cos(pitch), std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)});
}

std::size_t cubeVertexIndex(const float position[3])
{
    return (position[0] > 0.0f ? 1u : 0u) |
        (position[1] > 0.0f ? 2u : 0u) |
        (position[2] > 0.0f ? 4u : 0u);
}

std::array<float, 3> cross3(const std::array<float, 3>& left,
    const std::array<float, 3>& right)
{
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0]
    };
}

Matrix lookAt(const std::array<float, 3>& eye, const std::array<float, 3>& target)
{
    const std::array<float, 3> forward = normalize3({
        target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]
    });
    const std::array<float, 3> side = normalize3(cross3(forward, {0.0f, 1.0f, 0.0f}));
    const std::array<float, 3> up = cross3(side, forward);

    Matrix result = identityMatrix();
    result[0] = side[0];
    result[4] = side[1];
    result[8] = side[2];
    result[12] = -dot3(side, eye);
    result[1] = up[0];
    result[5] = up[1];
    result[9] = up[2];
    result[13] = -dot3(up, eye);
    result[2] = -forward[0];
    result[6] = -forward[1];
    result[10] = -forward[2];
    result[14] = dot3(forward, eye);
    return result;
}

Matrix perspective(float aspect, float nearPlane, float farPlane)
{
    constexpr float fieldOfView = 0.872664626f;
    const float scale = 1.0f / std::tan(fieldOfView * 0.5f);
    Matrix result{};
    result[0] = scale / aspect;
    result[5] = scale;
    result[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    result[11] = -1.0f;
    result[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    return result;
}

Matrix cubeModel(const EngineObject& object)
{
    constexpr float degreesToRadians = 0.017453292519943295f;
    const float x = object.rotation[0] * degreesToRadians;
    const float y = object.rotation[1] * degreesToRadians;
    const float z = object.rotation[2] * degreesToRadians;
    Matrix translate = identityMatrix();
    translate[12] = object.position[0];
    translate[13] = object.position[1];
    translate[14] = object.position[2];

    Matrix rotateX = identityMatrix();
    rotateX[5] = std::cos(x);
    rotateX[6] = std::sin(x);
    rotateX[9] = -std::sin(x);
    rotateX[10] = std::cos(x);

    Matrix rotateY = identityMatrix();
    rotateY[0] = std::cos(y);
    rotateY[2] = -std::sin(y);
    rotateY[8] = std::sin(y);
    rotateY[10] = std::cos(y);

    Matrix rotateZ = identityMatrix();
    rotateZ[0] = std::cos(z);
    rotateZ[1] = std::sin(z);
    rotateZ[4] = -std::sin(z);
    rotateZ[5] = std::cos(z);
    Matrix scale = identityMatrix();
    scale[0] = object.scale[0];
    scale[5] = object.scale[1];
    scale[10] = object.scale[2];
    return multiply(translate, multiply(rotateZ, multiply(rotateY, multiply(rotateX, scale))));
}

Matrix inverse(const Matrix& matrix)
{
    double augmented[4][8]{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            augmented[row][column] = matrix[column * 4 + row];
        }
        augmented[row][row + 4] = 1.0;
    }
    for (std::size_t pivot = 0; pivot < 4; ++pivot)
    {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1; row < 4; ++row)
        {
            if (std::abs(augmented[row][pivot]) > std::abs(augmented[best][pivot]))
            {
                best = row;
            }
        }
        if (std::abs(augmented[best][pivot]) < 1.0e-12)
        {
            return identityMatrix();
        }
        if (best != pivot)
        {
            for (std::size_t column = 0; column < 8; ++column)
            {
                std::swap(augmented[pivot][column], augmented[best][column]);
            }
        }
        const double divisor = augmented[pivot][pivot];
        for (double& value : augmented[pivot])
        {
            value /= divisor;
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            if (row == pivot) continue;
            const double factor = augmented[row][pivot];
            for (std::size_t column = 0; column < 8; ++column)
            {
                augmented[row][column] -= factor * augmented[pivot][column];
            }
        }
    }
    Matrix result{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            result[column * 4 + row] = static_cast<float>(augmented[row][column + 4]);
        }
    }
    return result;
}

Matrix nodeMatrix(const EngineModelNode& node)
{
    if (node.hasMatrix)
    {
        return node.matrix;
    }
    const float x = node.rotation[0];
    const float y = node.rotation[1];
    const float z = node.rotation[2];
    const float w = node.rotation[3];
    Matrix result = identityMatrix();
    result[0] = (1.0f - 2.0f * (y * y + z * z)) * node.scale[0];
    result[1] = (2.0f * (x * y + z * w)) * node.scale[0];
    result[2] = (2.0f * (x * z - y * w)) * node.scale[0];
    result[4] = (2.0f * (x * y - z * w)) * node.scale[1];
    result[5] = (1.0f - 2.0f * (x * x + z * z)) * node.scale[1];
    result[6] = (2.0f * (y * z + x * w)) * node.scale[1];
    result[8] = (2.0f * (x * z + y * w)) * node.scale[2];
    result[9] = (2.0f * (y * z - x * w)) * node.scale[2];
    result[10] = (1.0f - 2.0f * (x * x + y * y)) * node.scale[2];
    result[12] = node.translation[0];
    result[13] = node.translation[1];
    result[14] = node.translation[2];
    return result;
}

std::vector<float> sampleAnimation(const EngineAnimationSampler& sampler,
    float time, bool rotation)
{
    const auto upper = std::upper_bound(sampler.times.begin(), sampler.times.end(), time);
    if (upper == sampler.times.begin())
    {
        const std::size_t offset = sampler.interpolation == "CUBICSPLINE"
            ? sampler.components : 0;
        return std::vector<float>(sampler.values.begin() + static_cast<std::ptrdiff_t>(offset),
            sampler.values.begin() + static_cast<std::ptrdiff_t>(offset + sampler.components));
    }
    const std::size_t left = static_cast<std::size_t>(upper - sampler.times.begin() - 1);
    if (upper == sampler.times.end() || sampler.interpolation == "STEP")
    {
        const std::size_t offset = (left * (sampler.interpolation == "CUBICSPLINE" ? 3 : 1) +
            (sampler.interpolation == "CUBICSPLINE" ? 1 : 0)) * sampler.components;
        return std::vector<float>(sampler.values.begin() + static_cast<std::ptrdiff_t>(offset),
            sampler.values.begin() + static_cast<std::ptrdiff_t>(offset + sampler.components));
    }

    const std::size_t right = left + 1;
    const float duration = sampler.times[right] - sampler.times[left];
    const float amount = std::clamp((time - sampler.times[left]) / duration, 0.0f, 1.0f);
    std::vector<float> result(sampler.components, 0.0f);
    const bool cubic = sampler.interpolation == "CUBICSPLINE";
    const std::size_t leftValue = (left * (cubic ? 3 : 1) + (cubic ? 1 : 0)) * sampler.components;
    const std::size_t rightValue = (right * (cubic ? 3 : 1) + (cubic ? 1 : 0)) * sampler.components;
    if (cubic)
    {
        const std::size_t leftOutTangent = (left * 3 + 2) * sampler.components;
        const std::size_t rightInTangent = right * 3 * sampler.components;
        const float t2 = amount * amount;
        const float t3 = t2 * amount;
        const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float h10 = t3 - 2.0f * t2 + amount;
        const float h01 = -2.0f * t3 + 3.0f * t2;
        const float h11 = t3 - t2;
        for (std::size_t component = 0; component < sampler.components; ++component)
        {
            result[component] = h00 * sampler.values[leftValue + component] +
                h10 * duration * sampler.values[leftOutTangent + component] +
                h01 * sampler.values[rightValue + component] +
                h11 * duration * sampler.values[rightInTangent + component];
        }
    }
    else
    {
        if (rotation)
        {
            float dot = 0.0f;
            for (std::size_t component = 0; component < sampler.components; ++component)
            {
                dot += sampler.values[leftValue + component] * sampler.values[rightValue + component];
            }
            const float direction = dot < 0.0f ? -1.0f : 1.0f;
            for (std::size_t component = 0; component < sampler.components; ++component)
            {
                result[component] = sampler.values[leftValue + component] * (1.0f - amount) +
                    direction * sampler.values[rightValue + component] * amount;
            }
        }
        else
        {
            for (std::size_t component = 0; component < sampler.components; ++component)
            {
                result[component] = sampler.values[leftValue + component] * (1.0f - amount) +
                    sampler.values[rightValue + component] * amount;
            }
        }
    }
    if (rotation)
    {
        float length = 0.0f;
        for (float value : result) length += value * value;
        length = std::sqrt(length);
        if (length > 1.0e-8f)
        {
            for (float& value : result) value /= length;
        }
    }
    return result;
}

std::vector<Matrix> animatedNodeWorld(const EngineModelInstance& instance)
{
    std::vector<EngineModelNode> nodes = instance.data.nodes;
    if (instance.activeAnimation >= 0 &&
        static_cast<std::size_t>(instance.activeAnimation) < instance.data.animations.size())
    {
        const EngineAnimationClip& clip =
            instance.data.animations[static_cast<std::size_t>(instance.activeAnimation)];
        for (const EngineAnimationChannel& channel : clip.channels)
        {
            const std::vector<float> value = sampleAnimation(
                clip.samplers[channel.samplerIndex], instance.animationTime,
                channel.path == EngineAnimationPath::Rotation);
            EngineModelNode& node = nodes[channel.nodeIndex];
            float* destination = channel.path == EngineAnimationPath::Translation
                ? node.translation
                : channel.path == EngineAnimationPath::Rotation ? node.rotation : node.scale;
            std::copy(value.begin(), value.end(), destination);
        }
    }

    std::vector<Matrix> world(nodes.size(), identityMatrix());
    std::vector<unsigned char> state(nodes.size(), 0);
    std::function<void(std::size_t)> resolve = [&](std::size_t index)
    {
        if (state[index] == 2) return;
        if (state[index] == 1) return;
        state[index] = 1;
        const int parent = nodes[index].parentIndex;
        const Matrix local = nodeMatrix(nodes[index]);
        if (parent >= 0 && static_cast<std::size_t>(parent) < nodes.size())
        {
            resolve(static_cast<std::size_t>(parent));
            world[index] = multiply(world[static_cast<std::size_t>(parent)], local);
        }
        else
        {
            world[index] = local;
        }
        state[index] = 2;
    };
    for (std::size_t index = 0; index < nodes.size(); ++index) resolve(index);
    return world;
}

std::array<float, 3> transformPoint(const Matrix& matrix, const EngineVertex& vertex)
{
    const float x = vertex.position[0];
    const float y = vertex.position[1];
    const float z = vertex.position[2];
    return {
        matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
        matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
        matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]
    };
}

GLuint compileShader(GLenum type, const char* source, std::string& error)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
    {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    error = "OpenGL shader compilation failed: " + log;
    glDeleteShader(shader);
    return 0;
}

GLuint createProgram(std::string& error)
{
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource, error);
    if (vertexShader == 0)
    {
        return 0;
    }
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource, error);
    if (fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aNormal");
    glBindAttribLocation(program, 2, "aUv");
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE)
    {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    error = "OpenGL shader linking failed: " + log;
    glDeleteProgram(program);
    return 0;
}

void appendFace(std::vector<RenderVertex>& vertices,
    const std::array<float, 3>& normal,
    const std::array<std::array<float, 3>, 4>& corners)
{
    constexpr std::array<std::array<float, 2>, 6> uvs = {{
        {{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{1.0f, 1.0f}},
        {{0.0f, 0.0f}}, {{1.0f, 1.0f}}, {{0.0f, 1.0f}}
    }};
    constexpr std::array<int, 6> indices = {0, 1, 2, 0, 2, 3};
    for (std::size_t index = 0; index < indices.size(); ++index)
    {
        const auto& point = corners[static_cast<std::size_t>(indices[index])];
        vertices.push_back({
            {point[0], point[1], point[2]},
            {normal[0], normal[1], normal[2]},
            {uvs[index][0], uvs[index][1]}
        });
    }
}

const std::vector<RenderVertex>& cubeVertices()
{
    static const std::vector<RenderVertex> vertices = []
    {
        std::vector<RenderVertex> result;
        result.reserve(36);
        appendFace(result, {0.0f, 0.0f, 1.0f}, {{
        {{-0.5f, -0.5f, 0.5f}}, {{0.5f, -0.5f, 0.5f}},
        {{0.5f, 0.5f, 0.5f}}, {{-0.5f, 0.5f, 0.5f}}
        }});
        appendFace(result, {0.0f, 0.0f, -1.0f}, {{
        {{0.5f, -0.5f, -0.5f}}, {{-0.5f, -0.5f, -0.5f}},
        {{-0.5f, 0.5f, -0.5f}}, {{0.5f, 0.5f, -0.5f}}
        }});
        appendFace(result, {0.0f, 1.0f, 0.0f}, {{
        {{-0.5f, 0.5f, 0.5f}}, {{0.5f, 0.5f, 0.5f}},
        {{0.5f, 0.5f, -0.5f}}, {{-0.5f, 0.5f, -0.5f}}
        }});
        appendFace(result, {0.0f, -1.0f, 0.0f}, {{
        {{-0.5f, -0.5f, -0.5f}}, {{0.5f, -0.5f, -0.5f}},
        {{0.5f, -0.5f, 0.5f}}, {{-0.5f, -0.5f, 0.5f}}
        }});
        appendFace(result, {1.0f, 0.0f, 0.0f}, {{
        {{0.5f, -0.5f, 0.5f}}, {{0.5f, -0.5f, -0.5f}},
        {{0.5f, 0.5f, -0.5f}}, {{0.5f, 0.5f, 0.5f}}
        }});
        appendFace(result, {-1.0f, 0.0f, 0.0f}, {{
        {{-0.5f, -0.5f, -0.5f}}, {{-0.5f, -0.5f, 0.5f}},
        {{-0.5f, 0.5f, 0.5f}}, {{-0.5f, 0.5f, -0.5f}}
        }});
        return result;
    }();
    return vertices;
}

void configureVertexArray(GLuint vertexArray, GLuint vertexBuffer)
{
    glBindVertexArray(vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex),
        reinterpret_cast<const void*>(offsetof(RenderVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex),
        reinterpret_cast<const void*>(offsetof(RenderVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex),
        reinterpret_cast<const void*>(offsetof(RenderVertex, uv)));
}

void deleteMeshResources(EngineMesh& mesh)
{
    if (mesh.vertexArray != 0)
    {
        glDeleteVertexArrays(1, &mesh.vertexArray);
        mesh.vertexArray = 0;
    }
    if (mesh.vertexBuffer != 0)
    {
        glDeleteBuffers(1, &mesh.vertexBuffer);
        mesh.vertexBuffer = 0;
    }
    if (mesh.indexBuffer != 0)
    {
        glDeleteBuffers(1, &mesh.indexBuffer);
        mesh.indexBuffer = 0;
    }
}

void setMeshGpuData(EngineMesh& mesh)
{
    if (mesh.vertexArray != 0)
    {
        return;
    }
    glGenVertexArrays(1, &mesh.vertexArray);
    glGenBuffers(1, &mesh.vertexBuffer);
    glGenBuffers(1, &mesh.indexBuffer);
    glBindVertexArray(mesh.vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(EngineVertex)),
        mesh.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
        mesh.indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EngineVertex),
        reinterpret_cast<const void*>(offsetof(EngineVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(EngineVertex),
        reinterpret_cast<const void*>(offsetof(EngineVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(EngineVertex),
        reinterpret_cast<const void*>(offsetof(EngineVertex, uv)));
    glBindVertexArray(0);
}

void deleteRetired(EngineMesh& mesh)
{
    deleteMeshResources(mesh);
    if (mesh.texture && mesh.texture->glTexture != 0)
    {
        glDeleteTextures(1, &mesh.texture->glTexture);
        mesh.texture->glTexture = 0;
    }
}
}

EngineScene::EngineScene()
{
    addCube("Cube", 0.0f, 0.0f, 0.0f);
}

int EngineScene::addCube(const std::string& name, float x, float y, float z)
{
    EngineObject object;
    object.name = name;
    object.position[0] = x;
    object.position[1] = y;
    object.position[2] = z;
    objects_.push_back(object);
    return static_cast<int>(objects_.size());
}

int EngineScene::addPolygon(const std::string& name, const EngineVar::Polygon& polygon,
    EngineVar::Polygon::Color3 color)
{
    EngineMesh mesh;
    std::string error;
    if (!buildEngineMesh(polygon, mesh, error))
    {
        return 0;
    }
    mesh.baseColor[0] = std::clamp(color.r, 0.0f, 1.0f);
    mesh.baseColor[1] = std::clamp(color.g, 0.0f, 1.0f);
    mesh.baseColor[2] = std::clamp(color.b, 0.0f, 1.0f);
    mesh.baseColor[3] = 1.0f;

    EngineObject object;
    object.name = name;
    object.isModel = true;
    object.isGeneratedGeometry = true;
    objects_.push_back(std::move(object));

    EngineModelInstance instance;
    instance.objectId = static_cast<int>(objects_.size());
    instance.data.meshes.push_back(std::move(mesh));
    models_.push_back(std::move(instance));
    return static_cast<int>(objects_.size());
}

bool EngineScene::setPosition(int objectId, float x, float y, float z)
{
    if (objectId < 1 || static_cast<std::size_t>(objectId) > objects_.size())
    {
        return false;
    }
    EngineObject& object = objects_[static_cast<std::size_t>(objectId - 1)];
    object.position[0] = x;
    object.position[1] = y;
    object.position[2] = z;
    return true;
}

bool EngineScene::setRotation(int objectId, float x, float y, float z)
{
    if (objectId < 1 || static_cast<std::size_t>(objectId) > objects_.size())
    {
        return false;
    }
    EngineObject& object = objects_[static_cast<std::size_t>(objectId - 1)];
    object.rotation[0] = x;
    object.rotation[1] = y;
    object.rotation[2] = z;
    return true;
}

bool EngineScene::setScale(int objectId, float x, float y, float z)
{
    if (objectId < 1 || static_cast<std::size_t>(objectId) > objects_.size())
    {
        return false;
    }
    EngineObject& object = objects_[static_cast<std::size_t>(objectId - 1)];
    object.scale[0] = x;
    object.scale[1] = y;
    object.scale[2] = z;
    return true;
}

bool EngineScene::setColor(int objectId, float red, float green, float blue)
{
    if (objectId < 1 || static_cast<std::size_t>(objectId) > objects_.size())
    {
        return false;
    }
    EngineObject& object = objects_[static_cast<std::size_t>(objectId - 1)];
    object.color[0] = std::clamp(red, 0.0f, 1.0f);
    object.color[1] = std::clamp(green, 0.0f, 1.0f);
    object.color[2] = std::clamp(blue, 0.0f, 1.0f);
    return true;
}

bool EngineScene::setObjectName(int objectId, const std::string& name)
{
    if (objectId < 1 || static_cast<std::size_t>(objectId) > objects_.size())
    {
        return false;
    }
    objects_[static_cast<std::size_t>(objectId - 1)].name = name;
    return true;
}

bool EngineScene::setCubeVertex(int objectId, std::size_t vertexIndex,
    float x, float y, float z)
{
    if (objectId < 1 || static_cast<std::size_t>(objectId) > objects_.size() ||
        vertexIndex >= 8 || objects_[static_cast<std::size_t>(objectId - 1)].isModel ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
        return false;
    }
    EngineObject& object = objects_[static_cast<std::size_t>(objectId - 1)];
    object.vertices[vertexIndex] = {x, y, z};
    return true;
}

bool EngineScene::importGlb(const std::filesystem::path& path, std::string& error)
{
    EngineModelInstance instance;
    if (!loadGlb(path, instance.data, error))
    {
        return false;
    }
    EngineObject object;
    object.name = path.stem().string();
    object.sourcePath = path.string();
    object.isModel = true;
    objects_.push_back(std::move(object));
    instance.objectId = static_cast<int>(objects_.size());
    models_.push_back(std::move(instance));
    return true;
}

std::size_t EngineScene::objectCount() const
{
    return objects_.size();
}

const EngineObject* EngineScene::object(int objectId) const
{
    if (objectId < 1 || static_cast<std::size_t>(objectId) > objects_.size())
    {
        return nullptr;
    }
    return &objects_[static_cast<std::size_t>(objectId - 1)];
}

int EngineScene::pickObject(float x, float y, int width, int height,
    const EngineCamera& camera) const
{
    if (width <= 0 || height <= 0) return 0;

    constexpr float halfFieldOfView = 0.436332313f;
    const std::array<float, 3> eye = {camera.position[0], camera.position[1], camera.position[2]};
    const std::array<float, 3> forward = cameraForward(camera);
    const std::array<float, 3> right = normalize3(cross3(forward, {0.0f, 1.0f, 0.0f}));
    const std::array<float, 3> up = cross3(right, forward);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float screenX = (2.0f * x / static_cast<float>(width) - 1.0f) * aspect *
        std::tan(halfFieldOfView);
    const float screenY = (1.0f - 2.0f * y / static_cast<float>(height)) *
        std::tan(halfFieldOfView);
    const std::array<float, 3> ray = normalize3({
        forward[0] + right[0] * screenX + up[0] * screenY,
        forward[1] + right[1] * screenX + up[1] * screenY,
        forward[2] + right[2] * screenX + up[2] * screenY
    });

    std::vector<std::vector<Matrix>> nodeWorldTransforms;
    nodeWorldTransforms.reserve(models_.size());
    for (const EngineModelInstance& instance : models_)
    {
        nodeWorldTransforms.push_back(animatedNodeWorld(instance));
    }

    float closestDistance = std::numeric_limits<float>::max();
    int closestObject = 0;
    for (const EngineObject& object : objects_)
    {
        const Matrix objectTransform = cubeModel(object);
        const std::array<float, 3> center = {
            object.position[0], object.position[1], object.position[2]
        };
        float radiusSquared = 0.0f;
        const auto includePoint = [&](const std::array<float, 3>& point)
        {
            const float dx = point[0] - center[0];
            const float dy = point[1] - center[1];
            const float dz = point[2] - center[2];
            radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
        };

        if (!object.isModel)
        {
            for (const auto& vertex : object.vertices)
            {
                EngineVertex local;
                local.position[0] = vertex[0];
                local.position[1] = vertex[1];
                local.position[2] = vertex[2];
                includePoint(transformPoint(objectTransform, local));
            }
        }
        else
        {
            for (std::size_t modelIndex = 0; modelIndex < models_.size(); ++modelIndex)
            {
                const EngineModelInstance& instance = models_[modelIndex];
                if (instance.objectId != static_cast<int>(&object - objects_.data()) + 1) continue;
                for (const EngineMesh& mesh : instance.data.meshes)
                {
                    Matrix nodeDelta = identityMatrix();
                    if (mesh.nodeIndex >= 0 && static_cast<std::size_t>(mesh.nodeIndex) <
                        nodeWorldTransforms[modelIndex].size())
                    {
                        nodeDelta = multiply(nodeWorldTransforms[modelIndex][
                            static_cast<std::size_t>(mesh.nodeIndex)], inverse(mesh.baseNodeTransform));
                    }
                    const Matrix transform = multiply(objectTransform, nodeDelta);
                    for (const EngineVertex& vertex : mesh.vertices)
                    {
                        includePoint(transformPoint(transform, vertex));
                    }
                }
                break;
            }
        }

        const float radius = std::sqrt(std::max(radiusSquared, 0.01f));
        const std::array<float, 3> toCenter = {
            center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]
        };
        const float alongRay = dot3(toCenter, ray);
        if (alongRay < 0.0f) continue;
        const float perpendicularSquared = dot3(toCenter, toCenter) - alongRay * alongRay;
        if (perpendicularSquared > radius * radius) continue;
        const float hitDistance = alongRay - std::sqrt(radius * radius - perpendicularSquared);
        if (hitDistance < closestDistance)
        {
            closestDistance = hitDistance;
            closestObject = static_cast<int>(&object - objects_.data()) + 1;
        }
    }
    return closestObject;
}

std::size_t EngineScene::animationCount(int objectId) const
{
    for (const EngineModelInstance& instance : models_)
    {
        if (instance.objectId == objectId) return instance.data.animations.size();
    }
    return 0;
}

std::string EngineScene::animationName(int objectId, std::size_t animationIndex) const
{
    for (const EngineModelInstance& instance : models_)
    {
        if (instance.objectId == objectId && animationIndex < instance.data.animations.size())
        {
            return instance.data.animations[animationIndex].name;
        }
    }
    return {};
}

float EngineScene::animationDuration(int objectId, std::size_t animationIndex) const
{
    for (const EngineModelInstance& instance : models_)
    {
        if (instance.objectId == objectId && animationIndex < instance.data.animations.size())
        {
            return instance.data.animations[animationIndex].duration;
        }
    }
    return 0.0f;
}

int EngineScene::activeAnimation(int objectId) const
{
    for (const EngineModelInstance& instance : models_)
    {
        if (instance.objectId == objectId) return instance.activeAnimation;
    }
    return -1;
}

float EngineScene::animationTime(int objectId) const
{
    for (const EngineModelInstance& instance : models_)
    {
        if (instance.objectId == objectId) return instance.animationTime;
    }
    return 0.0f;
}

bool EngineScene::setAnimation(int objectId, int animationIndex)
{
    for (EngineModelInstance& instance : models_)
    {
        if (instance.objectId != objectId) continue;
        if (animationIndex < 0 ||
            static_cast<std::size_t>(animationIndex) >= instance.data.animations.size())
        {
            return false;
        }
        instance.activeAnimation = animationIndex;
        instance.animationTime = 0.0f;
        return true;
    }
    return false;
}

bool EngineScene::setAnimationPlaying(int objectId, bool playing)
{
    for (EngineModelInstance& instance : models_)
    {
        if (instance.objectId == objectId && instance.activeAnimation >= 0)
        {
            instance.animationPlaying = playing;
            return true;
        }
    }
    return false;
}

bool EngineScene::setAnimationTime(int objectId, float time)
{
    for (EngineModelInstance& instance : models_)
    {
        if (instance.objectId == objectId && instance.activeAnimation >= 0)
        {
            const float duration = animationDuration(objectId,
                static_cast<std::size_t>(instance.activeAnimation));
            instance.animationTime = std::clamp(time, 0.0f, duration);
            return true;
        }
    }
    return false;
}

void EngineScene::updateAnimations(float deltaTime)
{
    for (EngineModelInstance& instance : models_)
    {
        if (!instance.animationPlaying || instance.activeAnimation < 0 ||
            static_cast<std::size_t>(instance.activeAnimation) >= instance.data.animations.size())
        {
            continue;
        }
        const float duration = instance.data.animations[
            static_cast<std::size_t>(instance.activeAnimation)].duration;
        if (duration <= 0.0f)
        {
            instance.animationTime = 0.0f;
            instance.animationPlaying = false;
            continue;
        }
        instance.animationTime = std::fmod(instance.animationTime + deltaTime, duration);
    }
}

void EngineScene::clear()
{
    objects_.clear();
    for (EngineModelInstance& instance : models_)
    {
        for (EngineMesh& mesh : instance.data.meshes)
        {
            retiredMeshes_.push_back(std::move(mesh));
        }
    }
    models_.clear();
}

bool EngineScene::initializeOpenGl(std::string& error)
{
    if (shaderProgram_ != 0)
    {
        return true;
    }
    shaderProgram_ = createProgram(error);
    if (shaderProgram_ == 0)
    {
        return false;
    }

    const std::vector<RenderVertex>& cube = cubeVertices();
    glGenVertexArrays(1, &cubeVertexArray_);
    glGenBuffers(1, &cubeVertexBuffer_);
    configureVertexArray(cubeVertexArray_, cubeVertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(cube.size() * sizeof(RenderVertex)),
        cube.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    glGenVertexArrays(1, &gridVertexArray_);
    glGenBuffers(1, &gridVertexBuffer_);
    configureVertexArray(gridVertexArray_, gridVertexBuffer_);
    glBindVertexArray(0);
    error.clear();
    return true;
}

void EngineScene::releaseOpenGlResources()
{
    for (EngineModelInstance& instance : models_)
    {
        for (EngineMesh& mesh : instance.data.meshes)
        {
            deleteRetired(mesh);
        }
    }
    for (EngineMesh& mesh : retiredMeshes_)
    {
        deleteRetired(mesh);
    }
    retiredMeshes_.clear();
    if (cubeVertexArray_ != 0) glDeleteVertexArrays(1, &cubeVertexArray_);
    if (cubeVertexBuffer_ != 0) glDeleteBuffers(1, &cubeVertexBuffer_);
    if (gridVertexArray_ != 0) glDeleteVertexArrays(1, &gridVertexArray_);
    if (gridVertexBuffer_ != 0) glDeleteBuffers(1, &gridVertexBuffer_);
    if (shaderProgram_ != 0) glDeleteProgram(shaderProgram_);
    cubeVertexArray_ = cubeVertexBuffer_ = 0;
    gridVertexArray_ = gridVertexBuffer_ = 0;
    shaderProgram_ = 0;
}

void EngineScene::render(int width, int height, const EngineCamera& camera,
    int selectedObjectId, int selectedVertexIndex)
{
    for (EngineMesh& mesh : retiredMeshes_)
    {
        deleteRetired(mesh);
    }
    retiredMeshes_.clear();
    if (shaderProgram_ == 0 || width <= 0 || height <= 0)
    {
        return;
    }

    float minimum[3] = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    float maximum[3] = {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };
    const auto includePoint = [&minimum, &maximum](float x, float y, float z)
    {
        const float point[3] = {x, y, z};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], point[axis]);
            maximum[axis] = std::max(maximum[axis], point[axis]);
        }
    };

    std::vector<std::vector<Matrix>> nodeWorldTransforms;
    nodeWorldTransforms.reserve(models_.size());
    for (const EngineModelInstance& instance : models_)
    {
        nodeWorldTransforms.push_back(animatedNodeWorld(instance));
    }

    for (const EngineObject& object : objects_)
    {
        if (object.isModel) continue;
        const Matrix transform = cubeModel(object);
        for (const auto& vertex : object.vertices)
        {
            includePoint(transform[0] * vertex[0] + transform[4] * vertex[1] +
                    transform[8] * vertex[2] + transform[12],
                transform[1] * vertex[0] + transform[5] * vertex[1] +
                    transform[9] * vertex[2] + transform[13],
                transform[2] * vertex[0] + transform[6] * vertex[1] +
                    transform[10] * vertex[2] + transform[14]);
        }
    }
    for (std::size_t modelIndex = 0; modelIndex < models_.size(); ++modelIndex)
    {
        const EngineModelInstance& instance = models_[modelIndex];
        if (instance.objectId < 1 || static_cast<std::size_t>(instance.objectId) > objects_.size())
        {
            continue;
        }
        const Matrix objectTransform = cubeModel(objects_[static_cast<std::size_t>(instance.objectId - 1)]);
        for (const EngineMesh& mesh : instance.data.meshes)
        {
            Matrix nodeDelta = identityMatrix();
            if (mesh.nodeIndex >= 0 &&
                static_cast<std::size_t>(mesh.nodeIndex) < nodeWorldTransforms[modelIndex].size())
            {
                nodeDelta = multiply(nodeWorldTransforms[modelIndex][
                    static_cast<std::size_t>(mesh.nodeIndex)], inverse(mesh.baseNodeTransform));
            }
            const Matrix transform = multiply(objectTransform, nodeDelta);
            for (const EngineVertex& vertex : mesh.vertices)
            {
                const auto point = transformPoint(transform, vertex);
                includePoint(point[0], point[1], point[2]);
            }
        }
    }
    if (minimum[0] == std::numeric_limits<float>::max())
    {
        minimum[0] = minimum[1] = minimum[2] = -1.0f;
        maximum[0] = maximum[1] = maximum[2] = 1.0f;
    }

    const float halfWidth = (maximum[0] - minimum[0]) * 0.5f;
    const float halfHeight = (maximum[1] - minimum[1]) * 0.5f;
    const float halfDepth = (maximum[2] - minimum[2]) * 0.5f;
    const float radius = std::max(0.5f,
        std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight + halfDepth * halfDepth));
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const std::array<float, 3> eye = {camera.position[0], camera.position[1], camera.position[2]};
    const std::array<float, 3> forward = cameraForward(camera);
    const std::array<float, 3> target = {
        eye[0] + forward[0], eye[1] + forward[1], eye[2] + forward[2]
    };
    const Matrix viewProjection = multiply(
        perspective(aspect, 0.1f, std::max(1000.0f, radius * 8.0f)),
        lookAt(eye, target));

    glViewport(0, 0, width, height);
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(shaderProgram_);

    const GLint mvpLocation = glGetUniformLocation(shaderProgram_, "uMvp");
    const GLint modelLocation = glGetUniformLocation(shaderProgram_, "uModel");
    const GLint colorLocation = glGetUniformLocation(shaderProgram_, "uBaseColor");
    const GLint hasTextureLocation = glGetUniformLocation(shaderProgram_, "uHasTexture");
    const GLint unlitLocation = glGetUniformLocation(shaderProgram_, "uUnlit");
    const GLint textureLocation = glGetUniformLocation(shaderProgram_, "uTexture");
    glUniform1i(textureLocation, 0);

    std::vector<RenderVertex> grid;
    grid.reserve(84);
    const float gridHeight = minimum[1] - std::max(0.05f, radius * 0.03f);
    for (int coordinate = -10; coordinate <= 10; ++coordinate)
    {
        const float line = static_cast<float>(coordinate);
        const float normal[3] = {0.0f, 1.0f, 0.0f};
        const float uv[2] = {0.0f, 0.0f};
        grid.push_back({{line, gridHeight, -10.0f}, {normal[0], normal[1], normal[2]}, {uv[0], uv[1]}});
        grid.push_back({{line, gridHeight, 10.0f}, {normal[0], normal[1], normal[2]}, {uv[0], uv[1]}});
        grid.push_back({{-10.0f, gridHeight, line}, {normal[0], normal[1], normal[2]}, {uv[0], uv[1]}});
        grid.push_back({{10.0f, gridHeight, line}, {normal[0], normal[1], normal[2]}, {uv[0], uv[1]}});
    }
    glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, viewProjection.data());
    const Matrix identity = identityMatrix();
    glUniformMatrix4fv(modelLocation, 1, GL_FALSE, identity.data());
    glUniform4f(colorLocation, 0.24f, 0.27f, 0.32f, 1.0f);
    glUniform1i(hasTextureLocation, GL_FALSE);
    glUniform1i(unlitLocation, GL_TRUE);
    glBindVertexArray(gridVertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, gridVertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(grid.size() * sizeof(RenderVertex)),
        grid.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(grid.size()));

    glUniform1i(unlitLocation, GL_FALSE);
    const std::vector<RenderVertex>& cube = cubeVertices();
    glBindVertexArray(cubeVertexArray_);
    for (const EngineObject& object : objects_)
    {
        if (object.isModel) continue;
        const Matrix model = cubeModel(object);
        const Matrix mvp = multiply(viewProjection, model);
        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, mvp.data());
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, model.data());
        glUniform4f(colorLocation, object.color[0], object.color[1], object.color[2], 1.0f);
        glUniform1i(hasTextureLocation, GL_FALSE);
        std::vector<RenderVertex> editedCube = cube;
        for (RenderVertex& vertex : editedCube)
        {
            const std::size_t index = cubeVertexIndex(vertex.position);
            std::copy(object.vertices[index].begin(), object.vertices[index].end(),
                vertex.position);
        }
        for (std::size_t index = 0; index + 2 < editedCube.size(); index += 3)
        {
            const std::array<float, 3> first = {
                editedCube[index].position[0], editedCube[index].position[1], editedCube[index].position[2]
            };
            const std::array<float, 3> second = {
                editedCube[index + 1].position[0], editedCube[index + 1].position[1],
                editedCube[index + 1].position[2]
            };
            const std::array<float, 3> third = {
                editedCube[index + 2].position[0], editedCube[index + 2].position[1],
                editedCube[index + 2].position[2]
            };
            const std::array<float, 3> normal = normalize3(cross3(
                {second[0] - first[0], second[1] - first[1], second[2] - first[2]},
                {third[0] - first[0], third[1] - first[1], third[2] - first[2]}));
            for (std::size_t vertex = 0; vertex < 3; ++vertex)
            {
                std::copy(normal.begin(), normal.end(), editedCube[index + vertex].normal);
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, cubeVertexBuffer_);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(editedCube.size() * sizeof(RenderVertex)),
            editedCube.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(cube.size()));
    }

    if (selectedVertexIndex >= 0 && selectedVertexIndex < 8)
    {
        const EngineObject* selected = object(selectedObjectId);
        if (selected && !selected->isModel)
        {
            std::vector<RenderVertex> handles;
            handles.reserve(selected->vertices.size());
            for (const auto& position : selected->vertices)
            {
                handles.push_back({{position[0], position[1], position[2]},
                    {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
            }
            const Matrix model = cubeModel(*selected);
            const Matrix mvp = multiply(viewProjection, model);
            glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, mvp.data());
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, model.data());
            glUniform1i(unlitLocation, GL_TRUE);
            glUniform4f(colorLocation, 0.15f, 0.85f, 0.95f, 1.0f);
            glBindBuffer(GL_ARRAY_BUFFER, cubeVertexBuffer_);
            glBufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(handles.size() * sizeof(RenderVertex)),
                handles.data(), GL_DYNAMIC_DRAW);
            glPointSize(9.0f);
            glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(handles.size()));
            glUniform4f(colorLocation, 1.0f, 0.55f, 0.12f, 1.0f);
            glDrawArrays(GL_POINTS, selectedVertexIndex, 1);
            glPointSize(1.0f);
            glUniform1i(unlitLocation, GL_FALSE);
        }
    }

    for (std::size_t modelIndex = 0; modelIndex < models_.size(); ++modelIndex)
    {
        EngineModelInstance& instance = models_[modelIndex];
        if (instance.objectId < 1 || static_cast<std::size_t>(instance.objectId) > objects_.size())
        {
            continue;
        }
        const EngineObject& object = objects_[static_cast<std::size_t>(instance.objectId - 1)];
        const Matrix objectTransform = cubeModel(object);
        for (EngineMesh& mesh : instance.data.meshes)
        {
            setMeshGpuData(mesh);
            if (mesh.texture && !mesh.texture->pixels.empty() && mesh.texture->glTexture == 0)
            {
                glGenTextures(1, &mesh.texture->glTexture);
                glBindTexture(GL_TEXTURE_2D, mesh.texture->glTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mesh.texture->width,
                    mesh.texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, mesh.texture->pixels.data());
            }

            Matrix nodeDelta = identityMatrix();
            if (mesh.nodeIndex >= 0 &&
                static_cast<std::size_t>(mesh.nodeIndex) < nodeWorldTransforms[modelIndex].size())
            {
                nodeDelta = multiply(nodeWorldTransforms[modelIndex][
                    static_cast<std::size_t>(mesh.nodeIndex)], inverse(mesh.baseNodeTransform));
            }
            const Matrix model = multiply(objectTransform, nodeDelta);
            const Matrix mvp = multiply(viewProjection, model);
            const bool hasTexture = mesh.texture && mesh.texture->glTexture != 0;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, hasTexture ? mesh.texture->glTexture : 0);
            glUniform1i(hasTextureLocation, hasTexture ? GL_TRUE : GL_FALSE);
            glUniform4fv(colorLocation, 1, mesh.baseColor);
            glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, mvp.data());
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, model.data());
            glBindVertexArray(mesh.vertexArray);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()),
                GL_UNSIGNED_INT, nullptr);
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}
