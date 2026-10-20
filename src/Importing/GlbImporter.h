#pragma once

#include "Engine.h"

#include <filesystem>
#include <string>
#include <vector>

bool loadGlb(const std::filesystem::path& path,
    std::vector<EngineMesh>& meshes, std::string& error);
bool loadGlb(const std::filesystem::path& path,
    EngineModelData& model, std::string& error);
