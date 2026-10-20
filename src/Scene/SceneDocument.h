#pragma once

#include "Engine.h"

#include <filesystem>
#include <string>

bool saveSceneXml(const EngineScene& scene,
    const std::filesystem::path& path, std::string& error);
bool loadSceneXml(EngineScene& scene,
    const std::filesystem::path& path, std::string& error);