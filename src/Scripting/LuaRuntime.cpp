#include "LuaRuntime.h"

#include "Engine.h"
#include "InputService.h"

#include <lua.hpp>

#include <string>

namespace
{
EngineScene& getScene(lua_State* state)
{
    return *static_cast<EngineScene*>(lua_touserdata(state, lua_upvalueindex(1)));
}

InputService& getInput(lua_State* state)
{
    return *static_cast<InputService*>(lua_touserdata(state, lua_upvalueindex(1)));
}

int inputIsDown(lua_State* state)
{
    const char* action = luaL_checkstring(state, 1);
    lua_pushboolean(state, getInput(state).isDown(action));
    return 1;
}

int inputAxis(lua_State* state)
{
    const char* name = luaL_checkstring(state, 1);
    lua_pushnumber(state, getInput(state).axis(name));
    return 1;
}

int gamepadConnected(lua_State* state)
{
    lua_pushboolean(state, getInput(state).gamepadConnected());
    return 1;
}

int keyPressed(lua_State* state)
{
    lua_pushboolean(state, getInput(state).keyPressed(luaL_checkstring(state, 1)));
    return 1;
}

int keyDown(lua_State* state)
{
    lua_pushboolean(state, getInput(state).keyDown(luaL_checkstring(state, 1)));
    return 1;
}

int gamepadButtonPressed(lua_State* state)
{
    lua_pushboolean(state,
        getInput(state).gamepadButtonPressed(luaL_checkstring(state, 1)));
    return 1;
}

int gamepadButtonDown(lua_State* state)
{
    lua_pushboolean(state,
        getInput(state).gamepadButtonDown(luaL_checkstring(state, 1)));
    return 1;
}

int mouseX(lua_State* state)
{
    lua_pushnumber(state, getInput(state).mouseX());
    return 1;
}

int mouseY(lua_State* state)
{
    lua_pushnumber(state, getInput(state).mouseY());
    return 1;
}

int mouseButtonPressed(lua_State* state)
{
    const lua_Integer button = luaL_checkinteger(state, 1);
    lua_pushboolean(state, button > 0 &&
        getInput(state).mouseButtonPressed(static_cast<unsigned int>(button)));
    return 1;
}

int mouseButtonDown(lua_State* state)
{
    const lua_Integer button = luaL_checkinteger(state, 1);
    lua_pushboolean(state, button > 0 &&
        getInput(state).mouseButtonDown(static_cast<unsigned int>(button)));
    return 1;
}

int clearScene(lua_State* state)
{
    getScene(state).clear();
    return 0;
}

int addCube(lua_State* state)
{
    EngineScene& scene = getScene(state);
    const char* name = luaL_checkstring(state, 1);
    const float x = static_cast<float>(luaL_optnumber(state, 2, 0.0));
    const float y = static_cast<float>(luaL_optnumber(state, 3, 0.0));
    const float z = static_cast<float>(luaL_optnumber(state, 4, 0.0));
    lua_pushinteger(state, scene.addCube(name, x, y, z));
    return 1;
}

int setPosition(lua_State* state)
{
    const int objectId = static_cast<int>(luaL_checkinteger(state, 1));
    const float x = static_cast<float>(luaL_checknumber(state, 2));
    const float y = static_cast<float>(luaL_checknumber(state, 3));
    const float z = static_cast<float>(luaL_checknumber(state, 4));
    if (!getScene(state).setPosition(objectId, x, y, z))
    {
        return luaL_error(state, "set_position: unknown cube id %d", objectId);
    }
    return 0;
}

int setRotation(lua_State* state)
{
    const int objectId = static_cast<int>(luaL_checkinteger(state, 1));
    const float x = static_cast<float>(luaL_checknumber(state, 2));
    const float y = static_cast<float>(luaL_checknumber(state, 3));
    const float z = static_cast<float>(luaL_checknumber(state, 4));
    if (!getScene(state).setRotation(objectId, x, y, z))
    {
        return luaL_error(state, "set_rotation: unknown cube id %d", objectId);
    }
    return 0;
}

int setColor(lua_State* state)
{
    const int objectId = static_cast<int>(luaL_checkinteger(state, 1));
    const float red = static_cast<float>(luaL_checknumber(state, 2));
    const float green = static_cast<float>(luaL_checknumber(state, 3));
    const float blue = static_cast<float>(luaL_checknumber(state, 4));
    if (!getScene(state).setColor(objectId, red, green, blue))
    {
        return luaL_error(state, "set_color: unknown cube id %d", objectId);
    }
    return 0;
}

void registerSceneFunction(lua_State* state, EngineScene& scene,
    const char* name, lua_CFunction function)
{
    lua_pushlightuserdata(state, &scene);
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

void registerInputFunction(lua_State* state, InputService& input,
    const char* name, lua_CFunction function)
{
    lua_pushlightuserdata(state, &input);
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}
}

LuaRuntime::LuaRuntime(EngineScene& scene, InputService& input)
    : state_(luaL_newstate()), scene_(scene), input_(input)
{
    if (!state_)
    {
        return;
    }

    luaL_requiref(state_, "_G", luaopen_base, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(state_, 1);

    lua_pushnil(state_);
    lua_setglobal(state_, "dofile");
    lua_pushnil(state_);
    lua_setglobal(state_, "loadfile");

    lua_newtable(state_);
    registerSceneFunction(state_, scene_, "clear_scene", clearScene);
    registerSceneFunction(state_, scene_, "add_cube", addCube);
    registerSceneFunction(state_, scene_, "set_position", setPosition);
    registerSceneFunction(state_, scene_, "set_rotation", setRotation);
    registerSceneFunction(state_, scene_, "set_color", setColor);
    lua_setglobal(state_, "engine");

    lua_newtable(state_);
    registerInputFunction(state_, input_, "is_down", inputIsDown);
    registerInputFunction(state_, input_, "axis", inputAxis);
    registerInputFunction(state_, input_, "gamepad_connected", gamepadConnected);
    registerInputFunction(state_, input_, "getkeypressed", keyPressed);
    registerInputFunction(state_, input_, "get_key_pressed", keyPressed);
    registerInputFunction(state_, input_, "is_key_down", keyDown);
    registerInputFunction(state_, input_, "getgamepadpressed", gamepadButtonPressed);
    registerInputFunction(state_, input_, "get_gamepad_pressed", gamepadButtonPressed);
    registerInputFunction(state_, input_, "is_gamepad_down", gamepadButtonDown);
    registerInputFunction(state_, input_, "getmousex", mouseX);
    registerInputFunction(state_, input_, "get_mouse_x", mouseX);
    registerInputFunction(state_, input_, "getmousey", mouseY);
    registerInputFunction(state_, input_, "get_mouse_y", mouseY);
    registerInputFunction(state_, input_, "getmousepressed", mouseButtonPressed);
    registerInputFunction(state_, input_, "get_mouse_pressed", mouseButtonPressed);
    registerInputFunction(state_, input_, "is_mouse_down", mouseButtonDown);
    lua_setglobal(state_, "input");
}

LuaRuntime::~LuaRuntime()
{
    if (state_)
    {
        lua_close(state_);
    }
}

bool LuaRuntime::runFile(const std::string& path, std::string& error)
{
    if (!state_)
    {
        error = "Could not create the Lua runtime.";
        return false;
    }

    const int loadResult = luaL_loadfile(state_, path.c_str());
    if (loadResult != LUA_OK)
    {
        const char* message = lua_tostring(state_, -1);
        error = message ? message : "Lua could not load the script.";
        lua_pop(state_, 1);
        return false;
    }

    scene_.clear();
    lua_pushnil(state_);
    lua_setglobal(state_, "update");
    hasUpdateFunction_ = false;

    if (lua_pcall(state_, 0, 0, 0) != LUA_OK)
    {
        const char* message = lua_tostring(state_, -1);
        error = message ? message : "Lua script raised a non-string error.";
        lua_pop(state_, 1);
        return false;
    }

    lua_getglobal(state_, "update");
    hasUpdateFunction_ = lua_isfunction(state_, -1);
    lua_pop(state_, 1);
    error.clear();
    return true;
}

bool LuaRuntime::update(float deltaTime, std::string& error)
{
    if (!state_ || !hasUpdateFunction_)
    {
        return true;
    }

    lua_getglobal(state_, "update");
    lua_pushnumber(state_, deltaTime);
    if (lua_pcall(state_, 1, 0, 0) != LUA_OK)
    {
        const char* message = lua_tostring(state_, -1);
        error = message ? message : "Lua update raised a non-string error.";
        lua_pop(state_, 1);
        hasUpdateFunction_ = false;
        return false;
    }
    error.clear();
    return true;
}

void LuaRuntime::stopUpdates()
{
    hasUpdateFunction_ = false;
}
