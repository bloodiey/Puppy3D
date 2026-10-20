#pragma once

#include <SDL.h>

#include <array>
#include <string>
#include <unordered_set>

enum class GameAction
{
    Forward,
    Backward,
    Left,
    Right,
    Jump,
    Confirm,
    Cancel,
    Count
};

class InputService
{
public:
    InputService() = default;
    ~InputService();

    InputService(const InputService&) = delete;
    InputService& operator=(const InputService&) = delete;

    bool initialize(std::string& error);
    void shutdown();
    void poll();
    void handleKey(unsigned int keyval, bool pressed);
    void handleMouseMotion(double x, double y);
    void handleMouseButton(unsigned int button, bool pressed);
    void clearKeyboard();
    void clearMouse();
    void finishFrame();

    bool isDown(GameAction action) const;
    bool isDown(const std::string& action) const;
    float axis(const std::string& name) const;
    bool gamepadConnected() const;
    bool keyPressed(const std::string& key) const;
    bool keyDown(const std::string& key) const;
    bool gamepadButtonPressed(const std::string& button) const;
    bool gamepadButtonDown(const std::string& button) const;
    double mouseX() const;
    double mouseY() const;
    bool mouseButtonPressed(unsigned int button) const;
    bool mouseButtonDown(unsigned int button) const;

private:
    bool keyboardDown(GameAction action) const;

    std::unordered_set<unsigned int> pressedKeys_;
    std::unordered_set<unsigned int> keysPressedThisFrame_;
    std::unordered_set<unsigned int> pressedMouseButtons_;
    std::unordered_set<unsigned int> mouseButtonsPressedThisFrame_;
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> gamepadButtonsDown_{};
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> gamepadButtonsPressedThisFrame_{};
    double mouseX_ = 0.0;
    double mouseY_ = 0.0;
    SDL_GameController* controller_ = nullptr;
    int lastDeviceCount_ = -1;
    bool initialized_ = false;
};
