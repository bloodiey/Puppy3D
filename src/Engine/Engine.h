#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "EngineVariables/EngineVariables.h"

struct EngineVertex
{
    float position[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float uv[2] = {0.0f, 0.0f};
};

struct EngineTexture
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    unsigned int glTexture = 0;
};

struct EngineMesh
{
    std::string name;
    int nodeIndex = -1;
    std::array<float, 16> baseNodeTransform = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::vector<EngineVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::shared_ptr<EngineTexture> texture;
    float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    unsigned int vertexArray = 0;
    unsigned int vertexBuffer = 0;
    unsigned int indexBuffer = 0;
};

struct EngineModelNode
{
    std::string name;
    int parentIndex = -1;
    std::array<float, 16> matrix{};
    bool hasMatrix = false;
    float translation[3] = {0.0f, 0.0f, 0.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
};

struct EngineAnimationSampler
{
    std::vector<float> times;
    std::vector<float> values;
    std::size_t components = 0;
    std::string interpolation = "LINEAR";
};

enum class EngineAnimationPath
{
    Translation,
    Rotation,
    Scale
};

struct EngineAnimationChannel
{
    std::size_t nodeIndex = 0;
    std::size_t samplerIndex = 0;
    EngineAnimationPath path = EngineAnimationPath::Translation;
};

struct EngineAnimationClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<EngineAnimationSampler> samplers;
    std::vector<EngineAnimationChannel> channels;
};

struct EngineModelData
{
    std::vector<EngineMesh> meshes;
    std::vector<EngineModelNode> nodes;
    std::vector<EngineAnimationClip> animations;
};

struct EngineObject
{
    std::string name;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float color[3] = {0.25f, 0.65f, 0.95f};
    std::array<std::array<float, 3>, 8> vertices = {{
        {{-0.5f, -0.5f, -0.5f}}, {{0.5f, -0.5f, -0.5f}},
        {{-0.5f, 0.5f, -0.5f}}, {{0.5f, 0.5f, -0.5f}},
        {{-0.5f, -0.5f, 0.5f}}, {{0.5f, -0.5f, 0.5f}},
        {{-0.5f, 0.5f, 0.5f}}, {{0.5f, 0.5f, 0.5f}}
    }};
    bool isModel = false;
    bool isGeneratedGeometry = false;
    std::string sourcePath;
};

struct EngineCamera
{
    float position[3] = {0.0f, 2.0f, 8.0f};
    float yaw = -90.0f;
    float pitch = -10.0f;
};

struct EngineModelInstance
{
    int objectId = 0;
    EngineModelData data;
    int activeAnimation = -1;
    float animationTime = 0.0f;
    bool animationPlaying = false;
};

class EngineScene
{
public:
    EngineScene();

    int addCube(const std::string& name, float x, float y, float z);
    int addPolygon(const std::string& name, const EngineVar::Polygon& polygon,
        EngineVar::Polygon::Color3 color);
    bool setPosition(int objectId, float x, float y, float z);
    bool setRotation(int objectId, float x, float y, float z);
    bool setScale(int objectId, float x, float y, float z);
    bool setColor(int objectId, float red, float green, float blue);
    bool setObjectName(int objectId, const std::string& name);
    bool setCubeVertex(int objectId, std::size_t vertexIndex, float x, float y, float z);
    bool importGlb(const std::filesystem::path& path, std::string& error);
    std::size_t objectCount() const;
    const EngineObject* object(int objectId) const;
    int pickObject(float x, float y, int width, int height, const EngineCamera& camera) const;
    std::size_t animationCount(int objectId) const;
    std::string animationName(int objectId, std::size_t animationIndex) const;
    float animationDuration(int objectId, std::size_t animationIndex) const;
    int activeAnimation(int objectId) const;
    float animationTime(int objectId) const;
    bool setAnimation(int objectId, int animationIndex);
    bool setAnimationPlaying(int objectId, bool playing);
    bool setAnimationTime(int objectId, float time);
    void updateAnimations(float deltaTime);
    void clear();
    bool initializeOpenGl(std::string& error);
    void releaseOpenGlResources();
    void render(int width, int height, const EngineCamera& camera,
        int selectedObjectId = 0, int selectedVertexIndex = -1);

private:
    std::vector<EngineObject> objects_;
    std::vector<EngineMesh> meshes_;
    std::vector<EngineModelInstance> models_;
    std::vector<EngineMesh> retiredMeshes_;
    unsigned int shaderProgram_ = 0;
    unsigned int cubeVertexArray_ = 0;
    unsigned int cubeVertexBuffer_ = 0;
    unsigned int gridVertexArray_ = 0;
    unsigned int gridVertexBuffer_ = 0;
};
