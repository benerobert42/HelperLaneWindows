#pragma once
#include "Benchmark.h"
#include "BenchmarkOptions.h"
#include "RasterMetrics.h"
#include <atomic>
#include <functional>
#include <map>

namespace BenchmarkPreparation
{
using MeshResult = Triangulation3D::MeshTriangulationResult;

// Matches the surface shader's Params constant buffer.
struct Params
{
    Falcor::float4x4 viewProj, model;
    uint32_t gridCols;
    float gridSpacing, gridCenter;
    uint32_t instanceCount;
};

struct Prepared
{
    std::string name;
    MeshResult mesh;
    Benchmark::MeshMetrics metrics;
    int32_t view = -1, optimizationView = -1;
    uint32_t objectiveInstances = 0;
    std::map<uint32_t, RasterMetrics::Metrics> rasterMetrics;
};

struct Geometry
{
    std::vector<Prepared> prepared;
    Params params{};
    std::vector<uint32_t> sourceIndices;
    Falcor::float3 cameraPosition;
    float radius = 0;
};

Falcor::float4x4 modelForView(const BenchmarkOptions::Options& options, uint32_t view);
Params paramsForCase(const BenchmarkOptions::Options& options, Params geometry, uint32_t view, uint32_t instances);

// CPU construction, audits, index ordering and raster metrics; no graphics API calls.
Geometry prepare(const BenchmarkOptions::Options& options, size_t shapeIndex, const std::atomic<bool>& cancel,
                 const std::function<void(const MeshResult&)>& publishSource);
} // namespace BenchmarkPreparation
