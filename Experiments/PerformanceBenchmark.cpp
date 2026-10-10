#include "Falcor.h"
#include "Core/SampleApp.h"
#include "Core/API/GpuTimer.h"
#include "BenchmarkGeometry.h"
#include "BenchmarkOptions.h"
#include "BenchmarkSupport.h"
#include "Triangulation/MeshValidation.h"
#include "Triangulation/3D/SurfaceFlipExperiment.h"
#include "Triangulation/3D/Triangulation3D.h"
#include "meshoptimizer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
using namespace Falcor;
using MeshResult = Triangulation3D::MeshTriangulationResult;
using Edge = std::pair<uint32_t, uint32_t>;
using Clock = std::chrono::steady_clock;
using BenchmarkOptions::Options;
using BenchmarkOptions::parse;
Edge edge(uint32_t a, uint32_t b)
{
    return {std::min(a, b), std::max(a, b)};
}
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
    bool valid = false;
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
    std::ofstream summary, raw;
    ref<GraphicsState> state;
    ref<Texture> texture, counters;
    ref<Sampler> sampler;
    ref<GpuTimer> timer, passTimer;
    std::array<std::array<Pipeline, 2>, 2> pipelines;
    uint32_t caseId = 0;
    std::string runId = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    bool captured = false;
    bool geometryPrimed = false;
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
        if (name == "Source")
            p.mesh = Triangulation3D::useSourceMeshTriangulation(input);
        else if (name == "Heap3D" || name == "HeapPlane")
        {
            auto r = SurfaceFlipExperiment::optimize(source, reference, name == "HeapPlane" ? 4.0 : 0.0);
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
            meshopt_optimizeOverdraw(
                p.mesh.indices.data(),
                p.mesh.indices.data(),
                p.mesh.indices.size(),
                &p.mesh.vertices[0].x,
                p.mesh.vertices.size(),
                sizeof(float3),
                1.05f
            );
            meshopt_optimizeVertexFetch(
                p.mesh.vertices.data(),
                p.mesh.indices.data(),
                p.mesh.indices.size(),
                p.mesh.vertices.data(),
                p.mesh.vertices.size(),
                sizeof(float3)
            );
        }
        p.vao = upload(p.mesh);
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
        uint32_t stress
    )
    {
        if (!p.vao)
            return;
        auto& pipe = pipelines[stress][0];
        if (!geometryPrimed)
        {
            // Keep the GPU busy after CPU triangulation and pipeline creation.
            // An identical no-flip mesh remains a control for residual clock
            // and cache variation; warmup does not guarantee fixed GPU clocks.
            auto start = Clock::now();
            do
            {
                for (auto& shader : pipelines)
                {
                    context->clearFbo(fbo.get(), float4(0), 1, 0);
                    draw(context, fbo, p, shader[0], params);
                    getDevice()->endFrame();
                }
            } while (std::chrono::duration<double, std::milli>(Clock::now() - start).count() < 200);
            context->submit(true);
            geometryPrimed = true;
        }
        for (uint32_t i = 0; i < o.warmup; ++i)
        {
            context->clearFbo(fbo.get(), float4(0), 1, 0);
            draw(context, fbo, p, pipe, params);
            getDevice()->endFrame();
        }
        context->submit(true);
        std::vector<double> times, passTimes;
        times.reserve(o.samples);
        passTimes.reserve(o.samples);
        for (uint32_t i = 0; i < o.samples; ++i)
        {
            passTimer->begin();
            context->clearFbo(fbo.get(), float4(0), 1, 0);
            // Set constants and state before timestamping; pipeline is warmed.
            state->setFbo(fbo);
            state->setVao(p.vao);
            state->setProgram(pipe.program);
            pipe.vars->getRootVar()["Params"].setBlob(params);
            timer->begin();
            context->drawIndexedInstanced(state.get(), pipe.vars.get(), uint32_t(p.mesh.indices.size()), o.instances, 0, 0, 0);
            timer->end();
            passTimer->end();
            timer->resolve();
            passTimer->resolve();
            context->submit(true);
            double value = timer->getElapsedTime();
            double passValue = passTimer->getElapsedTime();
            if (!(value > 0 && std::isfinite(value)))
                throw std::runtime_error("Missing/invalid GPU timer sample.");
            if (!(passValue >= value && std::isfinite(passValue)))
                throw std::runtime_error("Invalid nested GPU pass timing.");
            times.push_back(value);
            passTimes.push_back(passValue);
            getDevice()->endFrame();
        }
        if (!captured && p.name == "Source" && msaa == 1 && stress == 0)
        {
            auto path = o.output;
            path.replace_extension(".png");
            fbo->getColorTexture(0)->captureToFile(0, 0, path, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None, false);
            captured = true;
        }
        // Instrumentation runs separately and is never included in timed draws.
        context->clearFbo(fbo.get(), float4(0), 1, 0);
        context->clearUAV(counters->getUAV().get(), uint4(0));
        draw(context, fbo, p, pipelines[stress][1], params);
        auto bytes = context->readTextureSubresource(counters.get(), 0);
        const uint32_t* counts = reinterpret_cast<const uint32_t*>(bytes.data());
        uint32_t helpers = counts[0], active = counts[1];
        getDevice()->endFrame();
        const auto drawStatistics = BenchmarkSupport::summarize(times);
        const auto passStatistics = BenchmarkSupport::summarize(passTimes);
        const auto median = drawStatistics.median;
        for (uint32_t i = 0; i < times.size(); ++i)
            raw << caseId << "," << i << "," << times[i] << "," << passTimes[i] << "\n";
        summary << caseId << "," << BenchmarkSupport::csvField(shape) << "," << BenchmarkSupport::csvField(p.name) << "," << block << ","
                << view << "," << msaa << "," << (stress ? "Material16" : "Texture1") << "," << o.instances << "," << o.width << ","
                << o.height << "," << o.depth << "," << o.meshopt << "," << o.samples << "," << median << "," << drawStatistics.mean << ","
                << drawStatistics.standardDeviation << "," << drawStatistics.minimum << "," << drawStatistics.maximum << "," << p.cpuMs
                << "," << p.mesh.vertices.size() << "," << p.mesh.indices.size() / 3 << "," << sourceTriangles << "," << p.failed << ","
                << p.check.duplicates << "," << p.check.nonmanifold << "," << p.check.winding << "," << p.check.missingBoundary << ","
                << p.check.unexpectedBoundary << "," << p.check.degenerate << "," << p.referenceError << "," << (p.valid && active > 0)
                << "," << p.flips << "," << p.check.edgeLength << "," << p.check.area << "," << helpers << "," << active << ","
                << (helpers + active ? double(helpers) / (helpers + double(active)) : 0.0) << "," << passStatistics.median << ","
                << passStatistics.mean << "," << passStatistics.standardDeviation << "," << passStatistics.minimum << ","
                << passStatistics.maximum << "," << p.valid << "," << runId << "\n";
        summary.flush();
        raw.flush();
        if (caseId % 16 == 0)
            std::cout << "Case " << caseId << ": " << shape << " " << p.name << " " << (stress ? "material" : "texture") << " " << msaa
                      << "x, " << median << " ms, H=" << helpers << ", P=" << active << std::endl;
        ++caseId;
    }

public:
    PerformanceApp(const SampleAppConfig& config, Options options) : SampleApp(config), o(std::move(options)) {}
    void onLoad(RenderContext* context) override
    {
        if (!o.output.parent_path().empty())
            std::filesystem::create_directories(o.output.parent_path());
        summary.exceptions(std::ios::failbit | std::ios::badbit);
        raw.exceptions(std::ios::failbit | std::ios::badbit);
        summary.open(o.output);
        auto rawPath = o.output;
        rawPath.replace_extension(".raw.csv");
        raw.open(rawPath);
        if (!summary || !raw)
            throw std::runtime_error("Cannot open benchmark output.");
        summary << std::setprecision(10);
        raw << std::setprecision(10);
        summary << "Case,Geometry,Method,Block,View,MSAA,Shader,Instances,Width,Height,Depth,Meshopt,Samples,GPU_Median_ms,GPU_Mean_ms,GPU_"
                   "StdDev_ms,GPU_Min_ms,GPU_Max_ms,ConstructionCPU_ms,OutputVertices,OutputTriangles,SourceTriangles,FailedPatches,"
                   "DuplicateTriangles,NonManifoldEdges,WindingErrors,MissingBoundaryEdges,UnexpectedBoundaryEdges,DegenerateTriangles,"
                   "SampledReferenceError,ValidForRanking,Flips,TotalEdgeLength3D,SurfaceArea,DiagnosticHelpers,DiagnosticActiveLanes,"
                   "DiagnosticHelperFraction,GPU_PassMedian_ms,GPU_PassMean_ms,GPU_PassStdDev_ms,GPU_PassMin_ms,GPU_PassMax_ms,MeshValid,"
                   "RunId\n";
        raw << "Case,Sample,GPU_Draw_ms,GPU_Pass_ms\n";
        auto info = getDevice()->getInfo();
        auto metadataPath = o.output;
        metadataPath.replace_extension(".metadata.txt");
        std::ofstream metadata;
        metadata.exceptions(std::ios::failbit | std::ios::badbit);
        metadata.open(metadataPath);
        metadata << "RunId: " << runId << "\nGPU: " << info.adapterName << "\nAPI: " << info.apiName
                 << "\nBuild: " << HELPERLANE_BUILD_CONFIG << "\nCompiler: " << HELPERLANE_COMPILER_DESC << "; debug layer disabled\nSource revision: " << HELPERLANE_GIT_REVISION
                 << "\nCompiled source digest: " << HELPERLANE_SOURCE_DIGEST
                 << "\nProtocol: 2; isolated synchronous draws, diagnostics separate\nTimer: uint64 timestamp subtraction before double "
                    "conversion\nGPU pass "
                    "timing: clear and draw, excluding resolve/readback/present/UI\nCPU timing: construction and per-flip guards; shared "
                    "reference accelerator setup and final "
                    "topology/reference audit excluded; single observation\nView sweep: Y=11.25+90*floor(view/2) degrees, "
                    "X=22.5+45*(view%2) degrees\nSeed: 20261010\nTimed region: indexed instanced draw only; clears, uploads, resolve, "
                    "readback, and diagnostics excluded\nColor: RGBA8Unorm\nDepth: "
                 << o.depth << " (D32Float with matching MSAA)\nCulling: None\nMeshopt: " << o.meshopt
                 << "\nReference budget: 0.1% of fixed source bounds diagonal; vertices, edge midpoints, triangle centroids sampled; not "
                    "Hausdorff guarantee\nHeapPlane score: old diagonal length - new diagonal length - 4 * quad plane residual\nTexture: "
                    "deterministic nonconstant 512x512 RGBA8, one mip, linear/wrap\nShader: 1 implicit sample or 16 dependent implicit "
                    "samples; no lane-dependent UVs or wave ALU in timed shader\nDiagnostics: separate shader with quad and wave "
                    "operations; these counts are proxies, not hardware counters\nGPU priming: 200 ms wall time per geometry, both "
                    "shaders\nWarmup frames per case: "
                 << o.warmup << "\nSamples per case: " << o.samples << "\nViews: " << o.views << "\nBlocks: " << o.blocks
                 << "\nResolution: " << o.width << 'x' << o.height << "\nInstances: " << o.instances << "\nGeometries: ";
        for (const auto& name : o.shapes)
            metadata << name << ' ';
        metadata << "\nMethods: ";
        for (const auto& name : o.methods)
            metadata << name << ' ';
        metadata << "\nMSAA: ";
        for (auto count : o.msaa)
            metadata << count << ' ';
        metadata << "\n";
        metadata.flush();
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
        timer = GpuTimer::create(getDevice());
        passTimer = GpuTimer::create(getDevice());
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
            geometryPrimed = false;
            auto input = BenchmarkGeometry::make(shape);
            auto source = Triangulation3D::useSourceMeshTriangulation(input);
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
            float halfX = 0.5f * (cols - 1) * spacing + radius, halfY = 0.5f * (rows - 1) * spacing + radius;
            float tanY = std::tan(math::radians(45.0f) * 0.5f), aspect = float(o.width) / o.height;
            float distance = std::max(halfY / tanY, halfX / (tanY * aspect)) + radius + 0.5f;
            auto viewMatrix = math::matrixFromLookAt(float3(0, 0, distance), float3(0), float3(0, 1, 0), math::Handedness::RightHanded);
            auto projection = math::perspective(math::radians(45.0f), aspect, 0.01f, distance + radius + 2);
            for (uint32_t block = 0; block < o.blocks; ++block)
                for (uint32_t view = 0; view < o.views; ++view)
                {
                    float yAngle = math::radians(11.25f + 90.0f * float(view / 2)), xAngle = math::radians(22.5f + 45.0f * float(view % 2));
                    Params params{
                        mul(projection, viewMatrix),
                        mul(math::matrixFromRotationX(xAngle), math::matrixFromRotationY(yAngle)),
                        cols,
                        spacing,
                        0.5f * float(cols - 1),
                        o.instances
                    };
                    for (uint32_t msaa : o.msaa)
                    {
                        const auto& fbo = framebuffers.at(msaa);
                        for (uint32_t stress = 0; stress < (o.quick ? 1u : 2u); ++stress)
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
                                    stress
                                );
                        }
                    }
                }
        }
        std::cout << "Benchmark complete: " << caseId << " cases, " << o.output.string() << std::endl;
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
