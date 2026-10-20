#define SDL_MAIN_HANDLED
#include <epoxy/gl.h>
#include <gtk/gtk.h>
#include <SDL.h>
#include <SDL_mixer.h>

#include "Engine.h"
#include "InputService.h"
#include "LuaRuntime.h"
#include "SceneDocument.h"
#include "ViewportController.h"

#include "EngineVariables/EngineVariables.h"
#include "GeneralPuppy3dObjects/GeneralObjects.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr gsize maxEditableFileSize = 8 * 1024 * 1024;

enum TreeColumn
{
    TreeColumnName,
    TreeColumnPath,
    TreeColumnIsDirectory,
    TreeColumnCount
};

enum SceneColumn
{
    SceneColumnId,
    SceneColumnName,
    SceneColumnCount
};

struct Editor
{
    GtkWidget* window = nullptr;
    GtkWidget* treeView = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* glArea = nullptr;
    GtkWidget* textView = nullptr;
    GtkWidget* statusBar = nullptr;
    GtkWidget* nameValue = nullptr;
    GtkWidget* pathValue = nullptr;
    GtkWidget* typeValue = nullptr;
    GtkWidget* sizeValue = nullptr;
    GtkWidget* playButton = nullptr;
    GtkWidget* stopButton = nullptr;
    GtkWidget* runLuaButton = nullptr;
    GtkWidget* sceneTreeView = nullptr;
    GtkWidget* objectNameEntry = nullptr;
    GtkWidget* transformSpins[9] = {};
    GtkWidget* gizmoModeButtons[3] = {};
    GtkWidget* vertexEditToggle = nullptr;
    GtkWidget* vertexIndexCombo = nullptr;
    GtkWidget* vertexSpins[3] = {};
    GtkWidget* animationCombo = nullptr;
    GtkWidget* animationTimeline = nullptr;
    GtkWidget* animationPlayButton = nullptr;
    GtkTreeStore* treeStore = nullptr;
    GtkListStore* sceneListStore = nullptr;
    fs::path resourceDirectory;
    fs::path selectedResource;
    fs::path currentFile;
    fs::path sceneFile;
    Mix_Music* music = nullptr;
    EngineScene scene;
    EngineCamera camera;
    InputService input;
    std::unique_ptr<LuaRuntime> luaRuntime;
    guint statusContext = 0;
    guint tickSource = 0;
    gint64 lastTick = 0;
    bool audioAvailable = false;
    bool midiAvailable = false;
    bool mixerInitialized = false;
    bool openGlAvailable = false;
    bool loadingText = false;
    bool dirty = false;
    bool documentEditable = false;
    bool updatingSceneControls = false;
    bool animationPlaying = false;
    bool viewportFocused = false;
    bool rotatingCamera = false;
    bool vertexEditEnabled = false;
    double lastPointerX = 0.0;
    double lastPointerY = 0.0;
    int selectedVertexIndex = 0;
    int selectedSceneObjectId = 1;
};

void refreshSceneOutliner(Editor& editor, int selectedObjectId);
void refreshSceneInspector(Editor& editor);

void setStatus(Editor& editor, const std::string& message)
{
    gtk_statusbar_pop(GTK_STATUSBAR(editor.statusBar), editor.statusContext);
    gtk_statusbar_push(GTK_STATUSBAR(editor.statusBar), editor.statusContext, message.c_str());
}

bool isLuaFile(const fs::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(g_ascii_tolower(character)); });
    return extension == ".lua";
}

bool isGlbFile(const fs::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(g_ascii_tolower(character)); });
    return extension == ".glb";
}

bool isMidiFile(const fs::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(g_ascii_tolower(character)); });
    return extension == ".mid" || extension == ".midi";
}

void updateScriptControls(Editor& editor)
{
    if (editor.runLuaButton)
    {
        gtk_widget_set_sensitive(editor.runLuaButton,
            editor.luaRuntime && (isLuaFile(editor.currentFile) || isLuaFile(editor.selectedResource)));
    }
}

void showError(Editor& editor, const std::string& message)
{
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(editor.window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        "%s",
        message.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void setEditorText(Editor& editor, const std::string& text, bool editable)
{
    editor.loadingText = true;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor.textView));
    gtk_text_buffer_set_text(buffer, text.data(), static_cast<gint>(text.size()));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(editor.textView), editable);
    editor.documentEditable = editable;
    editor.loadingText = false;
    editor.dirty = false;
}

void updateProperties(Editor& editor)
{
    const fs::path& path = editor.selectedResource;
    if (path.empty())
    {
        gtk_label_set_text(GTK_LABEL(editor.nameValue), "—");
        gtk_label_set_text(GTK_LABEL(editor.pathValue), "—");
        gtk_label_set_text(GTK_LABEL(editor.typeValue), "—");
        gtk_label_set_text(GTK_LABEL(editor.sizeValue), "—");
        gtk_widget_set_sensitive(editor.playButton, FALSE);
        return;
    }

    std::error_code error;
    const bool isDirectory = fs::is_directory(path, error);
    gtk_label_set_text(GTK_LABEL(editor.nameValue), path.filename().string().c_str());
    gtk_label_set_text(GTK_LABEL(editor.pathValue), path.string().c_str());

    if (error)
    {
        gtk_label_set_text(GTK_LABEL(editor.typeValue), "Unavailable");
        gtk_label_set_text(GTK_LABEL(editor.sizeValue), "Unavailable");
        gtk_widget_set_sensitive(editor.playButton, FALSE);
        return;
    }

    if (isDirectory)
    {
        gtk_label_set_text(GTK_LABEL(editor.typeValue), "Folder");
        gtk_label_set_text(GTK_LABEL(editor.sizeValue), "—");
        gtk_widget_set_sensitive(editor.playButton, FALSE);
        return;
    }

    const std::string extension = path.extension().string();
    gtk_label_set_text(GTK_LABEL(editor.typeValue), extension.empty() ? "File" : extension.c_str());
    const auto size = fs::file_size(path, error);
    const std::string sizeText = error ? "Unavailable" : std::to_string(size) + " bytes";
    gtk_label_set_text(GTK_LABEL(editor.sizeValue), sizeText.c_str());

    std::string lowercaseExtension = extension;
    std::transform(lowercaseExtension.begin(), lowercaseExtension.end(), lowercaseExtension.begin(),
        [](unsigned char character) { return static_cast<char>(g_ascii_tolower(character)); });
    const bool isAudio = lowercaseExtension == ".wav" ||
                         lowercaseExtension == ".ogg" ||
                         lowercaseExtension == ".mp3" ||
                         lowercaseExtension == ".flac" ||
                         lowercaseExtension == ".opus" ||
                         isMidiFile(path);
    gtk_widget_set_sensitive(editor.playButton, editor.audioAvailable && isAudio &&
        (!isMidiFile(path) || editor.midiAvailable));
}

void appendTreeItems(Editor& editor, const fs::path& directory, GtkTreeIter* parent)
{
    std::error_code error;
    fs::directory_iterator iterator(directory, error);
    if (error)
    {
        g_warning("Unable to read resource directory '%s': %s",
            directory.string().c_str(), error.message().c_str());
        return;
    }

    std::vector<fs::directory_entry> entries;
    for (const auto& entry : iterator)
    {
        const std::string name = entry.path().filename().string();
        if (!name.empty() && name.front() != '.')
        {
            entries.push_back(entry);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right)
    {
        std::error_code leftError;
        std::error_code rightError;
        const bool leftDirectory = left.is_directory(leftError);
        const bool rightDirectory = right.is_directory(rightError);
        if (leftDirectory != rightDirectory)
        {
            return leftDirectory;
        }
        return left.path().filename().string() < right.path().filename().string();
    });

    for (const auto& entry : entries)
    {
        std::error_code typeError;
        const bool isDirectory = entry.is_directory(typeError);
        GtkTreeIter child;
        const std::string name = entry.path().filename().string();
        const std::string path = entry.path().string();
        gtk_tree_store_append(editor.treeStore, &child, parent);
        gtk_tree_store_set(editor.treeStore, &child,
            TreeColumnName, name.c_str(),
            TreeColumnPath, path.c_str(),
            TreeColumnIsDirectory, isDirectory,
            -1);
        if (!typeError && isDirectory)
        {
            appendTreeItems(editor, entry.path(), &child);
        }
    }
}

void refreshTree(Editor& editor)
{
    gtk_tree_store_clear(editor.treeStore);
    GtkTreeIter root;
    const std::string rootName = editor.resourceDirectory.filename().string();
    const std::string rootPath = editor.resourceDirectory.string();
    gtk_tree_store_append(editor.treeStore, &root, nullptr);
    gtk_tree_store_set(editor.treeStore, &root,
        TreeColumnName, rootName.c_str(),
        TreeColumnPath, rootPath.c_str(),
        TreeColumnIsDirectory, TRUE,
        -1);
    appendTreeItems(editor, editor.resourceDirectory, &root);
    GtkTreePath* rootTreePath = gtk_tree_path_new_first();
    gtk_tree_view_expand_row(GTK_TREE_VIEW(editor.treeView), rootTreePath, FALSE);
    gtk_tree_path_free(rootTreePath);
}

gboolean onOpenGlRender(GtkGLArea* area, GdkGLContext*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    GError* error = gtk_gl_area_get_error(area);
    if (error)
    {
        setStatus(editor, std::string("OpenGL error: ") + error->message);
        return FALSE;
    }
    if (!editor.openGlAvailable)
    {
        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return TRUE;
    }

    editor.scene.render(
        gtk_widget_get_allocated_width(GTK_WIDGET(area)),
        gtk_widget_get_allocated_height(GTK_WIDGET(area)), editor.camera,
        editor.selectedSceneObjectId,
        editor.vertexEditEnabled ? editor.selectedVertexIndex : -1);
    const GLenum renderError = glGetError();
    if (renderError != GL_NO_ERROR)
    {
        setStatus(editor, "OpenGL rendering error: " + std::to_string(renderError));
        g_warning("OpenGL scene rendering failed with error code %u.",
            static_cast<unsigned int>(renderError));
    }
    return TRUE;
}

void onOpenGlRealize(GtkGLArea* area, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    gtk_gl_area_make_current(area);
    GError* error = gtk_gl_area_get_error(area);
    if (error)
    {
        setStatus(editor, std::string("Unable to initialize OpenGL: ") + error->message);
        return;
    }
    std::string engineError;
    if (!editor.scene.initializeOpenGl(engineError))
    {
        setStatus(editor, engineError);
        g_warning("%s", engineError.c_str());
        return;
    }
    editor.openGlAvailable = true;

    const GLubyte* version = glGetString(GL_VERSION);
    if (!version)
    {
        setStatus(editor, "OpenGL context is active but did not report a version.");
        return;
    }
    setStatus(editor, "OpenGL ready: " + std::string(reinterpret_cast<const char*>(version)));
}

gboolean onEngineTick(gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    editor.input.poll();
    const gint64 now = g_get_monotonic_time();
    const float deltaTime = editor.lastTick == 0
        ? 0.0f
        : static_cast<float>(now - editor.lastTick) / 1000000.0f;
    editor.lastTick = now;

    if (editor.viewportFocused && deltaTime > 0.0f)
    {
        const float forwardInput = (editor.input.keyDown("w") ? 1.0f : 0.0f) -
            (editor.input.keyDown("s") ? 1.0f : 0.0f);
        const float rightInput = (editor.input.keyDown("d") ? 1.0f : 0.0f) -
            (editor.input.keyDown("a") ? 1.0f : 0.0f);
        const float verticalInput = (editor.input.keyDown("e") ? 1.0f : 0.0f) -
            (editor.input.keyDown("q") ? 1.0f : 0.0f);
        EditorControls::moveCamera(editor.camera, forwardInput, rightInput,
            verticalInput, deltaTime);
    }

    if (editor.luaRuntime)
    {
        std::string error;
        if (!editor.luaRuntime->update(deltaTime, error))
        {
            setStatus(editor, "Lua update error: " + error);
        }
    }
    editor.scene.updateAnimations(deltaTime);
    if (editor.selectedSceneObjectId > 0 && editor.animationTimeline &&
        editor.animationPlayButton &&
        g_str_equal(gtk_button_get_label(GTK_BUTTON(editor.animationPlayButton)), "Pause"))
    {
        editor.updatingSceneControls = true;
        gtk_range_set_value(GTK_RANGE(editor.animationTimeline),
            editor.scene.animationTime(editor.selectedSceneObjectId));
        editor.updatingSceneControls = false;
    }
    editor.input.finishFrame();
    if (editor.glArea)
    {
        gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
    }
    return G_SOURCE_CONTINUE;
}

bool saveToPath(Editor& editor, const fs::path& path)
{
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor.textView));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        g_free(text);
        showError(editor, "Could not open '" + path.string() + "' for writing.");
        return false;
    }
    output << text;
    g_free(text);
    if (!output)
    {
        showError(editor, "An error occurred while writing '" + path.string() + "'.");
        return false;
    }

    editor.currentFile = path;
    editor.dirty = false;
    gtk_window_set_title(GTK_WINDOW(editor.window),
        (path.filename().string() + " - Puppy3D").c_str());
    updateScriptControls(editor);
    setStatus(editor, "Saved " + path.string());
    return true;
}

bool saveAs(Editor& editor)
{
    if (!editor.documentEditable)
    {
        setStatus(editor, "The current view is read-only; open or create a text file to save edits.");
        return false;
    }

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Save File",
        GTK_WINDOW(editor.window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
        editor.currentFile.empty() ? "untitled.txt" : editor.currentFile.filename().string().c_str());
    if (!editor.currentFile.empty())
    {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), editor.currentFile.string().c_str());
    }

    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    char* filename = response == GTK_RESPONSE_ACCEPT
        ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog))
        : nullptr;
    gtk_widget_destroy(dialog);
    if (!filename)
    {
        return false;
    }

    const fs::path path(filename);
    g_free(filename);
    return saveToPath(editor, path);
}

bool saveCurrent(Editor& editor)
{
    if (!editor.documentEditable)
    {
        setStatus(editor, "The current view is read-only; open or create a text file to save edits.");
        return false;
    }
    if (editor.currentFile.empty())
    {
        return saveAs(editor);
    }
    return saveToPath(editor, editor.currentFile);
}

bool confirmDiscard(Editor& editor)
{
    if (!editor.dirty)
    {
        return true;
    }

    const std::string name = editor.currentFile.empty()
        ? "Untitled"
        : editor.currentFile.filename().string();
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(editor.window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE,
        "Save changes to %s before continuing?",
        name.c_str());
    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Discard", GTK_RESPONSE_REJECT,
        "_Save", GTK_RESPONSE_ACCEPT,
        nullptr);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_ACCEPT)
    {
        return saveCurrent(editor);
    }
    return response == GTK_RESPONSE_REJECT;
}

bool openFile(Editor& editor, const fs::path& path)
{
    if (!confirmDiscard(editor))
    {
        return false;
    }

    std::error_code error;
    if (!fs::is_regular_file(path, error) || error)
    {
        showError(editor, "The selected path is not a readable file: " + path.string());
        return false;
    }

    const auto fileSize = fs::file_size(path, error);
    if (error)
    {
        showError(editor, "Could not read file information for '" + path.string() +
            "': " + error.message());
        return false;
    }

    if (fileSize > maxEditableFileSize)
    {
        showError(editor, "This file is larger than the 8 MiB editor limit.");
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        showError(editor, "Could not open '" + path.string() + "' for reading.");
        return false;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (input.bad())
    {
        showError(editor, "An error occurred while reading '" + path.string() + "'.");
        return false;
    }

    editor.currentFile = path;
    gtk_window_set_title(GTK_WINDOW(editor.window),
        (path.filename().string() + " - Puppy3D").c_str());
    updateScriptControls(editor);

    if (contents.find('\0') != std::string::npos ||
        !g_utf8_validate(contents.data(), static_cast<gssize>(contents.size()), nullptr))
    {
        setEditorText(editor, "This asset is binary or is not valid UTF-8.\n"
            "Use the resource tree and properties panel to inspect it.", FALSE);
        setStatus(editor, "Selected asset is not editable text.");
    }
    else
    {
        setEditorText(editor, contents, TRUE);
        setStatus(editor, "Opened " + path.string());
    }
    return true;
}

void importGlb(Editor& editor, const fs::path& path)
{
    std::string error;
    if (!editor.scene.importGlb(path, error))
    {
        showError(editor, "Could not import GLB:\n" + error);
        setStatus(editor, "GLB import failed");
        return;
    }
    editor.selectedSceneObjectId = static_cast<int>(editor.scene.objectCount());
    refreshSceneOutliner(editor, editor.selectedSceneObjectId);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(editor.notebook), 1);
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
    setStatus(editor, "Imported " + path.filename().string() + " into the 3D scene.");
}

void refreshSceneInspector(Editor& editor)
{
    editor.updatingSceneControls = true;
    const EngineObject* object = editor.scene.object(editor.selectedSceneObjectId);
    const bool hasObject = object != nullptr;
    if (editor.objectNameEntry)
    {
        gtk_entry_set_text(GTK_ENTRY(editor.objectNameEntry), hasObject ? object->name.c_str() : "");
        gtk_widget_set_sensitive(editor.objectNameEntry, hasObject);
    }
    for (GtkWidget* spin : editor.transformSpins)
    {
        if (spin) gtk_widget_set_sensitive(spin, hasObject);
    }
    const bool canEditVertices = hasObject && !object->isModel;
    if (!canEditVertices)
    {
        editor.vertexEditEnabled = false;
        if (editor.vertexEditToggle)
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(editor.vertexEditToggle), FALSE);
        }
    }
    if (editor.vertexEditToggle)
    {
        gtk_widget_set_sensitive(editor.vertexEditToggle, canEditVertices);
        editor.vertexEditEnabled = canEditVertices &&
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor.vertexEditToggle));
    }
    if (editor.vertexIndexCombo)
    {
        gtk_widget_set_sensitive(editor.vertexIndexCombo,
            canEditVertices && editor.vertexEditEnabled);
        if (canEditVertices)
        {
            gtk_combo_box_set_active(GTK_COMBO_BOX(editor.vertexIndexCombo),
                editor.selectedVertexIndex);
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (editor.vertexSpins[axis])
        {
            gtk_widget_set_sensitive(editor.vertexSpins[axis],
                canEditVertices && editor.vertexEditEnabled);
            if (canEditVertices)
            {
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(editor.vertexSpins[axis]),
                    object->vertices[static_cast<std::size_t>(editor.selectedVertexIndex)][axis]);
            }
        }
    }
    if (hasObject)
    {
        const float* values[] = {object->position, object->rotation, object->scale};
        for (std::size_t group = 0; group < 3; ++group)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(editor.transformSpins[group * 3 + axis]),
                    values[group][axis]);
            }
        }
    }

    const std::size_t clipCount = hasObject
        ? editor.scene.animationCount(editor.selectedSceneObjectId) : 0;
    if (editor.animationCombo)
    {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(editor.animationCombo));
        for (std::size_t index = 0; index < clipCount; ++index)
        {
            const std::string name = editor.scene.animationName(editor.selectedSceneObjectId, index);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(editor.animationCombo), name.c_str());
        }
        if (clipCount != 0)
        {
            int active = editor.scene.activeAnimation(editor.selectedSceneObjectId);
            if (active < 0)
            {
                editor.scene.setAnimation(editor.selectedSceneObjectId, 0);
                active = 0;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(editor.animationCombo), active);
        }
        gtk_widget_set_sensitive(editor.animationCombo, clipCount != 0);
    }
    if (editor.animationTimeline)
    {
        const int active = hasObject ? editor.scene.activeAnimation(editor.selectedSceneObjectId) : -1;
        const float duration = active >= 0
            ? editor.scene.animationDuration(editor.selectedSceneObjectId,
                static_cast<std::size_t>(active))
            : 0.0f;
        gtk_range_set_range(GTK_RANGE(editor.animationTimeline), 0.0,
            std::max(static_cast<double>(duration), 0.001));
        gtk_range_set_value(GTK_RANGE(editor.animationTimeline),
            hasObject ? editor.scene.animationTime(editor.selectedSceneObjectId) : 0.0);
        gtk_widget_set_sensitive(editor.animationTimeline, clipCount != 0);
    }
    if (editor.animationPlayButton)
    {
        gtk_button_set_label(GTK_BUTTON(editor.animationPlayButton),
            editor.animationPlaying && clipCount != 0 ? "Pause" : "Play");
        gtk_widget_set_sensitive(editor.animationPlayButton, clipCount != 0);
    }
    editor.updatingSceneControls = false;
}

void refreshSceneOutliner(Editor& editor, int selectedObjectId)
{
    if (!editor.sceneListStore) return;
    if (editor.animationPlaying && editor.selectedSceneObjectId > 0 &&
        editor.selectedSceneObjectId != selectedObjectId)
    {
        editor.scene.setAnimationPlaying(editor.selectedSceneObjectId, false);
        editor.animationPlaying = false;
    }
    editor.updatingSceneControls = true;
    gtk_list_store_clear(editor.sceneListStore);
    GtkTreeIter selectedIter;
    bool hasSelection = false;
    for (std::size_t index = 0; index < editor.scene.objectCount(); ++index)
    {
        const int objectId = static_cast<int>(index + 1);
        const EngineObject* object = editor.scene.object(objectId);
        if (!object) continue;
        GtkTreeIter iter;
        gtk_list_store_append(editor.sceneListStore, &iter);
        const std::string label = object->name + (object->isModel ? "  [GLB]" : "  [Cube]");
        gtk_list_store_set(editor.sceneListStore, &iter,
            SceneColumnId, objectId,
            SceneColumnName, label.c_str(),
            -1);
        if (objectId == selectedObjectId)
        {
            selectedIter = iter;
            hasSelection = true;
        }
    }
    editor.selectedSceneObjectId = hasSelection ? selectedObjectId : 0;
    if (hasSelection)
    {
        gtk_tree_selection_select_iter(
            gtk_tree_view_get_selection(GTK_TREE_VIEW(editor.sceneTreeView)), &selectedIter);
    }
    editor.updatingSceneControls = false;
    refreshSceneInspector(editor);
}

void onSceneSelectionChanged(GtkTreeSelection* selection, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.updatingSceneControls) return;
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    int objectId = 0;
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
    {
        gtk_tree_model_get(model, &iter, SceneColumnId, &objectId, -1);
    }
    if (editor.animationPlaying && editor.selectedSceneObjectId > 0)
    {
        editor.scene.setAnimationPlaying(editor.selectedSceneObjectId, false);
    }
    editor.animationPlaying = false;
    editor.selectedSceneObjectId = objectId;
    refreshSceneInspector(editor);
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onSceneTransformChanged(GtkSpinButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.updatingSceneControls || editor.selectedSceneObjectId < 1) return;
    const float position[3] = {
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[0]))),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[1]))),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[2])))
    };
    const float rotation[3] = {
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[3]))),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[4]))),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[5])))
    };
    const float scale[3] = {
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[6]))),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[7]))),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.transformSpins[8])))
    };
    editor.scene.setPosition(editor.selectedSceneObjectId, position[0], position[1], position[2]);
    editor.scene.setRotation(editor.selectedSceneObjectId, rotation[0], rotation[1], rotation[2]);
    editor.scene.setScale(editor.selectedSceneObjectId, scale[0], scale[1], scale[2]);
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onGizmoStepClicked(GtkButton* button, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.selectedSceneObjectId < 1) return;
    const int axis = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "axis"));
    const float direction = static_cast<float>(GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "direction")));
    if (axis < 0 || axis > 2) return;

    float position[3];
    float rotation[3];
    float scale[3];
    for (int index = 0; index < 3; ++index)
    {
        position[index] = static_cast<float>(gtk_spin_button_get_value(
            GTK_SPIN_BUTTON(editor.transformSpins[index])));
        rotation[index] = static_cast<float>(gtk_spin_button_get_value(
            GTK_SPIN_BUTTON(editor.transformSpins[index + 3])));
        scale[index] = static_cast<float>(gtk_spin_button_get_value(
            GTK_SPIN_BUTTON(editor.transformSpins[index + 6])));
    }

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor.gizmoModeButtons[1])))
    {
        rotation[axis] += direction * 5.0f;
        editor.scene.setRotation(editor.selectedSceneObjectId,
            rotation[0], rotation[1], rotation[2]);
    }
    else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor.gizmoModeButtons[2])))
    {
        scale[axis] = std::max(0.01f, scale[axis] + direction * 0.1f);
        editor.scene.setScale(editor.selectedSceneObjectId, scale[0], scale[1], scale[2]);
    }
    else
    {
        position[axis] += direction * 0.25f;
        editor.scene.setPosition(editor.selectedSceneObjectId,
            position[0], position[1], position[2]);
    }
    refreshSceneInspector(editor);
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onVertexEditToggled(GtkToggleButton* toggle, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.updatingSceneControls) return;
    editor.vertexEditEnabled = gtk_toggle_button_get_active(toggle);
    refreshSceneInspector(editor);
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onVertexIndexChanged(GtkComboBox* combo, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.updatingSceneControls) return;
    const int index = gtk_combo_box_get_active(combo);
    if (index < 0 || index >= 8) return;
    editor.selectedVertexIndex = index;
    refreshSceneInspector(editor);
}

void onVertexChanged(GtkSpinButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.updatingSceneControls || !editor.vertexEditEnabled ||
        editor.selectedSceneObjectId < 1) return;
    editor.scene.setCubeVertex(editor.selectedSceneObjectId,
        static_cast<std::size_t>(editor.selectedVertexIndex),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.vertexSpins[0]))),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.vertexSpins[1]))),
        static_cast<float>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(editor.vertexSpins[2]))));
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onSceneNameChanged(GtkEditable* editable, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.updatingSceneControls || editor.selectedSceneObjectId < 1) return;
    const char* name = gtk_entry_get_text(GTK_ENTRY(editable));
    editor.scene.setObjectName(editor.selectedSceneObjectId, name);
    GtkTreeModel* model = GTK_TREE_MODEL(editor.sceneListStore);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid)
    {
        int objectId = 0;
        gtk_tree_model_get(model, &iter, SceneColumnId, &objectId, -1);
        if (objectId == editor.selectedSceneObjectId)
        {
            const EngineObject* object = editor.scene.object(objectId);
            const std::string label = object
                ? object->name + (object->isModel ? "  [GLB]" : "  [Cube]")
                : std::string(name);
            gtk_list_store_set(editor.sceneListStore, &iter,
                SceneColumnName, label.c_str(), -1);
            break;
        }
        valid = gtk_tree_model_iter_next(model, &iter);
    }
}

void onAnimationChanged(GtkComboBox* combo, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.updatingSceneControls || editor.selectedSceneObjectId < 1) return;
    const int animationIndex = gtk_combo_box_get_active(combo);
    if (animationIndex < 0 || !editor.scene.setAnimation(editor.selectedSceneObjectId, animationIndex))
    {
        return;
    }
    editor.animationPlaying = false;
    editor.scene.setAnimationPlaying(editor.selectedSceneObjectId, false);
    refreshSceneInspector(editor);
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onAnimationPlayClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.selectedSceneObjectId < 1 ||
        editor.scene.activeAnimation(editor.selectedSceneObjectId) < 0)
    {
        return;
    }
    editor.animationPlaying = !editor.animationPlaying;
    editor.scene.setAnimationPlaying(editor.selectedSceneObjectId, editor.animationPlaying);
    gtk_button_set_label(GTK_BUTTON(editor.animationPlayButton),
        editor.animationPlaying ? "Pause" : "Play");
}

void onAnimationTimelineChanged(GtkRange* range, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.updatingSceneControls || editor.selectedSceneObjectId < 1) return;
    editor.scene.setAnimationTime(editor.selectedSceneObjectId,
        static_cast<float>(gtk_range_get_value(range)));
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onAddCubeClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.animationPlaying && editor.selectedSceneObjectId > 0)
    {
        editor.scene.setAnimationPlaying(editor.selectedSceneObjectId, false);
        editor.animationPlaying = false;
    }
    editor.selectedSceneObjectId = editor.scene.addCube("Cube", 0.0f, 0.0f, 0.0f);
    refreshSceneOutliner(editor, editor.selectedSceneObjectId);
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onSaveSceneClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.sceneFile.empty())
    {
        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Save Scene", GTK_WINDOW(editor.window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "scene.xml");
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Puppy3D scenes (*.xml)");
        gtk_file_filter_add_pattern(filter, "*.xml");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        char* filename = response == GTK_RESPONSE_ACCEPT
            ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)) : nullptr;
        gtk_widget_destroy(dialog);
        if (!filename) return;
        editor.sceneFile = fs::path(filename);
        g_free(filename);
    }
    std::string error;
    if (!saveSceneXml(editor.scene, editor.sceneFile, error))
    {
        showError(editor, "Could not save scene:\n" + error);
        setStatus(editor, "Scene save failed");
        return;
    }
    setStatus(editor, "Saved scene " + editor.sceneFile.string());
}

void onOpenSceneClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Open Scene", GTK_WINDOW(editor.window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Puppy3D scenes (*.xml)");
    gtk_file_filter_add_pattern(filter, "*.xml");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    char* filename = response == GTK_RESPONSE_ACCEPT
        ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)) : nullptr;
    gtk_widget_destroy(dialog);
    if (!filename) return;

    const fs::path path(filename);
    g_free(filename);
    std::string error;
    if (!loadSceneXml(editor.scene, path, error))
    {
        showError(editor, "Could not open scene:\n" + error);
        setStatus(editor, "Scene load failed");
        return;
    }
    editor.sceneFile = path;
    editor.animationPlaying = false;
    editor.selectedSceneObjectId = editor.scene.objectCount() == 0 ? 0 : 1;
    refreshSceneOutliner(editor, editor.selectedSceneObjectId);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(editor.notebook), 1);
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
    setStatus(editor, "Loaded scene " + editor.sceneFile.string());
}

void onTextChanged(GtkTextBuffer*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (!editor.loadingText && gtk_text_view_get_editable(GTK_TEXT_VIEW(editor.textView)))
    {
        editor.dirty = true;
        const std::string title = (editor.currentFile.empty() ? "Untitled" :
            editor.currentFile.filename().string()) + " * - Puppy3D";
        gtk_window_set_title(GTK_WINDOW(editor.window), title.c_str());
        setStatus(editor, "Unsaved changes");
    }
}

void onTreeSelectionChanged(GtkTreeSelection* selection, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    GtkTreeModel* model = nullptr;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
    {
        editor.selectedResource.clear();
        updateProperties(editor);
        return;
    }

    gchar* path = nullptr;
    gtk_tree_model_get(model, &iter, TreeColumnPath, &path, -1);
    editor.selectedResource = path ? fs::path(path) : fs::path();
    g_free(path);
    updateProperties(editor);
    updateScriptControls(editor);
}

void onTreeRowActivated(GtkTreeView* treeView, GtkTreePath* treePath,
    GtkTreeViewColumn*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    GtkTreeModel* model = gtk_tree_view_get_model(treeView);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, treePath))
    {
        return;
    }

    gboolean isDirectory = FALSE;
    gchar* path = nullptr;
    gtk_tree_model_get(model, &iter,
        TreeColumnPath, &path,
        TreeColumnIsDirectory, &isDirectory,
        -1);
    if (path && isDirectory)
    {
        if (gtk_tree_view_row_expanded(treeView, treePath))
        {
            gtk_tree_view_collapse_row(treeView, treePath);
        }
        else
        {
            gtk_tree_view_expand_row(treeView, treePath, FALSE);
        }
    }
    else if (path)
    {
        const fs::path selected(path);
        if (isGlbFile(selected))
        {
            importGlb(editor, selected);
        }
        else
        {
            openFile(editor, selected);
        }
    }
    g_free(path);
}

void onOpenClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Open File",
        GTK_WINDOW(editor.window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        nullptr);
    if (!editor.currentFile.empty())
    {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), editor.currentFile.string().c_str());
    }
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    char* filename = response == GTK_RESPONSE_ACCEPT
        ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog))
        : nullptr;
    gtk_widget_destroy(dialog);
    if (filename)
    {
        openFile(editor, fs::path(filename));
        g_free(filename);
    }
}

void onImportGlbClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Import glTF Binary Model",
        GTK_WINDOW(editor.window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Import", GTK_RESPONSE_ACCEPT,
        nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "glTF binary models (*.glb)");
    gtk_file_filter_add_pattern(filter, "*.glb");
    gtk_file_filter_add_pattern(filter, "*.GLB");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    char* filename = response == GTK_RESPONSE_ACCEPT
        ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog))
        : nullptr;
    gtk_widget_destroy(dialog);
    if (filename)
    {
        importGlb(editor, fs::path(filename));
        g_free(filename);
    }
}

void onSaveClicked(GtkButton*, gpointer userData)
{
    saveCurrent(*static_cast<Editor*>(userData));
}

void onSaveAsClicked(GtkButton*, gpointer userData)
{
    saveAs(*static_cast<Editor*>(userData));
}

void onNewClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (!confirmDiscard(editor))
    {
        return;
    }
    editor.currentFile.clear();
    setEditorText(editor, "", TRUE);
    gtk_window_set_title(GTK_WINDOW(editor.window), "Untitled - Puppy3D");
    updateScriptControls(editor);
    setStatus(editor, "New text file");
}

void onRunLuaClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (!editor.luaRuntime)
    {
        showError(editor, "The Lua runtime is unavailable.");
        return;
    }

    fs::path scriptPath = isLuaFile(editor.currentFile)
        ? editor.currentFile
        : editor.selectedResource;
    if (!isLuaFile(scriptPath))
    {
        showError(editor, "Select or open a .lua script before running it.");
        return;
    }

    if (editor.dirty && !saveCurrent(editor))
    {
        return;
    }
    if (editor.currentFile != scriptPath && !openFile(editor, scriptPath))
    {
        return;
    }

    std::string error;
    if (!editor.luaRuntime->runFile(scriptPath.string(), error))
    {
        setStatus(editor, "Lua error: " + error);
        showError(editor, "Could not run Lua script:\n" + error);
        return;
    }

    gtk_notebook_set_current_page(GTK_NOTEBOOK(editor.notebook), 1);
    setStatus(editor, "Running " + scriptPath.filename().string());
    gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
}

void onPlayClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (!editor.audioAvailable || editor.selectedResource.empty())
    {
        return;
    }
    if (isMidiFile(editor.selectedResource) && !editor.midiAvailable)
    {
        setStatus(editor, "MIDI playback unavailable: FluidSynth or res/sf2/gm.sf2 is missing.");
        return;
    }

    if (editor.music)
    {
        Mix_FreeMusic(editor.music);
        editor.music = nullptr;
    }
    editor.music = Mix_LoadMUS(editor.selectedResource.string().c_str());
    if (!editor.music)
    {
        setStatus(editor, std::string("Audio error: ") + Mix_GetError());
        g_warning("Unable to load audio file '%s': %s",
            editor.selectedResource.string().c_str(), Mix_GetError());
        return;
    }
    if (Mix_PlayMusic(editor.music, 0) == -1)
    {
        setStatus(editor, std::string("Audio error: ") + Mix_GetError());
        g_warning("Unable to play audio file '%s': %s",
            editor.selectedResource.string().c_str(), Mix_GetError());
        Mix_FreeMusic(editor.music);
        editor.music = nullptr;
        return;
    }
    setStatus(editor, "Playing " + editor.selectedResource.filename().string());
}

void onStopClicked(GtkButton*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    Mix_HaltMusic();
    setStatus(editor, "Audio stopped");
}

gboolean onDeleteWindow(GtkWidget*, GdkEvent*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (!confirmDiscard(editor))
    {
        return TRUE;
    }
    if (editor.glArea && gtk_widget_get_realized(editor.glArea))
    {
        gtk_gl_area_make_current(GTK_GL_AREA(editor.glArea));
        if (editor.openGlAvailable && !gtk_gl_area_get_error(GTK_GL_AREA(editor.glArea)))
        {
            editor.scene.releaseOpenGlResources();
        }
    }
    return FALSE;
}

void onDestroyWindow(GtkWidget*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    if (editor.tickSource != 0)
    {
        g_source_remove(editor.tickSource);
        editor.tickSource = 0;
    }
    if (editor.music)
    {
        Mix_HaltMusic();
        Mix_FreeMusic(editor.music);
        editor.music = nullptr;
    }
    if (editor.audioAvailable)
    {
        Mix_CloseAudio();
    }
    if (editor.mixerInitialized)
    {
        Mix_Quit();
    }
    editor.input.shutdown();
    SDL_Quit();
    gtk_main_quit();
}

gboolean onKeyPress(GtkWidget*, GdkEventKey* event, gpointer userData)
{
    static_cast<Editor*>(userData)->input.handleKey(event->keyval, true);
    return FALSE;
}

gboolean onKeyRelease(GtkWidget*, GdkEventKey* event, gpointer userData)
{
    static_cast<Editor*>(userData)->input.handleKey(event->keyval, false);
    return FALSE;
}

gboolean onMouseMotion(GtkWidget*, GdkEventMotion* event, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    editor.input.handleMouseMotion(event->x, event->y);
    if (editor.rotatingCamera)
    {
        EditorControls::orbitCamera(editor.camera, event->x - editor.lastPointerX,
            event->y - editor.lastPointerY);
        editor.lastPointerX = event->x;
        editor.lastPointerY = event->y;
        gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
    }
    return FALSE;
}

gboolean onMouseButton(GtkWidget*, GdkEventButton* event, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    editor.input.handleMouseMotion(event->x, event->y);
    editor.input.handleMouseButton(event->button, event->type == GDK_BUTTON_PRESS);
    if (event->type == GDK_BUTTON_PRESS)
    {
        editor.viewportFocused = true;
        gtk_widget_grab_focus(editor.glArea);
        if (event->button == 3)
        {
            editor.rotatingCamera = true;
            editor.lastPointerX = event->x;
            editor.lastPointerY = event->y;
        }
        else if (event->button == 1)
        {
            const int selected = editor.scene.pickObject(event->x, event->y,
                gtk_widget_get_allocated_width(editor.glArea),
                gtk_widget_get_allocated_height(editor.glArea), editor.camera);
            refreshSceneOutliner(editor, selected);
            gtk_gl_area_queue_render(GTK_GL_AREA(editor.glArea));
        }
    }
    else if (event->type == GDK_BUTTON_RELEASE && event->button == 3)
    {
        editor.rotatingCamera = false;
    }
    return FALSE;
}

gboolean onFocusOut(GtkWidget*, GdkEventFocus*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    editor.input.clearKeyboard();
    editor.input.clearMouse();
    editor.rotatingCamera = false;
    editor.viewportFocused = false;
    return FALSE;
}

gboolean onViewportFocusOut(GtkWidget*, GdkEventFocus*, gpointer userData)
{
    Editor& editor = *static_cast<Editor*>(userData);
    editor.viewportFocused = false;
    editor.rotatingCamera = false;
    return FALSE;
}

fs::path findResourceDirectory(const char* executable)
{
    std::error_code error;
    const fs::path executablePath = fs::absolute(executable, error);
    const fs::path executableDirectory = error ? fs::current_path() : executablePath.parent_path();
    const std::vector<fs::path> candidates = {
        fs::current_path() / "res",
        fs::current_path() / ".." / "res",
        executableDirectory / "res",
        executableDirectory / ".." / "res"
    };

    for (const auto& candidate : candidates)
    {
        error.clear();
        if (fs::is_directory(candidate, error) && !error)
        {
            return fs::weakly_canonical(candidate, error);
        }
    }
    return {};
}

GtkWidget* makePropertyRow(GtkWidget* grid, const char* title,
    GtkWidget** value, int row)
{
    GtkWidget* key = gtk_label_new(title);
    gtk_widget_set_halign(key, GTK_ALIGN_START);
    *value = gtk_label_new("—");
    gtk_widget_set_halign(*value, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(*value), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(*value), TRUE);
    gtk_grid_attach(GTK_GRID(grid), key, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), *value, 1, row, 1, 1);
    return *value;
}

GtkWidget* makeToolButton(const char* label, GCallback callback, Editor& editor)
{
    GtkWidget* button = gtk_button_new_with_label(label);
    g_signal_connect(button, "clicked", callback, &editor);
    return button;
}

void buildWindow(Editor& editor)
{
    editor.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(editor.window), "Puppy3D Editor");
    gtk_window_set_default_size(GTK_WINDOW(editor.window), 1200, 760);
    gtk_window_set_position(GTK_WINDOW(editor.window), GTK_WIN_POS_CENTER);
    g_signal_connect(editor.window, "delete-event", G_CALLBACK(onDeleteWindow), &editor);
    g_signal_connect(editor.window, "destroy", G_CALLBACK(onDestroyWindow), &editor);
    g_signal_connect(editor.window, "key-press-event", G_CALLBACK(onKeyPress), &editor);
    g_signal_connect(editor.window, "key-release-event", G_CALLBACK(onKeyRelease), &editor);
    g_signal_connect(editor.window, "focus-out-event", G_CALLBACK(onFocusOut), &editor);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(editor.window), outer);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 6);
    gtk_box_pack_start(GTK_BOX(outer), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), makeToolButton("New", G_CALLBACK(onNewClicked), editor),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), makeToolButton("Open", G_CALLBACK(onOpenClicked), editor),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), makeToolButton("Save", G_CALLBACK(onSaveClicked), editor),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), makeToolButton("Save As", G_CALLBACK(onSaveAsClicked), editor),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar),
        makeToolButton("Add Cube", G_CALLBACK(onAddCubeClicked), editor),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar),
        makeToolButton("Open Scene", G_CALLBACK(onOpenSceneClicked), editor),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar),
        makeToolButton("Save Scene", G_CALLBACK(onSaveSceneClicked), editor),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar),
        makeToolButton("Import GLB", G_CALLBACK(onImportGlbClicked), editor),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL),
        FALSE, FALSE, 3);
    editor.playButton = makeToolButton("Play Audio", G_CALLBACK(onPlayClicked), editor);
    editor.stopButton = makeToolButton("Stop Audio", G_CALLBACK(onStopClicked), editor);
    gtk_box_pack_start(GTK_BOX(toolbar), editor.playButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), editor.stopButton, FALSE, FALSE, 0);
    editor.runLuaButton = makeToolButton("Run Lua", G_CALLBACK(onRunLuaClicked), editor);
    gtk_box_pack_start(GTK_BOX(toolbar), editor.runLuaButton, FALSE, FALSE, 0);
    gtk_widget_set_sensitive(editor.playButton, FALSE);
    gtk_widget_set_sensitive(editor.stopButton, editor.audioAvailable);

    GtkWidget* mainPane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(outer), mainPane, TRUE, TRUE, 0);

    GtkWidget* resourcesFrame = gtk_frame_new("Resources");
    gtk_widget_set_size_request(resourcesFrame, 220, -1);
    GtkWidget* resourcesDock = gtk_handle_box_new();
    gtk_handle_box_set_handle_position(GTK_HANDLE_BOX(resourcesDock), GTK_POS_LEFT);
    gtk_container_add(GTK_CONTAINER(resourcesDock), resourcesFrame);
    gtk_paned_pack1(GTK_PANED(mainPane), resourcesDock, FALSE, FALSE);
    GtkWidget* resourcesScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(resourcesScroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(resourcesFrame), resourcesScroll);

    editor.treeStore = gtk_tree_store_new(TreeColumnCount,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    editor.treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(editor.treeStore));
    g_object_unref(editor.treeStore);
    GtkWidget* tree = editor.treeView;
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
        "Project files", renderer, "text", TreeColumnName, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    g_signal_connect(selection, "changed", G_CALLBACK(onTreeSelectionChanged), &editor);
    g_signal_connect(tree, "row-activated", G_CALLBACK(onTreeRowActivated), &editor);
    gtk_container_add(GTK_CONTAINER(resourcesScroll), tree);

    GtkWidget* workspacePane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack2(GTK_PANED(mainPane), workspacePane, TRUE, FALSE);
    GtkWidget* rightDockPane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_size_request(rightDockPane, 300, -1);
    gtk_paned_pack2(GTK_PANED(workspacePane), rightDockPane, FALSE, FALSE);

    editor.notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(editor.notebook), GTK_POS_TOP);
    gtk_paned_pack1(GTK_PANED(workspacePane), editor.notebook, TRUE, FALSE);

    GtkWidget* editorScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(editorScroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    editor.textView = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(editor.textView), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor.textView), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(editor.textView), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(editor.textView), 8);
    gtk_container_add(GTK_CONTAINER(editorScroll), editor.textView);
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor.textView)),
        "changed", G_CALLBACK(onTextChanged), &editor);
    gtk_notebook_append_page(GTK_NOTEBOOK(editor.notebook), editorScroll,
        gtk_label_new("File Editor"));

    editor.glArea = gtk_gl_area_new();
    gtk_gl_area_set_required_version(GTK_GL_AREA(editor.glArea), 3, 2);
    gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(editor.glArea), TRUE);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(editor.glArea), TRUE);
    gtk_widget_add_events(editor.glArea,
        GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_widget_set_hexpand(editor.glArea, TRUE);
    gtk_widget_set_vexpand(editor.glArea, TRUE);
    gtk_widget_set_can_focus(editor.glArea, TRUE);
    g_signal_connect(editor.glArea, "motion-notify-event", G_CALLBACK(onMouseMotion), &editor);
    g_signal_connect(editor.glArea, "button-press-event", G_CALLBACK(onMouseButton), &editor);
    g_signal_connect(editor.glArea, "button-release-event", G_CALLBACK(onMouseButton), &editor);
    g_signal_connect(editor.glArea, "focus-out-event", G_CALLBACK(onViewportFocusOut), &editor);
    g_signal_connect(editor.glArea, "realize", G_CALLBACK(onOpenGlRealize), &editor);
    g_signal_connect(editor.glArea, "render", G_CALLBACK(onOpenGlRender), &editor);
    gtk_notebook_append_page(GTK_NOTEBOOK(editor.notebook), editor.glArea,
        gtk_label_new("3D View"));

    GtkWidget* objectExplorerFrame = gtk_frame_new("Object Explorer");
    gtk_widget_set_size_request(objectExplorerFrame, 280, 170);
    GtkWidget* objectExplorerDock = gtk_handle_box_new();
    gtk_handle_box_set_handle_position(GTK_HANDLE_BOX(objectExplorerDock), GTK_POS_LEFT);
    gtk_container_add(GTK_CONTAINER(objectExplorerDock), objectExplorerFrame);
    gtk_paned_pack1(GTK_PANED(rightDockPane), objectExplorerDock, FALSE, FALSE);
    GtkWidget* sceneScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sceneScroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    editor.sceneListStore = gtk_list_store_new(SceneColumnCount, G_TYPE_INT, G_TYPE_STRING);
    editor.sceneTreeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(editor.sceneListStore));
    g_object_unref(editor.sceneListStore);
    GtkCellRenderer* sceneRenderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* sceneColumn = gtk_tree_view_column_new_with_attributes(
        "Object", sceneRenderer, "text", SceneColumnName, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(editor.sceneTreeView), sceneColumn);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(editor.sceneTreeView), FALSE);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(editor.sceneTreeView)),
        "changed", G_CALLBACK(onSceneSelectionChanged), &editor);
    gtk_container_add(GTK_CONTAINER(objectExplorerFrame), sceneScroll);
    gtk_container_add(GTK_CONTAINER(sceneScroll), editor.sceneTreeView);

    GtkWidget* propertiesFrame = gtk_frame_new("Properties");
    GtkWidget* propertiesDock = gtk_handle_box_new();
    gtk_handle_box_set_handle_position(GTK_HANDLE_BOX(propertiesDock), GTK_POS_LEFT);
    gtk_container_add(GTK_CONTAINER(propertiesDock), propertiesFrame);
    gtk_paned_pack2(GTK_PANED(rightDockPane), propertiesDock, TRUE, FALSE);
    GtkWidget* properties = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(properties), 8);
    gtk_grid_set_column_spacing(GTK_GRID(properties), 10);
    gtk_container_set_border_width(GTK_CONTAINER(properties), 10);
    gtk_container_add(GTK_CONTAINER(propertiesFrame), properties);
    makePropertyRow(properties, "Name", &editor.nameValue, 0);
    makePropertyRow(properties, "Path", &editor.pathValue, 1);
    makePropertyRow(properties, "Type", &editor.typeValue, 2);
    makePropertyRow(properties, "Size", &editor.sizeValue, 3);

    gtk_grid_attach(GTK_GRID(properties), gtk_label_new("Object Name"), 0, 4, 1, 1);
    editor.objectNameEntry = gtk_entry_new();
    g_signal_connect(editor.objectNameEntry, "changed", G_CALLBACK(onSceneNameChanged), &editor);
    gtk_grid_attach(GTK_GRID(properties), editor.objectNameEntry, 1, 4, 1, 1);

    const char* transformLabels[] = {"Position", "Rotation", "Scale"};
    for (std::size_t group = 0; group < 3; ++group)
    {
        const int row = static_cast<int>(5 + group);
        gtk_grid_attach(GTK_GRID(properties), gtk_label_new(transformLabels[group]), 0, row, 1, 1);
        GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const char* axisLabels[] = {"X", "Y", "Z"};
            gtk_box_pack_start(GTK_BOX(controls), gtk_label_new(axisLabels[axis]), FALSE, FALSE, 0);
            const double step = group == 1 ? 1.0 : 0.1;
            editor.transformSpins[group * 3 + axis] =
                gtk_spin_button_new_with_range(-100000.0, 100000.0, step);
            gtk_spin_button_set_digits(GTK_SPIN_BUTTON(editor.transformSpins[group * 3 + axis]),
                group == 1 ? 1 : 3);
            gtk_widget_set_size_request(editor.transformSpins[group * 3 + axis], 55, -1);
            gtk_box_pack_start(GTK_BOX(controls), editor.transformSpins[group * 3 + axis],
                TRUE, TRUE, 0);
            g_signal_connect(editor.transformSpins[group * 3 + axis], "value-changed",
                G_CALLBACK(onSceneTransformChanged), &editor);
        }
        gtk_grid_attach(GTK_GRID(properties), controls, 1, row, 1, 1);
    }

    gtk_grid_attach(GTK_GRID(properties), gtk_label_new("Gizmo"), 0, 8, 2, 1);
    GtkWidget* gizmoModes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    const char* gizmoLabels[] = {"Move", "Rotate", "Scale"};
    for (int mode = 0; mode < 3; ++mode)
    {
        editor.gizmoModeButtons[mode] = mode == 0
            ? gtk_radio_button_new_with_label(nullptr, gizmoLabels[mode])
            : gtk_radio_button_new_with_label_from_widget(
                GTK_RADIO_BUTTON(editor.gizmoModeButtons[0]), gizmoLabels[mode]);
        gtk_box_pack_start(GTK_BOX(gizmoModes), editor.gizmoModeButtons[mode], FALSE, FALSE, 0);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(editor.gizmoModeButtons[0]), TRUE);
    gtk_grid_attach(GTK_GRID(properties), gizmoModes, 0, 9, 2, 1);
    GtkWidget* gizmoArrows = gtk_grid_new();
    const char* arrowLabels[] = {"← X", "X →", "↓ Y", "Y ↑", "Z −", "Z +"};
    for (int axis = 0; axis < 3; ++axis)
    {
        for (int direction = 0; direction < 2; ++direction)
        {
            GtkWidget* arrow = gtk_button_new_with_label(arrowLabels[axis * 2 + direction]);
            g_object_set_data(G_OBJECT(arrow), "axis", GINT_TO_POINTER(axis));
            g_object_set_data(G_OBJECT(arrow), "direction",
                GINT_TO_POINTER(direction == 0 ? -1 : 1));
            g_signal_connect(arrow, "clicked", G_CALLBACK(onGizmoStepClicked), &editor);
            gtk_grid_attach(GTK_GRID(gizmoArrows), arrow, direction, axis, 1, 1);
        }
    }
    gtk_grid_attach(GTK_GRID(properties), gizmoArrows, 0, 10, 2, 1);

    editor.vertexEditToggle = gtk_check_button_new_with_label("Edit Cube Vertices");
    g_signal_connect(editor.vertexEditToggle, "toggled",
        G_CALLBACK(onVertexEditToggled), &editor);
    gtk_grid_attach(GTK_GRID(properties), editor.vertexEditToggle, 0, 11, 2, 1);
    editor.vertexIndexCombo = gtk_combo_box_text_new();
    for (int index = 0; index < 8; ++index)
    {
        const std::string label = "Vertex " + std::to_string(index);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(editor.vertexIndexCombo), label.c_str());
    }
    g_signal_connect(editor.vertexIndexCombo, "changed",
        G_CALLBACK(onVertexIndexChanged), &editor);
    gtk_grid_attach(GTK_GRID(properties), editor.vertexIndexCombo, 0, 12, 2, 1);
    GtkWidget* vertexControls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    const char* vertexAxes[] = {"X", "Y", "Z"};
    for (int axis = 0; axis < 3; ++axis)
    {
        gtk_box_pack_start(GTK_BOX(vertexControls), gtk_label_new(vertexAxes[axis]), FALSE, FALSE, 0);
        editor.vertexSpins[axis] = gtk_spin_button_new_with_range(-100.0, 100.0, 0.05);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(editor.vertexSpins[axis]), 3);
        gtk_widget_set_size_request(editor.vertexSpins[axis], 62, -1);
        g_signal_connect(editor.vertexSpins[axis], "value-changed",
            G_CALLBACK(onVertexChanged), &editor);
        gtk_box_pack_start(GTK_BOX(vertexControls), editor.vertexSpins[axis], TRUE, TRUE, 0);
    }
    gtk_grid_attach(GTK_GRID(properties), vertexControls, 0, 13, 2, 1);

    gtk_grid_attach(GTK_GRID(properties), gtk_label_new("Animation"), 0, 14, 2, 1);
    editor.animationCombo = gtk_combo_box_text_new();
    gtk_widget_set_sensitive(editor.animationCombo, FALSE);
    g_signal_connect(editor.animationCombo, "changed", G_CALLBACK(onAnimationChanged), &editor);
    gtk_grid_attach(GTK_GRID(properties), editor.animationCombo, 0, 15, 2, 1);
    editor.animationTimeline = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
    gtk_scale_set_draw_value(GTK_SCALE(editor.animationTimeline), TRUE);
    gtk_scale_set_digits(GTK_SCALE(editor.animationTimeline), 2);
    gtk_widget_set_sensitive(editor.animationTimeline, FALSE);
    g_signal_connect(editor.animationTimeline, "value-changed",
        G_CALLBACK(onAnimationTimelineChanged), &editor);
    gtk_grid_attach(GTK_GRID(properties), editor.animationTimeline, 0, 16, 2, 1);
    editor.animationPlayButton = makeToolButton("Play", G_CALLBACK(onAnimationPlayClicked), editor);
    gtk_widget_set_sensitive(editor.animationPlayButton, FALSE);
    gtk_grid_attach(GTK_GRID(properties), editor.animationPlayButton, 0, 17, 2, 1);

    editor.statusBar = gtk_statusbar_new();
    editor.statusContext = gtk_statusbar_get_context_id(GTK_STATUSBAR(editor.statusBar), "editor-status");
    gtk_box_pack_end(GTK_BOX(outer), editor.statusBar, FALSE, FALSE, 0);

    refreshTree(editor);
    refreshSceneOutliner(editor, 1);
    updateScriptControls(editor);
}
}

int main(int argc, char** argv)
{
    gtk_init(&argc, &argv);

    Editor editor;
    std::string inputError;
    if (!editor.input.initialize(inputError))
    {
        g_warning("Gamepad input is unavailable: %s", inputError.c_str());
    }
    editor.luaRuntime = std::make_unique<LuaRuntime>(editor.scene, editor.input);
    if (!editor.luaRuntime)
    {
        g_printerr("Could not initialize the Lua runtime.\n");
        return 1;
    }
    editor.resourceDirectory = findResourceDirectory(argv[0]);
    if (editor.resourceDirectory.empty())
    {
        g_printerr("Could not find the project's res/ directory.\n");
        return 1;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        g_warning("Audio playback is unavailable: %s", SDL_GetError());
    }
    else
    {
        const fs::path soundFont = editor.resourceDirectory / "sf2" / "gm.sf2";
        std::error_code soundFontError;
        bool soundFontAvailable = fs::is_regular_file(soundFont, soundFontError) &&
            !soundFontError;
        if (soundFontAvailable && Mix_SetSoundFonts(soundFont.string().c_str()) == 0)
        {
            g_warning("Could not configure MIDI SoundFont '%s': %s",
                soundFont.string().c_str(), Mix_GetError());
            soundFontAvailable = false;
        }

        const int mixerFlags = Mix_Init(MIX_INIT_MID);
        editor.mixerInitialized = true;
        editor.midiAvailable = soundFontAvailable && (mixerFlags & MIX_INIT_MID) != 0;
        if (!editor.midiAvailable)
        {
            g_warning("MIDI playback unavailable: %s",
                soundFontAvailable ? Mix_GetError() : "res/sf2/gm.sf2 was not found");
        }

        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0)
        {
            g_warning("Audio playback is unavailable: %s", Mix_GetError());
        }
        else
        {
            editor.audioAvailable = true;
        }
    }

    buildWindow(editor);
    updateProperties(editor);
    setEditorText(editor, "Select a resource in the project tree, or use Open to edit a file.\n"
        "Double-click a file in the tree to open it.", FALSE);
    const char* readyStatus = !editor.audioAvailable
        ? "Ready — audio playback is unavailable; file editing remains available."
        : editor.midiAvailable
            ? "Ready — select a file or open a project file."
            : "Ready — MIDI unavailable; install FluidSynth and provide res/sf2/gm.sf2.";
    setStatus(editor, readyStatus);
    gtk_widget_show_all(editor.window);
    editor.tickSource = g_timeout_add(16, onEngineTick, &editor);
    gtk_main();
    return 0;
}
