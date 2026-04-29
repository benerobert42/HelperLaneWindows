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

#include "ObjMeshLoader.h"
#include "ProceduralGeometry3D.h"
#include "Triangulation3D.h"
#include "TriangulationHelpers.h"
#include "SVGLoader.h"

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

struct GridParams
{
    uint32_t cols;
    uint32_t rows;
    float2 cellSize;
    float2 origin;
    float scale;
};

struct CameraParams
{
    float4x4 viewProj;
};

struct VolumeModelParams
{
    float4x4 transform;
};

struct VolumeGridParams
{
    uint32_t instanceCount;
    uint32_t cols;
    uint32_t rows;
    uint32_t layers;
    uint32_t use3DGrid;
    float spacing;
    float2 padding;
};

Triangulation3D::Method getVolumeTriangulationMethod(uint32_t type)
{
    switch (type)
    {
    case 1:
        return Triangulation3D::Method::ConstrainedDelaunayFlipped;
    case 2:
        return Triangulation3D::Method::GreedyPointSet;
    case 4:
        return Triangulation3D::Method::LmtMinimumWeight;
    case 5:
        return Triangulation3D::Method::PclGreedyProjection;
    case 0:
    default:
        return Triangulation3D::Method::ConstrainedDelaunay;
    }
}

std::string getVolumeTriangulationMethodName(uint32_t type)
{
    if (type == 3)
    {
        return "Source Faces";
    }
    return Triangulation3D::getMethodName(getVolumeTriangulationMethod(type));
}

std::string getVolumeGeometryName(uint32_t type)
{
    switch (type)
    {
    case 1:
        return "Anisotropic Ellipsoid";
    case 2:
        return "Jittered Sphere";
    case 3:
        return "Lumpy Ellipsoid";
    case 4:
        return "Saddle Stress Patch";
    case 5:
        return "Folded Sheet";
    case 6:
        return "Clustered Wave Patch";
    case 7:
        return "OBJ File";
    case 0:
    default:
        return "Sphere";
    }
}

bool isObjVolumeGeometry(uint32_t type)
{
    return type == 7;
}

bool isOpenPatchVolumeGeometry(uint32_t type)
{
    return type == 4 || type == 5 || type == 6;
}

uint32_t estimateVolumeSourceVertexCount(uint32_t geometryType, uint32_t latitudeSegments, uint32_t longitudeSegments)
{
    latitudeSegments = std::max(latitudeSegments, 3u);
    longitudeSegments = std::max(longitudeSegments, 3u);

    if (isOpenPatchVolumeGeometry(geometryType))
    {
        return (latitudeSegments + 1u) * (longitudeSegments + 1u);
    }

    return 2u + (latitudeSegments - 1u) * longitudeSegments;
}

void chooseVolumeSegmentsForTarget(uint32_t geometryType, uint32_t targetVertexCount, uint32_t& latitudeSegments, uint32_t& longitudeSegments)
{
    targetVertexCount = std::max(targetVertexCount, 9u);

    const float preferredRatio = isOpenPatchVolumeGeometry(geometryType) ? 1.0f : 1.75f;
    uint32_t bestLatitude = latitudeSegments;
    uint32_t bestLongitude = longitudeSegments;
    uint64_t bestError = std::numeric_limits<uint64_t>::max();
    float bestRatioError = std::numeric_limits<float>::max();

    for (uint32_t lat = 3; lat <= 128; ++lat)
    {
        for (uint32_t lon = 3; lon <= 128; ++lon)
        {
            const uint32_t vertexCount = estimateVolumeSourceVertexCount(geometryType, lat, lon);
            const uint64_t error =
                vertexCount > targetVertexCount ? static_cast<uint64_t>(vertexCount - targetVertexCount)
                                                : static_cast<uint64_t>(targetVertexCount - vertexCount);
            const float ratio = static_cast<float>(lon) / static_cast<float>(lat);
            const float ratioError = std::abs(ratio - preferredRatio);

            if (error < bestError || (error == bestError && ratioError < bestRatioError))
            {
                bestError = error;
                bestRatioError = ratioError;
                bestLatitude = lat;
                bestLongitude = lon;
            }
        }
    }

    latitudeSegments = bestLatitude;
    longitudeSegments = bestLongitude;
}

std::string getDownloadsPath()
{
    PWSTR widePath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &widePath)) && widePath)
    {
        char path[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, widePath, -1, path, MAX_PATH, nullptr, nullptr);
        CoTaskMemFree(widePath);
        return path;
    }

    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile)
    {
        return std::string(userProfile) + "\\Downloads";
    }

    return ".";
}

std::string makeTimestampedCsvPath(const std::string& prefix)
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);

    std::ostringstream filename;
    filename << prefix << "_";
    filename << std::put_time(&tm, "%Y%m%d_%H%M%S");
    filename << ".csv";

    return (std::filesystem::path(getDownloadsPath()) / filename.str()).string();
}

double length3(const float3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

std::set<std::pair<uint32_t, uint32_t>> makeEdgeSet(const std::vector<uint32_t>& indices);

constexpr uint32_t kVolumeBenchmarkZSteps = 8;
constexpr uint32_t kVolumeBenchmarkXSteps = 4;
constexpr uint32_t kVolumeBenchmarkOrientationCount = kVolumeBenchmarkZSteps * kVolumeBenchmarkXSteps;

float4x4 makeVolumeBenchmarkModelTransform(uint32_t orientationIndex)
{
    const uint32_t zStep = orientationIndex / kVolumeBenchmarkXSteps;
    const uint32_t xStep = orientationIndex % kVolumeBenchmarkXSteps;

    // Screen-right is world +X. Screen-up is world +Y, used here for the requested non-depth "Z" sweep.
    const float4x4 rotateAroundScreenUp = math::matrixFromRotationY(math::radians(45.0f * static_cast<float>(zStep)));
    const float4x4 rotateAroundScreenRight = math::matrixFromRotationX(math::radians(90.0f * static_cast<float>(xStep)));
    return mul(rotateAroundScreenRight, rotateAroundScreenUp);
}

float3 transformPointByMatrix(const float4x4& transform, const float3& position)
{
    const float4 transformed = mul(transform, float4(position.x, position.y, position.z, 1.0f));
    if (std::abs(transformed.w) <= 1e-6f)
    {
        return float3(transformed.x, transformed.y, transformed.z);
    }

    return float3(transformed.x, transformed.y, transformed.z) / transformed.w;
}

double computeUniqueEdgeLength3D(const std::vector<float3>& vertices, const std::vector<uint32_t>& indices, uint32_t& uniqueEdgeCount)
{
    std::set<std::pair<uint32_t, uint32_t>> edges;
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const uint32_t tri[3] = {indices[i], indices[i + 1], indices[i + 2]};
        for (uint32_t edgeIndex = 0; edgeIndex < 3; ++edgeIndex)
        {
            uint32_t a = tri[edgeIndex];
            uint32_t b = tri[(edgeIndex + 1) % 3];
            if (a > b)
            {
                std::swap(a, b);
            }
            edges.insert({a, b});
        }
    }

    double length = 0.0;
    for (const auto& edge : edges)
    {
        if (edge.first < vertices.size() && edge.second < vertices.size())
        {
            const float3 delta = vertices[edge.first] - vertices[edge.second];
            length += length3(delta);
        }
    }

    uniqueEdgeCount = static_cast<uint32_t>(edges.size());
    return length;
}

float4x4 makeVolumeViewProj(
    uint32_t width,
    uint32_t height,
    uint32_t gridCols,
    uint32_t gridRows,
    uint32_t gridLayers,
    bool use3DGrid,
    float gridSpacing,
    float boundsRadius
)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    const float fovY = math::radians(45.0f);
    const float halfX = 0.5f * static_cast<float>(std::max(gridCols, 1u) - 1u) * gridSpacing + boundsRadius;
    const float halfY = 0.5f * static_cast<float>(std::max(gridRows, 1u) - 1u) * gridSpacing + boundsRadius;
    const float halfZ = (use3DGrid ? 0.5f * static_cast<float>(std::max(gridLayers, 1u) - 1u) * gridSpacing : 0.0f) + boundsRadius;
    const float tanHalfFovY = std::tan(fovY * 0.5f);
    const float tanHalfFovX = tanHalfFovY * aspectRatio;
    const float fitDistanceY = halfY / std::max(tanHalfFovY, 1e-3f);
    const float fitDistanceX = halfX / std::max(tanHalfFovX, 1e-3f);
    const float cameraDistance = std::max(2.0f, std::max(fitDistanceX, fitDistanceY) + halfZ + 0.5f);
    const float farPlane = std::max(10.0f, cameraDistance + halfZ + 2.0f);

    const float4x4 view = math::matrixFromLookAt(
        float3(0.0f, 0.0f, cameraDistance),
        float3(0.0f, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        math::Handedness::RightHanded
    );
    const float4x4 projection = math::perspective(fovY, aspectRatio, 0.01f, farPlane);
    return mul(projection, view);
}

float3 getVolumeInstanceOffset(
    uint32_t instanceId,
    uint32_t gridCols,
    uint32_t gridRows,
    uint32_t gridLayers,
    bool use3DGrid,
    float gridSpacing
)
{
    gridCols = std::max(gridCols, 1u);
    gridRows = std::max(gridRows, 1u);
    gridLayers = std::max(gridLayers, 1u);

    uint32_t layer = 0;
    uint32_t indexInLayer = instanceId;
    if (use3DGrid)
    {
        const uint32_t cellsPerLayer = std::max(gridCols * gridRows, 1u);
        layer = instanceId / cellsPerLayer;
        indexInLayer = instanceId - layer * cellsPerLayer;
    }

    const uint32_t row = indexInLayer / gridCols;
    const uint32_t col = indexInLayer - row * gridCols;
    const float gridDepth = use3DGrid ? static_cast<float>(gridLayers - 1u) : 0.0f;
    const float3 gridCenter = 0.5f * float3(static_cast<float>(gridCols - 1u), static_cast<float>(gridRows - 1u), gridDepth);
    const float3 instanceCoord = float3(static_cast<float>(col), static_cast<float>(row), static_cast<float>(layer));
    return (instanceCoord - gridCenter) * gridSpacing;
}

bool projectToScreenSpace(const float4x4& viewProj, const float3& position, uint32_t width, uint32_t height, float2& outPosition)
{
    const float4 clip = mul(viewProj, float4(position.x, position.y, position.z, 1.0f));
    if (std::abs(clip.w) <= 1e-6f)
    {
        return false;
    }

    const float2 ndc = float2(clip.x, clip.y) / clip.w;
    outPosition = (ndc * 0.5f + 0.5f) * float2(static_cast<float>(width), static_cast<float>(height));
    return true;
}

double computeProjectedEdgeLengthScreenSpace(
    const std::vector<float3>& vertices,
    const std::vector<uint32_t>& indices,
    const float4x4& modelTransform,
    uint32_t instanceCount,
    uint32_t width,
    uint32_t height,
    uint32_t gridCols,
    uint32_t gridRows,
    uint32_t gridLayers,
    bool use3DGrid,
    float gridSpacing,
    float boundsRadius
)
{
    const std::set<std::pair<uint32_t, uint32_t>> edges = makeEdgeSet(indices);
    const float4x4 viewProj = makeVolumeViewProj(width, height, gridCols, gridRows, gridLayers, use3DGrid, gridSpacing, boundsRadius);
    double length = 0.0;

    instanceCount = std::max(instanceCount, 1u);
    for (uint32_t instanceId = 0; instanceId < instanceCount; ++instanceId)
    {
        const float3 offset = getVolumeInstanceOffset(instanceId, gridCols, gridRows, gridLayers, use3DGrid, gridSpacing);
        for (const auto& edge : edges)
        {
            if (edge.first >= vertices.size() || edge.second >= vertices.size())
            {
                continue;
            }

            float2 a;
            float2 b;
            if (!projectToScreenSpace(viewProj, transformPointByMatrix(modelTransform, vertices[edge.first]) + offset, width, height, a) ||
                !projectToScreenSpace(viewProj, transformPointByMatrix(modelTransform, vertices[edge.second]) + offset, width, height, b))
            {
                continue;
            }

            const float2 delta = a - b;
            length += std::sqrt(static_cast<double>(delta.x * delta.x + delta.y * delta.y));
        }
    }

    return length;
}

float computeMaxDistanceFromOrigin(const std::vector<float3>& vertices)
{
    float maxDistance = 0.0f;
    for (const float3& vertex : vertices)
    {
        maxDistance = std::max(maxDistance, static_cast<float>(length3(vertex)));
    }
    return maxDistance;
}

std::set<std::pair<uint32_t, uint32_t>> makeEdgeSet(const std::vector<uint32_t>& indices)
{
    std::set<std::pair<uint32_t, uint32_t>> edges;
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const uint32_t tri[3] = {indices[i], indices[i + 1], indices[i + 2]};
        for (uint32_t edgeIndex = 0; edgeIndex < 3; ++edgeIndex)
        {
            uint32_t a = tri[edgeIndex];
            uint32_t b = tri[(edgeIndex + 1) % 3];
            if (a > b)
            {
                std::swap(a, b);
            }
            edges.insert({a, b});
        }
    }
    return edges;
}

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

    mpResolvedTexture =
        getDevice()->createTexture2D(getTargetFbo()->getWidth(), getTargetFbo()->getHeight(), ResourceFormat::RGBA16Float, 1, 1);
}

void HelperLaneViz::onLoad(RenderContext* pRenderContext)
{
    ProgramDesc d;
    d.addShaderLibrary("Samples/HelperLaneWindows/Shaders/MainShader.slang").vsEntry("vsMain").psEntry("psMain");

    pRenderContext->getDevice()->getProgramManager()->setGenerateDebugInfoEnabled(true);

    mpPass = FullScreenPass::create(getDevice(), d);

    mpProgram = Program::create(getDevice(), d);
    mpProgram->addDefine("USE_3D_GEOMETRY", "0");
    mpVars = ProgramVars::create(getDevice(), mpProgram.get());
    mpState = GraphicsState::create(getDevice());
    mpState->setProgram(mpProgram);

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
        1, 1, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);

    uint32_t texWidth = 2048;
    uint32_t texHeight = 2048;
    std::vector<uint32_t> pixels(texWidth * texHeight, 0xFF0000FF); // ARGB: A=255, R=0, G=0, B=255
    mpDummyTexture = getDevice()->createTexture2D(
        texWidth, texHeight, ResourceFormat::RGBA8Unorm, 1, 1, pixels.data(), ResourceBindFlags::ShaderResource
    );

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

void HelperLaneViz::uploadGeometry()
{
    if (mVertices.size() < 3 || mIndices.empty())
    {
        mIndexCount = 0;
        mpVao = nullptr;
        mpState->setVao(nullptr);
        return;
    }

    const uint64_t vbSize = (uint64_t)mVertices.size() * sizeof(Vertex);
    const uint64_t ibSize = (uint64_t)mIndices.size() * sizeof(uint32_t);

    mpVB = make_ref<Buffer>(getDevice(), vbSize, ResourceBindFlags::Vertex, MemoryType::DeviceLocal, mVertices.data());
    getDevice()->getRenderContext()->updateBuffer(mpVB.get(), mVertices.data(), 0, vbSize);

    mpIB = make_ref<Buffer>(getDevice(), ibSize, ResourceBindFlags::Index, MemoryType::DeviceLocal, mIndices.data());
    getDevice()->getRenderContext()->updateBuffer(mpIB.get(), mIndices.data(), 0, ibSize);

    Vao::BufferVec vbuffers = {mpVB};
    mpVao = Vao::create(Vao::Topology::TriangleList, mpLayout, vbuffers, mpIB, ResourceFormat::R32Uint);
    mpState->setVao(mpVao);

    mIndexCount = (uint32_t)mIndices.size();
}

void HelperLaneViz::uploadVolumeGeometry()
{
    if (mVolumeVertices.size() < 3 || mVolumeIndices.empty())
    {
        mIndexCount = 0;
        mpVao = nullptr;
        mpState->setVao(nullptr);
        return;
    }

    const uint64_t vbSize = (uint64_t)mVolumeVertices.size() * sizeof(float3);
    const uint64_t ibSize = (uint64_t)mVolumeIndices.size() * sizeof(uint32_t);

    mpVB = make_ref<Buffer>(getDevice(), vbSize, ResourceBindFlags::Vertex, MemoryType::DeviceLocal, mVolumeVertices.data());
    getDevice()->getRenderContext()->updateBuffer(mpVB.get(), mVolumeVertices.data(), 0, vbSize);

    mpIB = make_ref<Buffer>(getDevice(), ibSize, ResourceBindFlags::Index, MemoryType::DeviceLocal, mVolumeIndices.data());
    getDevice()->getRenderContext()->updateBuffer(mpIB.get(), mVolumeIndices.data(), 0, ibSize);

    Vao::BufferVec vbuffers = {mpVB};
    mpVao = Vao::create(Vao::Topology::TriangleList, mpLayout3D, vbuffers, mpIB, ResourceFormat::R32Uint);
    mpState->setVao(mpVao);

    mIndexCount = (uint32_t)mVolumeIndices.size();
}

void HelperLaneViz::generateVolumeGeometry()
{
    mVolumeVertices.clear();
    mVolumeIndices.clear();
    mVolumeSourceFaceCount = 0;
    mVolumeSourceTriangleCount = 0;
    mVolumeObjFanTriangulatedFaceCount = 0;
    mVolumePatchCount = 0;
    mVolumeFailedPatchCount = 0;
    mVolumeObjStatus.clear();

    auto start = std::chrono::high_resolution_clock::now();

    Triangulation3D::PatchGrowthOptions patchOptions;
    patchOptions.maxNormalDeviationDegrees = mVolumeMaxNormalDeviationDegrees;
    patchOptions.maxRelativePlaneDeviation = mVolumeMaxRelativePlaneDeviation;
    patchOptions.maxFacesPerPatch = mVolumeMaxFacesPerPatch;

    Triangulation3D::TriangulationOptions triangulationOptions;
    triangulationOptions.method = getVolumeTriangulationMethod(mVolumeTriangulationType);
    triangulationOptions.use3DEdgeLengthForGreedy = true;
    triangulationOptions.pclMaximumNearestNeighbors = mVolumePclMaximumNearestNeighbors;
    triangulationOptions.pclMu = mVolumePclMu;
    triangulationOptions.pclSearchRadius = mVolumePclSearchRadius;
    triangulationOptions.pclMinimumAngleDegrees = mVolumePclMinimumAngleDegrees;
    triangulationOptions.pclMaximumAngleDegrees = mVolumePclMaximumAngleDegrees;

    mVolumeGeometryName = getVolumeGeometryName(mVolumeGeometryType);
    Triangulation3D::Mesh sourceMesh;
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
            mVolumeLatitudeSegments,
            mVolumeLongitudeSegments,
            mVolumeRadius,
            0.22f,
            0.18f,
            float3(0.0f, 0.0f, 0.0f)
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
        const ObjMeshLoader::LoadResult loadResult = ObjMeshLoader::load(mVolumeObjPath, loadOptions);
        sourceMesh = loadResult.mesh;
        mVolumeObjStatus = loadResult.message;
        mVolumeObjFanTriangulatedFaceCount = loadResult.fanTriangulatedFaceCount;
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
            mVolumeLatitudeSegments,
            mVolumeLongitudeSegments,
            mVolumeRadius,
            float3(0.0f, 0.0f, 0.0f)
        );
        break;
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
        mVolumeTriangulationType == 3 ? Triangulation3D::useSourceMeshTriangulation(sourceMesh)
                                      : Triangulation3D::triangulateMesh(sourceMesh, patchOptions, triangulationOptions);

    mVolumeVertices = result.vertices;
    mVolumeIndices = result.indices;
    mVolumeBoundsRadius = std::max(computeMaxDistanceFromOrigin(mVolumeVertices), 0.001f);
    mVolumePatchCount = static_cast<uint32_t>(result.patches.size());
    mVolumeFailedPatchCount = static_cast<uint32_t>(result.messages.size());
    if (mVolumeGeometryType == 7 && !result.messages.empty())
    {
        mVolumeObjStatus += " Triangulation notes: " + std::to_string(result.messages.size());
    }
    updateVolumeGridParams();

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
    mVolumeTargetVertexCount =
        estimateVolumeSourceVertexCount(mVolumeGeometryType, mVolumeLatitudeSegments, mVolumeLongitudeSegments);
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
    const float aspectRatio =
        getTargetFbo() && getTargetFbo()->getHeight() > 0
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

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    const float fovY = math::radians(45.0f);
    float cameraDistance = 2.0f;
    float farPlane = 10.0f;

    if (mGeometryDimensionality == GeometryDimensionality::Volumetric)
    {
        const float halfX = 0.5f * static_cast<float>(mVolumeGridCols - 1u) * mVolumeGridSpacing + mVolumeBoundsRadius;
        const float halfY = 0.5f * static_cast<float>(mVolumeGridRows - 1u) * mVolumeGridSpacing + mVolumeBoundsRadius;
        const float halfZ =
            (mVolumeUse3DInstancing ? 0.5f * static_cast<float>(mVolumeGridLayers - 1u) * mVolumeGridSpacing : 0.0f) +
            mVolumeBoundsRadius;
        const float tanHalfFovY = std::tan(fovY * 0.5f);
        const float tanHalfFovX = tanHalfFovY * aspectRatio;
        const float fitDistanceY = halfY / std::max(tanHalfFovY, 1e-3f);
        const float fitDistanceX = halfX / std::max(tanHalfFovX, 1e-3f);

        cameraDistance = std::max(2.0f, std::max(fitDistanceX, fitDistanceY) + halfZ + 0.5f);
        farPlane = std::max(10.0f, cameraDistance + halfZ + 2.0f);
    }

    const float4x4 view = math::matrixFromLookAt(
        float3(0.0f, 0.0f, cameraDistance),
        float3(0.0f, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        math::Handedness::RightHanded
    );
    const float4x4 projection = math::perspective(fovY, aspectRatio, 0.01f, farPlane);

    CameraParams cameraParams;
    cameraParams.viewProj = mul(projection, view);
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

void HelperLaneViz::startBenchmarkConfig(int configPhase)
{
    mBenchmarkConfigPhase = configPhase;

    // Set MSAA and Grid based on config phase
    // Config 0: MSAA=1, Grid=1x1
    // Config 1: MSAA=1, Grid=100x100
    // Config 2: MSAA=4, Grid=1x1
    // Config 3: MSAA=4, Grid=100x100
    switch (configPhase)
    {
    case 0:
        cntMSAA = 1;
        mGridCols = 1;
        mGridRows = 1;
        break;
    case 1:
        cntMSAA = 1;
        mGridCols = 100;
        mGridRows = 100;
        break;
    case 2:
        cntMSAA = 4;
        mGridCols = 1;
        mGridRows = 1;
        break;
    case 3:
        cntMSAA = 4;
        mGridCols = 100;
        mGridRows = 100;
        break;
    }

    // Apply settings
    CreateMSAATargets();
    updateGridParams();

    // Reset iteration state
    mCurrentFolderFile = 0;
    mSyntheticShapePhase = 0;
    mCurrentMethod = 0;
    mBenchmarkStep = 0;
    mWaitFrameCounter = 0;
    mGpuFrameCounter = 0;
    mUseCircle = false;

    if (!mFolderFiles.empty())
    {
        mSvgPath = mFolderFiles[0];
    }

    // Write config header to file
    if (mBenchmarkFile.is_open())
    {
        std::string msaaStr = (cntMSAA == 1) ? "Off" : std::to_string(cntMSAA) + "x";
        mBenchmarkFile << "\n";
        mBenchmarkFile << "=== Configuration: MSAA " << msaaStr << ", Grid " << mGridCols << "x" << mGridRows << " ===\n";
        mBenchmarkFile << "File,Method,CPU_Time_ms,GPU_Median_ms,GPU_Mean_ms,GPU_StdDev_ms,HelperLaneCount,EdgeLength\n";
    }
}

void HelperLaneViz::benchmarkFolder(const std::string& folderPath)
{
    if (folderPath.empty())
    {
        return;
    }

    mFolderFiles.clear();
    for (const auto& entry : std::filesystem::directory_iterator(folderPath))
    {
        if (entry.is_regular_file())
        {
            std::string filePath = entry.path().string();
            std::string ext = filePath.substr(filePath.find_last_of(".") + 1);
            if (ext == "svg")
            {
                mFolderFiles.push_back(filePath);
            }
        }
    }

    if (mFolderFiles.empty())
    {
        return;
    }

    mBenchmarkOutputPath = folderPath + "/benchmark_results.txt";
    mBenchmarkFile.open(mBenchmarkOutputPath, std::ios::out | std::ios::trunc);
    if (!mBenchmarkFile.is_open())
    {
        return;
    }

    mBenchmarkFolderActive = true;
    mBenchmarkMethods = {1, 3, 7, 8, 9, 10}; // Skip centroid fan

    // Start with first configuration (MSAA Off, Grid 1x1)
    startBenchmarkConfig(0);
}

void HelperLaneViz::processBenchmarkStep(RenderContext* pRenderContext)
{
    const std::unordered_map<int, std::string> kTriangulationMethodNames = {
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
        {10, "CDT + Flip"}};
    Profiler* pProfiler = getDevice()->getProfiler();
    if (pProfiler)
        pProfiler->setEnabled(true);

    int method = mBenchmarkMethods[mCurrentMethod];
    std::string methodName = kTriangulationMethodNames.at(method);

    switch (mBenchmarkStep)
    {
    case 0: // CPU triangulation
    {
        mTriangulationType = method;
        auto start = std::chrono::high_resolution_clock::now();
        if (mUseCircle)
        {
            generateCircle();
        }
        else
        {
            loadSvg(mSvgPath);
        }
        auto end = std::chrono::high_resolution_clock::now();
        mCpuTimeMs = std::chrono::duration<double, std::milli>(end - start).count();

        mWaitFrameCounter = 0;
        mBenchmarkStep = 1;
        break;
    }
    case 1: // Wait frames for stability
    {
        const int waitFrames = 50;
        mWaitFrameCounter++;
        if (mWaitFrameCounter >= waitFrames)
        {
            mGpuFrameCounter = 0;
            mGpuTimes.clear();
            mBenchmarkStep = 2;
        }
        break;
    }
    case 2: // Measure GPU frames
    {
        const int gpuFrames = 100;

        if (pProfiler && pProfiler->isEnabled())
        {
            const auto& events = pProfiler->getEvents();
            for (auto* pEvent : events)
            {
                if (pEvent->getName() == "/onFrameRender")
                {
                    float gpuTime = pEvent->getGpuTime(); // in ms
                    mGpuTimes.push_back(gpuTime);
                    break;
                }
            }
        }

        mGpuFrameCounter++;
        if (mGpuFrameCounter == gpuFrames)
        {
            // Save current VizMode and switch to HelperLanes for measurement
            mSavedVizMode = mVizMode;
            mVizMode = VizMode::HelperLanes;
            mpProgram->removeDefine("VIZ_MODE");
            mpProgram->addDefine("VIZ_MODE", "0");

            mReadBackHelperLaneCount = true;
            mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "1");
        }
        if (mGpuFrameCounter > gpuFrames)
        {
            // Restore original VizMode
            mVizMode = mSavedVizMode;
            mpProgram->removeDefine("VIZ_MODE");
            mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));

            // Compute stats
            std::sort(mGpuTimes.begin(), mGpuTimes.end());
            float median = mGpuTimes[mGpuTimes.size() / 2];
            float mean = std::accumulate(mGpuTimes.begin(), mGpuTimes.end(), 0.0f) / mGpuTimes.size();
            float sqSum = 0.0f;
            for (float t : mGpuTimes)
                sqSum += (t - mean) * (t - mean);
            float stddev = std::sqrt(sqSum / mGpuTimes.size());

            // Read helper lane counter
            uint32_t helperCount = mHelperLaneCount;
            mReadBackHelperLaneCount = false;
            mpProgram->removeDefine("READ_BACK_HELPER_LANE_COUNT");

            size_t totalEdgeLength;
            double uniqueEdgeLength;
            Triangulation::ComputeEdgeMetrics(mVertices, mIndices, totalEdgeLength, uniqueEdgeLength);

            std::ostringstream oss;
            if (mBenchmarkFolderActive)
            {
                std::string fileName;
                if (mSyntheticShapePhase == 1)
                {
                    fileName = "circle_512";
                }
                else if (mSyntheticShapePhase == 2)
                {
                    fileName = "ellipse_512";
                }
                else
                {
                    fileName = std::filesystem::path(mFolderFiles[mCurrentFolderFile]).filename().string();
                }
                oss << fileName << "," << methodName << "," << mCpuTimeMs << "," << median << "," << mean << "," << stddev << ","
                    << helperCount << "," << uniqueEdgeLength;
            }
            else
            {
                oss << methodName << "," << mCpuTimeMs << "," << median << "," << mean << "," << stddev << "," << helperCount << ","
                    << uniqueEdgeLength;
            }
            std::string logLine = oss.str();

            mBenchmarkLog.push_back(logLine);

            if (mBenchmarkFile.is_open())
            {
                mBenchmarkFile << logLine << "\n";
                mBenchmarkFile.flush();
            }

            // Move to next method
            mCurrentMethod++;
            if (mCurrentMethod >= (int)mBenchmarkMethods.size())
            {
                if (mBenchmarkFolderActive)
                {
                    mBenchmarkStep = 0;
                }
                else
                {
                    mBenchmarkActive = false;
                    mBenchmarkStep = 0;
                    if (mBenchmarkFile.is_open())
                        mBenchmarkFile.close();
                    mBenchmarkLog.push_back("Benchmark complete!");
                }
            }
            else
            {
                mBenchmarkStep = 0;
            }
        }
        break;
    }
    }
}

void HelperLaneViz::processBenchmarkFolderStep(RenderContext* pRenderContext)
{
    // Process SVG files or synthetic shapes
    if (mCurrentFolderFile < (int)mFolderFiles.size() || mSyntheticShapePhase > 0)
    {
        processBenchmarkStep(pRenderContext);

        if (mCurrentMethod >= (int)mBenchmarkMethods.size())
        {
            if (mCurrentFolderFile < (int)mFolderFiles.size())
            {
                // Move to next SVG file
                mCurrentFolderFile++;
                if (mCurrentFolderFile < (int)mFolderFiles.size())
                {
                    mSvgPath = mFolderFiles[mCurrentFolderFile];
                    mCurrentMethod = 0;
                    mBenchmarkStep = 0;
                    mWaitFrameCounter = 0;
                    mGpuFrameCounter = 0;
                }
                else
                {
                    // All SVG files done, move to synthetic shapes
                    mSyntheticShapePhase = 1;
                    mUseCircle = true;
                    mCircleVertexCount = 512;
                    // Use default radius (will be set to same for circle)
                    mEllipseRadiusX = 0.4f;
                    mEllipseRadiusY = 0.4f;
                    mCurrentMethod = 0;
                    mBenchmarkStep = 0;
                    mWaitFrameCounter = 0;
                    mGpuFrameCounter = 0;
                }
            }
            else if (mSyntheticShapePhase == 1)
            {
                // Circle done, move to ellipse
                mSyntheticShapePhase = 2;
                mUseCircle = true;
                mCircleVertexCount = 512;
                mEllipseRadiusX = 0.4f;
                mEllipseRadiusY = 0.2f; // 0.5x the x axis
                mCurrentMethod = 0;
                mBenchmarkStep = 0;
                mWaitFrameCounter = 0;
                mGpuFrameCounter = 0;
            }
            else if (mSyntheticShapePhase == 2)
            {
                // Ellipse done for current config, move to next config or finish
                if (mBenchmarkConfigPhase < 3)
                {
                    // Move to next configuration
                    startBenchmarkConfig(mBenchmarkConfigPhase + 1);
                }
                else
                {
                    // All configurations done
                    mBenchmarkFolderActive = false;
                    if (mBenchmarkFile.is_open())
                    {
                        mBenchmarkFile.flush();
                        mBenchmarkFile.close();
                    }
                    mBenchmarkLog.push_back("Folder benchmark complete!");
                    mSyntheticShapePhase = 0;
                    mBenchmarkConfigPhase = 0;
                    mUseCircle = false;
                }
            }
        }
    }
}

void HelperLaneViz::writeVolumeBenchmarkHeader()
{
    if (!mBenchmarkFile.is_open())
    {
        return;
    }

    mBenchmarkFile << "Geometry,SourceVertices,Radius,Instances,GridMode,GridCols,GridRows,GridLayers,Method,"
                   << "AverageGPU_Median_ms,MinGPU_Median_ms,MaxGPU_Median_ms,"
                   << "TotalEdgeLength3D,AverageTotalEdgeLengthScreenSpace,AverageHelperLaneCount\n";
}

void HelperLaneViz::writeVolumeBenchmarkRow(
    const std::string& methodName,
    double averageMedianGpuMs,
    double minMedianGpuMs,
    double maxMedianGpuMs,
    double averageProjectedEdgeLength,
    double averageHelperLaneCount
)
{
    if (!mBenchmarkFile.is_open())
    {
        return;
    }

    uint32_t uniqueEdgeCount = 0;
    const uint32_t instanceCount = std::max(mVolumeInstanceCount, 1u);
    const double totalEdgeLength3D = computeUniqueEdgeLength3D(mVolumeVertices, mVolumeIndices, uniqueEdgeCount) * static_cast<double>(instanceCount);
    (void)uniqueEdgeCount;

    mBenchmarkFile << mVolumeGeometryName << "," << mVolumeSourceVertexCount << "," << mVolumeRadius << "," << instanceCount << ","
                   << (mVolumeUse3DInstancing ? "XYZ" : "XY") << "," << mVolumeGridCols << "," << mVolumeGridRows << ","
                   << mVolumeGridLayers << "," << methodName << ","
                   << averageMedianGpuMs << "," << minMedianGpuMs << "," << maxMedianGpuMs << ","
                   << totalEdgeLength3D << "," << averageProjectedEdgeLength << "," << averageHelperLaneCount << "\n";
    mBenchmarkFile.flush();
}

void HelperLaneViz::writeVolumeBenchmarkDetails()
{
    if (!mBenchmarkFile.is_open())
    {
        return;
    }

    mBenchmarkFile << "\n\n";
    mBenchmarkFile << "Per-transform measurements\n";
    mBenchmarkFile << "Orientation,ZStep,ZRotationDegrees,XStep,XRotationDegrees,Method,GPU_Median_ms,TotalEdgeLengthScreenSpace,HelperLaneCount\n";

    for (uint32_t orientation = 0; orientation < kVolumeBenchmarkOrientationCount; ++orientation)
    {
        const uint32_t zStep = orientation / kVolumeBenchmarkXSteps;
        const uint32_t xStep = orientation % kVolumeBenchmarkXSteps;
        const uint32_t zDegrees = zStep * 45u;
        const uint32_t xDegrees = xStep * 90u;

        for (uint32_t method : mVolumeBenchmarkMethods)
        {
            auto row = std::find_if(
                mVolumeBenchmarkDetailRows.begin(),
                mVolumeBenchmarkDetailRows.end(),
                [&](const VolumeBenchmarkDetailRow& candidate)
                {
                    return candidate.orientation == orientation && candidate.method == method;
                }
            );

            if (row == mVolumeBenchmarkDetailRows.end())
            {
                continue;
            }

            mBenchmarkFile << orientation << "," << zStep << "," << zDegrees << "," << xStep << "," << xDegrees << ","
                           << getVolumeTriangulationMethodName(method) << "," << row->gpuMedianMs << ","
                           << row->projectedEdgeLength << "," << row->helperLaneCount << "\n";
        }

        mBenchmarkFile << "\n";
    }

    mBenchmarkFile.flush();
}

void HelperLaneViz::startVolumeBenchmark()
{
    if (mBenchmarkFile.is_open())
    {
        mBenchmarkFile.close();
    }

    mBenchmarkActive = false;
    mBenchmarkFolderActive = false;
    mVolumeBenchmarkActive = true;
    mVolumeBenchmarkStep = 0;
    mVolumeBenchmarkMethod = 0;
    mVolumeBenchmarkWaitFrameCounter = 0;
    mVolumeBenchmarkGpuFrameCounter = 0;
    mVolumeBenchmarkMedianGpuMs = 0.0f;
    mVolumeBenchmarkOrientation = 0;
    mVolumeBenchmarkCompletedOrientations = 0;
    mVolumeBenchmarkGpuMedianSum = 0.0;
    mVolumeBenchmarkGpuMedianMin = std::numeric_limits<double>::max();
    mVolumeBenchmarkGpuMedianMax = 0.0;
    mVolumeBenchmarkProjectedEdgeLengthSum = 0.0;
    mVolumeBenchmarkHelperLaneSum = 0.0;
    mVolumeBenchmarkGpuTimes.clear();
    mVolumeBenchmarkDetailRows.clear();
    mVolumeBenchmarkMethods = {3, 0, 1, 2, 4, 5};
    mSavedVolumeTriangulationType = mVolumeTriangulationType;
    mSavedReadBackHelperLaneCount = mReadBackHelperLaneCount;
    mReadBackHelperLaneCount = false;
    mpProgram->removeDefine("READ_BACK_HELPER_LANE_COUNT");
    mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
    mVolumeModelTransform = makeVolumeBenchmarkModelTransform(0);
    mGeometryDimensionality = GeometryDimensionality::Volumetric;
    updateVolumeGridParams();
    updateVolumeModelParams();

    mVolumeBenchmarkOutputPath = makeTimestampedCsvPath("helper_lane_volume_benchmark");
    std::filesystem::create_directories(std::filesystem::path(mVolumeBenchmarkOutputPath).parent_path());
    mBenchmarkFile.open(mVolumeBenchmarkOutputPath, std::ios::out | std::ios::trunc);
    writeVolumeBenchmarkHeader();
}

void HelperLaneViz::processVolumeBenchmarkStep(RenderContext* pRenderContext)
{
    if (mVolumeBenchmarkMethod >= static_cast<int>(mVolumeBenchmarkMethods.size()))
    {
        mVolumeBenchmarkActive = false;
        mVolumeTriangulationType = mSavedVolumeTriangulationType;
        mReadBackHelperLaneCount = mSavedReadBackHelperLaneCount;
        mpProgram->removeDefine("READ_BACK_HELPER_LANE_COUNT");
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", mReadBackHelperLaneCount ? "1" : "0");
        mVolumeModelTransform = float4x4::identity();
        updateVolumeModelParams();
        generateVolumeGeometry();
        if (mBenchmarkFile.is_open())
        {
            writeVolumeBenchmarkDetails();
            mBenchmarkFile.close();
        }
        return;
    }

    Profiler* pProfiler = getDevice()->getProfiler();
    if (pProfiler)
    {
        pProfiler->setEnabled(true);
    }

    switch (mVolumeBenchmarkStep)
    {
    case 0:
    {
        mVolumeTriangulationType = mVolumeBenchmarkMethods[mVolumeBenchmarkMethod];
        const auto start = std::chrono::high_resolution_clock::now();
        generateVolumeGeometry();
        const auto end = std::chrono::high_resolution_clock::now();
        mVolumeBenchmarkCpuTimeMs = std::chrono::duration<double, std::milli>(end - start).count();

        mVolumeBenchmarkWaitFrameCounter = 0;
        mVolumeBenchmarkGpuFrameCounter = 0;
        mVolumeBenchmarkMedianGpuMs = 0.0f;
        mVolumeBenchmarkOrientation = 0;
        mVolumeBenchmarkCompletedOrientations = 0;
        mVolumeBenchmarkGpuMedianSum = 0.0;
        mVolumeBenchmarkGpuMedianMin = std::numeric_limits<double>::max();
        mVolumeBenchmarkGpuMedianMax = 0.0;
        mVolumeBenchmarkProjectedEdgeLengthSum = 0.0;
        mVolumeBenchmarkHelperLaneSum = 0.0;
        mVolumeBenchmarkGpuTimes.clear();
        mReadBackHelperLaneCount = false;
        mpProgram->removeDefine("READ_BACK_HELPER_LANE_COUNT");
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
        mVolumeModelTransform = makeVolumeBenchmarkModelTransform(mVolumeBenchmarkOrientation);
        updateVolumeModelParams();
        mVolumeBenchmarkStep = 1;
        break;
    }
    case 1:
    {
        const int waitFrames = 50;
        ++mVolumeBenchmarkWaitFrameCounter;
        if (mVolumeBenchmarkWaitFrameCounter >= waitFrames)
        {
            mVolumeBenchmarkStep = 2;
        }
        break;
    }
    case 2:
    {
        const int gpuFrames = 200;
        if (pProfiler && pProfiler->isEnabled())
        {
            const auto& events = pProfiler->getEvents();
            for (auto* pEvent : events)
            {
                if (pEvent->getName() == "/onFrameRender")
                {
                    mVolumeBenchmarkGpuTimes.push_back(pEvent->getGpuTime());
                    break;
                }
            }
        }

        ++mVolumeBenchmarkGpuFrameCounter;
        if (mVolumeBenchmarkGpuFrameCounter >= gpuFrames)
        {
            std::sort(mVolumeBenchmarkGpuTimes.begin(), mVolumeBenchmarkGpuTimes.end());
            mVolumeBenchmarkMedianGpuMs =
                mVolumeBenchmarkGpuTimes.empty() ? 0.0f : mVolumeBenchmarkGpuTimes[mVolumeBenchmarkGpuTimes.size() / 2];

            mSavedVizMode = mVizMode;
            mVizMode = VizMode::HelperLanes;
            mReadBackHelperLaneCount = true;
            mpProgram->removeDefine("VIZ_MODE");
            mpProgram->addDefine("VIZ_MODE", "0");
            mpProgram->removeDefine("READ_BACK_HELPER_LANE_COUNT");
            mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "1");

            mVolumeBenchmarkStep = 3;
        }
        break;
    }
    case 3:
    {
        const uint32_t helperLaneCount = mHelperLaneCount;
        mVizMode = mSavedVizMode;
        mReadBackHelperLaneCount = false;
        mpProgram->removeDefine("VIZ_MODE");
        mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));
        mpProgram->removeDefine("READ_BACK_HELPER_LANE_COUNT");
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");

        const uint32_t instanceCount = std::max(mVolumeInstanceCount, 1u);
        const uint32_t width = getTargetFbo() ? getTargetFbo()->getWidth() : 1;
        const uint32_t height = getTargetFbo() ? getTargetFbo()->getHeight() : 1;
        const double projectedEdgeLength = computeProjectedEdgeLengthScreenSpace(
            mVolumeVertices,
            mVolumeIndices,
            mVolumeModelTransform,
            instanceCount,
            width,
            height,
            mVolumeGridCols,
            mVolumeGridRows,
            mVolumeGridLayers,
            mVolumeUse3DInstancing,
            mVolumeGridSpacing,
            mVolumeBoundsRadius
        );

        mVolumeBenchmarkGpuMedianSum += static_cast<double>(mVolumeBenchmarkMedianGpuMs);
        mVolumeBenchmarkGpuMedianMin = std::min(mVolumeBenchmarkGpuMedianMin, static_cast<double>(mVolumeBenchmarkMedianGpuMs));
        mVolumeBenchmarkGpuMedianMax = std::max(mVolumeBenchmarkGpuMedianMax, static_cast<double>(mVolumeBenchmarkMedianGpuMs));
        mVolumeBenchmarkProjectedEdgeLengthSum += projectedEdgeLength;
        mVolumeBenchmarkHelperLaneSum += static_cast<double>(helperLaneCount);
        mVolumeBenchmarkDetailRows.push_back(
            VolumeBenchmarkDetailRow{
                mVolumeBenchmarkOrientation,
                mVolumeTriangulationType,
                static_cast<double>(mVolumeBenchmarkMedianGpuMs),
                projectedEdgeLength,
                static_cast<double>(helperLaneCount)}
        );
        ++mVolumeBenchmarkCompletedOrientations;
        ++mVolumeBenchmarkOrientation;

        if (mVolumeBenchmarkOrientation < kVolumeBenchmarkOrientationCount)
        {
            mVolumeModelTransform = makeVolumeBenchmarkModelTransform(mVolumeBenchmarkOrientation);
            updateVolumeModelParams();
            mVolumeBenchmarkWaitFrameCounter = 0;
            mVolumeBenchmarkGpuFrameCounter = 0;
            mVolumeBenchmarkMedianGpuMs = 0.0f;
            mVolumeBenchmarkGpuTimes.clear();
            mVolumeBenchmarkStep = 1;
        }
        else
        {
            const double sampleCount = static_cast<double>(std::max(mVolumeBenchmarkCompletedOrientations, 1u));
            writeVolumeBenchmarkRow(
                getVolumeTriangulationMethodName(mVolumeTriangulationType),
                mVolumeBenchmarkGpuMedianSum / sampleCount,
                mVolumeBenchmarkGpuMedianMin == std::numeric_limits<double>::max() ? 0.0 : mVolumeBenchmarkGpuMedianMin,
                mVolumeBenchmarkGpuMedianMax,
                mVolumeBenchmarkProjectedEdgeLengthSum / sampleCount,
                mVolumeBenchmarkHelperLaneSum / sampleCount
            );

            ++mVolumeBenchmarkMethod;
            mVolumeBenchmarkStep = 0;
        }
        break;
    }
    default:
        mVolumeBenchmarkStep = 0;
        break;
    }
}

void HelperLaneViz::onShutdown() {}
void HelperLaneViz::onResize(uint32_t width, uint32_t height)
{
    updateGridParams();
    updateVolumeGridParams();
    updateVolumeModelParams();
    updateCameraParams(width, height);
    mpHelperLaneCounter = getDevice()->createTexture2D(
        1, 1, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);
    CreateMSAATargets();
}

void HelperLaneViz::onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    if (mBenchmarkFolderActive)
    {
        processBenchmarkFolderStep(pRenderContext);
    }
    else if (mVolumeBenchmarkActive)
    {
        processVolumeBenchmarkStep(pRenderContext);
    }
    else if (mBenchmarkActive)
    {
        processBenchmarkStep(pRenderContext);
    }

    mpState->setFbo(mpFbo);
    float4 bgColor = (mVizMode == VizMode::Wireframe) ? float4(1, 1, 1, 1) : float4(0, 0, 0, 0);

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

    if (mGeometryDimensionality == GeometryDimensionality::Volumetric)
    {
        updateVolumeGridParams();
        updateVolumeModelParams();
        updateCameraParams(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    }

    if (mIndexCount)
    {
        uint32_t instanceCount =
            mGeometryDimensionality == GeometryDimensionality::Volumetric ? std::max(mVolumeInstanceCount, 1u) : mGridCols * mGridRows;
        pRenderContext->drawIndexedInstanced(mpState.get(), mpVars.get(), mIndexCount, instanceCount, 0, 0, 0);
    }

    if (mpHelperLaneCounter && mIndexCount && mReadBackHelperLaneCount)
    {
        std::vector<uint8_t> counterData = pRenderContext->readTextureSubresource(mpHelperLaneCounter->asTexture().get(), 0);
        mHelperLaneCount = *(uint32_t*)counterData.data();
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
    Gui::Window w(pGui, "SVG Loader", {300, 150});

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
    Gui::DropdownList vizModes = {{0, "Helper Lanes"}, {1, "Wireframe"}, {2, "Texture"}, {3, "Material Stress"}};
    w.dropdown("Visualization", vizModes, *(uint32_t*)&mVizMode);

    mpProgram->removeDefine("VIZ_MODE");
    mpProgram->removeDefine("USE_DUMMY_TEXTURE");
    mpProgram->removeDefine("USE_3D_GEOMETRY");

    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));
    mpProgram->addDefine("USE_3D_GEOMETRY", isVolumetric ? "1" : "0");

    if (mVizMode == VizMode::Texture || mVizMode == VizMode::MaterialStress || (mVizMode == VizMode::HelperLanes && mUseDummyTexture))
    {
        mpVars->setTexture("gDummyTexture", mpDummyTexture);
    }

    if (mVizMode == VizMode::HelperLanes && mUseDummyTexture)
    {
        mpProgram->addDefine("USE_DUMMY_TEXTURE", "1");
    }

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
            {7, "OBJ File"}};
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
            {4, "LMT MWT"},
            {5, "PCL Greedy Projection"}};
        volumeChanged |= w.dropdown("Volumetric Triangulation", volumeTriangTypes, mVolumeTriangulationType);

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
            volumeChanged |= w.var("PCL Max Neighbors", mVolumePclMaximumNearestNeighbors, 3u, 512u);
            volumeChanged |= w.var("PCL Mu", mVolumePclMu, 0.1f, 10.0f);
            volumeChanged |= w.var("PCL Search Radius", mVolumePclSearchRadius, 0.0f, 100.0f);
            volumeChanged |= w.var("PCL Min Angle", mVolumePclMinimumAngleDegrees, 0.0f, 60.0f);
            volumeChanged |= w.var("PCL Max Angle", mVolumePclMaximumAngleDegrees, 60.0f, 179.0f);
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
            if (mVolumeObjFanTriangulatedFaceCount > 0)
            {
                w.text("Fan-triangulated OBJ polygon faces: %u", mVolumeObjFanTriangulatedFaceCount);
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
            w.text(
                "Instance grid: " + std::to_string(mVolumeGridCols) + " x " + std::to_string(mVolumeGridRows)
            );
        }
        w.text("Patches: %u", mVolumePatchCount);
        if (mVolumeFailedPatchCount > 0)
        {
            w.text("Failed patches: %u", mVolumeFailedPatchCount);
        }

        if (w.button("Run Benchmark"))
        {
            startVolumeBenchmark();
        }
        if (mVolumeBenchmarkActive)
        {
            w.text(
                "Benchmarking volumetric method " + std::to_string(mVolumeBenchmarkMethod + 1) + " / " +
                std::to_string(mVolumeBenchmarkMethods.size())
            );
            w.text(
                "Orientation " + std::to_string(std::min(mVolumeBenchmarkOrientation + 1u, kVolumeBenchmarkOrientationCount)) +
                " / " + std::to_string(kVolumeBenchmarkOrientationCount)
            );
        }
        if (!mVolumeBenchmarkOutputPath.empty())
        {
            w.text("Benchmark CSV: %s", mVolumeBenchmarkOutputPath.c_str());
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
            {10, "CDT + Flip"}};
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
            mBenchmarkActive = true;
            mBenchmarkStep = 0;
            mCurrentMethod = 0;
            mWaitFrameCounter = 0;
            mGpuFrameCounter = 0;
            mBenchmarkMethods = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10};
            mBenchmarkLog.clear();

            mBenchmarkOutputPath = makeTimestampedCsvPath("helper_lane_planar_benchmark");
            std::filesystem::create_directories(std::filesystem::path(mBenchmarkOutputPath).parent_path());
            mBenchmarkFile.open(mBenchmarkOutputPath, std::ios::out | std::ios::trunc);
            if (mBenchmarkFile.is_open())
                mBenchmarkFile << "Method,CPU_Time_ms,GPU_Median_ms,GPU_Mean_ms,GPU_StdDev_ms,HelperLaneCount,EdgeLength\n";
        }

        if (!mBenchmarkOutputPath.empty())
        {
            w.text("Benchmark CSV: %s", mBenchmarkOutputPath.c_str());
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

int runMain(int argc, char** argv)
{
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
