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
#include "Triangulation/3D/ProceduralGeometry3D.h"
#include "Triangulation/3D/Triangulation3D.h"
#include "Triangulation/2D/TriangulationHelpers.h"
#include "Import/SVGLoader.h"

#include "Falcor.h"
#include "Core/Program/ProgramManager.h"

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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/earcut.hpp/vendor/glfw/deps/stb_image_write.h"


using namespace Falcor;

FALCOR_EXPORT_D3D12_AGILITY_SDK

uint32_t mSampleGuiWidth = 250;
uint32_t mSampleGuiHeight = 200;
uint32_t mSampleGuiPositionX = 20;
uint32_t mSampleGuiPositionY = 40;

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
    mpVars = ProgramVars::create(getDevice(), mpProgram.get());
    mpState = GraphicsState::create(getDevice());
    mpState->setProgram(mpProgram);
    DepthStencilState::Desc depth;
    depth.setDepthEnabled(true).setDepthWriteMask(true).setDepthFunc(ComparisonFunc::Less);
    mpState->setDepthStencilState(DepthStencilState::create(depth));
    mpDrawTimer = GpuTimer::create(getDevice());

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
    loadSvg(mSvgPath);
    generateVolumeGeometry();

    getDevice()->getProfiler()->setEnabled(true);

    updateGridParams();
    updateVolumeModelParams();
    updateCameraParams();

    mpHelperLaneCounter = getDevice()->createTexture2D(
        2, 1, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);

    uint32_t texWidth = 2048;
    uint32_t texHeight = 2048;
    std::vector<uint32_t> pixels(texWidth * texHeight);
    uint32_t noise = 173;
    for (auto& pixel : pixels)
    {
        noise = noise * 1664525u + 1013904223u;
        pixel = 0xff000000u | (noise & 0x00ffffffu);
    }
    mpDummyTexture = getDevice()->createTexture2D(
        texWidth, texHeight, ResourceFormat::RGBA8Unorm, 1, 1, pixels.data(), ResourceBindFlags::ShaderResource
    );

    mpVars->setTexture("gDummyTexture", mpDummyTexture);
    mpVars->getRootVar()["gDummySampler"] = make_ref<Sampler>(getDevice(), Sampler::Desc());

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

    if (mMeasureTriangulationTime)
    {
        auto end = std::chrono::high_resolution_clock::now();
        mLastTriangulationMs = std::chrono::duration<double, std::milli>(end - start).count();
    }
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

    if (mMeasureTriangulationTime)
    {
        auto end = std::chrono::high_resolution_clock::now();
        mLastTriangulationMs = std::chrono::duration<double, std::milli>(end - start).count();
    }

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
    std::ostringstream sourceKey;
    sourceKey << std::setprecision(9) << mVolumeGeometryType << ':' << mVolumeLatitudeSegments << ':' << mVolumeLongitudeSegments << ':'
              << mVolumeRadius << ':' << mVolumeNormalizeObj << ':' << mVolumeObjPath;
    if (isObjVolumeGeometry(mVolumeGeometryType) && !benchmarkRunning() && std::filesystem::exists(mVolumeObjPath))
        sourceKey << ':' << std::filesystem::last_write_time(mVolumeObjPath).time_since_epoch().count();
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
        case 7:
        {
            ObjMeshLoader::LoadOptions loadOptions;
            loadOptions.centerAndScale = mVolumeNormalizeObj;
            loadOptions.targetRadius = mVolumeRadius;
            ObjMeshLoader::LoadResult loadResult = ObjMeshLoader::load(mVolumeObjPath, loadOptions);
            sourceMesh = std::move(loadResult.mesh);
            mVolumeObjStatus = loadResult.message;
            mVolumeObjPolygonFaceCount = loadResult.polygonFaceCount;
            if (!loadResult.warnings.empty())
            {
                mVolumeObjStatus += " Warnings: " + std::to_string(loadResult.warnings.size());
            }
            if (loadResult.success)
            {
                mVolumeGeometryName = "OBJ: " + std::filesystem::path(mVolumeObjPath).filename().string();
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
        mVolumeGeometryName = "OBJ: " + std::filesystem::path(mVolumeObjPath).filename().string();
    std::ostringstream patchKey;
    patchKey << std::setprecision(9) << mVolumeMaxNormalDeviationDegrees << ':' << mVolumeMaxRelativePlaneDeviation << ':'
             << mVolumeMaxFacesPerPatch;
    if (patchKey.str() != mVolumePatchKey)
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
    mVolumeReferenceError = 0;
    if (result.success && mVolumeCachedBaseline.success)
    {
        auto check = MeshValidation::audit(result, mVolumeCachedBaseline);
        mVolumeReferenceError = mVolumeReference->sampledError(result);
        float3 lo = sourceMesh.vertices.front(), hi = lo;
        for (auto p : sourceMesh.vertices)
        {
            lo = math::min(lo, p);
            hi = math::max(hi, p);
        }
        mVolumeResultValid = result.status == Triangulation3D::ResultStatus::Complete && check.topologyOK() &&
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

    if (mMeasureTriangulationTime)
    {
        auto end = std::chrono::high_resolution_clock::now();
        mLastTriangulationMs = std::chrono::duration<double, std::milli>(end - start).count();
    }

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
        mVolumeGridCols = std::max(1u, static_cast<uint32_t>(std::ceil(std::cbrt(static_cast<float>(instanceCount) * gridAspect))));
        const uint32_t remainingAfterCols = std::max(1u, (instanceCount + mVolumeGridCols - 1u) / mVolumeGridCols);
        mVolumeGridRows = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(remainingAfterCols)))));
        mVolumeGridLayers = std::max(1u, (instanceCount + mVolumeGridCols * mVolumeGridRows - 1u) / (mVolumeGridCols * mVolumeGridRows));
    }
    else
    {
        mVolumeGridCols = std::max(1u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(instanceCount) * gridAspect))));
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
    if (mBenchmarkFile.is_open())
        mBenchmarkFile.close();
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
    try
    {
        if (mFolderBenchmark.active)
        {
            processBenchmarkFolderStep(pRenderContext);
        }
        else if (mVolumeBenchmark.active)
        {
            processVolumeBenchmarkStep(pRenderContext);
        }
        else if (mPlanarBenchmark.active)
        {
            processBenchmarkStep(pRenderContext);
        }
    }
    catch (const std::exception& e)
    {
        finishBenchmarkSession(std::string("Benchmark failed: ") + e.what());
    }

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

    if (mpHelperLaneCounter)
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

    mLastDrawMs = std::numeric_limits<double>::quiet_NaN();
    if (mIndexCount)
    {
        const bool timeDraw = benchmarkRunning() && !mReadBackHelperLaneCount;
        if (timeDraw)
            mpDrawTimer->begin();
        uint32_t instanceCount =
            mGeometryDimensionality == GeometryDimensionality::Volumetric ? std::max(mVolumeInstanceCount, 1u) : mGridCols * mGridRows;
        pRenderContext->drawIndexedInstanced(mpState.get(), mpVars.get(), mIndexCount, instanceCount, 0, 0, 0);
        if (timeDraw)
        {
            mpDrawTimer->end();
            mpDrawTimer->resolve();
            pRenderContext->submit(true);
            mLastDrawMs = mpDrawTimer->getElapsedTime();
        }
    }

    if (mpHelperLaneCounter && mIndexCount && mReadBackHelperLaneCount)
    {
        std::vector<uint8_t> counterData = pRenderContext->readTextureSubresource(mpHelperLaneCounter->asTexture().get(), 0);
        mHelperLaneCount = reinterpret_cast<const uint32_t*>(counterData.data())[0];
        mActiveLaneCount = reinterpret_cast<const uint32_t*>(counterData.data())[1];
    }

    if (cntMSAA > 1)
    {
        pRenderContext->resolveResource(mpFbo->getColorTexture(0), mpResolvedTexture);
        pRenderContext->blit(mpResolvedTexture->getSRV(), pTargetFbo->getRenderTargetView(0));
    }

    // Save screenshot if requested (before ImGui is rendered)
    if (mRequestScreenshot)
    {
        saveScreenshot(pRenderContext, pTargetFbo);
        mRequestScreenshot = false;
    }
}

static float linearToSrgb(float x)
{
    x = std::max(0.0f, std::min(1.0f, x));
    if (x <= 0.0031308f)
        return 12.92f * x;
    return 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

// Helper function to convert float16 (stored as uint16) to float
static float float16ToFloat(uint16_t halfVal)
{
    // IEEE 754 half-precision float format:
    // Sign: 1 bit, Exponent: 5 bits, Mantissa: 10 bits
    uint32_t sign = (halfVal & 0x8000) << 16;
    uint32_t exp = (halfVal & 0x7C00) >> 10;
    uint32_t mantissa = halfVal & 0x03FF;

    if (exp == 0)
    {
        // Zero or denormal
        if (mantissa == 0)
        {
            return sign ? -0.0f : 0.0f;
        }
        // Denormal: convert to normalized float
        // Denormals use implicit leading 0 instead of 1
        // Value = mantissa * 2^(-14) * 2^(-10) = mantissa * 2^(-24)
        float val = static_cast<float>(mantissa) * (1.0f / 16777216.0f); // 2^-24
        return sign ? -val : val;
    }
    else if (exp == 31)
    {
        // Infinity or NaN
        if (mantissa == 0)
        {
            return sign ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
        }
        return std::numeric_limits<float>::quiet_NaN();
    }
    else
    {
        // Normal number
        // Float32: sign(1) + exp(8) + mantissa(23)
        // Float16: sign(1) + exp(5) + mantissa(10)
        // Convert: exp16 - 15 + 127 = exp32
        uint32_t exp32 = (exp - 15 + 127) << 23;
        uint32_t mantissa32 = mantissa << 13; // Shift left by (23-10) = 13
        uint32_t bits = sign | exp32 | mantissa32;
        return *reinterpret_cast<float*>(&bits);
    }
}

void HelperLaneViz::saveScreenshot(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    // Get the render target texture (without ImGui overlay)
    ref<Texture> renderTexture;
    if (cntMSAA > 1)
    {
        // Use the resolved texture if MSAA is enabled
        renderTexture = mpResolvedTexture;
    }
    else
    {
        // Use the target FBO texture directly
        renderTexture = pTargetFbo->getColorTexture(0);
    }

    if (!renderTexture)
    {
        return;
    }

    // Get texture dimensions
    uint32_t width = renderTexture->getWidth();
    uint32_t height = renderTexture->getHeight();

    // Read texture data (RGBA16Float format)
    std::vector<uint8_t> textureData = pRenderContext->readTextureSubresource(renderTexture.get(), 0);

    size_t pixelCount = width * height;
    std::vector<uint8_t> rgba8Data(pixelCount * 4);

    const uint16_t* srcData = reinterpret_cast<const uint16_t*>(textureData.data());
    for (size_t i = 0; i < pixelCount; ++i)
    {
        size_t srcIdx = i * 4; // 4 channels (RGBA), each 2 bytes
        auto convertHalfToUint8_sRGB = [](uint16_t halfVal) -> uint8_t
        {
            float lin = float16ToFloat(halfVal);

            // (optional tone map if HDR; keeping minimal diff: just clamp)
            lin = std::max(0.0f, std::min(1.0f, lin));

            float srgb = linearToSrgb(lin);
            return static_cast<uint8_t>(srgb * 255.0f + 0.5f);
        };

        auto convertHalfToUint8_linear = [](uint16_t halfVal) -> uint8_t
        {
            float lin = float16ToFloat(halfVal);
            lin = std::max(0.0f, std::min(1.0f, lin));
            return static_cast<uint8_t>(lin * 255.0f + 0.5f);
        };

        rgba8Data[i * 4 + 0] = convertHalfToUint8_sRGB(srcData[srcIdx + 0]);   // R
        rgba8Data[i * 4 + 1] = convertHalfToUint8_sRGB(srcData[srcIdx + 1]);   // G
        rgba8Data[i * 4 + 2] = convertHalfToUint8_sRGB(srcData[srcIdx + 2]);   // B
        rgba8Data[i * 4 + 3] = convertHalfToUint8_linear(srcData[srcIdx + 3]); // A
    }

    // Flip vertically
    std::vector<uint8_t> flippedData(pixelCount * 4);
    for (uint32_t y = 0; y < height; ++y)
    {
        uint32_t srcRow = height - 1 - y;
        memcpy(&flippedData[y * width * 4], &rgba8Data[srcRow * width * 4], width * 4);
    }

    // Generate filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);

    std::string downloadsPath = "Folder to save your image";
    std::filesystem::create_directories(downloadsPath); // Ensure directory exists

    char filename[256];
    snprintf(
        filename,
        sizeof(filename),
        "%s/screenshot_%04d%02d%02d_%02d%02d%02d.png",
        downloadsPath.c_str(),
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
    );

    // Save as PNG using stb_image_write (lossless format for pixel precision)
    int stride = width * 4;
    int result = stbi_write_png(filename, width, height, 4, flippedData.data(), stride);

    if (result)
    {
        // Success - could log this if needed
    }
}

void HelperLaneViz::onGuiRender(Gui* pGui)
{
    Gui::Window w(pGui, "Helper Lane Sample", {300, 150});
    if (benchmarkRunning())
    {
        w.text("Benchmark running; configuration is locked.");
        w.text("GPU timing: isolated draw. Diagnostics run separately.");
        if (w.button("Cancel Benchmark"))
            finishBenchmarkSession("Benchmark cancelled.");
        return;
    }

    // Display GPU frametime
    Profiler* pProfiler = getDevice()->getProfiler();
    if (pProfiler && pProfiler->isEnabled())
    {
        const auto& events = pProfiler->getEvents();
        for (auto* pEvent : events)
        {
            if (pEvent->getName() == "/onFrameRender")
            {
                float gpuTime = pEvent->getGpuTime(); // in milliseconds
                w.text("GPU Frame Time: %.2f ms", gpuTime);
                break;
            }
        }
    }

    // MSAA
    Gui::DropdownList msaaTypes = {{1, "None"}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}};
    bool msaaChanged = w.dropdown("MSAA", msaaTypes, cntMSAA);
    if (msaaChanged && cntMSAA > 1u)
        CreateMSAATargets();

    Gui::DropdownList dimensionalityTypes = {{0, "Planar"}, {1, "Volumetric"}};
    uint32_t dimensionality = static_cast<uint32_t>(mGeometryDimensionality);
    bool dimensionalityChanged = w.dropdown("Dimensionality", dimensionalityTypes, dimensionality);
    if (dimensionalityChanged)
    {
        mGeometryDimensionality = static_cast<GeometryDimensionality>(dimensionality);
    }
    const bool isVolumetric = mGeometryDimensionality == GeometryDimensionality::Volumetric;

    bool modeChanged = false;
    if (!isVolumetric)
    {
        modeChanged = w.checkbox("Use Circle", mUseCircle);
    }

    // Visualization mode
    Gui::DropdownList vizModes = {{0, "Helper Lanes"}, {1, "Wireframe"}, {2, "Texture"}, {3, "Synthetic Wave + 8 samples"}};
    w.dropdown("Visualization", vizModes, *(uint32_t*)&mVizMode);

    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));
    mpProgram->addDefine("USE_3D_GEOMETRY", isVolumetric ? "1" : "0");
    if (dimensionalityChanged)
        updateCameraParams();

    if (mVizMode == VizMode::Texture || mVizMode == VizMode::MaterialStress || (mVizMode == VizMode::HelperLanes && mUseDummyTexture))
    {
        mpVars->setTexture("gDummyTexture", mpDummyTexture);
    }

    mpProgram->addDefine("USE_DUMMY_TEXTURE", mVizMode == VizMode::HelperLanes && mUseDummyTexture ? "1" : "0");

    w.separator();

    w.checkbox("Read back helper lane count", mReadBackHelperLaneCount);

    if (mReadBackHelperLaneCount)
    {
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "1");
        std::string helperLaneCountStr = "Helper lane count: " + std::to_string(mHelperLaneCount);
        w.text(helperLaneCountStr);
    }
    else
    {
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
    }

    w.checkbox("Bind dummy texture (force helper lanes)", mUseDummyTexture);

    if (mUseDummyTexture)
    {
        mpProgram->addDefine("USE_DUMMY_TEXTURE", "1");
        mpVars->setTexture("gDummyTexture", mpDummyTexture);
    }

    w.checkbox("Measure triangulation time", mMeasureTriangulationTime);

    if (mMeasureTriangulationTime)
    {
        w.text("Last triangulation: %.3f ms", mLastTriangulationMs);
    }

    w.separator();

    if (isVolumetric)
    {
        Gui::DropdownList volumeGeometryTypes = {
            {0, "Sphere"},
            {1, "Anisotropic Ellipsoid"},
            {2, "Jittered Sphere"},
            {3, "Lumpy Ellipsoid"},
            {4, "Saddle Stress Patch"},
            {5, "Folded Sheet"},
            {6, "Clustered Wave Patch"},
            {7, "OBJ File"}
        };
        bool geometryChanged = w.dropdown("Volume Geometry", volumeGeometryTypes, mVolumeGeometryType);
        if (geometryChanged)
        {
            if (isObjVolumeGeometry(mVolumeGeometryType))
            {
                mVolumeTriangulationType = 3;
            }
            else
            {
                applyVolumeTargetVertexCount();
            }
        }
        bool volumeChanged = geometryChanged;

        Gui::DropdownList volumeTriangTypes = {
            {3, "Source Faces"},
            {0, "CDT"},
            {1, "CDT + Flip"},
            {2, "Greedy Point Set"},
            {4, "LMT Heuristic"},
            {5, "Custom Greedy Projection"}
        };
        volumeChanged |= w.dropdown("Volumetric Triangulation", volumeTriangTypes, mVolumeTriangulationType);
        volumeChanged |= w.checkbox("Use 3D length for local flips", mVolumeSurfaceLocalFlips);

        if (isObjVolumeGeometry(mVolumeGeometryType))
        {
            volumeChanged |= w.textbox("OBJ Path", mVolumeObjPath);
            volumeChanged |= w.checkbox("Normalize OBJ to Scale", mVolumeNormalizeObj);
        }
        else
        {
            bool targetVertexCountChanged = w.var("Target Source Vertices", mVolumeTargetVertexCount, 9u, 16384u);
            if (targetVertexCountChanged)
            {
                applyVolumeTargetVertexCount();
                volumeChanged = true;
            }
            bool segmentChanged = false;
            segmentChanged |= w.var("Latitude/U Segments", mVolumeLatitudeSegments, 3u, 128u);
            segmentChanged |= w.var("Longitude/V Segments", mVolumeLongitudeSegments, 3u, 128u);
            if (segmentChanged)
            {
                mVolumeTargetVertexCount =
                    estimateVolumeSourceVertexCount(mVolumeGeometryType, mVolumeLatitudeSegments, mVolumeLongitudeSegments);
                volumeChanged = true;
            }
        }
        volumeChanged |= w.var("Base Radius/Scale", mVolumeRadius, 0.01f, 5.0f);
        volumeChanged |= w.var("Max Normal Deviation", mVolumeMaxNormalDeviationDegrees, 0.1f, 90.0f);
        volumeChanged |= w.var("Max Plane Deviation", mVolumeMaxRelativePlaneDeviation, 0.00001f, 0.1f);
        volumeChanged |= w.var("Max Faces Per Patch", mVolumeMaxFacesPerPatch, 1u, 2048u);
        if (mVolumeTriangulationType == 5)
        {
            volumeChanged |= w.var("GP Max Neighbors", mVolumePclMaximumNearestNeighbors, 3u, 512u);
            volumeChanged |= w.var("GP Mu", mVolumePclMu, 0.1f, 10.0f);
            volumeChanged |= w.var("GP Search Radius", mVolumePclSearchRadius, 0.0f, 100.0f);
            volumeChanged |= w.var("GP Min Angle", mVolumePclMinimumAngleDegrees, 0.0f, 60.0f);
            volumeChanged |= w.var("GP Max Angle", mVolumePclMaximumAngleDegrees, 60.0f, 179.0f);
        }
        bool volumeInstanceChanged = w.var("Instance Count", mVolumeInstanceCount, 1u, 100000u);
        volumeInstanceChanged |= w.checkbox("3D Instance Grid", mVolumeUse3DInstancing);

        if (dimensionalityChanged || volumeChanged)
        {
            generateVolumeGeometry();
        }
        if (volumeInstanceChanged)
        {
            updateVolumeGridParams();
            updateCameraParams();
        }
        else if (!mIndexCount)
        {
            uploadVolumeGeometry();
        }

        w.text("Geometry: " + mVolumeGeometryName);
        w.text("Source vertices: %u", mVolumeSourceVertexCount);
        w.text("Source faces: %u", mVolumeSourceFaceCount);
        w.text("Source triangles: %u", mVolumeSourceTriangleCount);
        w.text("Output vertices: %u", static_cast<uint32_t>(mVolumeVertices.size()));
        w.text("Triangles: %u", static_cast<uint32_t>(mVolumeIndices.size() / 3));
        if (isObjVolumeGeometry(mVolumeGeometryType) && !mVolumeObjStatus.empty())
        {
            w.text(mVolumeObjStatus);
            if (mVolumeObjPolygonFaceCount > 0)
            {
                w.text("Fan-triangulated OBJ polygon faces: %u", mVolumeObjPolygonFaceCount);
            }
        }
        if (mVolumeUse3DInstancing)
        {
            w.text(
                "Instance grid: " + std::to_string(mVolumeGridCols) + " x " + std::to_string(mVolumeGridRows) + " x " +
                std::to_string(mVolumeGridLayers)
            );
        }
        else
        {
            w.text("Instance grid: " + std::to_string(mVolumeGridCols) + " x " + std::to_string(mVolumeGridRows));
        }
        w.text("Patches: %u", mVolumePatchCount);
        if (!mVolumeResultValid)
            w.text("Excluded from rankings: source fallback or failed quality audit.");
        if (mVolumeFailedPatchCount > 0)
        {
            w.text("Failed patches: %u", mVolumeFailedPatchCount);
        }

        if (w.button("Run Benchmark"))
        {
            startVolumeBenchmark();
        }
        if (mVolumeBenchmark.active)
        {
            w.text(
                "Benchmarking volumetric method " + std::to_string(mVolumeBenchmark.method + 1) + " / " +
                std::to_string(mVolumeBenchmark.methods.size())
            );
            w.text(
                "Orientation " + std::to_string(std::min(mVolumeBenchmark.orientation + 1u, kVolumeBenchmarkOrientationCount)) + " / " +
                std::to_string(kVolumeBenchmarkOrientationCount)
            );
        }
        if (!mVolumeBenchmark.outputPath.empty())
        {
            w.text("Benchmark CSV: %s", mVolumeBenchmark.outputPath.c_str());
        }
    }
    else
    {
        // Triangulation type
        Gui::DropdownList triangTypes = {
            {0, "Ear Clipping"},
            {1, "MWT"},
            {2, "Centroid Fan"},
            {3, "Greedy"},
            {4, "Strip"},
            {5, "MaxMin"},
            {6, "MinMax"},
            {7, "CDT"},
            {8, "Earcut (Mapbox)"},
            {9, "Earcut + Flip"},
            {10, "CDT + Flip"}
        };
        bool triangChanged = w.dropdown("Triangulation", triangTypes, mTriangulationType);

        bool meshOptChanged = w.checkbox("Use Mesh Optimizer", mUseMeshOptimizer);

        if (mUseCircle)
        {
            bool circleChanged = false;
            circleChanged |= w.var("Vertex Count", mCircleVertexCount, 3u, 1000u);
            circleChanged |= w.var("Radius X", mEllipseRadiusX, 0.01f, 0.5f);
            circleChanged |= w.var("Radius Y", mEllipseRadiusY, 0.01f, 0.5f);
            if (circleChanged || modeChanged || triangChanged || meshOptChanged || dimensionalityChanged)
            {
                generateCircle();
            }
        }
        else
        {
            bool planarChanged = false;
            planarChanged |= w.var("Max Bezier Deviation", mMaxBezierDeviation, 0.1f, 100.0f);
            planarChanged |= w.textbox("SVG Path", mSvgPath);
            planarChanged |= modeChanged;
            planarChanged |= triangChanged;
            planarChanged |= meshOptChanged;
            planarChanged |= dimensionalityChanged;
            if (planarChanged)
            {
                loadSvg(mSvgPath);
            }
        }

        w.separator();
        bool gridChanged = false;
        gridChanged |= w.var("Grid Cols", mGridCols, 1u, 100u);
        gridChanged |= w.var("Grid Rows", mGridRows, 1u, 100u);
        if (gridChanged)
        {
            updateGridParams();
        }

        w.separator();
        if (w.button("Run Benchmark"))
        {
            beginBenchmarkSession();
            mPlanarBenchmark.active = true;
            mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
            mPlanarBenchmark.currentMethod = 0;
            mPlanarBenchmark.waitFrameCounter = 0;

            mPlanarBenchmark.methods = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10};
            mBenchmarkLog.clear();

            mFolderBenchmark.outputPath = makeTimestampedCsvPath("helper_lane_planar_benchmark");
            std::filesystem::create_directories(std::filesystem::path(mFolderBenchmark.outputPath).parent_path());
            mBenchmarkFile.open(mFolderBenchmark.outputPath, std::ios::out | std::ios::trunc);
            if (mBenchmarkFile.is_open())
                mBenchmarkFile << "Method,CPU_Time_ms,GPU_DrawMedian_ms,GPU_DrawMean_ms,GPU_DrawStdDev_ms,HelperLaneCount,EdgeLength,MSAA,"
                                  "GridCols,GridRows,Shader\n";
            writeBenchmarkMetadata(mFolderBenchmark.outputPath);
        }

        if (!mFolderBenchmark.outputPath.empty())
        {
            w.text("Benchmark CSV: %s", mFolderBenchmark.outputPath.c_str());
        }

        if (w.button("Benchmark Folder"))
        {
            std::string folderPath = selectFolder();
            if (!folderPath.empty())
            {
                benchmarkFolder(folderPath);
            }
        }
    }

    w.separator();
    if (w.button("Save Screenshot"))
    {
        mRequestScreenshot = true;
    }

    w.separator();
    w.text("Window Size");
    bool windowSizeChanged = false;
    windowSizeChanged |= w.var("Width", mDesiredWindowWidth, 100u, 7680u);
    windowSizeChanged |= w.var("Height", mDesiredWindowHeight, 100u, 4320u);
    if (w.button("Set Window Size"))
    {
        resizeWindow(mDesiredWindowWidth, mDesiredWindowHeight);
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

void HelperLaneViz::resizeWindow(uint32_t width, uint32_t height)
{
// Use Windows API to find and resize our window by title
#ifdef _WIN32
    HWND hwnd = FindWindowA(NULL, "Falcor Project Template");
    if (!hwnd)
    {
        // Try alternative: find window by class name (GLFW uses "GLFW30" as default class name)
        hwnd = FindWindowA("GLFW30", NULL);
    }

    if (!hwnd)
    {
        // Last resort: use foreground window (might not be our window, but better than nothing)
        hwnd = GetForegroundWindow();
    }

    if (hwnd)
    {
        // Get current window position to maintain it
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int x = rect.left;
        int y = rect.top;

        // Resize using Windows API
        // Note: SetWindowPos uses client area size, so we need to account for window frame
        // For simplicity, we'll use MoveWindow which works with client area
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int currentClientWidth = clientRect.right - clientRect.left;
        int currentClientHeight = clientRect.bottom - clientRect.top;

        // Calculate the difference between window size and client size (frame size)
        int frameWidth = (rect.right - rect.left) - currentClientWidth;
        int frameHeight = (rect.bottom - rect.top) - currentClientHeight;

        // Resize window (including frame) to achieve desired client size
        SetWindowPos(
            hwnd, NULL, x, y, static_cast<int>(width) + frameWidth, static_cast<int>(height) + frameHeight, SWP_NOZORDER | SWP_NOACTIVATE
        );
    }
#endif
}

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
    SampleAppConfig config;
    config.windowDesc.title = "Falcor Project Template";
    config.windowDesc.resizableWindow = true;

    HelperLaneViz project(config);
    return project.run();
}

int main(int argc, char** argv)
{
    return catchAndReportAllExceptions([&]() { return runMain(argc, argv); });
}
