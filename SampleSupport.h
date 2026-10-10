#pragma once
#include "Falcor.h"
#include "Triangulation/3D/Triangulation3D.h"
#include <set>
#include <string>
#include <vector>
using namespace Falcor;

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

constexpr uint32_t kVolumeBenchmarkZSteps = 8;
constexpr uint32_t kVolumeBenchmarkXSteps = 4;
constexpr uint32_t kVolumeBenchmarkOrientationCount = kVolumeBenchmarkZSteps * kVolumeBenchmarkXSteps;

Triangulation3D::Method getVolumeTriangulationMethod(uint32_t type);
std::string getVolumeTriangulationMethodName(uint32_t type);
std::string getVolumeGeometryName(uint32_t type);
bool isObjVolumeGeometry(uint32_t type);
bool isOpenPatchVolumeGeometry(uint32_t type);
uint32_t estimateVolumeSourceVertexCount(uint32_t geometryType, uint32_t latitudeSegments, uint32_t longitudeSegments);
void chooseVolumeSegmentsForTarget(
    uint32_t geometryType,
    uint32_t targetVertexCount,
    uint32_t& latitudeSegments,
    uint32_t& longitudeSegments
);
std::string getDownloadsPath();
std::string makeTimestampedCsvPath(const std::string& prefix);
double length3(const float3& v);
float4x4 makeVolumeBenchmarkModelTransform(uint32_t orientationIndex);
float3 transformPointByMatrix(const float4x4& transform, const float3& position);
double computeUniqueEdgeLength3D(const std::vector<float3>& vertices, const std::vector<uint32_t>& indices, uint32_t& uniqueEdgeCount);
float4x4 makeVolumeViewProj(
    uint32_t width,
    uint32_t height,
    uint32_t gridCols,
    uint32_t gridRows,
    uint32_t gridLayers,
    bool use3DGrid,
    float gridSpacing,
    float boundsRadius
);
float3 getVolumeInstanceOffset(
    uint32_t instanceId,
    uint32_t gridCols,
    uint32_t gridRows,
    uint32_t gridLayers,
    bool use3DGrid,
    float gridSpacing
);
bool projectToScreenSpace(const float4x4& viewProj, const float3& position, uint32_t width, uint32_t height, float2& outPosition);
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
);
float computeMaxDistanceFromOrigin(const std::vector<float3>& vertices);
std::set<std::pair<uint32_t, uint32_t>> makeEdgeSet(const std::vector<uint32_t>& indices);
