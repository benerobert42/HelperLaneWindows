/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "HelperLaneViz.h"
#include "SampleSupport.h"
#include "Triangulation/MeshValidation.h"
#include "Triangulation/3D/SurfaceFlipExperiment.h"

#include "Import/ObjMeshLoader.h"
#include "Import/ModelPresets.h"
#include "Triangulation/3D/ProceduralGeometry3D.h"
#include "Triangulation/3D/Triangulation3D.h"
#include "Triangulation/2D/TriangulationHelpers.h"
#include "Import/SVGLoader.h"

#include "Falcor.h"
#include "Core/Program/ProgramManager.h"
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <sstream>

#include "meshoptimizer.h"


using namespace Falcor;

FALCOR_EXPORT_D3D12_AGILITY_SDK

HelperLaneViz::HelperLaneViz(const SampleAppConfig& config) : SampleApp(config) {}
HelperLaneViz::~HelperLaneViz() {}

void HelperLaneViz::CreateMSAATargets()
{
    mpFbo = Fbo::create(getDevice());
    ref<Texture> tex = getDevice()->createTexture2DMS(
        getTargetFbo()->getWidth(),
        getTargetFbo()->getHeight(),
        ResourceFormat::RGBA16Float,
        cntMSAA,
        1,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::RenderTarget
    );
    mpFbo->attachColorTarget(tex, 0);
    mpFbo->attachDepthStencilTarget(getDevice()->createTexture2DMS(
        getTargetFbo()->getWidth(), getTargetFbo()->getHeight(), ResourceFormat::D32Float, cntMSAA, 1, ResourceBindFlags::DepthStencil
    ));

    mpResolvedTexture =
        getDevice()->createTexture2D(getTargetFbo()->getWidth(), getTargetFbo()->getHeight(), ResourceFormat::RGBA16Float, 1, 1);
}

void HelperLaneViz::onLoad(RenderContext* pRenderContext)
{
    ProgramDesc d;
    d.addShaderLibrary("Samples/HelperLaneWindows/Shaders/MainShader.slang").vsEntry("vsMain").psEntry("psMain");

    pRenderContext->getDevice()->getProgramManager()->setGenerateDebugInfoEnabled(true);

    mpProgram = Program::create(getDevice(), d);
    mpProgram->addDefine("USE_3D_GEOMETRY", "0");
    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode == VizMode::LitPreview ? VizMode::HelperLanes : mVizMode)));
    mpVars = ProgramVars::create(getDevice(), mpProgram.get());
    mpState = GraphicsState::create(getDevice());
    mpState->setProgram(mpProgram);
    DepthStencilState::Desc depth;
    depth.setDepthEnabled(true).setDepthWriteMask(true).setDepthFunc(ComparisonFunc::Less);
    mpState->setDepthStencilState(DepthStencilState::create(depth));

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);
    defaultRsState = RasterizerState::create(rsDesc);
    mpState->setRasterizerState(defaultRsState);

    RasterizerState::Desc wireDesc = rsDesc;
    wireDesc.setFillMode(RasterizerState::FillMode::Wireframe);
    wireframeRsState = RasterizerState::create(wireDesc);

    mpLayout = VertexLayout::create();
    auto pVbLayout = VertexBufferLayout::create();
    pVbLayout->addElement("POSITION", 0, ResourceFormat::RG32Float, 1, 0);
    mpLayout->addBufferLayout(0, pVbLayout);

    mpLayout3D = VertexLayout::create();
    auto pVbLayout3D = VertexBufferLayout::create();
    pVbLayout3D->addElement("POSITION", 0, ResourceFormat::RGB32Float, 1, 0);
    mpLayout3D->addBufferLayout(0, pVbLayout3D);

    mSvgPath = "Path to your SVG file";
    if (mGeometryDimensionality == GeometryDimensionality::Planar)
    {
        if (mUseCircle) generateCircle();
        else loadSvg(mSvgPath);
    }
    generateVolumeGeometry();
    if (mCaptureAndExit && !mVolumeResultValid)
        throw std::runtime_error("Screenshot requires a valid triangulation: " + mVolumeObjStatus);

    getDevice()->getProfiler()->setEnabled(true);

    updateGridParams();
    updateVolumeModelParams();
    updateCameraParams();

    mpHelperLaneCounter = getDevice()->createTexture2D(
        2, 1, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);

    // MSAA render target
    CreateMSAATargets();
}

void HelperLaneViz::loadSvg(const std::string& path)
{
    mVertices.clear();
    mIndices.clear();

    SVGLoader::Triangulator triangulator = [this](std::vector<Vertex>& verts) -> std::vector<uint32_t>
    {
        switch (mTriangulationType)
        {
        case 0:
            return Triangulation::earClippingTriangulation(verts);
        case 1:
            return Triangulation::minimumWeightTriangulation(verts, true);
        case 2:
            return Triangulation::centroidFanTriangulation(verts);
        case 3:
            return Triangulation::greedyMaxAreaTriangulation(verts, true);
        case 4:
            return Triangulation::stripTriangulation(verts);
        case 5:
            return Triangulation::maxMinAreaTriangulation(verts, true);
        case 6:
            return Triangulation::minMaxAreaTriangulation(verts, true);
        case 7:
            return Triangulation::constrainedDelaunay(verts);
        case 8:
            return Triangulation::earClippingMapbox(verts);
        case 9:
            return Triangulation::earClippingMapboxFlipped(verts);
        case 10:
            return Triangulation::constrainedDelaunayFlipped(verts);
        default:
            return Triangulation::earClippingTriangulation(verts);
        }
    };

    auto start = std::chrono::high_resolution_clock::now();

    const bool loaded = SVGLoader::TessellateSvgToMesh(path, mVertices, mIndices, triangulator, mMaxBezierDeviation);
    if (loaded)
    {
        if (mUseMeshOptimizer)
        {
            optimizeMesh();
        }
        uploadGeometry();
    }
    else if (mGeometryDimensionality == GeometryDimensionality::Planar)
    {
        uploadGeometry();
    }

    mLastTriangulationMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
}

void HelperLaneViz::generateCircle()
{
    mVertices.clear();
    mIndices.clear();

    mVertices = Triangulation::CreateVerticesForEllipse(mCircleVertexCount, mEllipseRadiusX, mEllipseRadiusY, float2(0.5f, 0.5f));

    auto start = std::chrono::high_resolution_clock::now();

    switch (mTriangulationType)
    {
    case 0:
        mIndices = Triangulation::earClippingTriangulation(mVertices);
        break;
    case 1:
        mIndices = Triangulation::minimumWeightTriangulation(mVertices, false);
        break;
    case 2:
        mIndices = Triangulation::centroidFanTriangulation(mVertices);
        break;
    case 3:
        mIndices = Triangulation::greedyMaxAreaTriangulation(mVertices, false);
        break;
    case 4:
        mIndices = Triangulation::stripTriangulation(mVertices);
        break;
    case 5:
        mIndices = Triangulation::maxMinAreaTriangulation(mVertices, false);
        break;
    case 6:
        mIndices = Triangulation::minMaxAreaTriangulation(mVertices, false);
        break;
    case 7:
        mIndices = Triangulation::constrainedDelaunay(mVertices);
        break;
    case 8:
        mIndices = Triangulation::earClippingMapbox(mVertices);
        break;
    case 9:
        mIndices = Triangulation::earClippingMapboxFlipped(mVertices);
        break;
    case 10:
        mIndices = Triangulation::constrainedDelaunayFlipped(mVertices);
        break;
    default:
        mIndices = Triangulation::earClippingTriangulation(mVertices);
        break;
    }

    mLastTriangulationMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();

    if (mUseMeshOptimizer)
    {
        optimizeMesh();
    }

    uploadGeometry();
}

void HelperLaneViz::uploadMesh(const void* vertices, size_t vertexCount, size_t stride, const std::vector<uint32_t>& indices, const ref<VertexLayout>& layout)
{
    mIndexCount = 0;
    mpVao = nullptr;
    mpState->setVao(nullptr);
    if (vertexCount < 3 || indices.empty()) return;
    mpVB = getDevice()->createBuffer(vertexCount * stride, ResourceBindFlags::Vertex, MemoryType::DeviceLocal, vertices);
    mpIB = getDevice()->createBuffer(indices.size() * sizeof(uint32_t), ResourceBindFlags::Index, MemoryType::DeviceLocal, indices.data());
    mpVao = Vao::create(Vao::Topology::TriangleList, layout, {mpVB}, mpIB, ResourceFormat::R32Uint);
    mpState->setVao(mpVao);
    mIndexCount = uint32_t(indices.size());
}

void HelperLaneViz::uploadGeometry()
{
    uploadMesh(mVertices.data(), mVertices.size(), sizeof(Vertex), mIndices, mpLayout);
}

void HelperLaneViz::uploadVolumeGeometry()
{
    uploadMesh(mVolumeVertices.data(), mVolumeVertices.size(), sizeof(float3), mVolumeIndices, mpLayout3D);
}

void HelperLaneViz::generateVolumeGeometry()
{
    mVolumeVertices.clear();
    mVolumeIndices.clear();
    mVolumeSourceFaceCount = 0;
    mVolumeSourceTriangleCount = 0;

    mVolumePatchCount = 0;
    mVolumeFailedPatchCount = 0;

    auto start = std::chrono::high_resolution_clock::now();

    Triangulation3D::PatchGrowthOptions patchOptions;
    patchOptions.maxNormalDeviationDegrees = mVolumeMaxNormalDeviationDegrees;
    patchOptions.maxRelativePlaneDeviation = mVolumeMaxRelativePlaneDeviation;
    patchOptions.maxFacesPerPatch = mVolumeMaxFacesPerPatch;

    Triangulation3D::TriangulationOptions triangulationOptions;
    triangulationOptions.method = getVolumeTriangulationMethod(mVolumeTriangulationType);
    triangulationOptions.use3DEdgeLengthForGreedy = true;
    triangulationOptions.use3DEdgeLengthForFlips = mVolumeSurfaceLocalFlips;
    triangulationOptions.pclMaximumNearestNeighbors = mVolumePclMaximumNearestNeighbors;
    triangulationOptions.pclMu = mVolumePclMu;
    triangulationOptions.pclSearchRadius = mVolumePclSearchRadius;
    triangulationOptions.pclMinimumAngleDegrees = mVolumePclMinimumAngleDegrees;
    triangulationOptions.pclMaximumAngleDegrees = mVolumePclMaximumAngleDegrees;

    mVolumeGeometryName = getVolumeGeometryName(mVolumeGeometryType);
    const auto objPath = mVolumeGeometryType >= 8 ? getModelPresetPath(mVolumeGeometryType).string() : mVolumeObjPath;
    std::ostringstream sourceKey;
    sourceKey << std::setprecision(9) << mVolumeGeometryType << ':' << mVolumeLatitudeSegments << ':' << mVolumeLongitudeSegments << ':'
              << mVolumeRadius << ':' << mVolumeNormalizeObj << ':' << objPath;
    if (isObjVolumeGeometry(mVolumeGeometryType) && !benchmarkRunning() && std::filesystem::exists(objPath))
        sourceKey << ':' << std::filesystem::last_write_time(objPath).time_since_epoch().count();
    const auto requestedSourceKey = benchmarkRunning() && !mVolumeSourceKey.empty() ? mVolumeSourceKey : sourceKey.str();
    if (requestedSourceKey != mVolumeSourceKey)
    {
        Triangulation3D::Mesh sourceMesh;
        mVolumeObjStatus.clear();
        mVolumeObjPolygonFaceCount = 0;

        switch (mVolumeGeometryType)
        {
        case 1:
            sourceMesh = ProceduralGeometry3D::makeEllipsoid(
                mVolumeLatitudeSegments,
                mVolumeLongitudeSegments,
                float3(mVolumeRadius * 1.65f, mVolumeRadius * 0.55f, mVolumeRadius * 0.95f),
                float3(0.0f, 0.0f, 0.0f)
            );
            break;
        case 2:
            sourceMesh = ProceduralGeometry3D::makeJitteredSphere(
                mVolumeLatitudeSegments, mVolumeLongitudeSegments, mVolumeRadius, 0.22f, 0.18f, float3(0.0f, 0.0f, 0.0f)
            );
            break;
        case 3:
            sourceMesh = ProceduralGeometry3D::makeLumpyEllipsoid(
                mVolumeLatitudeSegments,
                mVolumeLongitudeSegments,
                float3(mVolumeRadius * 1.45f, mVolumeRadius * 0.65f, mVolumeRadius * 1.05f),
                0.35f,
                float3(0.0f, 0.0f, 0.0f)
            );
            break;
        case 4:
            sourceMesh = ProceduralGeometry3D::makeSaddlePatch(
                mVolumeLongitudeSegments,
                mVolumeLatitudeSegments,
                mVolumeRadius * 3.0f,
                mVolumeRadius * 0.85f,
                1.0f,
                float3(0.0f, 0.0f, 0.0f)
            );
            break;
        case 5:
            sourceMesh = ProceduralGeometry3D::makeFoldedSheet(
                mVolumeLongitudeSegments,
                mVolumeLatitudeSegments,
                mVolumeRadius * 3.2f,
                mVolumeRadius * 0.9f,
                mVolumeRadius * 0.12f,
                1.0f,
                float3(0.0f, 0.0f, 0.0f)
            );
            break;
        case 6:
            sourceMesh = ProceduralGeometry3D::makeClusteredWavePatch(
                mVolumeLongitudeSegments,
                mVolumeLatitudeSegments,
                mVolumeRadius * 3.0f,
                mVolumeRadius * 0.8f,
                0.8f,
                float3(0.0f, 0.0f, 0.0f)
            );
            break;
        case 8:
        case 9:
        case 10:
        case 7:
        {
            ObjMeshLoader::LoadOptions loadOptions;
            loadOptions.centerAndScale = mVolumeNormalizeObj;
            loadOptions.targetRadius = mVolumeRadius;
            ObjMeshLoader::LoadResult loadResult = ObjMeshLoader::load(objPath, loadOptions);
            sourceMesh = std::move(loadResult.mesh);
            mVolumeObjStatus = loadResult.message;
            mVolumeObjPolygonFaceCount = loadResult.polygonFaceCount;
            if (!loadResult.warnings.empty())
            {
                mVolumeObjStatus += " Warnings: " + std::to_string(loadResult.warnings.size());
            }
            if (loadResult.success && mVolumeGeometryType == 7)
            {
                mVolumeGeometryName = "OBJ: " + std::filesystem::path(objPath).filename().string();
            }
            break;
        }
        case 0:
        default:
            sourceMesh = ProceduralGeometry3D::makeSphere(
                mVolumeLatitudeSegments, mVolumeLongitudeSegments, mVolumeRadius, float3(0.0f, 0.0f, 0.0f)
            );
            break;
        }

        mVolumeCachedSource = std::move(sourceMesh);
        mVolumeCachedBaseline = Triangulation3D::useSourceMeshTriangulation(mVolumeCachedSource);
        mVolumeReference.reset();
        if (mVolumeCachedBaseline.success)
            mVolumeReference = std::make_unique<SurfaceFlipExperiment::ReferenceSurface>(mVolumeCachedBaseline);
        mVolumeSourceKey = requestedSourceKey;
        mVolumePatchKey.clear();
    }
    const auto& sourceMesh = mVolumeCachedSource;
    if (mVolumeGeometryType == 7)
        mVolumeGeometryName = "OBJ: " + std::filesystem::path(objPath).filename().string();
    std::ostringstream patchKey;
    patchKey << std::setprecision(9) << mVolumeMaxNormalDeviationDegrees << ':' << mVolumeMaxRelativePlaneDeviation << ':'
             << mVolumeMaxFacesPerPatch;
    if (mVolumeTriangulationType != 3 && patchKey.str() != mVolumePatchKey)
    {
        mVolumeCachedPatches = Triangulation3D::growPatches(sourceMesh, patchOptions);
        mVolumePatchKey = patchKey.str();
    }
    mVolumeSourceVertexCount = static_cast<uint32_t>(sourceMesh.vertices.size());
    mVolumeSourceFaceCount = static_cast<uint32_t>(sourceMesh.faces.size());
    for (const std::vector<uint32_t>& face : sourceMesh.faces)
    {
        if (face.size() >= 3)
        {
            mVolumeSourceTriangleCount += static_cast<uint32_t>(face.size() - 2);
        }
    }

    Triangulation3D::MeshTriangulationResult result =
        mVolumeTriangulationType == 3
            ? mVolumeCachedBaseline
            : Triangulation3D::triangulatePreparedMesh(sourceMesh, mVolumeCachedBaseline, mVolumeCachedPatches, triangulationOptions);

    mVolumeBoundsRadius = std::max(computeMaxDistanceFromOrigin(sourceMesh.vertices), 0.001f);
    mVolumeResultValid = false;
    mVolumeAudit = {};
    mVolumeReferenceError = 0;
    if (result.success && mVolumeCachedBaseline.success)
    {
        mVolumeAudit = MeshValidation::audit(result, mVolumeCachedBaseline);
        mVolumeReferenceError = mVolumeReference->sampledError(result);
        float3 lo = sourceMesh.vertices.front(), hi = lo;
        for (auto p : sourceMesh.vertices)
        {
            lo = math::min(lo, p);
            hi = math::max(hi, p);
        }
        mVolumeResultValid = result.status == Triangulation3D::ResultStatus::Complete && mVolumeAudit.topologyOK() &&
                             result.indices.size() == mVolumeCachedBaseline.indices.size() &&
                             mVolumeReferenceError <= length3(hi - lo) * 0.001001;
    }
    mVolumeVertices = std::move(result.vertices);
    mVolumeIndices = std::move(result.indices);
    mVolumePatchCount = static_cast<uint32_t>(result.patches.size());
    mVolumeFailedPatchCount = result.failedPatchCount;
    if (!result.messages.empty())
    {
        mVolumeObjStatus += " " + result.messages.front();
    }
    updateVolumeGridParams();
    updateCameraParams();

    mLastTriangulationMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();

    if (mGeometryDimensionality == GeometryDimensionality::Volumetric)
    {
        uploadVolumeGeometry();
    }
}

void HelperLaneViz::applyVolumeTargetVertexCount()
{
    chooseVolumeSegmentsForTarget(mVolumeGeometryType, mVolumeTargetVertexCount, mVolumeLatitudeSegments, mVolumeLongitudeSegments);
    mVolumeTargetVertexCount = estimateVolumeSourceVertexCount(mVolumeGeometryType, mVolumeLatitudeSegments, mVolumeLongitudeSegments);
}

void HelperLaneViz::optimizeMesh()
{
    if (mVertices.size() < 3 || mIndices.empty())
        return;

    const size_t vertexCount = mVertices.size();
    const size_t indexCount = mIndices.size();

    // Create temporary float3 positions for meshoptimizer (it expects xyz positions)
    std::vector<float> positions(vertexCount * 3);
    for (size_t i = 0; i < vertexCount; ++i)
    {
        positions[i * 3 + 0] = mVertices[i].pos.x;
        positions[i * 3 + 1] = mVertices[i].pos.y;
        positions[i * 3 + 2] = 0.0f;
    }

    // Vertex cache optimization - reorders indices to improve vertex cache hit rate
    std::vector<uint32_t> optimizedIndices(indexCount);
    meshopt_optimizeVertexCache(optimizedIndices.data(), mIndices.data(), indexCount, vertexCount);

    // Overdraw optimization - reorders triangles to reduce pixel overdraw
    // The threshold (1.05f) means we allow up to 5% worse vertex cache efficiency to reduce overdraw
    meshopt_optimizeOverdraw(
        optimizedIndices.data(), optimizedIndices.data(), indexCount, positions.data(), vertexCount, sizeof(float) * 3, 1.05f
    );

    // Vertex fetch optimization - reorders vertices and updates indices
    std::vector<Vertex> optimizedVertices(vertexCount);
    meshopt_optimizeVertexFetch(
        optimizedVertices.data(), optimizedIndices.data(), indexCount, mVertices.data(), vertexCount, sizeof(Vertex)
    );

    // Replace original data with optimized data
    mVertices = std::move(optimizedVertices);
    mIndices = std::move(optimizedIndices);
}

void HelperLaneViz::updateGridParams()
{
    mGridCellSize = float2(1.0f / mGridCols, 1.0f / mGridRows);
    mGridOrigin = float2(0.0f, 0.0f);
    mGridScale = std::min(mGridCellSize.x, mGridCellSize.y);

    GridParams gridParams;
    gridParams.cols = mGridCols;
    gridParams.rows = mGridRows;
    gridParams.cellSize = mGridCellSize;
    gridParams.origin = mGridOrigin;
    gridParams.scale = mGridScale;
    mpVars->getRootVar()["Grid"].setBlob(gridParams);
}

void HelperLaneViz::updateVolumeGridParams()
{
    const uint32_t instanceCount = std::max(mVolumeInstanceCount, 1u);
    const float aspectRatio = getTargetFbo() && getTargetFbo()->getHeight() > 0
                                  ? static_cast<float>(getTargetFbo()->getWidth()) / static_cast<float>(getTargetFbo()->getHeight())
                                  : 1.0f;
    const float gridAspect = std::max(aspectRatio, 0.001f);

    if (mVolumeUse3DInstancing)
    {
        mVolumeGridCols = std::min(instanceCount, std::max(1u, static_cast<uint32_t>(std::ceil(std::cbrt(static_cast<float>(instanceCount) * gridAspect)))));
        const uint32_t remainingAfterCols = std::max(1u, (instanceCount + mVolumeGridCols - 1u) / mVolumeGridCols);
        mVolumeGridRows = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(remainingAfterCols)))));
        mVolumeGridLayers = std::max(1u, (instanceCount + mVolumeGridCols * mVolumeGridRows - 1u) / (mVolumeGridCols * mVolumeGridRows));
    }
    else
    {
        mVolumeGridCols = std::min(instanceCount, std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(instanceCount) * gridAspect)))));
        mVolumeGridRows = std::max(1u, (instanceCount + mVolumeGridCols - 1u) / mVolumeGridCols);
        mVolumeGridLayers = 1;
    }
    mVolumeGridSpacing = std::max(mVolumeBoundsRadius * 2.5f, 0.001f);

    VolumeGridParams volumeGridParams;
    volumeGridParams.instanceCount = instanceCount;
    volumeGridParams.cols = mVolumeGridCols;
    volumeGridParams.rows = mVolumeGridRows;
    volumeGridParams.layers = mVolumeGridLayers;
    volumeGridParams.use3DGrid = mVolumeUse3DInstancing ? 1u : 0u;
    volumeGridParams.spacing = mVolumeGridSpacing;
    volumeGridParams.padding = float2(0.0f, 0.0f);
    mpVars->getRootVar()["VolumeGrid"].setBlob(volumeGridParams);
}

void HelperLaneViz::updateVolumeModelParams()
{
    VolumeModelParams volumeModelParams;
    volumeModelParams.transform = mVolumeModelTransform;
    mpVars->getRootVar()["VolumeModel"].setBlob(volumeModelParams);
}

void HelperLaneViz::updateCameraParams(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        width = getTargetFbo() ? getTargetFbo()->getWidth() : 1;
        height = getTargetFbo() ? getTargetFbo()->getHeight() : 1;
    }

    width = std::max(width, 1u);
    height = std::max(height, 1u);

    CameraParams cameraParams;
    cameraParams.viewProj = makeVolumeViewProj(
        width, height, mVolumeGridCols, mVolumeGridRows, mVolumeGridLayers, mVolumeUse3DInstancing, mVolumeGridSpacing, mVolumeBoundsRadius
    );
    mpVars->getRootVar()["Camera"].setBlob(cameraParams);
}

std::string selectFolder()
{
    std::string folderPath;
    BROWSEINFOA bi = {0};
    bi.lpszTitle = "Select folder to benchmark";
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != nullptr)
    {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path))
        {
            folderPath = path;
        }
        CoTaskMemFree(pidl);
    }
    return folderPath;
}

void HelperLaneViz::onShutdown()
{
    if (benchmarkRunning()) finishBenchmarkSession("Benchmark interrupted by shutdown.");
}
void HelperLaneViz::onResize(uint32_t width, uint32_t height)
{
    if (benchmarkRunning())
        finishBenchmarkSession("Benchmark cancelled because the window was resized.");
    updateGridParams();
    updateVolumeGridParams();
    updateVolumeModelParams();
    updateCameraParams(width, height);
    mpHelperLaneCounter = getDevice()->createTexture2D(
        2, 1, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);
    CreateMSAATargets();
}

void HelperLaneViz::onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    if (mSmokeStage >= 0 && !advanceInteractiveSmoke())
        return;
    if (benchmarkRunning())
    {
        try { processBenchmarkFrame(pRenderContext, pTargetFbo); }
        catch (const std::exception& e) { finishBenchmarkSession(std::string("Benchmark failed: ") + e.what()); }
        return;
    }

    if (mGeometryDimensionality == GeometryDimensionality::Volumetric && mVizMode == VizMode::LitPreview)
    {
        renderLitPreview(pRenderContext, cntMSAA > 1 ? mpFbo : pTargetFbo);
    }
    else
    {
        mpState->setFbo(mpFbo);
        float4 bgColor = (mVizMode == VizMode::Wireframe) ? float4(1, 1, 1, 1) : float4(0, 0, 0, 0);

        mpProgram->addDefine("USE_3D_GEOMETRY", mGeometryDimensionality == GeometryDimensionality::Volumetric ? "1" : "0");
        if (cntMSAA > 1)
        {
            pRenderContext->clearFbo(mpFbo.get(), bgColor, 1.f, 0);
            mpState->setFbo(mpFbo);
        }
        else
        {
            pRenderContext->clearFbo(pTargetFbo.get(), bgColor, 1.f, 0);
            mpState->setFbo(pTargetFbo);
        }

        if (mpHelperLaneCounter && mReadBackHelperLaneCount && mVizMode == VizMode::HelperLanes)
        {
            pRenderContext->clearUAV(mpHelperLaneCounter->getUAV().get(), uint4(0));
        }

        if (mVizMode == VizMode::Wireframe)
        {
            mpState->setRasterizerState(wireframeRsState);
        }
        else
        {
            mpState->setRasterizerState(defaultRsState);
        }

        if (mIndexCount)
        {
            const uint32_t instanceCount = mGeometryDimensionality == GeometryDimensionality::Volumetric
                ? std::max(mVolumeInstanceCount, 1u) : mGridCols * mGridRows;
            FALCOR_PROFILE(pRenderContext, "MeshDraw");
            pRenderContext->drawIndexedInstanced(mpState.get(), mpVars.get(), mIndexCount, instanceCount, 0, 0, 0);
        }

        if (mpHelperLaneCounter && mIndexCount && mReadBackHelperLaneCount && mVizMode == VizMode::HelperLanes)
        {
            FALCOR_PROFILE(pRenderContext, "HelperReadback");
            std::vector<uint8_t> counterData = pRenderContext->readTextureSubresource(mpHelperLaneCounter->asTexture().get(), 0);
            mHelperLaneCount = reinterpret_cast<const uint32_t*>(counterData.data())[0];
            mActiveLaneCount = reinterpret_cast<const uint32_t*>(counterData.data())[1];
        }

    }

    if (cntMSAA > 1)
    {
        FALCOR_PROFILE(pRenderContext, "ResolveMSAA");
        pRenderContext->resolveResource(mpFbo->getColorTexture(0), mpResolvedTexture);
        pRenderContext->blit(mpResolvedTexture->getSRV(), pTargetFbo->getRenderTargetView(0));
    }

    // Save screenshot if requested (before ImGui is rendered)
    if (mRequestScreenshot)
    {
        saveScreenshot(pTargetFbo);
        mRequestScreenshot = false;
        if (mCaptureAndExit) shutdown();
    }
}

void HelperLaneViz::onGuiRender(Gui* pGui)
{
    const uint32_t width = std::min(680u, std::max(320u, getTargetFbo()->getWidth() * 2u / 5u));
    const uint32_t height = std::max(320u, getTargetFbo()->getHeight());
    const uint32_t controlsHeight = height * 3u / 5u;
    const uint32_t profilerY = 16u + controlsHeight + 12u;
    const auto flags = Gui::WindowFlags::ShowTitleBar | Gui::WindowFlags::AllowMove;
    if (!mUiLayoutInitialized)
    {
        // Establish this sample's default for Falcor's existing profiler window.
        // SampleApp renders its contents later in the same UI frame.
        Gui::Window profiler(pGui, "Profiler", {width, height - profilerY - 16u}, {16u, profilerY}, flags);
        profiler.windowPos(16u, profilerY);
        profiler.windowSize(width, height - profilerY - 16u);
    }
    Gui::Window w(pGui, "Helper Lane Sample", {width, controlsHeight}, {16, 16}, flags);
    if (!mUiLayoutInitialized)
    {
        w.windowPos(16, 16);
        w.windowSize(width, controlsHeight);
        mUiLayoutInitialized = true;
    }
    if (!mBenchmarkStatus.empty()) w.text(mBenchmarkStatus);
    if (!mBenchmarkOutputPath.empty()) w.text("Benchmark CSV: " + std::filesystem::path(mBenchmarkOutputPath).filename().string());
    if (benchmarkRunning())
    {
        w.text(fmt::format("Benchmark case {}/{}; configuration is locked.", mBenchmarkCaseIndex + 1, mBenchmarkCases.size()));
        w.text("GPU timing: flat raster draw; diagnostics run separately.");
        if (w.button("Cancel Benchmark")) finishBenchmarkSession("Benchmark cancelled.");
        return;
    }

    auto* profiler = getDevice()->getProfiler();
    if (profiler && profiler->isEnabled())
    {
        for (auto* event : profiler->getEvents())
        {
            if (event->getName() == "/onFrameRender")
                w.text(fmt::format("Render callback GPU: {:.3f} ms EMA (last {:.3f})", event->getGpuTimeAverage(), event->getGpuTime()));
            else if (event->getName() == "/onFrameRender/MeshDraw")
                w.text(fmt::format("Mesh draw GPU: {:.3f} ms EMA (last {:.3f})", event->getGpuTimeAverage(), event->getGpuTime()));
        }
    }
    w.text(fmt::format("Last geometry rebuild CPU: {:.3f} ms", mLastTriangulationMs));
    w.separator();

    uint32_t dimensionality = uint32_t(mGeometryDimensionality);
    const bool dimensionalityChanged = w.dropdown("Dimensionality", {{0, "Planar"}, {1, "Volumetric"}}, dimensionality);
    mGeometryDimensionality = GeometryDimensionality(dimensionality);
    const bool volumetric = mGeometryDimensionality == GeometryDimensionality::Volumetric;
    Gui::DropdownList views{{0, "Helper Lanes"}, {1, "Wireframe"}};
    if (volumetric) views.push_back({4, "Lit Preview (GraphEx Raster)"});
    else if (mVizMode == VizMode::LitPreview) mVizMode = VizMode::HelperLanes;
    uint32_t view = uint32_t(mVizMode);
    w.dropdown("Visualization", views, view);
    mVizMode = VizMode(view);
    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode == VizMode::LitPreview ? VizMode::HelperLanes : mVizMode)));
    mpProgram->addDefine("USE_3D_GEOMETRY", volumetric ? "1" : "0");
    if (w.dropdown("MSAA", {{1, "None"}, {2, "2x"}, {4, "4x"}, {8, "8x"}}, cntMSAA) && cntMSAA > 1) CreateMSAATargets();
    if (mVizMode == VizMode::HelperLanes)
    {
        w.checkbox("Read helper counters (synchronous)", mReadBackHelperLaneCount);
        if (mReadBackHelperLaneCount)
        {
            w.text(fmt::format("Helpers: {}   Active lanes: {}", mHelperLaneCount, mActiveLaneCount));
            w.text("Counter atomics and readback affect GPU timing.");
        }
    }
    mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", mReadBackHelperLaneCount && mVizMode == VizMode::HelperLanes ? "1" : "0");
    w.separator();

    if (volumetric)
    {
        bool changed = w.dropdown("Volume Geometry", {
            {0, "Sphere"}, {1, "Anisotropic Ellipsoid"}, {2, "Jittered Sphere"}, {3, "Lumpy Ellipsoid"},
            {4, "Saddle Stress Patch"}, {5, "Folded Sheet"}, {6, "Clustered Wave Patch"}, {7, "OBJ File"},
            {8, "Suzanne (OBJ)"}, {9, "Bunny (OBJ)"}, {10, "Teapot (OBJ)"}}, mVolumeGeometryType);
        if (changed)
        {
            if (isObjVolumeGeometry(mVolumeGeometryType)) mVolumeTriangulationType = 3;
            else applyVolumeTargetVertexCount();
        }
        changed |= w.dropdown("Volumetric Triangulation", {
            {3, "Source Faces"}, {0, "CDT"}, {1, "CDT + Flip"}, {2, "Greedy Point Set"},
            {4, "LMT (experimental)"}, {5, "Greedy Projection (experimental)"}}, mVolumeTriangulationType);
        if (isObjVolumeGeometry(mVolumeGeometryType))
        {
            if (mVolumeGeometryType == 7) changed |= w.textbox("OBJ Path", mVolumeObjPath);
            changed |= w.checkbox("Normalize OBJ to Scale", mVolumeNormalizeObj);
        }
        else
        {
            if (w.var("Target Source Vertices", mVolumeTargetVertexCount, 9u, 16384u))
            {
                applyVolumeTargetVertexCount();
                changed = true;
            }
            if (auto resolution = w.group("Segment counts"))
            {
                bool segments = resolution.var("Latitude/U", mVolumeLatitudeSegments, 3u, 128u);
                segments |= resolution.var("Longitude/V", mVolumeLongitudeSegments, 3u, 128u);
                if (segments)
                {
                    mVolumeTargetVertexCount = estimateVolumeSourceVertexCount(mVolumeGeometryType, mVolumeLatitudeSegments, mVolumeLongitudeSegments);
                    changed = true;
                }
            }
        }
        if (!isObjVolumeGeometry(mVolumeGeometryType) || mVolumeNormalizeObj)
            changed |= w.var("Base Radius/Scale", mVolumeRadius, 0.01f, 5.0f);
        if (mVolumeTriangulationType != 3)
        {
            if (auto patch = w.group("Patch settings"))
            {
                changed |= patch.var("Max Normal Deviation", mVolumeMaxNormalDeviationDegrees, 0.1f, 90.f);
                changed |= patch.var("Max Plane Deviation", mVolumeMaxRelativePlaneDeviation, 0.00001f, 0.1f);
                changed |= patch.var("Max Faces Per Patch", mVolumeMaxFacesPerPatch, 1u, 2048u);
            }
            if (mVolumeTriangulationType == 1 || mVolumeTriangulationType == 4)
                changed |= w.checkbox("Score local flips in 3D", mVolumeSurfaceLocalFlips);
            if (mVolumeTriangulationType == 5)
            {
                if (auto gp = w.group("Greedy projection settings"))
                {
                    changed |= gp.var("Max Neighbors", mVolumePclMaximumNearestNeighbors, 3u, 512u);
                    changed |= gp.var("Mu", mVolumePclMu, 0.1f, 10.f);
                    changed |= gp.var("Search Radius", mVolumePclSearchRadius, 0.f, 100.f);
                    changed |= gp.var("Min Angle", mVolumePclMinimumAngleDegrees, 0.f, 60.f);
                    changed |= gp.var("Max Angle", mVolumePclMaximumAngleDegrees, 60.f, 179.f);
                }
            }
        }
        bool instances = w.var("Instance Count", mVolumeInstanceCount, 1u, 100000u);
        if (mVolumeInstanceCount > 1) instances |= w.checkbox("3D Instance Grid", mVolumeUse3DInstancing);
        if (dimensionalityChanged || changed) generateVolumeGeometry();
        if (instances)
        {
            updateVolumeGridParams();
            updateCameraParams();
        }
        if (!mVolumeResultValid) w.text("Source fallback or failed audit; excluded from benchmark rankings.");
        if (!mVolumeObjStatus.empty()) w.text(mVolumeObjStatus);
        if (auto stats = w.group("Mesh statistics"))
        {
            stats.text(fmt::format("Source: {} vertices, {} faces, {} triangles", mVolumeSourceVertexCount, mVolumeSourceFaceCount, mVolumeSourceTriangleCount));
            stats.text(fmt::format("Output: {} vertices, {} triangles", mVolumeVertices.size(), mVolumeIndices.size() / 3));
            stats.text(fmt::format("Patches: {}   Failed: {}", mVolumePatchCount, mVolumeFailedPatchCount));
            stats.text(fmt::format("Reference error: {:.6g}", mVolumeReferenceError));
            stats.text(fmt::format("Instance grid: {} x {} x {}", mVolumeGridCols, mVolumeGridRows, mVolumeGridLayers));
            if (mVolumeObjPolygonFaceCount) stats.text(fmt::format("OBJ polygon faces: {}", mVolumeObjPolygonFaceCount));
        }
        if (mVizMode == VizMode::LitPreview) mLitSettings.drawGui(w);
        if (w.button("Run Benchmark")) startVolumeBenchmark();

    }
    else
    {
        bool changed = w.checkbox("Use Circle", mUseCircle);
        changed |= w.dropdown("Triangulation", {
            {0, "Ear Clipping"}, {1, "MWT"}, {2, "Centroid Fan"}, {3, "Greedy"}, {4, "Strip"},
            {5, "MaxMin"}, {6, "MinMax"}, {7, "CDT"}, {8, "Earcut (Mapbox)"}, {9, "Earcut + Flip"}, {10, "CDT + Flip"}}, mTriangulationType);
        changed |= w.checkbox("Use Mesh Optimizer", mUseMeshOptimizer);
        if (mUseCircle)
        {
            changed |= w.var("Vertex Count", mCircleVertexCount, 3u, 1000u);
            changed |= w.var("Radius X", mEllipseRadiusX, 0.01f, 0.5f);
            changed |= w.var("Radius Y", mEllipseRadiusY, 0.01f, 0.5f);
            if (changed || dimensionalityChanged) generateCircle();
        }
        else
        {
            changed |= w.var("Max Bezier Deviation", mMaxBezierDeviation, 0.1f, 100.f);
            changed |= w.textbox("SVG Path", mSvgPath);
            if (changed || dimensionalityChanged) loadSvg(mSvgPath);
        }
        bool grid = w.var("Grid Cols", mGridCols, 1u, 100u);
        grid |= w.var("Grid Rows", mGridRows, 1u, 100u);
        if (grid) updateGridParams();
        if (w.button("Run Benchmark")) startPlanarBenchmark();
        if (w.button("Benchmark Folder"))
        {
            const auto path = selectFolder();
            if (!path.empty()) benchmarkFolder(path);
        }
    }
    if (auto capture = w.group("PNG capture"))
    {
        capture.textbox("PNG Path", mScreenshotPath);
        if (capture.button("Save Screenshot")) mRequestScreenshot = true;
    }
}

bool HelperLaneViz::onKeyEvent(const KeyboardEvent& keyEvent)
{
    return false;
}
bool HelperLaneViz::onMouseEvent(const MouseEvent& mouseEvent)
{
    return false;
}
void HelperLaneViz::onHotReload(HotReloadFlags reloaded) {}

int runPerformanceBenchmark(int argc, char** argv);
int runRegressionChecks();
int runInteractiveSmoke();

int runMain(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--ui-smoke")
        return runInteractiveSmoke();
    if (argc > 1 && std::string(argv[1]) == "--self-test")
        return runRegressionChecks();
    if (argc > 1 && std::string(argv[1]) == "--perf")
        return runPerformanceBenchmark(argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--screenshot")
        return runScreenshot(argc, argv);
    SampleAppConfig config;
    config.windowDesc.title = "Helper Lane Visualizer";
    config.windowDesc.width = 1920;
    config.windowDesc.height = 1080;
    config.windowDesc.resizableWindow = true;

    HelperLaneViz project(config);
    return project.run();
}

int main(int argc, char** argv)
{
    return catchAndReportAllExceptions([&]() { return runMain(argc, argv); });
}
