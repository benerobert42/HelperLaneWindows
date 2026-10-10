#pragma once
#include "Falcor.h"
#include <filesystem>

// These presets deliberately use the same loader and options as a custom OBJ.
inline std::filesystem::path getModelPresetPath(uint32_t geometryType)
{
    const char* name = geometryType == 8 ? "Suzanne.obj" : geometryType == 9 ? "Bunny.obj" : "Teapot.obj";
    return Falcor::getExecutableDirectory() / "Data/HelperLaneWindows/Models" / name;
}
