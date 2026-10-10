#pragma once
#include "Triangulation/2D/ProjectedPatchTriangulation.h"
#include "Triangulation/GeometryMath.h"
#include <unordered_set>
namespace ProjectedTriangulation::Detail
{
using namespace Triangulation3D;
using namespace TriangulationCommon;
Falcor::float3 unprojectFromFrame(const Falcor::float2& point, const ProjectionFrame& frame);
float cross2(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c);
bool pointOnSegment(const Falcor::float2& point, const Falcor::float2& a, const Falcor::float2& b, float eps = 1e-6f);
bool pointInsideOrOnPolygon(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& polygon, const Falcor::float2& point);
bool segmentsProperlyIntersect(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c, const Falcor::float2& d);
bool segmentsIntersectOrTouch(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c, const Falcor::float2& d);
bool edgesShareEndpoint(const EdgeKey& a, const EdgeKey& b);
bool edgePassesThroughOtherVertex(const std::vector<Vertex>& vertices, uint32_t a, uint32_t b);
bool candidateEdgeStaysInsideBoundaryUncached(const ProjectedPatch& patch, uint32_t a, uint32_t b);
bool candidateEdgeStaysInsideBoundary(const ProjectedPatch& patch, uint32_t a, uint32_t b);
bool edgeIntersectsExistingEdges(
    const std::vector<Vertex>& vertices,
    const std::unordered_set<EdgeKey, EdgeKeyHash>& existingEdges,
    const EdgeKey& candidate
);
bool pointStrictlyInsideTriangle(const Falcor::float2& point, const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c);
float triangleSignedArea2(const std::vector<Vertex>& vertices, uint32_t a, uint32_t b, uint32_t c);
bool triangleContainsNoOtherPoint(const std::vector<Vertex>& vertices, uint32_t a, uint32_t b, uint32_t c);
float angleRadiansAt(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c);
void triangleAnglesRadians(const std::vector<Vertex>& vertices, uint32_t a, uint32_t b, uint32_t c, float& minAngle, float& maxAngle);
bool triangleCentroidInsideBoundary(const ProjectedPatch& patch, uint32_t a, uint32_t b, uint32_t c);
bool triangleIsValidEmptyPatchTriangle(const ProjectedPatch& patch, uint32_t a, uint32_t b, uint32_t c);
void appendTriangleCCW(std::vector<uint32_t>& indices, const std::vector<Vertex>& vertices, uint32_t a, uint32_t b, uint32_t c);
float polygonSignedArea2(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& polygon);
Falcor::float2 polygonCentroid(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& polygon);
uint64_t makeDirectedEdgeKey(uint32_t from, uint32_t to);
bool appendEarClippedFace(std::vector<uint32_t>& indices, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& face);
std::vector<uint32_t> triangulateGreedyPointSet(const ProjectedPatch&, bool);
std::vector<uint32_t> triangulateLmtMinimumWeight(const ProjectedPatch&, bool, bool);
std::vector<uint32_t> triangulatePclGreedyProjection(const ProjectedPatch&, const TriangulationOptions&);
} // namespace ProjectedTriangulation::Detail
