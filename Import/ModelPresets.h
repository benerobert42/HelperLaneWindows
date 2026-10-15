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
inline constexpr std::array<ObjModelPreset, 15> kObjModelPresets{{
    {"Suzanne", "Suzanne.obj"},
    {"Bunny", "Bunny.obj"},
    {"Teapot", "Teapot.obj"},
    {"Swept Wing", "ResearchCandidates/SweptWing.obj"},
    {"Toothed Wheel", "ResearchCandidates/ToothedWheel.obj"},
    {"Faceted Arch", "ResearchCandidates/FacetedArch.obj"},
    {"Trefoil Knot", "ResearchCandidates/TrefoilKnot.obj"},
    {"Spiral Ramp", "ResearchCandidates/SpiralRamp.obj"},
    {"GraphEx Heart", "GraphExHeart.obj"},
    {"GraphEx Tangle Cube", "GraphExTangleCube.obj"},
    {"Swept Wing (control)", "ResearchCandidates/SweptWing_control.obj"},
    {"Toothed Wheel (control)", "ResearchCandidates/ToothedWheel_control.obj"},
    {"Faceted Arch (control)", "ResearchCandidates/FacetedArch_control.obj"},
    {"Trefoil Knot (control)", "ResearchCandidates/TrefoilKnot_control.obj"},
    {"Spiral Ramp (control)", "ResearchCandidates/SpiralRamp_control.obj"},
}};

// Fixed study fixtures reuse BenchmarkGeometry::make(), including its exact
// segment counts and skew. Adjustable procedural models remain IDs 0..6.
struct StudyGeometryPreset { const char* name; const char* fixture; };
inline constexpr std::array<StudyGeometryPreset, 22> kStudyGeometryPresets{{
    {"Sphere", "sphere"}, {"Dense Sphere", "sphere_dense"}, {"Jittered Sphere", "jittered"},
    {"Ellipsoid", "ellipsoid"}, {"Small Ellipsoid", "ellipsoid_small"},
    {"Lumpy Ellipsoid", "blob"}, {"Small Lumpy Ellipsoid", "blob_small"},
    {"Saddle", "saddle_branch"}, {"Clustered Wave", "clustered"},
    {"Planar Grid", "planar"}, {"Jittered Planar Grid", "jittered_planar"}, {"Folded Sheet", "folded"},
    {"Skew Grid 1", "skew1"}, {"Skew Grid 2", "skew2"}, {"Skew Grid 3", "skew3"},
    {"Skew Fold", "skew_fold"}, {"Skew Saddle", "skew_saddle"},
    {"Torus", "torus"}, {"Skew Torus", "torus_skew"},
    {"Tweaked Cylinder", "cylinder"}, {"Skew Tweaked Cylinder", "cylinder_skew"},
    {"Planar Ellipse", "ellipse2d"},
}};
inline constexpr uint32_t kStudyGeometryFirst = 8 + uint32_t(kObjModelPresets.size());
inline const StudyGeometryPreset* getStudyGeometryPreset(uint32_t geometryType)
{
    return geometryType >= kStudyGeometryFirst && geometryType - kStudyGeometryFirst < kStudyGeometryPresets.size()
        ? &kStudyGeometryPresets[geometryType - kStudyGeometryFirst] : nullptr;
}
inline std::filesystem::path getModelPresetPath(uint32_t geometryType)
{
    if (geometryType < 8 || geometryType - 8 >= kObjModelPresets.size()) return {};
    return Falcor::getExecutableDirectory() / "Data/HelperLaneWindows/Models" / kObjModelPresets[geometryType - 8].file;
}
