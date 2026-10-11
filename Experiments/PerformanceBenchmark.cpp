#include "Falcor.h"
#include "Core/SampleApp.h"
#include "BenchmarkGeometry.h"
#include "BenchmarkOptions.h"
#include "Benchmark.h"
#include "RasterMetrics.h"
#include "Rendering/LitPreview.h"
#include "Triangulation/MeshValidation.h"
#include "Triangulation/3D/SurfaceFlipExperiment.h"
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
    double cpuMs = 0, referenceError = 0;
    uint32_t failed = 0, flips = 0;
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
    std::ofstream rasterOutput;
    ref<GraphicsState> state;
    ref<Texture> texture, counters;
    ref<Sampler> sampler;
    std::array<std::array<Pipeline, 2>, 2> pipelines;
    bool captured = false;
    std::vector<uint32_t> sourceIndices;
    LitPreviewSettings lighting;
    float3 cameraPosition;
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
        const SurfaceFlipExperiment::ReferenceSurface& reference
    )
    {
        Prepared p;
        p.name = name;
        auto start = Clock::now();
        if (name == "Source" || name == "SourceControl")
            p.mesh = Triangulation3D::useSourceMeshTriangulation(input);
        else if (name == "Heap3D" || name == "HeapPlane" || name == "HeapScreen" || name == "HeapQuad")
        {
            std::function<double(uint32_t, uint32_t)> edgeCost;
            if (name == "HeapScreen" || name == "HeapQuad")
            {
                // Optimize an expected-view cost, not the single view being measured.
                // Normalize pixels to source world-length units before blending.
                const uint32_t cols = uint32_t(std::ceil(std::sqrt(double(o.instances))));
                const uint32_t rows = (o.instances + cols - 1) / cols;
                const float radius = computeMaxDistanceFromOrigin(source.vertices);
                const auto vp = makeVolumeViewProj(o.width, o.height, cols, rows, 1, false, radius * 2.5f, radius);
                std::vector<std::vector<float2>> screens;
                for (uint32_t view = 0; view < o.views; ++view)
                {
                    const auto model = modelForView(view);
                    screens.emplace_back(source.vertices.size());
                    for (size_t i = 0; i < source.vertices.size(); ++i)
                        if (!projectToScreenSpace(
                                vp, transformPointByMatrix(model, source.vertices[i]), o.width, o.height, screens.back()[i]
                            ))
                            throw std::runtime_error("Optimization camera must keep every source vertex in front.");
                }
                auto projectedCost = [screens = std::move(screens), manhattan = name == "HeapQuad"](uint32_t a, uint32_t b)
                {
                    double cost = 0;
                    for (const auto& screen : screens)
                    {
                        const auto d = screen[a] - screen[b];
                        cost += manhattan ? std::abs(d.x) + std::abs(d.y) : std::sqrt(double(d.x) * d.x + double(d.y) * d.y);
                    }
                    return cost / screens.size();
                };
                double worldSum = 0, pixelSum = 0;
                for (auto [a, b] : makeEdgeSet(source.indices))
                {
                    worldSum += length(source.vertices[a] - source.vertices[b]);
                    pixelSum += projectedCost(a, b);
                }
                edgeCost = [projectedCost, scale = worldSum / pixelSum, &source](uint32_t a, uint32_t b)
                { return 0.25 * length(source.vertices[a] - source.vertices[b]) + 0.75 * scale * projectedCost(a, b); };
            }
            auto r = SurfaceFlipExperiment::optimize(source, reference, name == "HeapPlane" ? 4.0 : 0.0, edgeCost);
            p.mesh = std::move(r.mesh);
            p.flips = r.flips;
        }
        else
        {
            Triangulation3D::TriangulationOptions options;
            if (name == "CDT")
                options.method = Triangulation3D::Method::ConstrainedDelaunay;
            else if (name == "CDTFlip")
                options.method = Triangulation3D::Method::ConstrainedDelaunayFlipped;
            else if (name == "Greedy")
                options.method = Triangulation3D::Method::GreedyPointSet;
            else if (name == "LMT")
                options.method = Triangulation3D::Method::LmtMinimumWeight;
            else if (name == "GP")
                options.method = Triangulation3D::Method::PclGreedyProjection;
            else
                throw std::runtime_error("Unknown method: " + name);
            p.mesh = Triangulation3D::triangulateMesh(input, {}, options);
            p.failed = p.mesh.failedPatchCount;
        }
        p.cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        p.check = audit(p.mesh, source);
        p.referenceError = reference.sampledError(p.mesh);
        p.valid = p.mesh.success && p.mesh.indices.size() == source.indices.size() && p.check.topologyOK() && !p.failed &&
                  p.referenceError <= errorLimit * 1.001;
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
        std::cout << "Prepared " << name << ": " << p.cpuMs << " ms, " << p.mesh.indices.size() / 3 << " triangles, valid=" << p.valid
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
        c.mesh.failedPatches = p.failed;
        c.mesh.referenceError = p.referenceError;
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
            context->clearFbo(fbo.get(), float4(0), 1, 0);
            context->clearUAV(counters->getUAV().get(), uint4(0));
            if (o.lit)
                drawLit(context, fbo, p, params, 1);
            else
                draw(context, fbo, p, pipelines[stress][1], params);
            const auto bytes = context->readTextureSubresource(counters.get(), 0);
            const auto counts = reinterpret_cast<const uint32_t*>(bytes.data());
            return Benchmark::Counts{counts[0], counts[1]};
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
        if (!captured && p.name == "Source" && msaa == 1 && stress == 0)
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
        writer = std::make_unique<Benchmark::Writer>(
            o.output,
            *getDevice(),
            "Frontend: CLI; debug layer disabled\nSeed: 20261010; randomized method order per block/view/MSAA/shader"
            "\nGPU priming: all timed/diagnostic/MSAA pipelines, then 1 second wall time of timed draws per geometry"
            "\nColor: RGBA8Unorm; depth: D32Float matching MSAA; culling: None"
            "\nCPU scope: construction and per-flip guards; shared reference setup, final topology/reference audit and upload excluded."
            "\nReference budget: 0.1% of fixed source bounds diagonal; vertices, midpoints and centroids sampled."
            "\nHeapPlane: diagonal length reduction minus 4 * quad plane residual."
            "\nTexture: deterministic nonconstant 512x512 RGBA8, one mip, linear/wrap."
            "\nTexture shaders: 1 sample or 16 dependent samples; implicit LOD or explicit LOD 0 as selected."
            "\nEdge length: unique geometric edges per mesh; projected Euclidean pixel lengths summed over all instances, including "
            "occluded edges."
            "\nLit: existing GraphEx raster preview, fixed source normals, default single point light; early depth for opaque timed and "
            "diagnostic variants."
            "\nHeapScreen/HeapQuad: 25% world length + 75% normalized mean projected Euclidean/L1 length, fixed view set, central "
            "representative instance."
            "\nSourceControl: identical source geometry, independently uploaded, randomized with other methods."
            "\nMeshopt: selected index ordering only; vertex storage is fixed in every workload."
            "\nLit helpers: diagnostic raster quad-waste proxy; the timed lit shader has no derivatives/quad operations."
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
            auto source = Triangulation3D::useSourceMeshTriangulation(input);
            sourceIndices = source.indices;
            if (!source.success)
                throw std::runtime_error(
                    "Source mesh failed validation: " + shape + (source.messages.empty() ? "" : ": " + source.messages.front())
                );
            SurfaceFlipExperiment::ReferenceSurface reference(source);
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
                prepared.push_back(prepare(input, source, name, errorLimit, reference));
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
                            std::vector<size_t> order(prepared.size());
                            std::iota(order.begin(), order.end(), 0);
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
