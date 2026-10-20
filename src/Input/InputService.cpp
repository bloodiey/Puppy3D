#include "InputService.h"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
constexpr Sint16 stickDeadzone = 8000;

float normalizeAxis(Sint16 value)
{
    if (std::abs(static_cast<int>(value)) <= stickDeadzone)
    {
        return 0.0f;
    }
    const float normalized = value < 0
        ? static_cast<float>(value) / 32768.0f
        : static_cast<float>(value) / 32767.0f;
    const float deadzone = static_cast<float>(stickDeadzone) / 32767.0f;
    return std::copysign((std::abs(normalized) - deadzone) / (1.0f - deadzone), normalized);
}

unsigned int keyvalFromName(const std::string& name)
{
    const guint keyval = gdk_keyval_from_name(name.c_str());
    return gdk_keyval_to_lower(keyval);
}

SDL_GameControllerButton gamepadButtonFromName(const std::string& name)
{
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (normalized == "a") return SDL_CONTROLLER_BUTTON_A;
    if (normalized == "b") return SDL_CONTROLLER_BUTTON_B;
    if (normalized == "x") return SDL_CONTROLLER_BUTTON_X;
    if (normalized == "y") return SDL_CONTROLLER_BUTTON_Y;
    if (normalized == "back") return SDL_CONTROLLER_BUTTON_BACK;
    if (normalized == "guide") return SDL_CONTROLLER_BUTTON_GUIDE;
    if (normalized == "start") return SDL_CONTROLLER_BUTTON_START;
    if (normalized == "leftstick" || normalized == "left_stick") return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    if (normalized == "rightstick" || normalized == "right_stick") return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    if (normalized == "leftshoulder" || normalized == "left_shoulder") return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if (normalized == "rightshoulder" || normalized == "right_shoulder") return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if (normalized == "dpup" || normalized == "dpad_up") return SDL_CONTROLLER_BUTTON_DPAD_UP;
    if (normalized == "dpdown" || normalized == "dpad_down") return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if (normalized == "dpleft" || normalized == "dpad_left") return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if (normalized == "dpright" || normalized == "dpad_right") return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    return SDL_CONTROLLER_BUTTON_INVALID;
}
}

InputService::~InputService()
{
    shutdown();
}

bool InputService::initialize(std::string& error)
{
    if (initialized_)
    {
        return true;
    }
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
    {
        error = SDL_GetError();
        return false;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    initialized_ = true;
    poll();
    error.clear();
    return true;
}

void InputService::shutdown()
{
    if (controller_)
    {
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
    }
    if (initialized_)
    {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        initialized_ = false;
    }
    lastDeviceCount_ = -1;
    clearKeyboard();
}

void InputService::poll()
{
    if (!initialized_)
    {
        return;
    }

    SDL_PumpEvents();
    SDL_GameControllerUpdate();
    if (controller_ && !SDL_GameControllerGetAttached(controller_))
    {
        SDL_GameControllerClose(controller_);
        controller_ = nullptr;
        gamepadButtonsDown_.fill(false);
    }
    if (!controller_)
    {
        const int deviceCount = SDL_NumJoysticks();
        if (deviceCount != lastDeviceCount_)
        {
            lastDeviceCount_ = deviceCount;
            for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
            {
                if (!SDL_IsGameController(deviceIndex))
                {
                    continue;
                }
                controller_ = SDL_GameControllerOpen(deviceIndex);
                if (controller_)
                {
                    break;
                }
                SDL_LogWarn(SDL_LOG_CATEGORY_INPUT,
                    "Could not open game controller: %s", SDL_GetError());
            }
        }
    }

    for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button)
    {
        const bool down = controller_ && SDL_GameControllerGetButton(controller_,
            static_cast<SDL_GameControllerButton>(button)) != 0;
        if (down && !gamepadButtonsDown_[static_cast<std::size_t>(button)])
        {
            gamepadButtonsPressedThisFrame_[static_cast<std::size_t>(button)] = true;
        }
        gamepadButtonsDown_[static_cast<std::size_t>(button)] = down;
    }
}

bool InputService::keyboardDown(GameAction action) const
{
    switch (action)
    {
    case GameAction::Forward:
        return pressedKeys_.count(GDK_KEY_w) || pressedKeys_.count(GDK_KEY_Up);
    case GameAction::Backward:
        return pressedKeys_.count(GDK_KEY_s) || pressedKeys_.count(GDK_KEY_Down);
    case GameAction::Left:
        return pressedKeys_.count(GDK_KEY_a) || pressedKeys_.count(GDK_KEY_Left);
    case GameAction::Right:
        return pressedKeys_.count(GDK_KEY_d) || pressedKeys_.count(GDK_KEY_Right);
    case GameAction::Jump:
        return pressedKeys_.count(GDK_KEY_space) != 0;
    case GameAction::Confirm:
        return pressedKeys_.count(GDK_KEY_Return) || pressedKeys_.count(GDK_KEY_KP_Enter);
    case GameAction::Cancel:
        return pressedKeys_.count(GDK_KEY_Escape) != 0;
    case GameAction::Count:
        return false;
    }
    return false;
}

void InputService::handleKey(unsigned int keyval, bool pressed)
{
    const unsigned int normalizedKey = gdk_keyval_to_lower(keyval);
    if (pressed)
    {
        if (pressedKeys_.insert(normalizedKey).second)
        {
            keysPressedThisFrame_.insert(normalizedKey);
        }
    }
    else
    {
        pressedKeys_.erase(normalizedKey);
    }
}

void InputService::handleMouseMotion(double x, double y)
{
    mouseX_ = x;
    mouseY_ = y;
}

void InputService::handleMouseButton(unsigned int button, bool pressed)
{
    if (pressed)
    {
        if (pressedMouseButtons_.insert(button).second)
        {
            mouseButtonsPressedThisFrame_.insert(button);
        }
    }
    else
    {
        pressedMouseButtons_.erase(button);
    }
}

void InputService::clearKeyboard()
{
    pressedKeys_.clear();
    keysPressedThisFrame_.clear();
}

void InputService::clearMouse()
{
    pressedMouseButtons_.clear();
    mouseButtonsPressedThisFrame_.clear();
}

void InputService::finishFrame()
{
    keysPressedThisFrame_.clear();
    mouseButtonsPressedThisFrame_.clear();
    gamepadButtonsPressedThisFrame_.fill(false);
}

bool InputService::isDown(GameAction action) const
{
    if (keyboardDown(action))
    {
        return true;
    }
    if (!controller_)
    {
        return false;
    }

    switch (action)
    {
    case GameAction::Forward:
        return SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTY) < -stickDeadzone ||
            SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0;
    case GameAction::Backward:
        return SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTY) > stickDeadzone ||
            SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0;
    case GameAction::Left:
        return SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTX) < -stickDeadzone ||
            SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
    case GameAction::Right:
        return SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTX) > stickDeadzone ||
            SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;
    case GameAction::Jump:
        return SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_A) != 0;
    case GameAction::Confirm:
        return SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_START) != 0;
    case GameAction::Cancel:
        return SDL_GameControllerGetButton(controller_, SDL_CONTROLLER_BUTTON_B) != 0;
    case GameAction::Count:
        return false;
    }
    return false;
}

bool InputService::isDown(const std::string& action) const
{
    if (action == "forward") return isDown(GameAction::Forward);
    if (action == "backward" || action == "back") return isDown(GameAction::Backward);
    if (action == "left") return isDown(GameAction::Left);
    if (action == "right") return isDown(GameAction::Right);
    if (action == "jump") return isDown(GameAction::Jump);
    if (action == "confirm") return isDown(GameAction::Confirm);
    if (action == "cancel") return isDown(GameAction::Cancel);
    return false;
}

float InputService::axis(const std::string& name) const
{
    if (name == "move_x")
    {
        const float keyboard = (keyboardDown(GameAction::Right) ? 1.0f : 0.0f) -
            (keyboardDown(GameAction::Left) ? 1.0f : 0.0f);
        const float gamepad = controller_
            ? normalizeAxis(SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTX))
            : 0.0f;
        return std::clamp(keyboard + gamepad, -1.0f, 1.0f);
    }
    if (name == "move_y")
    {
        const float keyboard = (keyboardDown(GameAction::Forward) ? 1.0f : 0.0f) -
            (keyboardDown(GameAction::Backward) ? 1.0f : 0.0f);
        const float gamepad = controller_
            ? -normalizeAxis(SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTY))
            : 0.0f;
        return std::clamp(keyboard + gamepad, -1.0f, 1.0f);
    }
    return 0.0f;
}

bool InputService::gamepadConnected() const
{
    return controller_ && SDL_GameControllerGetAttached(controller_);
}

bool InputService::keyPressed(const std::string& key) const
{
    const unsigned int keyval = keyvalFromName(key);
    return keyval != 0 && keysPressedThisFrame_.count(keyval) != 0;
}

bool InputService::keyDown(const std::string& key) const
{
    const unsigned int keyval = keyvalFromName(key);
    return keyval != 0 && pressedKeys_.count(keyval) != 0;
}

bool InputService::gamepadButtonPressed(const std::string& button) const
{
    const SDL_GameControllerButton id = gamepadButtonFromName(button);
    return id != SDL_CONTROLLER_BUTTON_INVALID &&
        gamepadButtonsPressedThisFrame_[static_cast<std::size_t>(id)];
}

bool InputService::gamepadButtonDown(const std::string& button) const
{
    const SDL_GameControllerButton id = gamepadButtonFromName(button);
    return id != SDL_CONTROLLER_BUTTON_INVALID &&
        gamepadButtonsDown_[static_cast<std::size_t>(id)];
}

double InputService::mouseX() const
{
    return mouseX_;
}

double InputService::mouseY() const
{
    return mouseY_;
}

bool InputService::mouseButtonPressed(unsigned int button) const
{
    return mouseButtonsPressedThisFrame_.count(button) != 0;
}

bool InputService::mouseButtonDown(unsigned int button) const
{
    return pressedMouseButtons_.count(button) != 0;
}
