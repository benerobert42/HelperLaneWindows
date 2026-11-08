#include "Falcor.h"
#include "Core/SampleApp.h"
#include "BenchmarkOptions.h"
#include "BenchmarkPreparation.h"
#include "PlanarBenchmark.h"
#include "SampleSupport.h"
#include "Benchmark.h"
#include "RasterMetrics.h"
#include "Rendering/LitPreview.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <atomic>
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
using BenchmarkPreparation::Params;
struct Prepared : BenchmarkPreparation::Prepared
{
    ref<Vao> vao;
    std::array<std::unique_ptr<LitPreview>, 2> lit;
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
    ref<DepthStencilState> defaultDepth, noDepth;
    ref<Texture> texture, counters;
    ref<Sampler> sampler;
    std::array<std::array<Pipeline, 2>, 2> pipelines;
    bool captured = false, caseStarted = false, complete = false, priming = false;
    struct Job
    {
        size_t prepared;
        uint32_t block, view, msaa, stress, instances;
    };
    std::vector<Prepared> prepared;
    std::shared_ptr<const MeshResult> sourcePreview;
    Prepared preview;
    Params previewParams{};
    float3 previewCamera;
    std::vector<Job> jobs;
    std::map<uint32_t, ref<Fbo>> framebuffers;
    std::mt19937 randomOrder{20261010};
    size_t shapeIndex = 0, jobIndex = 0, primeIndex = 0;
    float geometryRadius = 0;
    Params currentParams{};
    Params geometryParams{};
    ref<Texture> resolved;
    Clock::time_point primeStart;
    std::vector<uint32_t> sourceIndices;
    LitPreviewSettings lighting;
    float3 cameraPosition;
    std::atomic<bool> cancelPreparation{false};
    // Destroy/join the worker before any state it may still access.
    std::future<BenchmarkPreparation::Geometry> geometryPreparation;
    float4x4 modelForView(uint32_t view) const { return BenchmarkPreparation::modelForView(o, view); }
    uint32_t stressCount() const { return o.quick || o.lit || o.paperPlanar ? 1u : 2u; }
    Params paramsForJob(const Job& job) const
    {
        return BenchmarkPreparation::paramsForCase(o, geometryParams, job.view, job.instances);
    }
    void setTarget(const ref<Fbo>& fbo)
    {
        state->setFbo(fbo);
        state->setDepthStencilState(o.paperPlanar && !fbo->getDepthStencilTexture() ? noDepth : defaultDepth);
    }
    void setParams(Pipeline& pipeline, const Params& params)
    {
        if (o.paperPlanar)
        {
            const float cell = 1.f / params.gridCols;
            const GridParams grid{params.gridCols, params.gridCols, float2(cell), float2(0), cell};
            pipeline.vars->getRootVar()["Grid"].setBlob(grid);
        }
        else pipeline.vars->getRootVar()["Params"].setBlob(params);
    }
    ref<Vao> upload(const MeshResult& m)
    {
        if (m.indices.empty())
            return nullptr;
        const auto planar = o.paperPlanar ? PlanarBenchmark::prepareVertexStream(m) : PlanarBenchmark::VertexStream{};
        auto vb = getDevice()->createBuffer(
            m.vertices.size() * (o.paperPlanar ? sizeof(float2) : sizeof(float3)), ResourceBindFlags::Vertex, MemoryType::DeviceLocal,
            o.paperPlanar ? static_cast<const void*>(planar.vertices.data()) : m.vertices.data()
        );
        auto ib = getDevice()->createBuffer(
            m.indices.size() * sizeof(uint32_t), ResourceBindFlags::Index, MemoryType::DeviceLocal,
            o.paperPlanar ? planar.indices.data() : m.indices.data()
        );
        auto layout = VertexLayout::create();
        auto bufferLayout = VertexBufferLayout::create();
        bufferLayout->addElement("POSITION", 0, o.paperPlanar ? ResourceFormat::RG32Float : ResourceFormat::RGB32Float, 1, 0);
        layout->addBufferLayout(0, bufferLayout);
        return Vao::create(Vao::Topology::TriangleList, layout, {vb}, ib, ResourceFormat::R32Uint);
    }
    void draw(RenderContext* context, const ref<Fbo>& fbo, Prepared& p, Pipeline& pipe, const Params& params)
    {
        setTarget(fbo);
        state->setVao(p.vao);
        state->setProgram(pipe.program);
        setParams(pipe, params);
        context->drawIndexedInstanced(state.get(), pipe.vars.get(), uint32_t(p.mesh.indices.size()), params.instanceCount, 0, 0, 0);
    }
    void drawLit(RenderContext* context, const ref<Fbo>& fbo, Prepared& p, const Params& params, uint32_t diagnostic,
                 const std::vector<uint32_t>* normals = nullptr, const float3* eye = nullptr)
    {
        const VolumeGridParams grid{
            params.instanceCount, params.gridCols, (params.instanceCount + params.gridCols - 1) / params.gridCols,
            1, 0, params.gridSpacing, float2(0)};
        p.lit[diagnostic]->draw(
            context,
            fbo,
            p.vao->getVertexBuffer(0),
            p.vao->getIndexBuffer(),
            p.mesh.vertices,
            normals ? *normals : sourceIndices,
            uint32_t(p.mesh.indices.size()),
            {params.viewProj},
            {params.model},
            grid,
            eye ? *eye : cameraPosition,
            lighting
        );
    }
    bool measure(
        RenderContext* context,
        const ref<Fbo>& fbo,
        Prepared& p,
        const Params& params,
        const std::string& shape,
        uint32_t block,
        uint32_t view,
        uint32_t msaa,
        uint32_t stress,
        float radius
    )
    {
        if (!p.vao)
            return true;
        auto& pipe = pipelines[stress][0];
        if (!caseStarted)
        {
            Benchmark::Case c;
            c.id = writer->caseCount();
            c.geometry = shape;
            c.method = p.name;
            c.radius = radius;
            c.block = block;
            c.view = view;
            c.optimizationView = p.optimizationView;
            c.objectiveInstances = p.objectiveInstances;
            c.connectivitySeed = o.paperPlanar ? "Earcut" : p.name == "HeapQuadWarm" || p.name == "HeapQuadWarmLag" ? "Heap3D" : "Source";
            c.msaa = msaa;
            c.shader = o.paperPlanar ? "PaperPlanarTexture2K" : o.lit ? (o.litQuadWork == 1 ? "LitDdxNormal" : o.litQuadWork == 2 ? "LitTex2K" : o.litQuadWork == 3 ? "LitTex2K16" : "LitPreview") : stress ? "Material16" : "Texture1";
            if (o.explicitLod)
                c.shader += "ExplicitLOD";
            c.instances = params.instanceCount;
            c.width = o.width;
            c.height = o.height;
            c.depth = o.depth && (!o.paperPlanar || msaa == 1);
            c.meshopt = o.meshopt;
            c.warmup = o.warmup;
            c.samples = o.samples;
            c.queueDepth = o.queueDepth;
            c.gridCols = params.gridCols;
            c.gridRows = (params.instanceCount + params.gridCols - 1) / params.gridCols;
            const auto angles = BenchmarkOptions::rotationDegrees(o, view);
            c.xRotation = angles[0];
            c.yRotation = angles[1];
            c.zRotation = angles[2];
            c.passScope = "ClearDraw";
            c.cpuScope = "ConstructionExcludingFinalAuditUpload";
            c.mesh = p.metrics;
            c.mesh.projectedEdgeLength = o.rasterMetrics ? p.rasterMetrics.at(o.paperPlanar ? params.instanceCount : view).edgeLength : computeProjectedEdgeLengthScreenSpace(
                p.mesh.vertices,
                p.mesh.indices,
                params.model,
                params.instanceCount,
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
            resolved = msaa > 1 ? getDevice()->createTexture2D(
                o.width, o.height, o.paperPlanar ? ResourceFormat::RGBA16Float : ResourceFormat::RGBA8Unorm, 1, 1, nullptr,
                ResourceBindFlags::RenderTarget | ResourceBindFlags::ShaderResource) : nullptr;
            if (resolved)
                c.passScope = "ClearDrawResolve";
            runner->start(std::move(c));
            caseStarted = true;
        }
        Benchmark::Work work;
        // Constants and pipeline selection precede every draw timestamp.
        work.clear = [&]
        {
            context->clearFbo(fbo.get(), o.lit ? float4(lighting.background, 1) : float4(0), 1, 0);
            setTarget(fbo);
            state->setVao(p.vao);
            state->setProgram(pipe.program);
            setParams(pipe, params);
        };
        work.draw = [&]
        {
            if (o.lit)
                drawLit(context, fbo, p, params, 0);
            else
                context->drawIndexedInstanced(state.get(), pipe.vars.get(), uint32_t(p.mesh.indices.size()), params.instanceCount, 0, 0, 0);
        };
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
        if (!runner->step(context, work))
            return false;
        // Diagnostics overwrite the framebuffer. Restore the uninstrumented
        // image before presentation; this extra draw is outside all timestamps.
        work.clear();
        work.draw();
        work.finish();
        const auto& result = runner->result();
        writer->write(result);
        caseStarted = false;
        if (o.rasterMetrics)
            RasterMetrics::write(rasterOutput, result.description.id, o.ordering, p.rasterMetrics.at(o.paperPlanar ? params.instanceCount : view));
        if (!captured && p.name == o.captureMethod && msaa == 1 && stress == 0)
        {
            auto path = o.output;
            path.replace_extension(".png");
            (resolved ? resolved : fbo->getColorTexture(0))
                ->captureToFile(0, 0, path, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None, false);
            captured = true;
        }
        if (result.description.id % 16 == 0)
        {
            std::cout << "Case " << result.description.id << ": " << shape << " " << p.name << " " << result.description.shader << " "
                      << msaa << "x, last draw=" << result.samples.back().drawMs << " ms, H=" << result.counts.helpers
                      << ", P=" << result.counts.active << std::endl;
        }
        return true;
    }

public:
    PerformanceApp(const SampleAppConfig& config, Options options) : SampleApp(config), o(std::move(options)) {}
    void onLoad(RenderContext* context) override
    {
        getDevice()->getProfiler()->setEnabled(false);
        runner = std::make_unique<Benchmark::Runner>(getDevice());
        pipelineStatistics = std::make_unique<PipelineStatistics>(getDevice().get());
        if (std::filesystem::exists(o.output))
            throw std::runtime_error("Output already exists; use a new filename to preserve previous measurements: " + o.output.string());
        writer = std::make_unique<Benchmark::Writer>(o.output, *getDevice(), BenchmarkOptions::describeRun(o));
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
        defaultDepth = DepthStencilState::create(depth);
        depth.setDepthEnabled(false).setDepthWriteMask(false);
        noDepth = DepthStencilState::create(depth);
        state->setDepthStencilState(defaultDepth);
        const uint32_t textureSize = o.paperPlanar ? 2048 : 512;
        std::vector<uint32_t> pixels(textureSize * textureSize);
        uint32_t random = 173;
        for (auto& p : pixels)
        {
            random = random * 1664525u + 1013904223u;
            p = o.paperPlanar ? 0xFF0000FF : 0xff000000u | (random & 0x00ffffffu);
        }
        texture =
            getDevice()->createTexture2D(textureSize, textureSize, ResourceFormat::RGBA8Unorm, 1, 1, pixels.data(), ResourceBindFlags::ShaderResource);
        counters = getDevice()->createTexture2D(
            2, 1, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
        );
        sampler = make_ref<Sampler>(getDevice(), Sampler::Desc());
        for (uint32_t stress = 0; stress < stressCount(); ++stress)
            for (uint32_t diagnostic = 0; diagnostic < 2; ++diagnostic)
            {
                ProgramDesc desc;
                desc.addShaderLibrary(o.paperPlanar ? "Samples/HelperLaneWindows/Shaders/MainShader.slang" :
                                      "Samples/HelperLaneWindows/Shaders/PerfShader.slang").vsEntry("vsMain").psEntry("psMain");
                desc.setShaderModel(ShaderModel::SM6_6);
                auto& p = pipelines[stress][diagnostic];
                p.program = Program::create(getDevice(), desc);
                p.program->addDefine("MATERIAL_STRESS", std::to_string(stress));
                p.program->addDefine("DIAGNOSTICS", std::to_string(diagnostic));
                p.program->addDefine("EXPLICIT_LOD", std::to_string(o.explicitLod));
                if (o.paperPlanar)
                {
                    p.program->addDefine("PAPER_PLANAR_TEXTURE", "1");
                    p.program->addDefine("VIZ_MODE", diagnostic ? "0" : "2");
                    p.program->addDefine("READ_BACK_HELPER_LANE_COUNT", std::to_string(diagnostic));
                }
                p.vars = ProgramVars::create(getDevice(), p.program.get());
                if (!o.paperPlanar || !diagnostic)
                {
                    p.vars->setTexture(o.paperPlanar ? "gDummyTexture" : "gTexture", texture);
                    p.vars->getRootVar()[o.paperPlanar ? "gDummySampler" : "gSampler"] = sampler;
                }
                if (diagnostic)
                    p.vars->setTexture(o.paperPlanar ? "gHelperLaneCounter" : "gCounters", counters);
            }
        for (auto msaa : o.msaa)
        {
            Fbo::Desc desc;
            desc.setColorTarget(0, o.paperPlanar ? (msaa > 1 ? ResourceFormat::RGBA16Float : ResourceFormat::BGRA8UnormSrgb) :
                                                 ResourceFormat::RGBA8Unorm).setSampleCount(msaa);
            if (o.depth && (!o.paperPlanar || msaa == 1))
                desc.setDepthStencilTarget(ResourceFormat::D32Float);
            framebuffers.emplace(msaa, Fbo::create2D(getDevice(), o.width, o.height, desc));
        }
        toggleVsync(false);
    }

private:
    void finishPreparation(RenderContext* context)
    {
        for (auto& p : prepared)
        {
            const auto start = Clock::now();
            p.vao = upload(p.mesh);
            if (o.lit && p.vao)
            {
                p.lit[0] = std::make_unique<LitPreview>(nullptr, o.litQuadWork);
                p.lit[1] = std::make_unique<LitPreview>(counters, o.litQuadWork);
            }
            p.metrics.setupMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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
                            drawLit(context, fbo, p, geometryParams, diagnostic);
                        else
                            for (uint32_t stress = 0; stress < stressCount(); ++stress)
                                draw(context, fbo, p, pipelines[stress][diagnostic], geometryParams);
                    }
        context->submit(true);
        jobs.clear();
        jobIndex = 0;
        const std::vector<uint32_t> instanceCounts = o.paperPlanar ? std::vector<uint32_t>{1, 10000} : std::vector<uint32_t>{o.instances};
        for (uint32_t instances : instanceCounts)
        for (uint32_t block = 0; block < o.blocks; ++block)
        {
            std::vector<uint32_t> views(o.views);
            std::iota(views.begin(), views.end(), 0);
            if (o.axisSteps)
                std::shuffle(views.begin(), views.end(), randomOrder);
            for (uint32_t view : views)
            {
                for (uint32_t msaa : o.msaa)
                {
                    for (uint32_t stress = 0; stress < stressCount(); ++stress)
                    {
                        std::vector<size_t> order;
                        for (size_t index = 0; index < prepared.size(); ++index)
                            if (prepared[index].view < 0 || uint32_t(prepared[index].view) == view)
                                order.push_back(index);
                        std::shuffle(order.begin(), order.end(), randomOrder);
                        for (size_t index : order)
                            if (prepared[index].vao)
                                jobs.push_back({index, block, view, msaa, stress, instances});
                    }
                }
            }
        }
        if (jobs.empty())
            throw std::runtime_error("No drawable benchmark cases for " + o.shapes.at(shapeIndex));
        primeIndex = 0;
        priming = true;
        primeStart = Clock::now();
    }

    void renderPreparation(RenderContext* context, const ref<Fbo>& target)
    {
        context->clearFbo(target.get(), float4(lighting.background, 1), 1, 0);
        const auto source = std::atomic_load(&sourcePreview);
        if (!source) return;
        if (!preview.vao)
        {
            preview.mesh = *source;
            preview.vao = upload(preview.mesh);
            preview.lit[0] = std::make_unique<LitPreview>();
            const float radius = computeMaxDistanceFromOrigin(source->vertices);
            previewParams = {makeVolumeViewProj(o.width, o.height, 1, 1, 1, false, 2.5f * radius, radius, &previewCamera),
                             modelForView(0), 1, 2.5f * radius, 0, 1};
            getWindow()->setWindowTitle("Preparing triangulations - untimed source preview");
        }
        drawLit(context, target, preview, previewParams, 0, &preview.mesh.indices, &previewCamera);
    }
    void display(RenderContext* context, const ref<Fbo>& target, const ref<Texture>& color)
    {
        // Same-resolution transfer only. Never include display work in the
        // draw/pass timestamps or use a second scene draw for presentation.
        if (color->getFormat() == target->getColorTexture(0)->getFormat())
            context->copyResource(target->getColorTexture(0).get(), color.get());
        else
            context->blit(color->getSRV(), target->getRenderTargetView(0));
    }

public:
    void onFrameRender(RenderContext* context, const ref<Fbo>& target) override
    {
        try
        {
            if (target->getWidth() != o.width || target->getHeight() != o.height)
                throw std::runtime_error("Benchmark display is " + std::to_string(target->getWidth()) + "x" + std::to_string(target->getHeight()) +
                                         "; the requested benchmark is " + std::to_string(o.width) + "x" + std::to_string(o.height));
            if (geometryPreparation.valid())
            {
                if (geometryPreparation.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                {
                    renderPreparation(context, target);
                    return; // Keep processing window events while CPU preparation runs.
                }
                auto geometry = geometryPreparation.get();
                geometryParams = geometry.params;
                geometryRadius = geometry.radius;
                cameraPosition = geometry.cameraPosition;
                sourceIndices = std::move(geometry.sourceIndices);
                prepared.clear();
                for (auto& mesh : geometry.prepared)
                {
                    Prepared p;
                    static_cast<BenchmarkPreparation::Prepared&>(p) = std::move(mesh);
                    prepared.push_back(std::move(p));
                }
                preview = {};
                std::atomic_store(&sourcePreview, std::shared_ptr<const MeshResult>{});
                finishPreparation(context); // All graphics API calls stay on the app thread.
            }
            if (prepared.empty())
            {
                if (shapeIndex == o.shapes.size())
                {
                    if (o.rasterMetrics) rasterOutput.close();
                    writer->finish(true, "Onscreen benchmark complete.");
                    complete = true;
                    std::cout << "Benchmark complete: " << writer->caseCount() << " cases, " << o.output.string() << std::endl;
                    shutdown();
                    return;
                }
                getWindow()->setWindowTitle("Preparing triangulations: " + o.shapes[shapeIndex]);
                geometryPreparation = std::async(std::launch::async, [this]
                {
                    return BenchmarkPreparation::prepare(o, shapeIndex, cancelPreparation, [this](const MeshResult& source)
                    { std::atomic_store(&sourcePreview, std::make_shared<const MeshResult>(source)); });
                });
                context->clearFbo(target.get(), float4(0), 1, 0);
                return;
            }
            if (priming)
            {
                const auto& c = jobs.at(primeIndex++ % jobs.size());
                auto& p = prepared[c.prepared];
                currentParams = paramsForJob(c);
                const auto& fbo = framebuffers.begin()->second;
                context->clearFbo(fbo.get(), o.lit ? float4(lighting.background, 1) : float4(0), 1, 0);
                if (o.lit) drawLit(context, fbo, p, currentParams, 0);
                else draw(context, fbo, p, pipelines[c.stress][0], currentParams);
                if (framebuffers.begin()->first > 1)
                {
                    if (!resolved) resolved = getDevice()->createTexture2D(
                        o.width, o.height, o.paperPlanar ? ResourceFormat::RGBA16Float : ResourceFormat::RGBA8Unorm, 1, 1, nullptr,
                        ResourceBindFlags::RenderTarget | ResourceBindFlags::ShaderResource);
                    context->resolveResource(fbo->getColorTexture(0), resolved);
                }
                display(context, target, framebuffers.begin()->first > 1 ? resolved : fbo->getColorTexture(0));
                if (std::chrono::duration<double>(Clock::now() - primeStart).count() >= 1)
                {
                    context->submit(true);
                    priming = false;
                }
                return;
            }
            const auto& c = jobs.at(jobIndex);
            auto& p = prepared[c.prepared];
            if (!caseStarted)
            {
                currentParams = paramsForJob(c);
                getWindow()->setWindowTitle((o.paperPlanar ? "Planar benchmark: " : "Surface benchmark: ") + o.shapes[shapeIndex] + " / " + p.name +
                                           " / view " + std::to_string(c.view) + " / " + std::to_string(c.msaa) + "x MSAA");
            }
            const bool done = measure(context, framebuffers.at(c.msaa), p, currentParams, o.shapes[shapeIndex],
                                      c.block, c.view, c.msaa, c.stress, geometryRadius);
            display(context, target, resolved ? resolved : framebuffers.at(c.msaa)->getColorTexture(0));
            if (done && ++jobIndex == jobs.size())
            {
                // The outer SampleApp loop presents this final case before the
                // next callback prepares another geometry or closes the run.
                prepared.clear();
                ++shapeIndex;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Benchmark failed: " << e.what() << std::endl;
            preserveInterruptedCase();
            writer->finish(false, e.what());
            shutdown(1);
        }
    }
    bool onKeyEvent(const KeyboardEvent&) override { return true; }
    void onShutdown() override
    {
        cancelPreparation.store(true);
        if (writer && !complete)
        {
            preserveInterruptedCase();
            writer->finish(false, "Benchmark window closed before completion.");
        }
    }
private:
    void preserveInterruptedCase()
    {
        if (caseStarted)
        {
            runner->drain();
            if (!runner->result().samples.empty()) writer->write(runner->result());
            caseStarted = false;
        }
    }

};
} // namespace

int runPerformanceBenchmark(int argc, char** argv)
{
    auto options = parse(argc, argv);
    SampleAppConfig config;
    config.headless = false;
    config.showUI = false;
    config.colorFormat = options.paperPlanar ? ResourceFormat::BGRA8UnormSrgb : ResourceFormat::RGBA8Unorm;
    config.windowDesc.title = "Surface connectivity benchmark";
    config.windowDesc.resizableWindow = false;
    config.windowDesc.mode = Window::WindowMode::Fullscreen;
    config.windowDesc.enableVSync = false;
    config.windowDesc.width = options.width;
    config.windowDesc.height = options.height;
    config.deviceDesc.type = Device::Type::D3D12;
    config.deviceDesc.enableDebugLayer = false;
    config.deviceDesc.shaderCachePath = "";
    PerformanceApp app(config, std::move(options));
    return app.run();
}
