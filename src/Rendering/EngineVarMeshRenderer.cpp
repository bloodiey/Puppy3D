#include "EngineVarMeshRenderer.h"

#include <cmath>

namespace
{
EngineVar::Vector3 subtract(const EngineVar::Vector3& left, const EngineVar::Vector3& right)
{
    return EngineVar::Vector3(left.x - right.x, left.y - right.y, left.z - right.z);
}

EngineVar::Vector3 cross(const EngineVar::Vector3& left, const EngineVar::Vector3& right)
{
    return EngineVar::Vector3(left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x);
}

EngineVar::Vector3 faceNormal(const std::vector<EngineVar::Edge>& edges)
{
    const EngineVar::Vector3 origin = edges.front().v1.position;
    for (std::size_t index = 1; index + 1 < edges.size(); ++index)
    {
        EngineVar::Vector3 normal = cross(subtract(edges[index].v1.position, origin),
            subtract(edges[index + 1].v1.position, origin));
        if (normal.length() > 0.000001f)
        {
            normal.normalize();
            return normal;
        }
    }
    return EngineVar::Vector3(0.0f, 0.0f, 1.0f);
}
}

bool buildEngineMesh(const EngineVar::Polygon& polygon, EngineMesh& mesh,
    std::string& error)
{
    if (polygon.edges.size() < 3)
    {
        error = "A renderable polygon must have at least three edges.";
        return false;
    }

    const EngineVar::Vector3 generatedNormal = faceNormal(polygon.edges);
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.vertices.reserve(polygon.edges.size());
    mesh.indices.reserve((polygon.edges.size() - 2) * 3);

    for (const EngineVar::Edge& edge : polygon.edges)
    {
        const EngineVar::Vertex& source = edge.v1;
        EngineVar::Vector3 normal = source.normal;
        if (normal.length() <= 0.000001f)
        {
            normal = generatedNormal;
        }
        else
        {
            normal.normalize();
        }
        mesh.vertices.push_back({
            {source.position.x, source.position.y, source.position.z},
            {normal.x, normal.y, normal.z},
            {source.uv.x, source.uv.y}
        });
    }

    for (std::uint32_t index = 1; index + 1 < mesh.vertices.size(); ++index)
    {
        mesh.indices.push_back(0);
        mesh.indices.push_back(index);
        mesh.indices.push_back(index + 1);
    }
    error.clear();
    return true;
}