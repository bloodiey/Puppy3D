#include "GlbImporter.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <memory>

namespace
{
using Json = nlohmann::json;
using Matrix = std::array<float, 16>;

constexpr std::uint32_t glbMagic = 0x46546c67;
constexpr std::uint32_t jsonChunkType = 0x4e4f534a;
constexpr std::uint32_t binChunkType = 0x004e4942;
constexpr std::uint64_t maxGlbBytes = 512ull * 1024ull * 1024ull;
constexpr std::uint32_t maxImageDimension = 8192;
constexpr std::uint64_t maxImagePixels = 64ull * 1024ull * 1024ull;

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 4)
    {
        throw std::runtime_error("Unexpected end of GLB header.");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

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

Matrix nodeTransform(const Json& node)
{
    if (node.contains("matrix"))
    {
        const Json& values = node.at("matrix");
        if (!values.is_array() || values.size() != 16)
        {
            throw std::runtime_error("A node has an invalid transform matrix.");
        }
        Matrix result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            result[index] = values[index].get<float>();
        }
        return result;
    }

    const Json translation = node.value("translation", Json::array({0.0, 0.0, 0.0}));
    const Json rotation = node.value("rotation", Json::array({0.0, 0.0, 0.0, 1.0}));
    const Json scale = node.value("scale", Json::array({1.0, 1.0, 1.0}));
    if (!translation.is_array() || translation.size() != 3 ||
        !rotation.is_array() || rotation.size() != 4 ||
        !scale.is_array() || scale.size() != 3)
    {
        throw std::runtime_error("A node has an invalid translation, rotation, or scale.");
    }

    const float x = rotation[0].get<float>();
    const float y = rotation[1].get<float>();
    const float z = rotation[2].get<float>();
    const float w = rotation[3].get<float>();
    Matrix result = identityMatrix();
    result[0] = (1.0f - 2.0f * (y * y + z * z)) * scale[0].get<float>();
    result[1] = (2.0f * (x * y + z * w)) * scale[0].get<float>();
    result[2] = (2.0f * (x * z - y * w)) * scale[0].get<float>();
    result[4] = (2.0f * (x * y - z * w)) * scale[1].get<float>();
    result[5] = (1.0f - 2.0f * (x * x + z * z)) * scale[1].get<float>();
    result[6] = (2.0f * (y * z + x * w)) * scale[1].get<float>();
    result[8] = (2.0f * (x * z + y * w)) * scale[2].get<float>();
    result[9] = (2.0f * (y * z - x * w)) * scale[2].get<float>();
    result[10] = (1.0f - 2.0f * (x * x + y * y)) * scale[2].get<float>();
    result[12] = translation[0].get<float>();
    result[13] = translation[1].get<float>();
    result[14] = translation[2].get<float>();
    return result;
}

void transformPosition(EngineVertex& vertex, const Matrix& matrix)
{
    const float x = vertex.position[0];
    const float y = vertex.position[1];
    const float z = vertex.position[2];
    vertex.position[0] = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
    vertex.position[1] = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
    vertex.position[2] = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14];

    const float nx = matrix[0] * vertex.normal[0] + matrix[4] * vertex.normal[1] +
        matrix[8] * vertex.normal[2];
    const float ny = matrix[1] * vertex.normal[0] + matrix[5] * vertex.normal[1] +
        matrix[9] * vertex.normal[2];
    const float nz = matrix[2] * vertex.normal[0] + matrix[6] * vertex.normal[1] +
        matrix[10] * vertex.normal[2];
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length > 0.0f)
    {
        vertex.normal[0] = nx / length;
        vertex.normal[1] = ny / length;
        vertex.normal[2] = nz / length;
    }
}

std::size_t componentSize(std::uint32_t type)
{
    switch (type)
    {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        throw std::runtime_error("An accessor uses an unsupported component type.");
    }
}

std::size_t componentCount(const std::string& type)
{
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    throw std::runtime_error("An accessor uses an unsupported element type.");
}

float readComponent(const std::uint8_t* data, std::uint32_t type, bool normalized)
{
    switch (type)
    {
    case 5120:
    {
        std::int8_t value;
        std::memcpy(&value, data, sizeof(value));
        return normalized ? std::max(-1.0f, static_cast<float>(value) / 127.0f)
                          : static_cast<float>(value);
    }
    case 5121:
        return normalized ? static_cast<float>(*data) / 255.0f : static_cast<float>(*data);
    case 5122:
    {
        std::int16_t value;
        std::memcpy(&value, data, sizeof(value));
        return normalized ? std::max(-1.0f, static_cast<float>(value) / 32767.0f)
                          : static_cast<float>(value);
    }
    case 5123:
    {
        std::uint16_t value;
        std::memcpy(&value, data, sizeof(value));
        return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
    }
    case 5125:
    {
        std::uint32_t value;
        std::memcpy(&value, data, sizeof(value));
        return static_cast<float>(value);
    }
    case 5126:
    {
        float value;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }
    default:
        throw std::runtime_error("An accessor uses an unsupported component type.");
    }
}

std::vector<float> readAccessor(const Json& document,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    std::size_t accessorIndex, const std::string& expectedType,
    std::uint32_t expectedComponentType = 0)
{
    const Json& accessors = document.at("accessors");
    if (accessorIndex >= accessors.size())
    {
        throw std::runtime_error("A mesh references an invalid accessor.");
    }
    const Json& accessor = accessors.at(accessorIndex);
    if (accessor.contains("sparse"))
    {
        throw std::runtime_error("Sparse accessors are not supported.");
    }
    if (!accessor.contains("bufferView"))
    {
        throw std::runtime_error("Accessors without a buffer view are not supported.");
    }
    const std::uint32_t componentType = accessor.at("componentType").get<std::uint32_t>();
    if (expectedComponentType != 0 && componentType != expectedComponentType)
    {
        throw std::runtime_error("A mesh attribute uses an unsupported component type.");
    }
    const std::size_t components = componentCount(accessor.at("type").get<std::string>());
    if (accessor.at("type").get<std::string>() != expectedType)
    {
        throw std::runtime_error("A mesh attribute has the wrong number of components.");
    }
    const std::size_t count = accessor.at("count").get<std::size_t>();
    if (count > std::numeric_limits<std::size_t>::max() / components)
    {
        throw std::runtime_error("An accessor is too large.");
    }

    const std::size_t viewIndex = accessor.at("bufferView").get<std::size_t>();
    const Json& views = document.at("bufferViews");
    if (viewIndex >= views.size())
    {
        throw std::runtime_error("An accessor references an invalid buffer view.");
    }
    const Json& view = views.at(viewIndex);
    const std::size_t bufferIndex = view.value("buffer", std::size_t{0});
    if (bufferIndex >= buffers.size())
    {
        throw std::runtime_error("A buffer view references an invalid buffer.");
    }

    const std::size_t bytesPerComponent = componentSize(componentType);
    const std::size_t elementSize = bytesPerComponent * components;
    const std::size_t stride = view.value("byteStride", elementSize);
    if (stride < elementSize)
    {
        throw std::runtime_error("A buffer view has an invalid byte stride.");
    }
    const std::size_t viewOffset = view.value("byteOffset", std::size_t{0});
    const std::size_t accessorOffset = accessor.value("byteOffset", std::size_t{0});
    if (accessorOffset > std::numeric_limits<std::size_t>::max() - viewOffset)
    {
        throw std::runtime_error("An accessor offset overflows.");
    }
    const std::size_t baseOffset = viewOffset + accessorOffset;
    const auto& buffer = buffers[bufferIndex];
    if (count != 0)
    {
        const std::size_t finalElement = count - 1;
        if (finalElement > (std::numeric_limits<std::size_t>::max() - baseOffset) / stride)
        {
            throw std::runtime_error("An accessor range overflows.");
        }
        const std::size_t lastOffset = baseOffset + finalElement * stride;
        if (lastOffset > buffer.size() || elementSize > buffer.size() - lastOffset)
        {
            throw std::runtime_error("An accessor reads beyond the end of its buffer.");
        }
    }
    if (count > std::numeric_limits<std::size_t>::max() / components)
    {
        throw std::runtime_error("An accessor is too large.");
    }

    const bool normalized = accessor.value("normalized", false);
    std::vector<float> result(count * components);
    for (std::size_t item = 0; item < count; ++item)
    {
        for (std::size_t component = 0; component < components; ++component)
        {
            const std::size_t offset = baseOffset + item * stride + component * bytesPerComponent;
            result[item * components + component] =
                readComponent(buffer.data() + offset, componentType, normalized);
        }
    }
    return result;
}

std::vector<std::uint8_t> decodeDataUri(const std::string& uri)
{
    const std::size_t comma = uri.find(',');
    if (uri.compare(0, 5, "data:") != 0 || comma == std::string::npos ||
        uri.substr(0, comma).find(";base64") == std::string::npos)
    {
        throw std::runtime_error("Only base64 data URIs are supported for embedded assets.");
    }
    const std::string encoded = uri.substr(comma + 1);
    gsize decodedLength = 0;
    guchar* decoded = g_base64_decode(encoded.c_str(), &decodedLength);
    if (!decoded && !encoded.empty())
    {
        throw std::runtime_error("Could not decode a base64 data URI.");
    }
    std::vector<std::uint8_t> result;
    if (decodedLength != 0)
    {
        result.assign(decoded, decoded + decodedLength);
    }
    g_free(decoded);
    return result;
}

EngineTexture decodeImage(const std::vector<std::uint8_t>& encoded)
{
    if (encoded.empty())
    {
        throw std::runtime_error("A texture image is empty.");
    }
    GError* error = nullptr;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    if (!gdk_pixbuf_loader_write(loader, encoded.data(), encoded.size(), &error) ||
        !gdk_pixbuf_loader_close(loader, &error))
    {
        const std::string message = error ? error->message : "Unknown image decoding error.";
        if (error) g_error_free(error);
        g_object_unref(loader);
        throw std::runtime_error("Could not decode GLB texture: " + message);
    }

    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (!pixbuf)
    {
        g_object_unref(loader);
        throw std::runtime_error("The GLB image decoder returned no image.");
    }
    g_object_ref(pixbuf);
    g_object_unref(loader);

    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const int rowStride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar* source = gdk_pixbuf_get_pixels(pixbuf);
    if (width <= 0 || height <= 0 || width > static_cast<int>(maxImageDimension) ||
        height > static_cast<int>(maxImageDimension) ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > maxImagePixels ||
        (channels != 3 && channels != 4))
    {
        g_object_unref(pixbuf);
        throw std::runtime_error("A GLB texture has unsupported or excessive dimensions.");
    }

    EngineTexture result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const guchar* pixel = source + y * rowStride + x * channels;
            const std::size_t destination =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)) * 4;
            result.pixels[destination] = pixel[0];
            result.pixels[destination + 1] = pixel[1];
            result.pixels[destination + 2] = pixel[2];
            result.pixels[destination + 3] = channels == 4 ? pixel[3] : 255;
        }
    }
    g_object_unref(pixbuf);
    return result;
}

Matrix loadDocumentMatrix(const Json& node)
{
    return nodeTransform(node);
}

void loadPrimitive(const Json& document,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    const std::vector<std::shared_ptr<EngineTexture>>& images,
    const std::vector<std::size_t>& textureSources,
    const Json& primitive, const Matrix& transform, int nodeIndex,
    const std::string& meshName,
    std::vector<EngineMesh>& output)
{
    if (primitive.value("mode", 4) != 4)
    {
        return;
    }
    if (!primitive.contains("attributes") ||
        !primitive.at("attributes").contains("POSITION"))
    {
        throw std::runtime_error("A triangle primitive is missing POSITION data.");
    }
    const Json& attributes = primitive.at("attributes");
    const auto positions = readAccessor(document, buffers,
        attributes.at("POSITION").get<std::size_t>(), "VEC3", 5126);
    const std::size_t vertexCount = positions.size() / 3;
    if (vertexCount == 0)
    {
        return;
    }

    std::vector<float> normals;
    if (attributes.contains("NORMAL"))
    {
        normals = readAccessor(document, buffers,
            attributes.at("NORMAL").get<std::size_t>(), "VEC3");
        if (normals.size() != vertexCount * 3)
        {
            throw std::runtime_error("A primitive's NORMAL count does not match POSITION.");
        }
    }
    std::vector<float> texcoords;
    if (attributes.contains("TEXCOORD_0"))
    {
        texcoords = readAccessor(document, buffers,
            attributes.at("TEXCOORD_0").get<std::size_t>(), "VEC2");
        if (texcoords.size() != vertexCount * 2)
        {
            throw std::runtime_error("A primitive's TEXCOORD_0 count does not match POSITION.");
        }
    }

    EngineMesh mesh;
    mesh.name = meshName;
    mesh.nodeIndex = nodeIndex;
    mesh.baseNodeTransform = transform;
    mesh.vertices.resize(vertexCount);
    for (std::size_t index = 0; index < vertexCount; ++index)
    {
        EngineVertex& vertex = mesh.vertices[index];
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            vertex.position[axis] = positions[index * 3 + axis];
            if (!normals.empty())
            {
                vertex.normal[axis] = normals[index * 3 + axis];
            }
        }
        if (!texcoords.empty())
        {
            vertex.uv[0] = texcoords[index * 2];
            vertex.uv[1] = texcoords[index * 2 + 1];
        }
        transformPosition(vertex, transform);
    }

    if (primitive.contains("indices"))
    {
        const std::size_t accessorIndex = primitive.at("indices").get<std::size_t>();
        if (accessorIndex >= document.at("accessors").size())
        {
            throw std::runtime_error("A primitive references an invalid index accessor.");
        }
        const Json& accessor = document.at("accessors").at(accessorIndex);
        const std::uint32_t componentType = accessor.at("componentType").get<std::uint32_t>();
        if (componentType != 5121 && componentType != 5123 && componentType != 5125)
        {
            throw std::runtime_error("Mesh indices must use unsigned byte, short, or int.");
        }
        const auto rawIndices = readAccessor(document, buffers, accessorIndex, "SCALAR");
        mesh.indices.reserve(rawIndices.size());
        for (float index : rawIndices)
        {
            if (index < 0.0f || index >= static_cast<float>(vertexCount))
            {
                throw std::runtime_error("A primitive contains an out-of-range vertex index.");
            }
            mesh.indices.push_back(static_cast<std::uint32_t>(index));
        }
    }
    else
    {
        mesh.indices.resize(vertexCount);
        for (std::size_t index = 0; index < vertexCount; ++index)
        {
            mesh.indices[index] = static_cast<std::uint32_t>(index);
        }
    }

    if (normals.empty())
    {
        std::vector<std::array<float, 3>> accumulated(vertexCount, {0.0f, 0.0f, 0.0f});
        for (std::size_t triangle = 0; triangle + 2 < mesh.indices.size(); triangle += 3)
        {
            const EngineVertex& a = mesh.vertices[mesh.indices[triangle]];
            const EngineVertex& b = mesh.vertices[mesh.indices[triangle + 1]];
            const EngineVertex& c = mesh.vertices[mesh.indices[triangle + 2]];
            const float abx = b.position[0] - a.position[0];
            const float aby = b.position[1] - a.position[1];
            const float abz = b.position[2] - a.position[2];
            const float acx = c.position[0] - a.position[0];
            const float acy = c.position[1] - a.position[1];
            const float acz = c.position[2] - a.position[2];
            const std::array<float, 3> face = {
                aby * acz - abz * acy,
                abz * acx - abx * acz,
                abx * acy - aby * acx
            };
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                auto& normal = accumulated[mesh.indices[triangle + corner]];
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    normal[axis] += face[axis];
                }
            }
        }
        for (std::size_t index = 0; index < vertexCount; ++index)
        {
            const float length = std::sqrt(
                accumulated[index][0] * accumulated[index][0] +
                accumulated[index][1] * accumulated[index][1] +
                accumulated[index][2] * accumulated[index][2]);
            if (length > 0.0f)
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    mesh.vertices[index].normal[axis] = accumulated[index][axis] / length;
                }
            }
        }
    }

    if (primitive.contains("material"))
    {
        const std::size_t materialIndex = primitive.at("material").get<std::size_t>();
        if (materialIndex >= document.at("materials").size())
        {
            throw std::runtime_error("A primitive references an invalid material.");
        }
        const Json& material = document.at("materials").at(materialIndex);
        if (material.contains("pbrMetallicRoughness"))
        {
            const Json& pbr = material.at("pbrMetallicRoughness");
            const Json color = pbr.value("baseColorFactor", Json::array({1.0, 1.0, 1.0, 1.0}));
            if (!color.is_array() || color.size() != 4)
            {
                throw std::runtime_error("A material has an invalid base color factor.");
            }
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                mesh.baseColor[channel] = std::clamp(color[channel].get<float>(), 0.0f, 1.0f);
            }

            if (pbr.contains("baseColorTexture") && !texcoords.empty())
            {
                const std::size_t textureIndex =
                    pbr.at("baseColorTexture").at("index").get<std::size_t>();
                if (textureIndex >= textureSources.size() ||
                    textureSources[textureIndex] >= images.size())
                {
                    throw std::runtime_error("A material references an invalid base color texture.");
                }
                mesh.texture = images[textureSources[textureIndex]];
            }
        }
    }
    output.push_back(std::move(mesh));
}

void visitNode(const Json& document,
    const std::vector<std::vector<std::uint8_t>>& buffers,
    const std::vector<std::shared_ptr<EngineTexture>>& images,
    const std::vector<std::size_t>& textureSources,
    std::size_t nodeIndex, const Matrix& parentTransform,
    std::vector<bool>& visiting, std::vector<EngineMesh>& output)
{
    const Json& nodes = document.at("nodes");
    if (nodeIndex >= nodes.size())
    {
        throw std::runtime_error("A scene references an invalid node.");
    }
    if (visiting[nodeIndex])
    {
        throw std::runtime_error("The scene graph contains a node cycle.");
    }

    visiting[nodeIndex] = true;
    const Json& node = nodes.at(nodeIndex);
    const Matrix transform = multiply(parentTransform, loadDocumentMatrix(node));
    if (node.contains("mesh"))
    {
        const std::size_t meshIndex = node.at("mesh").get<std::size_t>();
        const Json& meshes = document.at("meshes");
        if (meshIndex >= meshes.size())
        {
            throw std::runtime_error("A scene node references an invalid mesh.");
        }
        const Json& mesh = meshes.at(meshIndex);
        const std::string name = mesh.value("name", "Mesh " + std::to_string(meshIndex + 1));
        for (const Json& primitive : mesh.at("primitives"))
        {
            loadPrimitive(document, buffers, images, textureSources,
                primitive, transform, static_cast<int>(nodeIndex), name, output);
        }
    }
    if (node.contains("children"))
    {
        for (const Json& child : node.at("children"))
        {
            visitNode(document, buffers, images, textureSources,
                child.get<std::size_t>(), transform, visiting, output);
        }
    }
    visiting[nodeIndex] = false;
}
}

bool loadGlb(const std::filesystem::path& path,
    EngineModelData& model, std::string& error)
{
    try
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Could not open '" + path.string() + "'.");
        }
        input.seekg(0, std::ios::end);
        const std::streamoff fileSize = input.tellg();
        if (fileSize < 12 || static_cast<std::uint64_t>(fileSize) > maxGlbBytes)
        {
            throw std::runtime_error("The GLB file is empty, truncated, or larger than 512 MiB.");
        }
        input.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> file(static_cast<std::size_t>(fileSize));
        input.read(reinterpret_cast<char*>(file.data()), fileSize);
        if (!input)
        {
            throw std::runtime_error("Could not read the complete GLB file.");
        }
        if (readU32(file, 0) != glbMagic || readU32(file, 4) != 2)
        {
            throw std::runtime_error("The file is not a glTF 2.0 binary (GLB).");
        }
        const std::uint32_t declaredLength = readU32(file, 8);
        if (declaredLength != file.size())
        {
            throw std::runtime_error("The GLB header length does not match the file size.");
        }

        Json document;
        std::vector<std::uint8_t> binaryChunk;
        bool hasJson = false;
        std::size_t offset = 12;
        while (offset < file.size())
        {
            if (file.size() - offset < 8)
            {
                throw std::runtime_error("A GLB chunk header is truncated.");
            }
            const std::uint32_t chunkLength = readU32(file, offset);
            const std::uint32_t chunkType = readU32(file, offset + 4);
            offset += 8;
            if (chunkLength > file.size() - offset)
            {
                throw std::runtime_error("A GLB chunk extends beyond the file.");
            }
            if (chunkType == jsonChunkType)
            {
                if (hasJson)
                {
                    throw std::runtime_error("The GLB contains more than one JSON chunk.");
                }
                document = Json::parse(file.begin() + static_cast<std::ptrdiff_t>(offset),
                    file.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
                hasJson = true;
            }
            else if (chunkType == binChunkType && binaryChunk.empty())
            {
                binaryChunk.assign(file.begin() + static_cast<std::ptrdiff_t>(offset),
                    file.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
            }
            offset += chunkLength;
        }
        if (!hasJson || !document.is_object())
        {
            throw std::runtime_error("The GLB does not contain a valid JSON chunk.");
        }
        if (document.value("asset", Json::object()).value("version", std::string()) != "2.0")
        {
            throw std::runtime_error("The GLB does not declare glTF 2.0.");
        }
        if (document.contains("extensionsRequired") &&
            !document.at("extensionsRequired").empty())
        {
            throw std::runtime_error(
                "This importer does not support required glTF extensions.");
        }

        std::vector<std::vector<std::uint8_t>> buffers;
        if (document.contains("buffers"))
        {
            const Json& definitions = document.at("buffers");
            buffers.reserve(definitions.size());
            for (std::size_t index = 0; index < definitions.size(); ++index)
            {
                const Json& definition = definitions.at(index);
                std::vector<std::uint8_t> bytes;
                if (definition.contains("uri"))
                {
                    const std::string uri = definition.at("uri").get<std::string>();
                    if (uri.compare(0, 5, "data:") == 0)
                    {
                        bytes = decodeDataUri(uri);
                    }
                    else
                    {
                        const std::filesystem::path external =
                            path.parent_path() / std::filesystem::path(uri);
                        std::ifstream externalInput(external, std::ios::binary);
                        if (!externalInput)
                        {
                            throw std::runtime_error("Could not open external buffer '" +
                                external.string() + "'.");
                        }
                        externalInput.seekg(0, std::ios::end);
                        const std::streamoff externalSize = externalInput.tellg();
                        if (externalSize < 0 ||
                            static_cast<std::uint64_t>(externalSize) > maxGlbBytes)
                        {
                            throw std::runtime_error("An external buffer has an invalid size.");
                        }
                        externalInput.seekg(0, std::ios::beg);
                        bytes.resize(static_cast<std::size_t>(externalSize));
                        externalInput.read(reinterpret_cast<char*>(bytes.data()), externalSize);
                        if (!externalInput)
                        {
                            throw std::runtime_error("Could not read an external buffer completely.");
                        }
                    }
                }
                else if (index == 0)
                {
                    bytes = binaryChunk;
                }
                else
                {
                    throw std::runtime_error("A GLB buffer has no URI or binary chunk.");
                }

                if (bytes.size() < definition.at("byteLength").get<std::size_t>())
                {
                    throw std::runtime_error("A GLB buffer is shorter than its declared byte length.");
                }
                buffers.push_back(std::move(bytes));
            }
        }

        std::vector<std::shared_ptr<EngineTexture>> images;
        if (document.contains("images"))
        {
            const Json& definitions = document.at("images");
            images.reserve(definitions.size());
            for (const Json& image : definitions)
            {
                std::vector<std::uint8_t> encoded;
                if (image.contains("bufferView"))
                {
                    const std::size_t viewIndex = image.at("bufferView").get<std::size_t>();
                    if (viewIndex >= document.at("bufferViews").size())
                    {
                        throw std::runtime_error("A GLB image references an invalid buffer view.");
                    }
                    const Json& view = document.at("bufferViews").at(viewIndex);
                    const std::size_t bufferIndex = view.value("buffer", std::size_t{0});
                    if (bufferIndex >= buffers.size())
                    {
                        throw std::runtime_error("A GLB image references an invalid buffer.");
                    }
                    const std::size_t byteOffset = view.value("byteOffset", std::size_t{0});
                    const std::size_t byteLength = view.at("byteLength").get<std::size_t>();
                    const auto& buffer = buffers[bufferIndex];
                    if (byteOffset > buffer.size() || byteLength > buffer.size() - byteOffset)
                    {
                        throw std::runtime_error("A GLB image extends beyond its buffer.");
                    }
                    encoded.assign(buffer.begin() + static_cast<std::ptrdiff_t>(byteOffset),
                        buffer.begin() + static_cast<std::ptrdiff_t>(byteOffset + byteLength));
                }
                else if (image.contains("uri"))
                {
                    const std::string uri = image.at("uri").get<std::string>();
                    if (uri.compare(0, 5, "data:") == 0)
                    {
                        encoded = decodeDataUri(uri);
                    }
                    else
                    {
                        const std::filesystem::path external =
                            path.parent_path() / std::filesystem::path(uri);
                        std::ifstream imageInput(external, std::ios::binary);
                        if (!imageInput)
                        {
                            throw std::runtime_error("Could not open external image '" +
                                external.string() + "'.");
                        }
                        imageInput.seekg(0, std::ios::end);
                        const std::streamoff imageSize = imageInput.tellg();
                        if (imageSize <= 0 ||
                            static_cast<std::uint64_t>(imageSize) > maxGlbBytes)
                        {
                            throw std::runtime_error("An external image has an invalid size.");
                        }
                        imageInput.seekg(0, std::ios::beg);
                        encoded.resize(static_cast<std::size_t>(imageSize));
                        imageInput.read(reinterpret_cast<char*>(encoded.data()), imageSize);
                        if (!imageInput)
                        {
                            throw std::runtime_error("Could not read an external image completely.");
                        }
                    }
                }
                else
                {
                    throw std::runtime_error("A GLB image has no URI or buffer view.");
                }
                images.push_back(std::make_shared<EngineTexture>(decodeImage(encoded)));
            }
        }

        std::vector<std::size_t> textureSources;
        if (document.contains("textures"))
        {
            const Json& textures = document.at("textures");
            textureSources.reserve(textures.size());
            for (const Json& texture : textures)
            {
                const std::size_t source = texture.value("source", std::size_t{0});
                if (source >= images.size())
                {
                    throw std::runtime_error("A texture references an invalid image.");
                }
                textureSources.push_back(source);
            }
        }

        EngineModelData imported;
        if (document.contains("nodes") && !document.at("nodes").empty())
        {
            const Json& nodes = document.at("nodes");
            imported.nodes.resize(nodes.size());
            for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
            {
                const Json& node = nodes.at(nodeIndex);
                EngineModelNode& parsed = imported.nodes[nodeIndex];
                parsed.name = node.value("name", "Node " + std::to_string(nodeIndex + 1));
                if (node.contains("matrix"))
                {
                    const Json& values = node.at("matrix");
                    if (!values.is_array() || values.size() != 16)
                    {
                        throw std::runtime_error("A node has an invalid transform matrix.");
                    }
                    parsed.hasMatrix = true;
                    for (std::size_t component = 0; component < 16; ++component)
                    {
                        parsed.matrix[component] = values[component].get<float>();
                    }
                }
                else
                {
                    const Json translation = node.value("translation",
                        Json::array({0.0, 0.0, 0.0}));
                    const Json rotation = node.value("rotation",
                        Json::array({0.0, 0.0, 0.0, 1.0}));
                    const Json scale = node.value("scale", Json::array({1.0, 1.0, 1.0}));
                    if (!translation.is_array() || translation.size() != 3 ||
                        !rotation.is_array() || rotation.size() != 4 ||
                        !scale.is_array() || scale.size() != 3)
                    {
                        throw std::runtime_error("A node has an invalid translation, rotation, or scale.");
                    }
                    for (std::size_t axis = 0; axis < 3; ++axis)
                    {
                        parsed.translation[axis] = translation[axis].get<float>();
                        parsed.scale[axis] = scale[axis].get<float>();
                    }
                    for (std::size_t component = 0; component < 4; ++component)
                    {
                        parsed.rotation[component] = rotation[component].get<float>();
                    }
                }
            }
            for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
            {
                const Json& node = nodes.at(nodeIndex);
                if (!node.contains("children"))
                {
                    continue;
                }
                for (const Json& child : node.at("children"))
                {
                    const std::size_t childIndex = child.get<std::size_t>();
                    if (childIndex >= nodes.size() || childIndex == nodeIndex ||
                        imported.nodes[childIndex].parentIndex != -1)
                    {
                        throw std::runtime_error("A node has an invalid or duplicate parent reference.");
                    }
                    imported.nodes[childIndex].parentIndex = static_cast<int>(nodeIndex);
                }
            }
            std::vector<std::size_t> roots;
            const std::size_t sceneIndex = document.value("scene", std::size_t{0});
            if (document.contains("scenes") && sceneIndex < document.at("scenes").size())
            {
                const Json& scene = document.at("scenes").at(sceneIndex);
                if (scene.contains("nodes"))
                {
                    for (const Json& root : scene.at("nodes"))
                    {
                        roots.push_back(root.get<std::size_t>());
                    }
                }
            }
            if (roots.empty())
            {
                std::vector<bool> isChild(document.at("nodes").size(), false);
                for (const Json& node : document.at("nodes"))
                {
                    if (node.contains("children"))
                    {
                        for (const Json& child : node.at("children"))
                        {
                            const std::size_t childIndex = child.get<std::size_t>();
                            if (childIndex >= isChild.size())
                            {
                                throw std::runtime_error("A node references an invalid child.");
                            }
                            isChild[childIndex] = true;
                        }
                    }
                }
                for (std::size_t index = 0; index < isChild.size(); ++index)
                {
                    if (!isChild[index]) roots.push_back(index);
                }
            }
            std::vector<bool> visiting(document.at("nodes").size(), false);
            for (std::size_t root : roots)
            {
                visitNode(document, buffers, images, textureSources,
                    root, identityMatrix(), visiting, imported.meshes);
            }
        }
        else if (document.contains("meshes"))
        {
            for (std::size_t index = 0; index < document.at("meshes").size(); ++index)
            {
                const Json& mesh = document.at("meshes").at(index);
                const std::string name = mesh.value("name", "Mesh " + std::to_string(index + 1));
                for (const Json& primitive : mesh.at("primitives"))
                {
                    loadPrimitive(document, buffers, images, textureSources,
                        primitive, identityMatrix(), -1, name, imported.meshes);
                }
            }
        }
        if (document.contains("animations"))
        {
            const Json& animations = document.at("animations");
            for (std::size_t animationIndex = 0; animationIndex < animations.size(); ++animationIndex)
            {
                const Json& animation = animations.at(animationIndex);
                EngineAnimationClip clip;
                clip.name = animation.value("name", "Animation " +
                    std::to_string(animationIndex + 1));
                const Json& samplerDefinitions = animation.at("samplers");
                clip.samplers.reserve(samplerDefinitions.size());
                for (const Json& definition : samplerDefinitions)
                {
                    EngineAnimationSampler sampler;
                    sampler.times = readAccessor(document, buffers,
                        definition.at("input").get<std::size_t>(), "SCALAR", 5126);
                    if (sampler.times.empty())
                    {
                        throw std::runtime_error("An animation sampler has no keyframes.");
                    }
                    for (std::size_t key = 0; key < sampler.times.size(); ++key)
                    {
                        if (!std::isfinite(sampler.times[key]) || sampler.times[key] < 0.0f ||
                            (key > 0 && sampler.times[key] <= sampler.times[key - 1]))
                        {
                            throw std::runtime_error(
                                "Animation key times must be finite and strictly increasing.");
                        }
                    }
                    const std::size_t outputAccessor =
                        definition.at("output").get<std::size_t>();
                    if (outputAccessor >= document.at("accessors").size())
                    {
                        throw std::runtime_error("An animation references an invalid output accessor.");
                    }
                    const Json& outputDefinition = document.at("accessors").at(outputAccessor);
                    const std::string outputType = outputDefinition.at("type").get<std::string>();
                    if (outputType != "SCALAR" && outputType != "VEC3" && outputType != "VEC4")
                    {
                        throw std::runtime_error("An animation uses an unsupported output accessor type.");
                    }
                    sampler.components = componentCount(outputType);
                    sampler.values = readAccessor(document, buffers, outputAccessor,
                        outputType, 5126);
                    sampler.interpolation = definition.value("interpolation", "LINEAR");
                    if (sampler.interpolation != "LINEAR" && sampler.interpolation != "STEP" &&
                        sampler.interpolation != "CUBICSPLINE")
                    {
                        throw std::runtime_error("An animation uses an unsupported interpolation mode.");
                    }
                    const std::size_t multiplier = sampler.interpolation == "CUBICSPLINE" ? 3 : 1;
                    if (sampler.times.size() > std::numeric_limits<std::size_t>::max() /
                        (sampler.components * multiplier) || sampler.values.size() !=
                        sampler.times.size() * sampler.components * multiplier)
                    {
                        throw std::runtime_error("An animation sampler has mismatched keyframe data.");
                    }
                    clip.duration = std::max(clip.duration, sampler.times.back());
                    clip.samplers.push_back(std::move(sampler));
                }
                for (const Json& channel : animation.at("channels"))
                {
                    const Json& target = channel.at("target");
                    const std::string pathName = target.value("path", std::string());
                    if (pathName == "weights")
                    {
                        continue;
                    }
                    EngineAnimationChannel parsed;
                    parsed.nodeIndex = target.at("node").get<std::size_t>();
                    parsed.samplerIndex = channel.at("sampler").get<std::size_t>();
                    if (parsed.nodeIndex >= imported.nodes.size() ||
                        parsed.samplerIndex >= clip.samplers.size())
                    {
                        throw std::runtime_error(
                            "An animation channel references an invalid node or sampler.");
                    }
                    if (imported.nodes[parsed.nodeIndex].hasMatrix)
                    {
                        throw std::runtime_error(
                            "Animated nodes must use translation, rotation, and scale properties rather than a matrix.");
                    }
                    if (pathName == "translation")
                    {
                        parsed.path = EngineAnimationPath::Translation;
                    }
                    else if (pathName == "rotation")
                    {
                        parsed.path = EngineAnimationPath::Rotation;
                    }
                    else if (pathName == "scale")
                    {
                        parsed.path = EngineAnimationPath::Scale;
                    }
                    else
                    {
                        throw std::runtime_error("An animation targets an unsupported node property.");
                    }
                    const std::size_t expectedComponents =
                        parsed.path == EngineAnimationPath::Rotation ? 4 : 3;
                    if (clip.samplers[parsed.samplerIndex].components != expectedComponents)
                    {
                        throw std::runtime_error(
                            "An animation output has the wrong number of components for its target.");
                    }
                    clip.channels.push_back(parsed);
                }
                if (!clip.channels.empty())
                {
                    imported.animations.push_back(std::move(clip));
                }
            }
        }

        if (imported.meshes.empty())
        {
            throw std::runtime_error("The GLB contains no supported triangle meshes.");
        }
        model = std::move(imported);
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool loadGlb(const std::filesystem::path& path,
    std::vector<EngineMesh>& meshes, std::string& error)
{
    EngineModelData model;
    if (!loadGlb(path, model, error))
    {
        return false;
    }
    meshes.insert(meshes.end(),
        std::make_move_iterator(model.meshes.begin()),
        std::make_move_iterator(model.meshes.end()));
    return true;
}
