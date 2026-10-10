#include "SampleSupport.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

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

void chooseVolumeSegmentsForTarget(
    uint32_t geometryType,
    uint32_t targetVertexCount,
    uint32_t& latitudeSegments,
    uint32_t& longitudeSegments
)
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
            const uint64_t error = vertexCount > targetVertexCount ? static_cast<uint64_t>(vertexCount - targetVertexCount)
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
    filename << "_" << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() << ".csv";

    return (std::filesystem::path(HELPERLANE_SOURCE_DIR) / "analysis/gui" / filename.str()).string();
}

double length3(const float3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

std::set<std::pair<uint32_t, uint32_t>> makeEdgeSet(const std::vector<uint32_t>& indices);

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
    const auto edges = makeEdgeSet(indices);
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
        float3(0.0f, 0.0f, cameraDistance), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), math::Handedness::RightHanded
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
        std::vector<float2> screen(vertices.size());
        std::vector<bool> visible(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i)
            visible[i] =
                projectToScreenSpace(viewProj, transformPointByMatrix(modelTransform, vertices[i]) + offset, width, height, screen[i]);
        for (const auto& edge : edges)
        {
            if (edge.first >= vertices.size() || edge.second >= vertices.size() || !visible[edge.first] || !visible[edge.second])
                continue;
            const auto delta = screen[edge.first] - screen[edge.second];
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
