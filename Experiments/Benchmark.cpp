#include "Benchmark.h"
#include <fmt/format.h>
#include <chrono>

namespace Benchmark
{
namespace
{
using Row = std::vector<std::pair<std::string, std::string>>;
template<typename T>
std::string number(T value)
{
    return fmt::format("{}", value);
}
std::string number(double value)
{
    return fmt::format("{:.10g}", value);
}
std::string number(bool value)
{
    return value ? "1" : "0";
}

Row summaryRow(const Result& r, const BenchmarkSupport::Statistics& draw, const BenchmarkSupport::Statistics& pass, const std::string& run)
{
    const auto& c = r.description;
    const auto& m = c.mesh;
    return {
        {"Case", number(c.id)},
        {"Geometry", BenchmarkSupport::csvField(c.geometry)},
        {"Method", BenchmarkSupport::csvField(c.method)},
        {"Block", number(c.block)},
        {"View", number(c.view)},
        {"MSAA", number(c.msaa)},
        {"Shader", BenchmarkSupport::csvField(c.shader)},
        {"Instances", number(c.instances)},
        {"Width", number(c.width)},
        {"Height", number(c.height)},
        {"Depth", number(c.depth)},
        {"Meshopt", number(c.meshopt)},
        {"Samples", number(r.samples.size())},
        {"GPU_Median_ms", number(draw.median)},
        {"GPU_Mean_ms", number(draw.mean)},
        {"GPU_StdDev_ms", number(draw.standardDeviation)},
        {"GPU_Min_ms", number(draw.minimum)},
        {"GPU_Max_ms", number(draw.maximum)},
        {"ConstructionCPU_ms", number(m.cpuMs)},
        {"SetupCPU_ms", number(m.setupMs)},
        {"ReferenceSetupCPU_ms", number(m.referenceSetupMs)},
        {"SourceSetupCPU_ms", number(m.sourceSetupMs)},
        {"PatchSetupCPU_ms", number(m.patchSetupMs)},
        {"OutputVertices", number(m.vertices)},
        {"OutputTriangles", number(m.triangles)},
        {"SourceTriangles", number(m.sourceTriangles)},
        {"FailedPatches", number(m.failedPatches)},
        {"DuplicateTriangles", number(m.audit.duplicates)},
        {"NonManifoldEdges", number(m.audit.nonmanifold)},
        {"WindingErrors", number(m.audit.winding)},
        {"MissingBoundaryEdges", number(m.audit.missingBoundary)},
        {"UnexpectedBoundaryEdges", number(m.audit.unexpectedBoundary)},
        {"DegenerateTriangles", number(m.audit.degenerate)},
        {"SampledReferenceError", number(m.referenceError)},
        {"ReverseSampledReferenceError", number(m.reverseReferenceError)},
        {"ReferenceErrorBudget", number(m.referenceBudget)},
        {"ReferenceAuditDirections", number(m.referenceAuditDirections)},
        {"SurfaceEquivalent", number(m.surfaceEquivalent)},
        {"ExperimentClass", BenchmarkSupport::csvField(m.experimentClass)},
        {"TriangulationStatus", number(m.triangulationStatus)},
        {"PatchCount", number(m.patchCount)},
        {"ChangedTriangles", number(m.changedTriangles)},
        {"StructuralValid", number(m.structuralValid)},
        {"ConnectedComponents", number(m.audit.components)},
        {"UniqueEdges", number(m.audit.edges)},
        {"EulerCharacteristic", number(m.audit.euler)},
        {"MaxValence", number(m.audit.maxValence)},
        {"ValenceVariance", number(m.audit.valenceVariance)},
        {"ValidForRanking", number(r.complete && m.valid && m.surfaceEquivalent && r.counts.active > 0)},
        {"Flips", number(m.flips)},
        {"StarReplacements", number(m.starReplacements)},
        {"TotalEdgeLength3D", number(m.audit.edgeLength)},
        {"TotalSceneEdgeLength3D", number(m.audit.edgeLength * c.instances)},
        {"SurfaceArea", number(m.audit.area)},
        {"DiagnosticHelpers", number(r.counts.helpers)},
        {"DiagnosticActiveLanes", number(r.counts.active)},
        {"PipelineStatisticsAvailable", number(r.counts.pipeline.available)},
        {"HardwareInputPrimitives", number(r.counts.pipeline.inputPrimitives)},
        {"HardwareVSInvocations", number(r.counts.pipeline.vertexInvocations)},
        {"HardwarePSInvocations", number(r.counts.pipeline.pixelInvocations)},
        {"HardwareRasterPrimitives", number(r.counts.pipeline.rasterPrimitives)},
        {"DiagnosticHelperFraction", number(double(r.counts.helpers) / std::max(1.0, double(r.counts.helpers) + r.counts.active))},
        {"GPU_PassMedian_ms", number(pass.median)},
        {"GPU_PassMean_ms", number(pass.mean)},
        {"GPU_PassStdDev_ms", number(pass.standardDeviation)},
        {"GPU_PassMin_ms", number(pass.minimum)},
        {"GPU_PassMax_ms", number(pass.maximum)},
        {"MeshValid", number(m.valid)},
        {"RunId", run},
        {"Protocol", "4"},
        {"QueueDepth", number(c.queueDepth)},
        {"OptimizationView", number(c.optimizationView)},
        {"ObjectiveInstances", number(c.objectiveInstances)},
        {"ConnectivitySeed", BenchmarkSupport::csvField(c.connectivitySeed)},
        {"PassScope", BenchmarkSupport::csvField(c.passScope)},
        {"CPUScope", BenchmarkSupport::csvField(c.cpuScope)},
        {"Warmup", number(c.warmup)},
        {"XRotationDegrees", number(c.xRotation)},
        {"YRotationDegrees", number(c.yRotation)},
        {"ZRotationDegrees", number(c.zRotation)},
        {"GridCols", number(c.gridCols)},
        {"GridRows", number(c.gridRows)},
        {"GridLayers", number(c.gridLayers)},
        {"Radius", number(c.radius)},
        {"SurfaceLocalFlips", number(c.surfaceLocalFlips)},
        {"ProjectedEdgeLength", number(m.projectedEdgeLength)},
        {"ExpectedSamples", number(c.samples)},
        {"CaseComplete", number(r.complete)}};
}

Row rawRow(const Case& c, uint32_t index, Sample s, const std::string& run)
{
    return {
        {"Case", number(c.id)},
        {"Sample", number(index)},
        {"Frame", number(c.warmup + index)},
        {"GPU_Draw_ms", number(s.drawMs)},
        {"GPU_Pass_ms", number(s.passMs)},
        {"RunId", run}};
}

void writeRow(std::ostream& stream, const Row& row, bool header = false)
{
    for (size_t i = 0; i < row.size(); ++i)
        stream << (i ? "," : "") << (header ? row[i].first : row[i].second);
    stream << '\n';
}
} // namespace

Runner::Runner(Falcor::ref<Falcor::Device> device) : mpDevice(device), mpFence(device->createFence()) {}

void Runner::start(Case description)
{
    if (!description.samples || !description.instances || !description.width || !description.height || !description.queueDepth ||
        description.queueDepth > 32)
        throw std::runtime_error("Invalid benchmark case settings");
    if (mStarted && !mResult.complete)
        throw std::runtime_error("Previous benchmark case is unfinished");
    mResult = {std::move(description), {}, {}};
    mResult.samples.reserve(mResult.description.samples);
    mWarmupRemaining = mResult.description.warmup;
    mIssued = 0;
    if (mSlots.size() != mResult.description.queueDepth)
    {
        mSlots.clear();
        for (uint32_t i = 0; i < mResult.description.queueDepth; ++i)
            mSlots.push_back({Falcor::GpuTimer::create(mpDevice), Falcor::GpuTimer::create(mpDevice)});
    }
    mStarted = true;
}

void Runner::drain()
{
    while (mResult.samples.size() < mIssued)
    {
        auto& slot = mSlots[mResult.samples.size() % mSlots.size()];
        mpFence->wait(slot.fenceValue);
        const Sample sample{slot.draw->getElapsedTime(), slot.pass->getElapsedTime()};
        if (!(sample.drawMs > 0 && std::isfinite(sample.drawMs) && sample.passMs >= sample.drawMs && std::isfinite(sample.passMs)))
            throw std::runtime_error("Invalid GPU draw/pass sample");
        mResult.samples.push_back(sample);
    }
}

bool Runner::step(Falcor::RenderContext* context, const Work& work)
{
    if (!mStarted || mResult.complete)
        throw std::runtime_error("No pending benchmark case");
    // Retire queries in issue order. Wait only when reusing a full ring or draining.
    if (!mWarmupRemaining && mResult.samples.size() < mIssued)
    {
        auto& slot = mSlots[mResult.samples.size() % mSlots.size()];
        if (mIssued - mResult.samples.size() == mSlots.size() || mIssued == mResult.description.samples)
            mpFence->wait(slot.fenceValue);
        if (mpFence->getCurrentValue() >= slot.fenceValue)
        {
            const Sample sample{slot.draw->getElapsedTime(), slot.pass->getElapsedTime()};
            if (!(sample.drawMs > 0 && std::isfinite(sample.drawMs) && sample.passMs >= sample.drawMs && std::isfinite(sample.passMs)))
                throw std::runtime_error("Invalid GPU draw/pass sample");
            mResult.samples.push_back(sample);
        }
    }
    if (mWarmupRemaining)
    {
        work.clear();
        work.draw();
        work.finish();
        if (!--mWarmupRemaining)
            context->submit(true);
    }
    else if (mIssued < mResult.description.samples)
    {
        auto& slot = mSlots[mIssued % mSlots.size()];
        slot.pass->begin();
        work.clear();
        slot.draw->begin();
        work.draw();
        slot.draw->end();
        work.finish();
        slot.pass->end();
        slot.draw->resolve();
        slot.pass->resolve();
        context->submit(false);
        slot.fenceValue = context->signal(mpFence.get());
        ++mIssued;
        if (mSlots.size() == 1)
            drain();
    }
    else if (mResult.samples.size() == mResult.description.samples)
    {
        // Counter atomics and synchronous readback are outside every timed sample.
        mResult.counts = work.diagnose();
        mResult.complete = true;
    }
    return mResult.complete;
}

Writer::Writer(const std::filesystem::path& path, const Falcor::Device& device, const std::string& settings)
{
    if (path.extension() != ".csv" || path.stem().extension() == ".raw")
        throw std::runtime_error("Benchmark output must be a .csv basename, not a .raw.csv sidecar");
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    mRunId = number(std::chrono::system_clock::now().time_since_epoch().count());
    auto sidecar = path;
    for (auto* stream : {&mSummary, &mRaw, &mMetadata})
        stream->exceptions(std::ios::failbit | std::ios::badbit);
    mSummary.open(path);
    sidecar.replace_extension(".raw.csv");
    mRaw.open(sidecar);
    sidecar = path;
    sidecar.replace_extension(".metadata.txt");
    mMetadata.open(sidecar);
    writeRow(mSummary, summaryRow({}, {}, {}, mRunId), true);
    writeRow(mRaw, rawRow({}, 0, {}, mRunId), true);
    mMetadata << "Protocol: 4; bounded fence-retired draw/pass queries; separate diagnostics\nRunId: " << mRunId
              << "\nBuild: " << HELPERLANE_BUILD_CONFIG << "\nCompiler: " << HELPERLANE_COMPILER_DESC
              << "\nSource revision: " << HELPERLANE_GIT_REVISION << "\nCompiled source digest: " << HELPERLANE_SOURCE_DIGEST
              << "\nGPU: " << device.getInfo().adapterName << "\nAPI: " << device.getInfo().apiName
              << "\nGPU timestamp ms/tick: " << number(device.getGpuTimestampFrequency())
              << "\nTiming: indexed draw and enclosing pass; excludes query readback, diagnostics, UI and presentation."
              << "\nScopes, settings and mesh eligibility: recorded per case. Diagnostic counts are shader proxies."
              << "\nOptional CLI D3D12 pipeline statistics count a separate uninstrumented draw; availability recorded per case."
              << "\nInitial status: Running\n"
              << settings << '\n';
    mMetadata.flush();
}

Writer::~Writer()
{
    if (!mFinished)
    {
        try
        {
            finish(false, "Interrupted");
        }
        catch (...)
        {}
    }
}

void Writer::write(const Result& result)
{
    if (mFinished || result.description.id != mCases || result.samples.empty() || result.samples.size() > result.description.samples ||
        (result.complete && result.samples.size() != result.description.samples))
        throw std::runtime_error("Wrong case ownership or incomplete benchmark samples");
    std::vector<double> draw, pass;
    for (auto s : result.samples)
    {
        if (s.passMs < s.drawMs)
            throw std::runtime_error("Pass must enclose its draw");
        draw.push_back(s.drawMs);
        pass.push_back(s.passMs);
    }
    const auto drawStats = BenchmarkSupport::summarize(draw), passStats = BenchmarkSupport::summarize(pass);
    writeRow(mSummary, summaryRow(result, drawStats, passStats, mRunId));
    for (uint32_t i = 0; i < result.samples.size(); ++i)
        writeRow(mRaw, rawRow(result.description, i, result.samples[i], mRunId));
    mSummary.flush();
    mRaw.flush();
    ++mCases;
}

void Writer::finish(bool complete, const std::string& message)
{
    if (mFinished)
        return;
    mMetadata << "Cases: " << mCases << "\nStatus: " << (complete ? "Complete" : "Incomplete") << "\nMessage: " << message << '\n';
    mSummary.close();
    mRaw.close();
    mMetadata.close();
    mFinished = true;
}
} // namespace Benchmark
