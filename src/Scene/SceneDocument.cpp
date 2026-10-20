#include "SceneDocument.h"

#include <glib.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <vector>

namespace
{
struct SceneEntry
{
    std::string type;
    std::string name;
    std::string source;
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
};

struct ParseState
{
    std::vector<SceneEntry> entries;
    bool rootSeen = false;
};

const gchar* attributeValue(const gchar** names, const gchar** values, const char* wanted)
{
    for (std::size_t index = 0; names && names[index]; ++index)
    {
        if (g_str_equal(names[index], wanted)) return values[index];
    }
    return nullptr;
}

bool parseFloatAttribute(const gchar** names, const gchar** values,
    const char* name, float fallback, float& result, GError** error)
{
    const gchar* text = attributeValue(names, values, name);
    if (!text)
    {
        result = fallback;
        return true;
    }
    gchar* end = nullptr;
    const double parsed = g_ascii_strtod(text, &end);
    if (!end || *end != '\0' || !std::isfinite(parsed))
    {
        g_set_error(error, g_quark_from_static_string("puppy3d-scene"), 1,
            "Scene attribute '%s' is not a finite number.", name);
        return false;
    }
    result = static_cast<float>(parsed);
    return true;
}

void onStartElement(GMarkupParseContext*, const gchar* element,
    const gchar** names, const gchar** values, gpointer userData, GError** error)
{
    ParseState& state = *static_cast<ParseState*>(userData);
    if (g_str_equal(element, "scene"))
    {
        if (state.rootSeen || !g_str_equal(attributeValue(names, values, "version"), "1"))
        {
            g_set_error_literal(error, g_quark_from_static_string("puppy3d-scene"), 2,
                "Expected a version 1 scene document.");
            return;
        }
        state.rootSeen = true;
        return;
    }
    if (!g_str_equal(element, "object") || !state.rootSeen)
    {
        g_set_error(error, g_quark_from_static_string("puppy3d-scene"), 3,
            "Unsupported scene element '%s'.", element);
        return;
    }

    SceneEntry entry;
    const gchar* type = attributeValue(names, values, "type");
    const gchar* objectName = attributeValue(names, values, "name");
    if (!type || !objectName || (!g_str_equal(type, "cube") && !g_str_equal(type, "model")))
    {
        g_set_error_literal(error, g_quark_from_static_string("puppy3d-scene"), 4,
            "Each object requires a name and a supported type.");
        return;
    }
    entry.type = type;
    entry.name = objectName;
    if (entry.type == "model")
    {
        const gchar* source = attributeValue(names, values, "source");
        if (!source || !*source)
        {
            g_set_error_literal(error, g_quark_from_static_string("puppy3d-scene"), 5,
                "Model objects require a source GLB path.");
            return;
        }
        entry.source = source;
    }

    const char* vectorAttributes[] = {
        "x", "y", "z", "rx", "ry", "rz", "sx", "sy", "sz", "r", "g", "b"
    };
    float* destinations[] = {
        &entry.position[0], &entry.position[1], &entry.position[2],
        &entry.rotation[0], &entry.rotation[1], &entry.rotation[2],
        &entry.scale[0], &entry.scale[1], &entry.scale[2],
        &entry.color[0], &entry.color[1], &entry.color[2]
    };
    const float defaults[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 0.25f, 0.65f, 0.95f};
    for (std::size_t index = 0; index < 12; ++index)
    {
        if (!parseFloatAttribute(names, values, vectorAttributes[index], defaults[index],
            *destinations[index], error))
        {
            return;
        }
    }
    if (entry.type == "cube")
    {
        for (std::size_t vertex = 0; vertex < entry.vertices.size(); ++vertex)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const std::string attribute = "v" + std::to_string(vertex) +
                    (axis == 0 ? "x" : axis == 1 ? "y" : "z");
                if (!parseFloatAttribute(names, values, attribute.c_str(),
                    entry.vertices[vertex][axis], entry.vertices[vertex][axis], error))
                {
                    return;
                }
            }
        }
    }
    state.entries.push_back(std::move(entry));
}

std::string escaped(const std::string& value)
{
    gchar* result = g_markup_escape_text(value.c_str(), static_cast<gssize>(value.size()));
    std::string output = result ? result : "";
    g_free(result);
    std::size_t quote = 0;
    while ((quote = output.find('"', quote)) != std::string::npos)
    {
        output.replace(quote, 1, "&quot;");
        quote += 6;
    }
    return output;
}

void writeNumber(std::ostream& output, float value)
{
    output << std::setprecision(9) << value;
}
}

bool saveSceneXml(const EngineScene& scene,
    const std::filesystem::path& path, std::string& error)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Could not open '" + path.string() + "' for writing.";
        return false;
    }
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<scene version=\"1\">\n";
    const std::filesystem::path sceneDirectory = std::filesystem::absolute(path).parent_path();
    for (std::size_t index = 0; index < scene.objectCount(); ++index)
    {
        const int objectId = static_cast<int>(index + 1);
        const EngineObject* object = scene.object(objectId);
        if (!object || object->isGeneratedGeometry) continue;
        std::string source;
        if (object->isModel)
        {
            std::error_code relativeError;
            const auto relative = std::filesystem::relative(
                std::filesystem::absolute(object->sourcePath), sceneDirectory, relativeError);
            source = (relativeError ? std::filesystem::path(object->sourcePath) : relative)
                .generic_string();
        }
        output << "  <object type=\"" << (object->isModel ? "model" : "cube")
            << "\" name=\"" << escaped(object->name) << "\"";
        if (object->isModel)
        {
            output << " source=\"" << escaped(source) << "\"";
        }
        output << " x=\""; writeNumber(output, object->position[0]);
        output << "\" y=\""; writeNumber(output, object->position[1]);
        output << "\" z=\""; writeNumber(output, object->position[2]);
        output << "\" rx=\""; writeNumber(output, object->rotation[0]);
        output << "\" ry=\""; writeNumber(output, object->rotation[1]);
        output << "\" rz=\""; writeNumber(output, object->rotation[2]);
        output << "\" sx=\""; writeNumber(output, object->scale[0]);
        output << "\" sy=\""; writeNumber(output, object->scale[1]);
        output << "\" sz=\""; writeNumber(output, object->scale[2]);
        output << "\" r=\""; writeNumber(output, object->color[0]);
        output << "\" g=\""; writeNumber(output, object->color[1]);
        output << "\" b=\""; writeNumber(output, object->color[2]);
        if (!object->isModel)
        {
            for (std::size_t vertex = 0; vertex < object->vertices.size(); ++vertex)
            {
                output << "\" v" << vertex << "x=\""; writeNumber(output, object->vertices[vertex][0]);
                output << "\" v" << vertex << "y=\""; writeNumber(output, object->vertices[vertex][1]);
                output << "\" v" << vertex << "z=\""; writeNumber(output, object->vertices[vertex][2]);
            }
        }
        output << "\" />\n";
    }
    output << "</scene>\n";
    if (!output)
    {
        error = "An error occurred while writing '" + path.string() + "'.";
        return false;
    }
    error.clear();
    return true;
}

bool loadSceneXml(EngineScene& scene,
    const std::filesystem::path& path, std::string& error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Could not open '" + path.string() + "' for reading.";
        return false;
    }
    const std::string document((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (document.size() > 8 * 1024 * 1024)
    {
        error = "Scene XML exceeds the 8 MiB limit.";
        return false;
    }

    ParseState state;
    GMarkupParser parser{};
    parser.start_element = onStartElement;
    GMarkupParseContext* context = g_markup_parse_context_new(&parser,
        G_MARKUP_TREAT_CDATA_AS_TEXT, &state, nullptr);
    GError* parseError = nullptr;
    const gboolean parsed = g_markup_parse_context_parse(context, document.data(),
        static_cast<gssize>(document.size()), &parseError) &&
        g_markup_parse_context_end_parse(context, &parseError);
    g_markup_parse_context_free(context);
    if (!parsed || !state.rootSeen)
    {
        error = parseError ? parseError->message : "The file is not a Puppy3D scene document.";
        if (parseError) g_error_free(parseError);
        return false;
    }
    if (parseError) g_error_free(parseError);

    const std::filesystem::path sceneDirectory = std::filesystem::absolute(path).parent_path();
    for (SceneEntry& entry : state.entries)
    {
        if (entry.type == "model")
        {
            std::filesystem::path source(entry.source);
            if (source.is_relative()) source = sceneDirectory / source;
            std::error_code sourceError;
            if (!std::filesystem::is_regular_file(source, sourceError) || sourceError)
            {
                error = "Model source does not exist: " + source.string();
                return false;
            }
            entry.source = source.string();
        }
    }

    EngineScene validation;
    validation.clear();
    for (const SceneEntry& entry : state.entries)
    {
        if (entry.type == "model" && !validation.importGlb(entry.source, error))
        {
            error = "Could not load model '" + entry.source + "': " + error;
            return false;
        }
    }

    scene.clear();
    for (const SceneEntry& entry : state.entries)
    {
        int objectId = 0;
        if (entry.type == "model")
        {
            if (!scene.importGlb(entry.source, error)) return false;
            objectId = static_cast<int>(scene.objectCount());
        }
        else
        {
            objectId = scene.addCube(entry.name, entry.position[0],
                entry.position[1], entry.position[2]);
        }
        scene.setObjectName(objectId, entry.name);
        scene.setPosition(objectId, entry.position[0], entry.position[1], entry.position[2]);
        scene.setRotation(objectId, entry.rotation[0], entry.rotation[1], entry.rotation[2]);
        scene.setScale(objectId, entry.scale[0], entry.scale[1], entry.scale[2]);
        scene.setColor(objectId, entry.color[0], entry.color[1], entry.color[2]);
        if (entry.type == "cube")
        {
            for (std::size_t vertex = 0; vertex < entry.vertices.size(); ++vertex)
            {
                scene.setCubeVertex(objectId, vertex, entry.vertices[vertex][0],
                    entry.vertices[vertex][1], entry.vertices[vertex][2]);
            }
        }
    }
    error.clear();
    return true;
}