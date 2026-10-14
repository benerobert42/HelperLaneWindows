#pragma once
#include "Falcor.h"
#include "BenchmarkSupport.h"
#include "PipelineStatistics.h"
#include "Triangulation/MeshValidation.h"
#include <filesystem>
#include <fstream>
#include <functional>

namespace Benchmark
{
struct MeshMetrics
{
    MeshValidation::Audit audit;
    uint32_t vertices = 0, triangles = 0, sourceTriangles = 0, failedPatches = 0, flips = 0, starReplacements = 0;
    double cpuMs = 0, setupMs = 0, referenceSetupMs = 0, referenceError = 0, projectedEdgeLength = 0;
    bool valid = false;
    double reverseReferenceError = 0, referenceBudget = 0;
    bool surfaceEquivalent = true;
    std::string experimentClass = "Strict";
    int32_t triangulationStatus = -1;
    uint32_t patchCount = 0, changedTriangles = 0;
    bool structuralValid = false;
    uint32_t referenceAuditDirections = 0;
    double sourceSetupMs = 0, patchSetupMs = 0;
};

struct Case
{
    uint32_t id = 0, block = 0, view = 0, msaa = 1, instances = 1, width = 0, height = 0;
    uint32_t gridCols = 1, gridRows = 1, gridLayers = 1, warmup = 0, samples = 1;
    uint32_t queueDepth = 1;
    int32_t optimizationView = -1;
    uint32_t objectiveInstances = 0;
    double xRotation = 0, yRotation = 0, zRotation = 0, radius = 0;
    bool depth = true, meshopt = false, surfaceLocalFlips = false;
    std::string geometry, method, shader, cpuScope, passScope;
    std::string connectivitySeed = "Source";
    MeshMetrics mesh;
};

struct Sample
{
    double drawMs, passMs;
};
struct Counts
{
    uint32_t helpers = 0, active = 0;
    PipelineCounts pipeline;
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
    void drain(); // Preserve issued samples when a case is cancelled.
    bool started() const { return mStarted; }
    const Result& result() const { return mResult; }

private:
    struct Slot
    {
        Falcor::ref<Falcor::GpuTimer> draw, pass;
        uint64_t fenceValue = 0;
    };
    Falcor::ref<Falcor::Device> mpDevice;
    Falcor::ref<Falcor::Fence> mpFence;
    std::vector<Slot> mSlots;
    Result mResult;
    uint32_t mWarmupRemaining = 0, mIssued = 0;
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
