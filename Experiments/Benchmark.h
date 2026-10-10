#pragma once
#include "Falcor.h"
#include "BenchmarkSupport.h"
#include "Triangulation/MeshValidation.h"
#include <filesystem>
#include <fstream>
#include <functional>

namespace Benchmark
{
struct MeshMetrics
{
    MeshValidation::Audit audit;
    uint32_t vertices = 0, triangles = 0, sourceTriangles = 0, failedPatches = 0, flips = 0;
    double cpuMs = 0, referenceError = 0, projectedEdgeLength = 0;
    bool valid = false;
};

struct Case
{
    uint32_t id = 0, block = 0, view = 0, msaa = 1, instances = 1, width = 0, height = 0;
    uint32_t gridCols = 1, gridRows = 1, gridLayers = 1, warmup = 0, samples = 1;
    double xRotation = 0, yRotation = 0, radius = 0;
    bool depth = true, meshopt = false, surfaceLocalFlips = false;
    std::string geometry, method, shader, cpuScope, passScope;
    MeshMetrics mesh;
};

struct Sample
{
    double drawMs, passMs;
};
struct Counts
{
    uint32_t helpers = 0, active = 0;
};
struct Result
{
    Case description;
    std::vector<Sample> samples;
    Counts counts;
    bool complete = false;
};

// Callbacks bind the existing renderer; the runner owns all measurement phases.
struct Work
{
    std::function<void()> clear, draw, finish;
    std::function<Counts()> diagnose;
};

class Runner
{
public:
    explicit Runner(Falcor::ref<Falcor::Device> device);
    void start(Case description);
    bool step(Falcor::RenderContext* context, const Work& work);
    bool started() const { return mStarted; }
    const Result& result() const { return mResult; }

private:
    Falcor::ref<Falcor::GpuTimer> mDrawTimer, mPassTimer;
    Result mResult;
    uint32_t mWarmupRemaining = 0;
    bool mStarted = false;
};

// One column definition generates headers and rows for both frontends.
class Writer
{
public:
    Writer(const std::filesystem::path& path, const Falcor::Device& device, const std::string& settings);
    ~Writer();
    void write(const Result& result);
    void finish(bool complete, const std::string& message);
    uint32_t caseCount() const { return mCases; }

private:
    std::ofstream mSummary, mRaw, mMetadata;
    std::string mRunId;
    uint32_t mCases = 0;
    bool mFinished = false;
};
} // namespace Benchmark
