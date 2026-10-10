#include "HelperLaneViz.h"
#include "SampleSupport.h"
#include "Triangulation/ShaderTypes.h"
#include <algorithm>
#include <chrono>
#include <fmt/format.h>

namespace
{
const char* planarMethodName(uint32_t method)
{
    static const char* names[] = {
        "Ear Clipping",
        "MWT",
        "Centroid Fan",
        "Greedy",
        "Strip",
        "MaxMin",
        "MinMax",
        "CDT",
        "Earcut (Mapbox)",
        "Earcut + Flip",
        "CDT + Flip"};
    if (method >= std::size(names))
        throw std::runtime_error("Unknown planar benchmark method");
    return names[method];
}
} // namespace

void HelperLaneViz::startInteractiveBenchmark(std::vector<InteractiveCase> cases, const std::string& path)
{
    if (benchmarkRunning() || cases.empty())
        return;
    beginBenchmarkSession();
    mBenchmarkCases = std::move(cases);
    mBenchmarkCaseIndex = 0;
    mBenchmarkOutputPath = path;
    try
    {
        for (uint32_t i = 0; i < mBenchmarkCases.size(); ++i)
        {
            auto& c = mBenchmarkCases[i];
            auto& d = c.measurement;
            d.id = i;
            d.width = getTargetFbo()->getWidth();
            d.height = getTargetFbo()->getHeight();
            d.warmup = mBenchmarkConfig.warmup;
            d.samples = c.volume ? mBenchmarkConfig.volumeSamples : mBenchmarkConfig.planarSamples;
            d.instances = c.volume ? std::max(mVolumeInstanceCount, 1u) : c.cols * c.rows;
            d.shader = "FlatRaster";
            d.cpuScope = "ConstructionAuditUploadSharedSourceCached";
            d.passScope = d.msaa > 1 ? "ClearDrawResolveBlit" : "ClearDraw";
            d.meshopt = !c.volume && mUseMeshOptimizer;
            d.surfaceLocalFlips = c.volume && mVolumeSurfaceLocalFlips;
        }
        const auto settings = fmt::format(
            "Frontend: GUI\nSegments: {},{}\nSource radius: {}\nOBJ: {}\nNormalize OBJ: {}\nPatch normal/plane/max faces: {},{},{}"
            "\nCustom GP neighbors/mu/radius/min/max angle: {},{},{},{},{}\nXYZ instancing: {}\nGrid spacing: {}"
            "\nBezier tolerance: {}\nCircle vertices/radii: {},{},{}\nSVG: {}"
            "\nEdge length: unique geometric edges per mesh; projected length sums instances and is unclipped."
            "\nDepth: Less, writes enabled, D32Float; culling: None; reference audit retained."
            "\nCPU scope includes cached preparation, final audit and upload; single observation per mesh.",
            mVolumeLatitudeSegments,
            mVolumeLongitudeSegments,
            mVolumeRadius,
            mVolumeObjPath,
            mVolumeNormalizeObj,
            mVolumeMaxNormalDeviationDegrees,
            mVolumeMaxRelativePlaneDeviation,
            mVolumeMaxFacesPerPatch,
            mVolumePclMaximumNearestNeighbors,
            mVolumePclMu,
            mVolumePclSearchRadius,
            mVolumePclMinimumAngleDegrees,
            mVolumePclMaximumAngleDegrees,
            mVolumeUse3DInstancing,
            mVolumeGridSpacing,
            mMaxBezierDeviation,
            mCircleVertexCount,
            mEllipseRadiusX,
            mEllipseRadiusY,
            mSvgPath
        );
        mBenchmarkRunner = std::make_unique<Benchmark::Runner>(getDevice());
        mBenchmarkWriter = std::make_unique<Benchmark::Writer>(path, *getDevice(), settings);
        mBenchmarkStatus = "Benchmark running.";
    }
    catch (const std::exception& e)
    {
        finishBenchmarkSession(std::string("Benchmark failed: ") + e.what());
    }
}

void HelperLaneViz::startVolumeBenchmark(const std::vector<uint32_t>& methods)
{
    std::vector<InteractiveCase> cases;
    for (auto method : methods)
        for (uint32_t view = 0; view < mBenchmarkConfig.orientations; ++view)
        {
            InteractiveCase c;
            c.method = method;
            c.measurement.geometry = mVolumeGeometryName;
            c.measurement.method = method == 3 ? "Source" : getVolumeTriangulationMethodName(method);
            c.measurement.view = view;
            c.measurement.msaa = cntMSAA;
            c.measurement.xRotation = (view % kVolumeBenchmarkXSteps) * 90;
            c.measurement.yRotation = (view / kVolumeBenchmarkXSteps) * 45;
            cases.push_back(std::move(c));
        }
    startInteractiveBenchmark(std::move(cases), makeTimestampedCsvPath("helper_lane_volume_benchmark"));
}

void HelperLaneViz::startPlanarBenchmark(const std::vector<uint32_t>& methods)
{
    std::vector<InteractiveCase> cases;
    for (auto method : methods)
    {
        InteractiveCase c;
        c.volume = false;
        c.circle = mUseCircle;
        c.method = method;
        c.circleVertices = mCircleVertexCount;
        c.ellipseX = mEllipseRadiusX;
        c.ellipseY = mEllipseRadiusY;
        c.svgPath = mSvgPath;
        c.cols = mGridCols;
        c.rows = mGridRows;
        c.measurement.geometry = mUseCircle ? (mEllipseRadiusX == mEllipseRadiusY ? "Circle" : "Ellipse") : mSvgPath;
        c.measurement.method = planarMethodName(method);
        c.measurement.msaa = cntMSAA;
        cases.push_back(std::move(c));
    }
    startInteractiveBenchmark(std::move(cases), makeTimestampedCsvPath("helper_lane_planar_benchmark"));
}

void HelperLaneViz::benchmarkFolder(const std::string& folderPath)
{
    if (folderPath.empty())
        return;
    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(folderPath))
        if (e.is_regular_file() && e.path().extension() == ".svg")
            files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    if (files.empty())
    {
        mBenchmarkStatus = "No SVG files in benchmark folder.";
        return;
    }
    std::vector<InteractiveCase> cases;
    for (auto msaa : {1u, 4u})
        for (auto grid : {1u, 100u})
            for (size_t shape = 0; shape < files.size() + 2; ++shape)
                for (auto method : {1u, 3u, 7u, 8u, 9u, 10u})
                {
                    InteractiveCase c;
                    c.volume = false;
                    c.circle = shape >= files.size();
                    c.method = method;
                    c.circleVertices = 512;
                    c.ellipseY = shape == files.size() + 1 ? 0.2f : 0.4f;
                    c.svgPath = c.circle ? "" : files[shape];
                    c.cols = c.rows = grid;
                    c.measurement.geometry = c.circle ? (c.ellipseY == 0.4f ? "circle_512" : "ellipse_512") : c.svgPath;
                    c.measurement.method = planarMethodName(method);
                    c.measurement.msaa = msaa;
                    cases.push_back(std::move(c));
                }
    startInteractiveBenchmark(std::move(cases), makeTimestampedCsvPath("helper_lane_folder_benchmark"));
}

void HelperLaneViz::processBenchmarkFrame(RenderContext* context, const ref<Fbo>& target)
{
    const auto& c = mBenchmarkCases.at(mBenchmarkCaseIndex);
    if (!mBenchmarkRunner->started() || mBenchmarkRunner->result().description.id != mBenchmarkCaseIndex)
    {
        const bool rebuild = !mBenchmarkCaseIndex || !c.volume || c.method != mBenchmarkCases[mBenchmarkCaseIndex - 1].method;
        mGeometryDimensionality = c.volume ? GeometryDimensionality::Volumetric : GeometryDimensionality::Planar;
        if (cntMSAA != c.measurement.msaa)
        {
            cntMSAA = c.measurement.msaa;
            CreateMSAATargets();
        }
        if (rebuild)
        {
            mVolumeTriangulationType = c.volume ? c.method : mVolumeTriangulationType;
            mTriangulationType = c.volume ? mTriangulationType : c.method;
            if (!c.volume)
            {
                mUseCircle = c.circle;
                mCircleVertexCount = c.circleVertices;
                mEllipseRadiusX = c.ellipseX;
                mEllipseRadiusY = c.ellipseY;
                mSvgPath = c.svgPath;
                mGridCols = c.cols;
                mGridRows = c.rows;
            }
            const auto start = std::chrono::steady_clock::now();
            if (c.volume)
                generateVolumeGeometry();
            else if (mUseCircle)
                generateCircle();
            else
                loadSvg(mSvgPath);
            const double cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (!mIndexCount)
                throw std::runtime_error("Benchmark geometry is empty");
            mBenchmarkMeshMetrics = {};
            auto& m = mBenchmarkMeshMetrics;
            if (c.volume)
            {
                m.audit = mVolumeAudit;
                m.vertices = uint32_t(mVolumeVertices.size());
                m.triangles = uint32_t(mVolumeIndices.size() / 3);
            }
            else
            {
                Triangulation3D::MeshTriangulationResult mesh;
                for (const auto& v : mVertices)
                    mesh.vertices.push_back(float3(v.pos.x, v.pos.y, 0));
                mesh.indices = mIndices;
                m.audit = MeshValidation::audit(mesh, mesh);
                m.vertices = uint32_t(mesh.vertices.size());
                m.triangles = uint32_t(mesh.indices.size() / 3);
            }
            m.sourceTriangles = c.volume ? mVolumeSourceTriangleCount : m.triangles;
            m.cpuMs = cpuMs;
            m.failedPatches = c.volume ? mVolumeFailedPatchCount : 0;
            m.referenceError = c.volume ? mVolumeReferenceError : 0;
            m.valid = c.volume ? mVolumeResultValid : m.audit.topologyOK();
        }
        if (c.volume)
        {
            mVolumeModelTransform = makeVolumeBenchmarkModelTransform(c.measurement.view);
            updateVolumeGridParams();
            updateVolumeModelParams();
        }
        else
            updateGridParams();
        updateCameraParams();
        auto description = c.measurement;
        description.mesh = mBenchmarkMeshMetrics;
        description.gridCols = c.volume ? mVolumeGridCols : c.cols;
        description.gridRows = c.volume ? mVolumeGridRows : c.rows;
        description.gridLayers = c.volume ? mVolumeGridLayers : 1;
        description.radius = c.volume ? mVolumeBoundsRadius : c.ellipseX;
        if (c.volume)
            description.mesh.projectedEdgeLength = computeProjectedEdgeLengthScreenSpace(
                mVolumeVertices,
                mVolumeIndices,
                mVolumeModelTransform,
                description.instances,
                description.width,
                description.height,
                mVolumeGridCols,
                mVolumeGridRows,
                mVolumeGridLayers,
                mVolumeUse3DInstancing,
                mVolumeGridSpacing,
                mVolumeBoundsRadius
            );
        mBenchmarkRunner->start(std::move(description));
    }
    mpProgram->addDefine("USE_3D_GEOMETRY", c.volume ? "1" : "0");
    mpState->setFbo(cntMSAA > 1 ? mpFbo : target);
    mpState->setRasterizerState(defaultRsState);
    Benchmark::Work work;
    work.clear = [&] { context->clearFbo(mpState->getFbo().get(), float4(0), 1, 0); };
    work.draw = [&] { context->drawIndexedInstanced(mpState.get(), mpVars.get(), mIndexCount, c.measurement.instances, 0, 0, 0); };
    work.finish = [&]
    {
        if (cntMSAA > 1)
        {
            context->resolveResource(mpFbo->getColorTexture(0), mpResolvedTexture);
            context->blit(mpResolvedTexture->getSRV(), target->getRenderTargetView(0));
        }
    };
    work.diagnose = [&]
    {
        mpProgram->addDefine("VIZ_MODE", "0");
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "1");
        work.clear();
        context->clearUAV(mpHelperLaneCounter->getUAV().get(), uint4(0));
        work.draw();
        const auto bytes = context->readTextureSubresource(mpHelperLaneCounter.get(), 0);
        const auto counts = reinterpret_cast<const uint32_t*>(bytes.data());
        const Benchmark::Counts result{counts[0], counts[1]};
        mpProgram->addDefine("VIZ_MODE", "2");
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
        return result;
    };
    if (mBenchmarkRunner->step(context, work))
    {
        mBenchmarkWriter->write(mBenchmarkRunner->result());
        if (++mBenchmarkCaseIndex == mBenchmarkCases.size())
            finishBenchmarkSession("Benchmark complete.", true);
    }
}
void HelperLaneViz::beginBenchmarkSession()
{
    if (mBenchmarkSavedState)
        return;
    mBenchmarkSavedState = SavedInteractiveState{
        mGeometryDimensionality,
        mVizMode,
        mReadBackHelperLaneCount,
        mUseCircle,
        cntMSAA,
        mGridCols,
        mGridRows,
        mTriangulationType,
        mVolumeTriangulationType,
        mCircleVertexCount,
        mEllipseRadiusX,
        mEllipseRadiusY,
        mSvgPath,
        mVolumeModelTransform};
    mReadBackHelperLaneCount = false;
    mVizMode = VizMode::Benchmark;
    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode == VizMode::LitPreview ? VizMode::HelperLanes : mVizMode)));
    mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
}

void HelperLaneViz::finishBenchmarkSession(const std::string& message, bool complete)
{
    mBenchmarkStatus = message;
    if (mBenchmarkWriter)
    {
        try
        {
            if (!complete && mBenchmarkRunner && mBenchmarkRunner->started())
            {
                const auto& result = mBenchmarkRunner->result();
                if (!result.complete && !result.samples.empty() && result.description.id == mBenchmarkWriter->caseCount())
                    mBenchmarkWriter->write(result);
            }
            mBenchmarkWriter->finish(complete, message);
        }
        catch (const std::exception& e)
        {
            mBenchmarkStatus = std::string("Benchmark output failed: ") + e.what();
        }
    }
    mBenchmarkWriter.reset();
    mBenchmarkRunner.reset();
    if (!mBenchmarkSavedState)
        return;
    auto saved = std::move(*mBenchmarkSavedState);
    mBenchmarkSavedState.reset();
    mGeometryDimensionality = saved.dimensionality;
    mVizMode = saved.viz;
    mReadBackHelperLaneCount = saved.readback;
    mUseCircle = saved.circle;
    cntMSAA = saved.msaa;
    mGridCols = saved.cols;
    mGridRows = saved.rows;
    mTriangulationType = saved.planarMethod;
    mVolumeTriangulationType = saved.volumeMethod;
    mCircleVertexCount = saved.circleVertices;
    mEllipseRadiusX = saved.ellipseX;
    mEllipseRadiusY = saved.ellipseY;
    mSvgPath = std::move(saved.svgPath);
    mVolumeModelTransform = saved.model;
    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode == VizMode::LitPreview ? VizMode::HelperLanes : mVizMode)));
    mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", mReadBackHelperLaneCount ? "1" : "0");
    if (mGeometryDimensionality == GeometryDimensionality::Volumetric)
        generateVolumeGeometry();
    else if (mUseCircle)
        generateCircle();
    else
        loadSvg(mSvgPath);
    CreateMSAATargets();
    updateGridParams();
    updateVolumeGridParams();
    updateVolumeModelParams();
    updateCameraParams();
}
