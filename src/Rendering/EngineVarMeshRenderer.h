#pragma once

#include "Engine.h"

#include <string>

bool buildEngineMesh(const EngineVar::Polygon& polygon, EngineMesh& mesh,
    std::string& error);