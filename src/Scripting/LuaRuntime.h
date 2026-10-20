#pragma once

#include <string>

struct lua_State;
class EngineScene;
class InputService;

class LuaRuntime
{
public:
    LuaRuntime(EngineScene& scene, InputService& input);
    ~LuaRuntime();

    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;

    bool runFile(const std::string& path, std::string& error);
    bool update(float deltaTime, std::string& error);
    void stopUpdates();

private:
    lua_State* state_ = nullptr;
    EngineScene& scene_;
    InputService& input_;
    bool hasUpdateFunction_ = false;
};
