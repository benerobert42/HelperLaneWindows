#include "Falcor.h"
#include "Core/SampleApp.h"
#include "BenchmarkGeometry.h"
#include "BenchmarkOptions.h"
#include "Benchmark.h"
#include "RasterMetrics.h"
#include "Rendering/LitPreview.h"
#include "Triangulation/MeshValidation.h"
#include "Triangulation/3D/SurfaceFlipExperiment.h"
#include "Triangulation/3D/SurfaceStarExperiment.h"
#include "Triangulation/3D/Triangulation3D.h"
#include "meshoptimizer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <set>

namespace
{
using namespace Falcor;
using MeshResult = Triangulation3D::MeshTriangulationResult;
using Clock = std::chrono::steady_clock;
using BenchmarkOptions::Options;
using BenchmarkOptions::parse;
double length(float3 p)
{
    return std::sqrt(double(p.x) * p.x + double(p.y) * p.y + double(p.z) * p.z);
}
using MeshValidation::Audit;
using MeshValidation::audit;
struct Prepared
{
    std::string name;
    MeshResult mesh;
    Audit check;
    double cpuMs = 0, setupMs = 0, referenceError = 0;
    double reverseReferenceError = 0, referenceBudget = 0;
    bool surfaceEquivalent = true;
    std::string experimentClass = "Strict";
    bool structuralValid = false;
    uint32_t changedTriangles = 0;
    int32_t view = -1, optimizationView = -1;
    uint32_t objectiveInstances = 0;
    uint32_t failed = 0, flips = 0, starReplacements = 0;
    ref<Vao> vao;
    std::array<std::unique_ptr<LitPreview>, 2> lit;
    bool valid = false;
    std::map<uint32_t, RasterMetrics::Metrics> rasterMetrics;
};
struct Params
{
    float4x4 viewProj, model;
    uint32_t gridCols;
    float gridSpacing, gridCenter;
    uint32_t instanceCount;
};
struct Pipeline
{
    ref<Program> program;
    ref<ProgramVars> vars;
};

class PerformanceApp : public SampleApp
{
    Options o;
    std::unique_ptr<Benchmark::Runner> runner;
    std::unique_ptr<Benchmark::Writer> writer;
    std::unique_ptr<PipelineStatistics> pipelineStatistics;
    std::ofstream rasterOutput;
    ref<GraphicsState> state;
    ref<Texture> texture, counters;
    ref<Sampler> sampler;
    std::array<std::array<Pipeline, 2>, 2> pipelines;
    bool captured = false;
    std::vector<uint32_t> sourceIndices;
    LitPreviewSettings lighting;
    float3 cameraPosition;
    double referenceSetupMs = 0;
    double sourceSetupMs = 0, patchSetupMs = 0;
    std::vector<Triangulation3D::Patch> cachedPatches;
    uint32_t sourceUsed = 0;
    std::set<std::array<uint32_t, 3>> sourceTriangleSet;
    float4x4 modelForView(uint32_t view) const
    {
        const auto angles = BenchmarkOptions::rotationDegrees(o, view);
        return mul(mul(math::matrixFromRotationX(math::radians(float(angles[0]))),
                       math::matrixFromRotationY(math::radians(float(angles[1])))),
                   math::matrixFromRotationZ(math::radians(float(angles[2]))));
    }
    ref<Vao> upload(const MeshResult& m)
    {
        if (m.indices.empty())
            return nullptr;
        auto vb = getDevice()->createBuffer(
            m.vertices.size() * sizeof(float3), ResourceBindFlags::Vertex, MemoryType::DeviceLocal, m.vertices.data()
        );
        auto ib = getDevice()->createBuffer(
            m.indices.size() * sizeof(uint32_t), ResourceBindFlags::Index, MemoryType::DeviceLocal, m.indices.data()
        );
        auto layout = VertexLayout::create();
        auto bufferLayout = VertexBufferLayout::create();
        bufferLayout->addElement("POSITION", 0, ResourceFormat::RGB32Float, 1, 0);
        layout->addBufferLayout(0, bufferLayout);
        return Vao::create(Vao::Topology::TriangleList, layout, {vb}, ib, ResourceFormat::R32Uint);
    }
    Prepared prepare(
        const Triangulation3D::Mesh& input,
        const MeshResult& source,
        const std::string& name,
        double errorLimit,
        const SurfaceFlipExperiment::ReferenceSurface& reference,
        int32_t view = -1,
        const MeshResult* seed = nullptr
    )
    {
        Prepared p;
        p.name = name;
        p.view = view;
        p.optimizationView = view;
        if (name == "HeapQuadLag" || name == "HeapQuadWarmLag")
            p.optimizationView = (view / o.axisSteps) * o.axisSteps + (view + o.axisSteps - 1) % o.axisSteps;
        auto start = Clock::now();
        SurfaceFlipExperiment::Constraints constraints;
        if (name == "HeapLoose" || name == "StarLoose") constraints.relativeError = 0.005;
        if (name == "HeapWide" || name == "StarWide") constraints.relativeError = 0.02;
        p.referenceBudget = errorLimit * constraints.relativeError / 0.001;
        if (constraints.relativeError > 0.001) p.experimentClass = "RelaxedGeometry";
        if (name == "Source" || name == "SourceControl")
            p.mesh = source;
        else if (name == "TopologySplit")
        {
            p.mesh = BenchmarkGeometry::splitTorus(source);
            p.experimentClass = "TopologyChange";
            p.referenceBudget = errorLimit * 1000;
        }
        else if (name == "GridAligned" || name == "GridOpposite")
        {
            p.mesh = BenchmarkGeometry::rewriteGrid(input, source, name == "GridAligned");
            p.experimentClass = "GlobalConnectivity";
            p.referenceBudget = errorLimit * 20;
        }
        else if (name == "Star3D" || name == "StarLoose" || name == "StarWide")
        {
            auto heap = SurfaceFlipExperiment::optimize(source, reference, 0, {}, constraints);
            auto star = SurfaceStarExperiment::optimize(heap.mesh, reference, constraints);
            auto settled = SurfaceFlipExperiment::optimize(star.mesh, reference, 0, {}, constraints);
            p.mesh = std::move(settled.mesh);
            p.flips = heap.flips + settled.flips;
            p.starReplacements = star.replacements;
        }
        else if (name == "Heap3D" || name == "HeapPlane" || name == "HeapScreen" || name == "HeapQuad" ||
                 name == "HeapQuadSet" || name == "HeapLoose" || name == "HeapWide" || BenchmarkOptions::isViewMethod(name))
        {
            std::function<double(uint32_t, uint32_t)> edgeCost;
            if (name == "HeapScreen" || name == "HeapQuad" || name == "HeapQuadSet" || view >= 0)
            {
                // Static methods average the view set; View/Lag use one pose.
                // Normalize pixels to world units for the optimizer's tolerance.
                const uint32_t cols = uint32_t(std::ceil(std::sqrt(double(o.instances))));
                const uint32_t rows = (o.instances + cols - 1) / cols;
                const float radius = computeMaxDistanceFromOrigin(source.vertices);
                const auto vp = makeVolumeViewProj(o.width, o.height, cols, rows, 1, false, radius * 2.5f, radius);
                std::vector<std::vector<float2>> screens;
                p.objectiveInstances = name == "HeapScreen" || name == "HeapQuad" ? 1 : o.instances;
                const uint32_t begin = view < 0 ? 0 : uint32_t(p.optimizationView);
                const uint32_t end = view < 0 ? o.views : begin + 1;
                for (uint32_t objectiveView = begin; objectiveView < end; ++objectiveView)
                {
                    const auto model = modelForView(objectiveView);
                    for (uint32_t instance = 0; instance < p.objectiveInstances; ++instance)
                    {
                        const auto offset = p.objectiveInstances == 1 ? float3(0) :
                            getVolumeInstanceOffset(instance, cols, rows, 1, false, radius * 2.5f);
                        screens.emplace_back(source.vertices.size());
                        for (size_t i = 0; i < source.vertices.size(); ++i)
                            if (!projectToScreenSpace(
                                    vp, transformPointByMatrix(model, source.vertices[i]) + offset, o.width, o.height, screens.back()[i]
                                ))
                                throw std::runtime_error("Optimization camera must keep every source vertex in front.");
                    }
                }
                std::unordered_map<uint64_t, double> costs;
                costs.reserve(source.indices.size());
                auto projectedCost = [screens = std::move(screens), costs = std::move(costs), manhattan = name != "HeapScreen"]
                                     (uint32_t a, uint32_t b) mutable
                {
                    const uint64_t key = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
                    if (const auto it = costs.find(key); it != costs.end())
                        return it->second;
                    double cost = 0;
                    for (const auto& screen : screens)
                    {
                        const auto d = screen[a] - screen[b];
                        cost += manhattan ? std::abs(d.x) + std::abs(d.y) : std::sqrt(double(d.x) * d.x + double(d.y) * d.y);
                    }
                    return costs.emplace(key, cost / screens.size()).first->second;
                };
                double worldSum = 0, pixelSum = 0;
                for (auto [a, b] : makeEdgeSet(source.indices))
                {
                    worldSum += length(source.vertices[a] - source.vertices[b]);
                    pixelSum += projectedCost(a, b);
                }
                if (!(pixelSum > 0) || !std::isfinite(pixelSum))
                    throw std::runtime_error("Projected objective must have finite positive source edge length.");
                const double worldWeight = name == "HeapScreen" || name == "HeapQuad" ? 0.25 : 0.0;
                edgeCost = [projectedCost = std::move(projectedCost), scale = worldSum / pixelSum, worldWeight, &source]
                           (uint32_t a, uint32_t b) mutable
                { return worldWeight * length(source.vertices[a] - source.vertices[b]) + (1 - worldWeight) * scale * projectedCost(a, b); };
            }
            auto r = SurfaceFlipExperiment::optimize(seed ? *seed : source, reference, name == "HeapPlane" ? 4.0 : 0.0, edgeCost, constraints);
            p.mesh = std::move(r.mesh);
            p.flips = r.flips;
        }
        else
        {
            Triangulation3D::TriangulationOptions options;
            p.experimentClass = "PatchRetriangulation";
            p.referenceBudget = errorLimit * 20;
            options.use3DEdgeLengthForFlips = name == "CDTFlip3D" || name == "LMT3D";
            if (name == "CDT")
                options.method = Triangulation3D::Method::ConstrainedDelaunay;
            else if (name == "CDTFlip" || name == "CDTFlip3D")
                options.method = Triangulation3D::Method::ConstrainedDelaunayFlipped;
            else if (name == "Greedy")
                options.method = Triangulation3D::Method::GreedyPointSet;
            else if (name == "LMT" || name == "LMT3D")
                options.method = Triangulation3D::Method::LmtMinimumWeight;
            else if (name == "GP")
                options.method = Triangulation3D::Method::PclGreedyProjection;
            else
                throw std::runtime_error("Unknown method: " + name);
            p.mesh = Triangulation3D::triangulatePreparedMesh(input, source, cachedPatches, options);
            p.failed = p.mesh.failedPatchCount;
        }
        p.cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        p.check = audit(p.mesh, source);
        p.referenceError = reference.sampledError(p.mesh);
        if (p.mesh.indices != source.indices && !p.mesh.indices.empty())
            p.reverseReferenceError = SurfaceFlipExperiment::ReferenceSurface(p.mesh).sampledError(source);
        p.surfaceEquivalent = std::max(p.referenceError, p.reverseReferenceError) <= errorLimit * 1.001;
        p.structuralValid = p.mesh.success && p.mesh.indices.size() == source.indices.size() && p.check.topologyOK() &&
                            p.check.used == sourceUsed;
        p.valid = p.structuralValid && !p.failed &&
                  std::max(p.referenceError, p.reverseReferenceError) <= p.referenceBudget * 1.001;
        for (size_t i = 0; i < p.mesh.indices.size(); i += 3)
        {
            std::array<uint32_t, 3> triangle{p.mesh.indices[i], p.mesh.indices[i + 1], p.mesh.indices[i + 2]};
            std::sort(triangle.begin(), triangle.end());
            p.changedTriangles += !sourceTriangleSet.count(triangle);
        }
        if (o.meshopt && !p.mesh.indices.empty())
        {
            meshopt_optimizeVertexCache(p.mesh.indices.data(), p.mesh.indices.data(), p.mesh.indices.size(), p.mesh.vertices.size());
            if (o.ordering == "cache-overdraw")
                meshopt_optimizeOverdraw(
                    p.mesh.indices.data(),
                    p.mesh.indices.data(),
                    p.mesh.indices.size(),
                    &p.mesh.vertices[0].x,
                    p.mesh.vertices.size(),
                    sizeof(float3),
                    1.05f
                );
            // Keep vertex storage fixed in every workload to isolate index ordering.
        }
        p.vao = upload(p.mesh);
        if (o.lit && p.vao)
        {
            if (p.mesh.vertices.size() != source.vertices.size() ||
                !std::equal(
                    p.mesh.vertices.begin(),
                    p.mesh.vertices.end(),
                    source.vertices.begin(),
                    [](float3 a, float3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
                ))
                throw std::runtime_error("Lit comparisons require identical source vertex ordering.");
            p.lit[0] = std::make_unique<LitPreview>();
            p.lit[1] = std::make_unique<LitPreview>(counters);
        }
        p.setupMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        std::cout << "Prepared " << name << " view=" << view << ": " << p.cpuMs << " ms, setup=" << p.setupMs
                  << " ms, " << p.mesh.indices.size() / 3 << " triangles, valid=" << p.valid
                  << ", flips=" << p.flips << std::endl;
        return p;
    }
    void draw(RenderContext* context, const ref<Fbo>& fbo, Prepared& p, Pipeline& pipe, const Params& params)
    {
        state->setFbo(fbo);
        state->setVao(p.vao);
        state->setProgram(pipe.program);
        pipe.vars->getRootVar()["Params"].setBlob(params);
        context->drawIndexedInstanced(state.get(), pipe.vars.get(), uint32_t(p.mesh.indices.size()), o.instances, 0, 0, 0);
    }
    void drawLit(RenderContext* context, const ref<Fbo>& fbo, Prepared& p, const Params& params, uint32_t diagnostic)
    {
        const VolumeGridParams grid{
            o.instances, params.gridCols, (o.instances + params.gridCols - 1) / params.gridCols, 1, 0, params.gridSpacing, float2(0)};
        p.lit[diagnostic]->draw(
            context,
            fbo,
            p.vao->getVertexBuffer(0),
            p.vao->getIndexBuffer(),
            p.mesh.vertices,
            sourceIndices,
            uint32_t(p.mesh.indices.size()),
            {params.viewProj},
            {params.model},
            grid,
            cameraPosition,
            lighting
        );
    }
    void measure(
        RenderContext* context,
        const ref<Fbo>& fbo,
        Prepared& p,
        const Params& params,
        const std::string& shape,
        uint32_t sourceTriangles,
        uint32_t block,
        uint32_t view,
        uint32_t msaa,
        uint32_t stress,
        float radius
    )
    {
        if (!p.vao)
            return;
        auto& pipe = pipelines[stress][0];
        Benchmark::Case c;
        c.id = writer->caseCount();
        c.geometry = shape;
        c.method = p.name;
        c.radius = radius;
        c.block = block;
        c.view = view;
        c.optimizationView = p.optimizationView;
        c.objectiveInstances = p.objectiveInstances;
        c.connectivitySeed = p.name == "HeapQuadWarm" || p.name == "HeapQuadWarmLag" ? "Heap3D" : "Source";
        c.msaa = msaa;
        c.shader = o.lit ? "LitPreview" : stress ? "Material16" : "Texture1";
        if (o.explicitLod)
            c.shader += "ExplicitLOD";
        c.instances = o.instances;
        c.width = o.width;
        c.height = o.height;
        c.depth = o.depth;
        c.meshopt = o.meshopt;
        c.warmup = o.warmup;
        c.samples = o.samples;
        c.queueDepth = o.queueDepth;
        c.gridCols = params.gridCols;
        c.gridRows = (o.instances + params.gridCols - 1) / params.gridCols;
        const auto angles = BenchmarkOptions::rotationDegrees(o, view);
        c.xRotation = angles[0];
        c.yRotation = angles[1];
        c.zRotation = angles[2];
        c.passScope = "ClearDraw";
        c.cpuScope = "ConstructionExcludingFinalAuditUpload";
        c.mesh.audit = p.check;
        c.mesh.vertices = uint32_t(p.mesh.vertices.size());
        c.mesh.triangles = uint32_t(p.mesh.indices.size() / 3);
        c.mesh.sourceTriangles = sourceTriangles;
        c.mesh.cpuMs = p.cpuMs;
        c.mesh.setupMs = p.setupMs;
        c.mesh.referenceSetupMs = referenceSetupMs;
        c.mesh.sourceSetupMs = sourceSetupMs;
        c.mesh.patchSetupMs = patchSetupMs;
        c.mesh.referenceAuditDirections = 2;
        c.mesh.failedPatches = p.failed;
        c.mesh.referenceError = p.referenceError;
        c.mesh.reverseReferenceError = p.reverseReferenceError;
        c.mesh.referenceBudget = p.referenceBudget;
        c.mesh.surfaceEquivalent = p.surfaceEquivalent;
        c.mesh.experimentClass = p.experimentClass;
        c.mesh.starReplacements = p.starReplacements;
        c.mesh.triangulationStatus = int32_t(p.mesh.status);
        c.mesh.patchCount = uint32_t(p.mesh.patches.size());
        c.mesh.changedTriangles = p.changedTriangles;
        c.mesh.structuralValid = p.structuralValid;
        c.mesh.flips = p.flips;
        c.mesh.valid = p.valid;
        c.mesh.projectedEdgeLength = o.rasterMetrics ? p.rasterMetrics.at(view).edgeLength : computeProjectedEdgeLengthScreenSpace(
            p.mesh.vertices,
            p.mesh.indices,
            params.model,
            o.instances,
            o.width,
            o.height,
            params.gridCols,
            o.lit ? c.gridRows : params.gridCols,
            1,
            false,
            params.gridSpacing,
            radius,
            &params.viewProj
        );
        Benchmark::Work work;
        // Constants and pipeline selection precede every draw timestamp.
        work.clear = [&]
        {
            context->clearFbo(fbo.get(), o.lit ? float4(lighting.background, 1) : float4(0), 1, 0);
            state->setFbo(fbo);
            state->setVao(p.vao);
            state->setProgram(pipe.program);
            pipe.vars->getRootVar()["Params"].setBlob(params);
        };
        work.draw = [&]
        {
            if (o.lit)
                drawLit(context, fbo, p, params, 0);
            else
                context->drawIndexedInstanced(state.get(), pipe.vars.get(), uint32_t(p.mesh.indices.size()), o.instances, 0, 0, 0);
        };
        auto resolved = msaa > 1 && o.lit ? getDevice()->createTexture2D(
                                                o.width,
                                                o.height,
                                                ResourceFormat::RGBA8Unorm,
                                                1,
                                                1,
                                                nullptr,
                                                ResourceBindFlags::RenderTarget | ResourceBindFlags::ShaderResource
                                            )
                                          : nullptr;
        if (resolved)
            c.passScope = "ClearDrawResolve";
        work.finish = [&]
        {
            if (resolved)
                context->resolveResource(fbo->getColorTexture(0), resolved);
        };
        work.diagnose = [&]
        {
            work.clear();
            const bool queried = pipelineStatistics->begin(context);
            work.draw();
            if (queried) pipelineStatistics->endResolve(context);
            context->clearFbo(fbo.get(), float4(0), 1, 0);
            context->clearUAV(counters->getUAV().get(), uint4(0));
            if (o.lit)
                drawLit(context, fbo, p, params, 1);
            else
                draw(context, fbo, p, pipelines[stress][1], params);
            const auto bytes = context->readTextureSubresource(counters.get(), 0);
            const auto counts = reinterpret_cast<const uint32_t*>(bytes.data());
            // Texture readback above waits for the preceding native query resolve.
            return Benchmark::Counts{counts[0], counts[1], queried ? pipelineStatistics->read() : PipelineCounts{}};
        };
        runner->start(std::move(c));
        bool complete;
        try
        {
            do
            {
                complete = runner->step(context, work);
                getDevice()->endFrame();
            } while (!complete);
        }
        catch (...)
        {
            runner->drain();
            if (!runner->result().samples.empty())
                writer->write(runner->result());
            writer->finish(false, "Measurement failed.");
            throw;
        }
        const auto& result = runner->result();
        writer->write(result);
        if (o.rasterMetrics)
            RasterMetrics::write(rasterOutput, result.description.id, o.ordering, p.rasterMetrics.at(view));
        if (!captured && p.name == o.captureMethod && msaa == 1 && stress == 0)
        {
            // Diagnostics alter the target: capture the original uninstrumented workload.
            work.clear();
            work.draw();
            work.finish();
            auto path = o.output;
            path.replace_extension(".png");
            (resolved ? resolved : fbo->getColorTexture(0))
                ->captureToFile(0, 0, path, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None, false);
            getDevice()->endFrame();
            captured = true;
        }
        if (result.description.id % 16 == 0)
        {
            std::cout << "Case " << result.description.id << ": " << shape << " " << p.name << " " << result.description.shader << " "
                      << msaa << "x, last draw=" << result.samples.back().drawMs << " ms, H=" << result.counts.helpers
                      << ", P=" << result.counts.active << std::endl;
        }
    }

public:
    PerformanceApp(const SampleAppConfig& config, Options options) : SampleApp(config), o(std::move(options)) {}
    void onLoad(RenderContext* context) override
    {
        getDevice()->getProfiler()->setEnabled(false);
        runner = std::make_unique<Benchmark::Runner>(getDevice());
        pipelineStatistics = std::make_unique<PipelineStatistics>(getDevice().get());
        writer = std::make_unique<Benchmark::Writer>(
            o.output,
            *getDevice(),
            "Frontend: CLI; debug layer disabled\nSeed: 20261010; randomized method order per block/view/MSAA/shader"
            "\nGPU priming: all timed/diagnostic/MSAA pipelines, then 1 second wall time of timed draws per geometry"
            "\nColor: RGBA8Unorm; depth: D32Float matching MSAA; culling: None"
            "\nCPU scope: construction and per-flip guards; shared reference setup, final topology/reference audit and upload excluded."
            "\nSetupCPU_ms: CLI preparation through audit, index ordering and VAO/buffer creation; excludes shared source/reference "
            "setup, lazy lighting/PSO initialization, GPU upload completion and diagnostics. GUI reports zero (unmeasured)."
            "\nReferenceSetupCPU_ms: shared fixed-source reference construction once per geometry; reuse across view updates."
            "\nSourceSetupCPU_ms/PatchSetupCPU_ms: shared canonical source/patch growth once per geometry; backend construction reuses both."
            "\nReferenceAuditDirections: CLI 2; GUI volume 1; 0 unmeasured. GUI zero setup/reverse-error fields are unmeasured."
            "\nReference budget: 0.1% of fixed source bounds diagonal; vertices, midpoints and centroids sampled."
            "\nCLI final audit samples both candidate-to-source and source-to-candidate; this is not a Hausdorff guarantee."
            "\nPatch methods: explicit 2% audit ceiling; SurfaceEquivalent separately tests 0.1%; status 0 failed, 1 complete, 2 source fallback."
            "\nCDTFlip3D/LMT3D opt into existing world-edge flip scoring; default CDTFlip/LMT use projected patch lengths."
            "\nHeapLoose/HeapWide: explicit 0.5%/2% bounds-relative error budgets; same 30 degree normal guards."
            "\nStar3D/Loose/Wide: world heap, then up to three convex vertex-star DP sweeps, then world heap; fixed counts."
            "\nTopologySplit: torus fixture only; reclose two half-tubes as two tori, fixed vertex/triangle counts, changed surface."
            "\nGridAligned/GridOpposite: whole procedural grid rewiring/opposite cell diagonals; fixed positions/counts, 2% error budget."
            "\nSurfaceEquivalent records the original 0.1% sampled budget; non-equivalent cases excluded from ValidForRanking."
            "\nHeapPlane: diagonal length reduction minus 4 * quad plane residual."
            "\nTexture: deterministic nonconstant 512x512 RGBA8, one mip, linear/wrap."
            "\nTexture shaders: 1 sample or 16 dependent samples; implicit LOD or explicit LOD 0 as selected."
            "\nEdge length: unique geometric edges per mesh; projected Euclidean pixel lengths summed over all instances, including "
            "occluded edges."
            "\nLit: existing GraphEx raster preview, fixed source normals, default single point light; early depth for opaque timed and "
            "diagnostic variants."
            "\nHeapScreen/HeapQuad: 25% world length + 75% normalized mean projected Euclidean/L1 length, fixed view set, central "
            "representative instance."
            "\nHeapQuadSet: pure normalized mean projected L1 length over the fixed view set and all actual instances."
            "\nHeapQuadView/HeapQuadLag: pure projected L1 length for the current/previous axis pose; variants prepared before "
            "GPU timing, averaging all actual instances. Lag tests one angular step of stale connectivity. OptimizationView "
            "records the training pose; ObjectiveInstances records the projection count. Fixed edge scores cached per run."
            "\nHeapQuadWarm/WarmLag: corresponding current/previous-pose pure cost initialized from the prepared Heap3D "
            "indices; fixed original reference/guards retained. CPU times exclude cached seed preparation; Flips count "
            "additional flips. Requires Heap3D before warm methods."
            "\nSourceControl: identical source geometry, independently uploaded, randomized with other methods."
            "\nMeshopt: selected index ordering only; vertex storage is fixed in every workload."
            "\nLit helpers: diagnostic raster quad-waste proxy; the timed lit shader has no derivatives/quad operations."
            "\nHardware pipeline statistics: separate uninstrumented draw after timing, same clear/depth; no query around timed draws."
            "\nPSInvocations are API-reported invocations, not a guarantee of useful pixels or helper-lane instruction work."
            "\nRaster metrics: pre-depth, unclipped projected triangles; area/shape include occluded faces."
            "\nCache simulation: meshoptimizer cache=16, warp=32, primitive group=128; not hardware counters."
            "\nOrdering: " + o.ordering + "\nExplicit texture LOD 0: " + std::to_string(o.explicitLod) +
            "\nSeparate-axis rotation steps: " + std::to_string(o.axisSteps) +
            "; model = Rx * Ry * Rz; randomized view order for axis sweeps."
        );
        if (o.rasterMetrics)
        {
            auto path = o.output;
            path.replace_extension(".raster.csv");
            rasterOutput.exceptions(std::ios::failbit | std::ios::badbit);
            rasterOutput.open(path);
            RasterMetrics::writeHeader(rasterOutput);
        }
        const auto info = getDevice()->getInfo();
        std::cout << "GPU: " << info.adapterName << " (" << info.apiName << ")" << std::endl;
        state = GraphicsState::create(getDevice());
        RasterizerState::Desc raster;
        raster.setCullMode(RasterizerState::CullMode::None);
        state->setRasterizerState(RasterizerState::create(raster));
        DepthStencilState::Desc depth;
        depth.setDepthEnabled(o.depth).setDepthWriteMask(o.depth).setDepthFunc(ComparisonFunc::Less);
        state->setDepthStencilState(DepthStencilState::create(depth));
        std::vector<uint32_t> pixels(512 * 512);
        uint32_t random = 173;
        for (auto& p : pixels)
        {
            random = random * 1664525u + 1013904223u;
            p = 0xff000000u | (random & 0x00ffffffu);
        }
        texture =
            getDevice()->createTexture2D(512, 512, ResourceFormat::RGBA8Unorm, 1, 1, pixels.data(), ResourceBindFlags::ShaderResource);
        counters = getDevice()->createTexture2D(
            2, 1, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
        );
        sampler = make_ref<Sampler>(getDevice(), Sampler::Desc());
        for (uint32_t stress = 0; stress < 2; ++stress)
            for (uint32_t diagnostic = 0; diagnostic < 2; ++diagnostic)
            {
                ProgramDesc desc;
                desc.addShaderLibrary("Samples/HelperLaneWindows/Shaders/PerfShader.slang").vsEntry("vsMain").psEntry("psMain");
                desc.setShaderModel(ShaderModel::SM6_6);
                auto& p = pipelines[stress][diagnostic];
                p.program = Program::create(getDevice(), desc);
                p.program->addDefine("MATERIAL_STRESS", std::to_string(stress));
                p.program->addDefine("DIAGNOSTICS", std::to_string(diagnostic));
                p.program->addDefine("EXPLICIT_LOD", std::to_string(o.explicitLod));
                p.vars = ProgramVars::create(getDevice(), p.program.get());
                p.vars->setTexture("gTexture", texture);
                p.vars->getRootVar()["gSampler"] = sampler;
                if (diagnostic)
                    p.vars->setTexture("gCounters", counters);
            }
        std::map<uint32_t, ref<Fbo>> framebuffers;
        for (auto msaa : o.msaa)
        {
            Fbo::Desc desc;
            desc.setColorTarget(0, ResourceFormat::RGBA8Unorm).setSampleCount(msaa);
            if (o.depth)
                desc.setDepthStencilTarget(ResourceFormat::D32Float);
            framebuffers.emplace(msaa, Fbo::create2D(getDevice(), o.width, o.height, desc));
        }
        std::mt19937 randomOrder(20261010);
        for (const auto& shape : o.shapes)
        {
            auto input = BenchmarkGeometry::make(shape);
            const auto sourceStart = Clock::now();
            auto source = Triangulation3D::useSourceMeshTriangulation(input);
            sourceSetupMs = std::chrono::duration<double, std::milli>(Clock::now() - sourceStart).count();
            cachedPatches.clear();
            patchSetupMs = 0;
            if (std::any_of(o.methods.begin(), o.methods.end(), [](const std::string& method)
                           { return method.rfind("CDT", 0) == 0 || method.rfind("LMT", 0) == 0 || method == "Greedy" || method == "GP"; }))
            {
                const auto patchStart = Clock::now();
                cachedPatches = Triangulation3D::growPatches(input);
                patchSetupMs = std::chrono::duration<double, std::milli>(Clock::now() - patchStart).count();
            }
            sourceIndices = source.indices;
            sourceUsed = audit(source, source).used;
            sourceTriangleSet.clear();
            for (size_t i = 0; i < source.indices.size(); i += 3)
            {
                std::array<uint32_t, 3> triangle{source.indices[i], source.indices[i + 1], source.indices[i + 2]};
                std::sort(triangle.begin(), triangle.end());
                sourceTriangleSet.insert(triangle);
            }
            if (!source.success)
                throw std::runtime_error(
                    "Source mesh failed validation: " + shape + (source.messages.empty() ? "" : ": " + source.messages.front())
                );
            const auto referenceStart = Clock::now();
            SurfaceFlipExperiment::ReferenceSurface reference(source);
            referenceSetupMs = std::chrono::duration<double, std::milli>(Clock::now() - referenceStart).count();
            float3 lo = source.vertices.front(), hi = lo;
            float radius = 0;
            for (auto p : source.vertices)
            {
                lo = math::min(lo, p);
                hi = math::max(hi, p);
                radius = std::max(radius, float(length(p)));
            }
            double errorLimit = length(hi - lo) * 0.001;
            std::cout << "Geometry " << shape << ": " << source.vertices.size() << " vertices, error budget " << errorLimit << std::endl;
            std::vector<Prepared> prepared;
            for (const auto& name : o.methods)
            {
                const bool warmStart = name == "HeapQuadWarm" || name == "HeapQuadWarmLag";
                size_t seedIndex = 0;
                if (warmStart)
                {
                    const auto seed = std::find_if(prepared.begin(), prepared.end(), [](const Prepared& p) { return p.name == "Heap3D"; });
                    if (seed == prepared.end() || !seed->structuralValid || seed->failed)
                        throw std::runtime_error("Warm projected methods require a structurally valid Heap3D earlier in --methods.");
                    seedIndex = size_t(seed - prepared.begin());
                }
                if (BenchmarkOptions::isViewMethod(name))
                    for (uint32_t view = 0; view < o.views; ++view)
                        prepared.push_back(prepare(input, source, name, errorLimit, reference, int32_t(view),
                                                   warmStart ? &prepared[seedIndex].mesh : nullptr));
                else
                    prepared.push_back(prepare(input, source, name, errorLimit, reference));
            }
            uint32_t cols = uint32_t(std::ceil(std::sqrt(double(o.instances)))), rows = (o.instances + cols - 1) / cols;
            float spacing = radius * 2.5f;
            const auto vp = makeVolumeViewProj(o.width, o.height, cols, rows, 1, false, spacing, radius, &cameraPosition);
            const Params prime{
                vp,
                modelForView(0),
                cols,
                spacing,
                0.5f * float(cols - 1),
                o.instances};
            // Geometry diagnostics run once per method/view before GPU priming.
            // No CPU projection work is inserted between timed blocks.
            if (o.rasterMetrics)
                for (auto& p : prepared)
                    if (p.vao)
                        for (uint32_t view = 0; view < o.views; ++view)
                        {
                            if (p.view >= 0 && uint32_t(p.view) != view)
                                continue;
                            const auto model = modelForView(view);
                            p.rasterMetrics.emplace(view, RasterMetrics::measure(p.mesh, model, vp, o.width, o.height, o.instances, cols, spacing));
                        }
            // Build every PSO and normal buffer before timing; diagnostics do not
            // first compile between measured cases. Then sustain GPU activity.
            for (auto& p : prepared)
                if (p.vao)
                    for (const auto& [samples, fbo] : framebuffers)
                        for (uint32_t diagnostic = 0; diagnostic < 2; ++diagnostic)
                        {
                            context->clearFbo(fbo.get(), float4(0), 1, 0);
                            context->clearUAV(counters->getUAV().get(), uint4(0));
                            if (o.lit)
                                drawLit(context, fbo, p, prime, diagnostic);
                            else
                                for (auto& shader : pipelines)
                                    draw(context, fbo, p, shader[diagnostic], prime);
                            getDevice()->endFrame();
                        }
            auto primeStart = Clock::now();
            do
            {
                for (auto& p : prepared)
                    if (p.vao)
                    {
                        const auto& fbo = framebuffers.begin()->second;
                        context->clearFbo(fbo.get(), float4(0), 1, 0);
                        if (o.lit)
                            drawLit(context, fbo, p, prime, 0);
                        else
                            for (auto& shader : pipelines)
                                draw(context, fbo, p, shader[0], prime);
                        getDevice()->endFrame();
                    }
            } while (std::chrono::duration<double>(Clock::now() - primeStart).count() < 1);
            context->submit(true);
            for (uint32_t block = 0; block < o.blocks; ++block)
            {
                std::vector<uint32_t> views(o.views);
                std::iota(views.begin(), views.end(), 0);
                if (o.axisSteps)
                    std::shuffle(views.begin(), views.end(), randomOrder);
                for (uint32_t view : views)
                {
                    Params params{
                        vp,
                        modelForView(view),
                        cols,
                        spacing,
                        0.5f * float(cols - 1),
                        o.instances};
                    for (uint32_t msaa : o.msaa)
                    {
                        const auto& fbo = framebuffers.at(msaa);
                        for (uint32_t stress = 0; stress < (o.quick || o.lit ? 1u : 2u); ++stress)
                        {
                            std::vector<size_t> order;
                            for (size_t index = 0; index < prepared.size(); ++index)
                                if (prepared[index].view < 0 || uint32_t(prepared[index].view) == view)
                                    order.push_back(index);
                            std::shuffle(order.begin(), order.end(), randomOrder);
                            for (size_t index : order)
                                measure(
                                    context,
                                    fbo,
                                    prepared[index],
                                    params,
                                    shape,
                                    uint32_t(source.indices.size() / 3),
                                    block,
                                    view,
                                    msaa,
                                    stress,
                                    radius
                                );
                        }
                    }
                }
            }
        }
        if (o.rasterMetrics)
            rasterOutput.close();
        writer->finish(true, "Benchmark complete.");
        std::cout << "Benchmark complete: " << writer->caseCount() << " cases, " << o.output.string() << std::endl;
        shutdown();
    }
};
} // namespace

int runPerformanceBenchmark(int argc, char** argv)
{
    auto options = parse(argc, argv);
    SampleAppConfig config;
    config.headless = true;
    config.showUI = false;
    config.windowDesc.width = options.width;
    config.windowDesc.height = options.height;
    config.deviceDesc.type = Device::Type::D3D12;
    config.deviceDesc.enableDebugLayer = false;
    config.deviceDesc.shaderCachePath = "";
    PerformanceApp app(config, std::move(options));
    return app.run();
}
