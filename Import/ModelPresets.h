#pragma once
#include "Falcor.h"
#include <array>
#include <filesystem>

// These presets deliberately use the same loader and options as a custom OBJ.
struct ObjModelPreset
{
    const char* name;
    const char* file;
};
inline constexpr std::array<ObjModelPreset, 8> kObjModelPresets{{
    {"Suzanne", "Suzanne.obj"},
    {"Bunny", "Bunny.obj"},
    {"Teapot", "Teapot.obj"},
    {"Swept Wing", "ResearchCandidates/SweptWing.obj"},
    {"Toothed Wheel", "ResearchCandidates/ToothedWheel.obj"},
    {"Faceted Arch", "ResearchCandidates/FacetedArch.obj"},
    {"Trefoil Knot", "ResearchCandidates/TrefoilKnot.obj"},
    {"Spiral Ramp", "ResearchCandidates/SpiralRamp.obj"},
}};
inline std::filesystem::path getModelPresetPath(uint32_t geometryType)
{
    if (geometryType < 8 || geometryType - 8 >= kObjModelPresets.size()) return {};
    return Falcor::getExecutableDirectory() / "Data/HelperLaneWindows/Models" / kObjModelPresets[geometryType - 8].file;
}
