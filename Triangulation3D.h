#pragma once

#include "Falcor.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Triangulation3D
{

struct Mesh
{
    std::vector<Falcor::float3> vertices;
    std::vector<std::vector<uint32_t>> faces;
};

enum class Method : uint32_t
{
    ConstrainedDelaunay,
    ConstrainedDelaunayFlipped,
    GreedyPointSet,
    LmtMinimumWeight,
    PclGreedyProjection,
};

struct PatchGrowthOptions
{
    float maxNormalDeviationDegrees = 15.0f;
    float maxRelativePlaneDeviation = 0.005f;
    float minPlaneDeviation = 1e-5f;
    uint32_t maxFacesPerPatch = 64;
};

struct TriangulationOptions
{
    Method method = Method::ConstrainedDelaunay;
    bool use3DEdgeLengthForGreedy = true;
    uint32_t pclMaximumNearestNeighbors = 100;
    float pclMu = 2.5f;
    float pclSearchRadius = 0.0f;
    float pclMinimumAngleDegrees = 10.0f;
    float pclMaximumAngleDegrees = 120.0f;
};

struct ProjectionFrame
{
    Falcor::float3 origin = Falcor::float3(0.0f, 0.0f, 0.0f);
    Falcor::float3 normal = Falcor::float3(0.0f, 0.0f, 1.0f);
    Falcor::float3 tangent = Falcor::float3(1.0f, 0.0f, 0.0f);
    Falcor::float3 bitangent = Falcor::float3(0.0f, 1.0f, 0.0f);
};

struct Patch
{
    std::vector<uint32_t> faceIndices;
    std::vector<uint32_t> vertexIndices;
    std::vector<std::pair<uint32_t, uint32_t>> boundaryEdges;
    std::vector<uint32_t> boundaryLoop;
    ProjectionFrame frame;
    float maxPlaneDistance = 0.0f;
    float maxNormalDeviationDegrees = 0.0f;
    bool isDiskLike = false;
    std::string message;
};

struct PatchTriangulationResult
{
    std::vector<Falcor::float3> vertices;
    std::vector<uint32_t> sourceVertexIndices;
    std::vector<uint32_t> indices;
    bool success = false;
    std::string message;
};

struct MeshTriangulationResult
{
    std::vector<Falcor::float3> vertices;
    std::vector<uint32_t> indices;
    std::vector<Patch> patches;
    std::vector<std::string> messages;
    bool success = false;
};

const char* getMethodName(Method method);

std::vector<Patch> growPatches(const Mesh& mesh, const PatchGrowthOptions& options = {});

PatchTriangulationResult triangulatePatch(
    const Mesh& mesh,
    const Patch& patch,
    const TriangulationOptions& options = {}
);

MeshTriangulationResult triangulateMesh(
    const Mesh& mesh,
    const PatchGrowthOptions& patchOptions = {},
    const TriangulationOptions& triangulationOptions = {}
);

MeshTriangulationResult useSourceMeshTriangulation(const Mesh& mesh);

} // namespace Triangulation3D
