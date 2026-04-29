#include "Triangulation3D.h"

#include "ShaderTypes.h"
#include "TriangulationHelpers.h"

#include <Eigen/Dense>
#include <igl/triangle/triangulate.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Triangulation3D
{
namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

struct EdgeKey
{
    uint32_t a = 0;
    uint32_t b = 0;

    bool operator==(const EdgeKey& other) const { return a == other.a && b == other.b; }
    bool operator<(const EdgeKey& other) const { return a < other.a || (a == other.a && b < other.b); }
};

struct EdgeKeyHash
{
    size_t operator()(const EdgeKey& key) const noexcept
    {
        return (static_cast<size_t>(key.a) << 32) ^ static_cast<size_t>(key.b);
    }
};

struct PatchStats
{
    ProjectionFrame frame;
    std::vector<uint32_t> vertexIndices;
    float maxPlaneDistance = 0.0f;
    float maxNormalDeviationDegrees = 0.0f;
    float planeTolerance = 0.0f;
    bool valid = false;
};

struct ProjectedPatch
{
    std::vector<uint32_t> localToGlobal;
    std::unordered_map<uint32_t, uint32_t> globalToLocal;
    std::vector<Vertex> projectedVertices;
    std::vector<Falcor::float3> originalVertices;
    std::vector<uint32_t> boundaryLoop;
    std::vector<std::pair<uint32_t, uint32_t>> boundaryEdges;
};

float dot3(const Falcor::float3& a, const Falcor::float3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Falcor::float3 add3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

Falcor::float3 subtract3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

Falcor::float3 multiply3(const Falcor::float3& v, float scale)
{
    return Falcor::float3(v.x * scale, v.y * scale, v.z * scale);
}

Falcor::float3 cross3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

float length3(const Falcor::float3& v)
{
    return std::sqrt(dot3(v, v));
}

float distanceSquared2(const Falcor::float2& a, const Falcor::float2& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float distanceSquared3(const Falcor::float3& a, const Falcor::float3& b)
{
    const Falcor::float3 delta = subtract3(a, b);
    return dot3(delta, delta);
}

Falcor::float3 normalize3(const Falcor::float3& v)
{
    const float len = length3(v);
    if (len <= 1e-12f)
    {
        return Falcor::float3(0.0f, 0.0f, 0.0f);
    }
    return multiply3(v, 1.0f / len);
}

EdgeKey makeEdgeKey(uint32_t a, uint32_t b)
{
    if (a > b)
    {
        std::swap(a, b);
    }
    return EdgeKey{a, b};
}

Falcor::float3 computeFaceNormalRaw(const Mesh& mesh, const std::vector<uint32_t>& face)
{
    Falcor::float3 normal(0.0f, 0.0f, 0.0f);
    const size_t vertexCount = face.size();

    for (size_t i = 0; i < vertexCount; ++i)
    {
        const Falcor::float3& current = mesh.vertices[face[i]];
        const Falcor::float3& next = mesh.vertices[face[(i + 1) % vertexCount]];

        normal.x += (current.y - next.y) * (current.z + next.z);
        normal.y += (current.z - next.z) * (current.x + next.x);
        normal.z += (current.x - next.x) * (current.y + next.y);
    }

    return normal;
}

bool computeFaceNormal(const Mesh& mesh, uint32_t faceIndex, Falcor::float3& outNormal, float& outWeight)
{
    if (faceIndex >= mesh.faces.size() || mesh.faces[faceIndex].size() < 3)
    {
        return false;
    }

    const Falcor::float3 rawNormal = computeFaceNormalRaw(mesh, mesh.faces[faceIndex]);
    const float rawLength = length3(rawNormal);
    if (rawLength <= 1e-8f)
    {
        return false;
    }

    outNormal = multiply3(rawNormal, 1.0f / rawLength);
    outWeight = rawLength;
    return true;
}

float angleDegreesBetweenUnitVectors(const Falcor::float3& a, const Falcor::float3& b)
{
    const float dotValue = std::clamp(std::abs(dot3(a, b)), 0.0f, 1.0f);
    return std::acos(dotValue) * 180.0f / kPi;
}

bool buildFrameFromNormal(const Falcor::float3& origin, const Falcor::float3& normal, ProjectionFrame& frame)
{
    frame.origin = origin;
    frame.normal = normalize3(normal);
    if (length3(frame.normal) <= 1e-8f)
    {
        return false;
    }

    const Falcor::float3 referenceAxis =
        std::abs(frame.normal.z) < 0.9f ? Falcor::float3(0.0f, 0.0f, 1.0f) : Falcor::float3(0.0f, 1.0f, 0.0f);

    frame.tangent = normalize3(cross3(referenceAxis, frame.normal));
    if (length3(frame.tangent) <= 1e-8f)
    {
        return false;
    }

    frame.bitangent = normalize3(cross3(frame.normal, frame.tangent));
    return length3(frame.bitangent) > 1e-8f;
}

Falcor::float2 projectToFrame(const Falcor::float3& point, const ProjectionFrame& frame)
{
    const Falcor::float3 relative = subtract3(point, frame.origin);
    return Falcor::float2(dot3(relative, frame.tangent), dot3(relative, frame.bitangent));
}

Falcor::float3 unprojectFromFrame(const Falcor::float2& point, const ProjectionFrame& frame)
{
    return add3(frame.origin, add3(multiply3(frame.tangent, point.x), multiply3(frame.bitangent, point.y)));
}

std::vector<uint32_t> collectVertexIndices(const Mesh& mesh, const std::vector<uint32_t>& faceIndices)
{
    std::set<uint32_t> uniqueVertices;
    for (uint32_t faceIndex : faceIndices)
    {
        if (faceIndex >= mesh.faces.size())
        {
            continue;
        }
        for (uint32_t vertexIndex : mesh.faces[faceIndex])
        {
            if (vertexIndex < mesh.vertices.size())
            {
                uniqueVertices.insert(vertexIndex);
            }
        }
    }
    return std::vector<uint32_t>(uniqueVertices.begin(), uniqueVertices.end());
}

float computeBoundsDiagonal(const Mesh& mesh, const std::vector<uint32_t>& vertexIndices)
{
    if (vertexIndices.empty())
    {
        return 0.0f;
    }

    Falcor::float3 minPoint = mesh.vertices[vertexIndices.front()];
    Falcor::float3 maxPoint = mesh.vertices[vertexIndices.front()];

    for (uint32_t vertexIndex : vertexIndices)
    {
        const Falcor::float3& point = mesh.vertices[vertexIndex];
        minPoint.x = std::min(minPoint.x, point.x);
        minPoint.y = std::min(minPoint.y, point.y);
        minPoint.z = std::min(minPoint.z, point.z);
        maxPoint.x = std::max(maxPoint.x, point.x);
        maxPoint.y = std::max(maxPoint.y, point.y);
        maxPoint.z = std::max(maxPoint.z, point.z);
    }

    return length3(subtract3(maxPoint, minPoint));
}

PatchStats computePatchStats(const Mesh& mesh, const std::vector<uint32_t>& faceIndices, const PatchGrowthOptions& options)
{
    PatchStats stats;
    stats.vertexIndices = collectVertexIndices(mesh, faceIndices);
    if (faceIndices.empty() || stats.vertexIndices.size() < 3)
    {
        return stats;
    }

    Falcor::float3 seedNormal;
    float seedWeight = 0.0f;
    if (!computeFaceNormal(mesh, faceIndices.front(), seedNormal, seedWeight))
    {
        return stats;
    }

    Falcor::float3 normalSum(0.0f, 0.0f, 0.0f);
    Falcor::float3 centroid(0.0f, 0.0f, 0.0f);
    uint32_t validFaceCount = 0;

    for (uint32_t vertexIndex : stats.vertexIndices)
    {
        centroid = add3(centroid, mesh.vertices[vertexIndex]);
    }
    centroid = multiply3(centroid, 1.0f / static_cast<float>(stats.vertexIndices.size()));

    for (uint32_t faceIndex : faceIndices)
    {
        Falcor::float3 faceNormal;
        float faceWeight = 0.0f;
        if (!computeFaceNormal(mesh, faceIndex, faceNormal, faceWeight))
        {
            continue;
        }

        if (dot3(faceNormal, seedNormal) < 0.0f)
        {
            faceNormal = multiply3(faceNormal, -1.0f);
        }

        normalSum = add3(normalSum, multiply3(faceNormal, faceWeight));
        ++validFaceCount;
    }

    if (validFaceCount == 0 || !buildFrameFromNormal(centroid, normalSum, stats.frame))
    {
        return stats;
    }

    for (uint32_t vertexIndex : stats.vertexIndices)
    {
        const float distance = std::abs(dot3(subtract3(mesh.vertices[vertexIndex], stats.frame.origin), stats.frame.normal));
        stats.maxPlaneDistance = std::max(stats.maxPlaneDistance, distance);
    }

    for (uint32_t faceIndex : faceIndices)
    {
        Falcor::float3 faceNormal;
        float faceWeight = 0.0f;
        if (!computeFaceNormal(mesh, faceIndex, faceNormal, faceWeight))
        {
            continue;
        }

        stats.maxNormalDeviationDegrees =
            std::max(stats.maxNormalDeviationDegrees, angleDegreesBetweenUnitVectors(faceNormal, stats.frame.normal));
    }

    const float diagonal = computeBoundsDiagonal(mesh, stats.vertexIndices);
    stats.planeTolerance = std::max(options.minPlaneDeviation, options.maxRelativePlaneDeviation * diagonal);
    stats.valid = stats.maxPlaneDistance <= stats.planeTolerance &&
                  stats.maxNormalDeviationDegrees <= options.maxNormalDeviationDegrees;
    return stats;
}

std::vector<std::vector<uint32_t>> buildFaceAdjacency(const Mesh& mesh)
{
    std::map<EdgeKey, std::vector<uint32_t>> edgeFaces;

    for (uint32_t faceIndex = 0; faceIndex < static_cast<uint32_t>(mesh.faces.size()); ++faceIndex)
    {
        const std::vector<uint32_t>& face = mesh.faces[faceIndex];
        if (face.size() < 2)
        {
            continue;
        }

        for (size_t i = 0; i < face.size(); ++i)
        {
            edgeFaces[makeEdgeKey(face[i], face[(i + 1) % face.size()])].push_back(faceIndex);
        }
    }

    std::vector<std::vector<uint32_t>> adjacency(mesh.faces.size());
    for (const auto& [edge, faces] : edgeFaces)
    {
        (void)edge;
        for (uint32_t faceA : faces)
        {
            for (uint32_t faceB : faces)
            {
                if (faceA != faceB)
                {
                    adjacency[faceA].push_back(faceB);
                }
            }
        }
    }

    for (std::vector<uint32_t>& neighbors : adjacency)
    {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }

    return adjacency;
}

std::vector<std::pair<uint32_t, uint32_t>> extractBoundaryEdges(const Mesh& mesh, const std::vector<uint32_t>& faceIndices)
{
    std::map<EdgeKey, uint32_t> edgeUseCount;

    for (uint32_t faceIndex : faceIndices)
    {
        const std::vector<uint32_t>& face = mesh.faces[faceIndex];
        for (size_t i = 0; i < face.size(); ++i)
        {
            ++edgeUseCount[makeEdgeKey(face[i], face[(i + 1) % face.size()])];
        }
    }

    std::vector<std::pair<uint32_t, uint32_t>> boundaryEdges;
    for (const auto& [edge, count] : edgeUseCount)
    {
        if (count == 1)
        {
            boundaryEdges.push_back({edge.a, edge.b});
        }
    }

    return boundaryEdges;
}

bool orderBoundaryLoop(const std::vector<std::pair<uint32_t, uint32_t>>& boundaryEdges, std::vector<uint32_t>& outLoop)
{
    outLoop.clear();
    if (boundaryEdges.size() < 3)
    {
        return false;
    }

    std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;
    for (const auto& edge : boundaryEdges)
    {
        adjacency[edge.first].push_back(edge.second);
        adjacency[edge.second].push_back(edge.first);
    }

    for (const auto& [vertex, neighbors] : adjacency)
    {
        (void)vertex;
        if (neighbors.size() != 2)
        {
            return false;
        }
    }

    const uint32_t start = std::min_element(adjacency.begin(), adjacency.end(), [](const auto& a, const auto& b)
                                           { return a.first < b.first; })
                               ->first;

    uint32_t previous = kInvalidIndex;
    uint32_t current = start;

    for (size_t step = 0; step <= boundaryEdges.size(); ++step)
    {
        outLoop.push_back(current);
        const std::vector<uint32_t>& neighbors = adjacency[current];
        const uint32_t next = neighbors[0] == previous ? neighbors[1] : neighbors[0];

        if (next == start)
        {
            return outLoop.size() == adjacency.size();
        }

        previous = current;
        current = next;
    }

    outLoop.clear();
    return false;
}

Patch makePatch(const Mesh& mesh, const std::vector<uint32_t>& faceIndices, const PatchGrowthOptions& options)
{
    Patch patch;
    patch.faceIndices = faceIndices;

    const PatchStats stats = computePatchStats(mesh, faceIndices, options);
    patch.vertexIndices = stats.vertexIndices;
    patch.frame = stats.frame;
    patch.maxPlaneDistance = stats.maxPlaneDistance;
    patch.maxNormalDeviationDegrees = stats.maxNormalDeviationDegrees;

    patch.boundaryEdges = extractBoundaryEdges(mesh, faceIndices);
    patch.isDiskLike = orderBoundaryLoop(patch.boundaryEdges, patch.boundaryLoop);
    patch.message = patch.isDiskLike ? "Patch is disk-like." : "Patch boundary is not a single disk-like loop.";
    return patch;
}

bool isFiniteFace(const Mesh& mesh, const std::vector<uint32_t>& face)
{
    if (face.size() < 3)
    {
        return false;
    }
    for (uint32_t vertexIndex : face)
    {
        if (vertexIndex >= mesh.vertices.size())
        {
            return false;
        }
    }
    return true;
}

ProjectedPatch projectPatch(const Mesh& mesh, const Patch& patch)
{
    ProjectedPatch projectedPatch;
    projectedPatch.localToGlobal = patch.vertexIndices;
    projectedPatch.projectedVertices.reserve(projectedPatch.localToGlobal.size());
    projectedPatch.originalVertices.reserve(projectedPatch.localToGlobal.size());

    for (uint32_t localIndex = 0; localIndex < static_cast<uint32_t>(projectedPatch.localToGlobal.size()); ++localIndex)
    {
        const uint32_t globalIndex = projectedPatch.localToGlobal[localIndex];
        projectedPatch.globalToLocal[globalIndex] = localIndex;

        Vertex projectedVertex;
        projectedVertex.pos = projectToFrame(mesh.vertices[globalIndex], patch.frame);
        projectedPatch.projectedVertices.push_back(projectedVertex);
        projectedPatch.originalVertices.push_back(mesh.vertices[globalIndex]);
    }

    for (uint32_t globalIndex : patch.boundaryLoop)
    {
        projectedPatch.boundaryLoop.push_back(projectedPatch.globalToLocal.at(globalIndex));
    }

    for (size_t i = 0; i < projectedPatch.boundaryLoop.size(); ++i)
    {
        const uint32_t a = projectedPatch.boundaryLoop[i];
        const uint32_t b = projectedPatch.boundaryLoop[(i + 1) % projectedPatch.boundaryLoop.size()];
        projectedPatch.boundaryEdges.push_back({a, b});
    }

    return projectedPatch;
}

float cross2(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointOnSegment(const Falcor::float2& point, const Falcor::float2& a, const Falcor::float2& b, float eps = 1e-6f)
{
    if (std::abs(cross2(a, b, point)) > eps)
    {
        return false;
    }
    return point.x >= std::min(a.x, b.x) - eps && point.x <= std::max(a.x, b.x) + eps &&
           point.y >= std::min(a.y, b.y) - eps && point.y <= std::max(a.y, b.y) + eps;
}

bool pointInsideOrOnPolygon(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& polygon, const Falcor::float2& point)
{
    bool inside = false;

    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    {
        const Falcor::float2& a = vertices[polygon[i]].pos;
        const Falcor::float2& b = vertices[polygon[j]].pos;

        if (pointOnSegment(point, a, b))
        {
            return true;
        }

        const bool crossesY = (a.y > point.y) != (b.y > point.y);
        if (crossesY)
        {
            const float x = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
            if (point.x < x)
            {
                inside = !inside;
            }
        }
    }

    return inside;
}

bool segmentsProperlyIntersect(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c, const Falcor::float2& d)
{
    const float o1 = cross2(a, b, c);
    const float o2 = cross2(a, b, d);
    const float o3 = cross2(c, d, a);
    const float o4 = cross2(c, d, b);

    return ((o1 > 0.0f && o2 < 0.0f) || (o1 < 0.0f && o2 > 0.0f)) &&
           ((o3 > 0.0f && o4 < 0.0f) || (o3 < 0.0f && o4 > 0.0f));
}

bool segmentsIntersectOrTouch(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c, const Falcor::float2& d)
{
    constexpr float eps = 1e-6f;
    const float o1 = cross2(a, b, c);
    const float o2 = cross2(a, b, d);
    const float o3 = cross2(c, d, a);
    const float o4 = cross2(c, d, b);

    if (((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps)) &&
        ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps)))
    {
        return true;
    }

    return (std::abs(o1) <= eps && pointOnSegment(c, a, b, eps)) ||
           (std::abs(o2) <= eps && pointOnSegment(d, a, b, eps)) ||
           (std::abs(o3) <= eps && pointOnSegment(a, c, d, eps)) ||
           (std::abs(o4) <= eps && pointOnSegment(b, c, d, eps));
}

bool edgesShareEndpoint(const EdgeKey& a, const EdgeKey& b)
{
    return a.a == b.a || a.a == b.b || a.b == b.a || a.b == b.b;
}

bool edgePassesThroughOtherVertex(const std::vector<Vertex>& vertices, uint32_t a, uint32_t b)
{
    const Falcor::float2& pa = vertices[a].pos;
    const Falcor::float2& pb = vertices[b].pos;
    for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i)
    {
        if (i == a || i == b)
        {
            continue;
        }
        if (pointOnSegment(vertices[i].pos, pa, pb))
        {
            return true;
        }
    }

    return false;
}

bool candidateEdgeStaysInsideBoundary(const ProjectedPatch& patch, uint32_t a, uint32_t b)
{
    const Falcor::float2& pa = patch.projectedVertices[a].pos;
    const Falcor::float2& pb = patch.projectedVertices[b].pos;
    const Falcor::float2 midpoint((pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f);

    if (!pointInsideOrOnPolygon(patch.projectedVertices, patch.boundaryLoop, midpoint))
    {
        return false;
    }

    if (edgePassesThroughOtherVertex(patch.projectedVertices, a, b))
    {
        return false;
    }

    const EdgeKey candidate = makeEdgeKey(a, b);
    for (const auto& boundaryEdge : patch.boundaryEdges)
    {
        const EdgeKey boundary = makeEdgeKey(boundaryEdge.first, boundaryEdge.second);
        if (edgesShareEndpoint(candidate, boundary))
        {
            continue;
        }

        const Falcor::float2& pc = patch.projectedVertices[boundary.a].pos;
        const Falcor::float2& pd = patch.projectedVertices[boundary.b].pos;
        if (segmentsIntersectOrTouch(pa, pb, pc, pd))
        {
            return false;
        }
    }

    return true;
}

bool edgeIntersectsExistingEdges(
    const std::vector<Vertex>& vertices,
    const std::unordered_set<EdgeKey, EdgeKeyHash>& existingEdges,
    const EdgeKey& candidate
)
{
    const Falcor::float2& a = vertices[candidate.a].pos;
    const Falcor::float2& b = vertices[candidate.b].pos;

    if (edgePassesThroughOtherVertex(vertices, candidate.a, candidate.b))
    {
        return true;
    }

    for (const EdgeKey& edge : existingEdges)
    {
        if (edgesShareEndpoint(candidate, edge))
        {
            continue;
        }

        const Falcor::float2& c = vertices[edge.a].pos;
        const Falcor::float2& d = vertices[edge.b].pos;
        if (segmentsIntersectOrTouch(a, b, c, d))
        {
            return true;
        }
    }

    return false;
}

bool pointStrictlyInsideTriangle(
    const Falcor::float2& point,
    const Falcor::float2& a,
    const Falcor::float2& b,
    const Falcor::float2& c
)
{
    const float ab = cross2(a, b, point);
    const float bc = cross2(b, c, point);
    const float ca = cross2(c, a, point);
    return (ab > 1e-6f && bc > 1e-6f && ca > 1e-6f) || (ab < -1e-6f && bc < -1e-6f && ca < -1e-6f);
}

float triangleSignedArea2(const std::vector<Vertex>& vertices, uint32_t a, uint32_t b, uint32_t c)
{
    return cross2(vertices[a].pos, vertices[b].pos, vertices[c].pos);
}

bool triangleContainsNoOtherPoint(const std::vector<Vertex>& vertices, uint32_t a, uint32_t b, uint32_t c)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i)
    {
        if (i == a || i == b || i == c)
        {
            continue;
        }

        if (pointStrictlyInsideTriangle(vertices[i].pos, vertices[a].pos, vertices[b].pos, vertices[c].pos))
        {
            return false;
        }
    }

    return true;
}

float angleRadiansAt(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c)
{
    const float abx = a.x - b.x;
    const float aby = a.y - b.y;
    const float cbx = c.x - b.x;
    const float cby = c.y - b.y;
    const float abLength = std::sqrt(abx * abx + aby * aby);
    const float cbLength = std::sqrt(cbx * cbx + cby * cby);
    if (abLength <= 1e-8f || cbLength <= 1e-8f)
    {
        return 0.0f;
    }

    const float cosine = std::clamp((abx * cbx + aby * cby) / (abLength * cbLength), -1.0f, 1.0f);
    return std::acos(cosine);
}

void triangleAnglesRadians(const std::vector<Vertex>& vertices, uint32_t a, uint32_t b, uint32_t c, float& minAngle, float& maxAngle)
{
    const float angleA = angleRadiansAt(vertices[b].pos, vertices[a].pos, vertices[c].pos);
    const float angleB = angleRadiansAt(vertices[a].pos, vertices[b].pos, vertices[c].pos);
    const float angleC = angleRadiansAt(vertices[a].pos, vertices[c].pos, vertices[b].pos);
    minAngle = std::min(angleA, std::min(angleB, angleC));
    maxAngle = std::max(angleA, std::max(angleB, angleC));
}

bool triangleCentroidInsideBoundary(const ProjectedPatch& patch, uint32_t a, uint32_t b, uint32_t c)
{
    const Falcor::float2 centroid(
        (patch.projectedVertices[a].pos.x + patch.projectedVertices[b].pos.x + patch.projectedVertices[c].pos.x) / 3.0f,
        (patch.projectedVertices[a].pos.y + patch.projectedVertices[b].pos.y + patch.projectedVertices[c].pos.y) / 3.0f
    );
    return pointInsideOrOnPolygon(patch.projectedVertices, patch.boundaryLoop, centroid);
}

bool triangleIsValidEmptyPatchTriangle(const ProjectedPatch& patch, uint32_t a, uint32_t b, uint32_t c)
{
    if (std::abs(triangleSignedArea2(patch.projectedVertices, a, b, c)) <= 1e-8f)
    {
        return false;
    }
    if (!triangleCentroidInsideBoundary(patch, a, b, c))
    {
        return false;
    }
    if (!triangleContainsNoOtherPoint(patch.projectedVertices, a, b, c))
    {
        return false;
    }
    return true;
}

void appendTriangleCCW(std::vector<uint32_t>& indices, const std::vector<Vertex>& vertices, uint32_t a, uint32_t b, uint32_t c)
{
    if (triangleSignedArea2(vertices, a, b, c) >= 0.0f)
    {
        indices.push_back(a);
        indices.push_back(b);
        indices.push_back(c);
    }
    else
    {
        indices.push_back(a);
        indices.push_back(c);
        indices.push_back(b);
    }
}

float polygonSignedArea2(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& polygon)
{
    float area = 0.0f;
    for (size_t i = 0; i < polygon.size(); ++i)
    {
        const Falcor::float2& a = vertices[polygon[i]].pos;
        const Falcor::float2& b = vertices[polygon[(i + 1) % polygon.size()]].pos;
        area += a.x * b.y - a.y * b.x;
    }
    return area;
}

Falcor::float2 polygonCentroid(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& polygon)
{
    Falcor::float2 centroid(0.0f, 0.0f);
    if (polygon.empty())
    {
        return centroid;
    }

    for (uint32_t vertexIndex : polygon)
    {
        centroid.x += vertices[vertexIndex].pos.x;
        centroid.y += vertices[vertexIndex].pos.y;
    }

    const float invCount = 1.0f / static_cast<float>(polygon.size());
    centroid.x *= invCount;
    centroid.y *= invCount;
    return centroid;
}

uint64_t makeDirectedEdgeKey(uint32_t from, uint32_t to)
{
    return (static_cast<uint64_t>(from) << 32) | static_cast<uint64_t>(to);
}

bool appendEarClippedFace(std::vector<uint32_t>& indices, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& face)
{
    if (face.size() < 3)
    {
        return false;
    }

    if (face.size() == 3)
    {
        appendTriangleCCW(indices, vertices, face[0], face[1], face[2]);
        return true;
    }

    std::vector<uint32_t> polygon = face;
    if (polygonSignedArea2(vertices, polygon) < 0.0f)
    {
        std::reverse(polygon.begin(), polygon.end());
    }

    bool emittedTriangle = false;
    while (polygon.size() > 3)
    {
        bool clippedEar = false;
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const uint32_t prev = polygon[(i + polygon.size() - 1) % polygon.size()];
            const uint32_t current = polygon[i];
            const uint32_t next = polygon[(i + 1) % polygon.size()];

            if (triangleSignedArea2(vertices, prev, current, next) <= 1e-8f)
            {
                continue;
            }

            bool containsPoint = false;
            for (uint32_t vertexIndex : polygon)
            {
                if (vertexIndex == prev || vertexIndex == current || vertexIndex == next)
                {
                    continue;
                }
                if (pointStrictlyInsideTriangle(vertices[vertexIndex].pos, vertices[prev].pos, vertices[current].pos, vertices[next].pos))
                {
                    containsPoint = true;
                    break;
                }
            }

            if (containsPoint)
            {
                continue;
            }

            appendTriangleCCW(indices, vertices, prev, current, next);
            polygon.erase(polygon.begin() + static_cast<std::ptrdiff_t>(i));
            emittedTriangle = true;
            clippedEar = true;
            break;
        }

        if (!clippedEar)
        {
            return emittedTriangle;
        }
    }

    appendTriangleCCW(indices, vertices, polygon[0], polygon[1], polygon[2]);
    return true;
}

std::vector<uint32_t> extractTrianglesFromPlanarGraph(
    const ProjectedPatch& patch,
    const std::unordered_set<EdgeKey, EdgeKeyHash>& edges
)
{
    std::vector<uint32_t> indices;
    const uint32_t vertexCount = static_cast<uint32_t>(patch.projectedVertices.size());
    std::vector<std::vector<uint32_t>> adjacency(vertexCount);

    for (const EdgeKey& edge : edges)
    {
        if (edge.a >= vertexCount || edge.b >= vertexCount || edge.a == edge.b)
        {
            continue;
        }
        adjacency[edge.a].push_back(edge.b);
        adjacency[edge.b].push_back(edge.a);
    }

    for (uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
    {
        std::sort(adjacency[vertexIndex].begin(), adjacency[vertexIndex].end(), [&](uint32_t lhs, uint32_t rhs)
                  {
                      const Falcor::float2& origin = patch.projectedVertices[vertexIndex].pos;
                      const Falcor::float2& a = patch.projectedVertices[lhs].pos;
                      const Falcor::float2& b = patch.projectedVertices[rhs].pos;
                      return std::atan2(a.y - origin.y, a.x - origin.x) < std::atan2(b.y - origin.y, b.x - origin.x);
                  });
    }

    std::unordered_set<uint64_t> visitedHalfEdges;
    visitedHalfEdges.reserve(edges.size() * 2);

    for (uint32_t from = 0; from < vertexCount; ++from)
    {
        for (uint32_t to : adjacency[from])
        {
            const uint64_t startKey = makeDirectedEdgeKey(from, to);
            if (visitedHalfEdges.find(startKey) != visitedHalfEdges.end())
            {
                continue;
            }

            std::vector<uint32_t> face;
            uint32_t currentFrom = from;
            uint32_t currentTo = to;
            const size_t maxSteps = edges.size() * 2 + 1;

            for (size_t step = 0; step < maxSteps; ++step)
            {
                const uint64_t directedKey = makeDirectedEdgeKey(currentFrom, currentTo);
                if (visitedHalfEdges.find(directedKey) != visitedHalfEdges.end())
                {
                    break;
                }

                visitedHalfEdges.insert(directedKey);
                face.push_back(currentFrom);

                const std::vector<uint32_t>& neighbors = adjacency[currentTo];
                const auto backIt = std::find(neighbors.begin(), neighbors.end(), currentFrom);
                if (backIt == neighbors.end() || neighbors.empty())
                {
                    face.clear();
                    break;
                }

                const size_t backIndex = static_cast<size_t>(std::distance(neighbors.begin(), backIt));
                const size_t nextIndex = (backIndex + neighbors.size() - 1) % neighbors.size();
                const uint32_t next = neighbors[nextIndex];
                currentFrom = currentTo;
                currentTo = next;

                if (currentFrom == from && currentTo == to)
                {
                    break;
                }
            }

            if (face.size() < 3 || currentFrom != from || currentTo != to)
            {
                continue;
            }

            const float faceArea = polygonSignedArea2(patch.projectedVertices, face);
            if (faceArea <= 1e-8f)
            {
                continue;
            }

            const Falcor::float2 centroid = polygonCentroid(patch.projectedVertices, face);
            if (!pointInsideOrOnPolygon(patch.projectedVertices, patch.boundaryLoop, centroid))
            {
                continue;
            }

            appendEarClippedFace(indices, patch.projectedVertices, face);
        }
    }

    return indices;
}

std::vector<uint32_t> triangulateGreedyPointSet(const ProjectedPatch& patch, bool use3DEdgeLength);

bool isBoundaryEdge(const ProjectedPatch& patch, const EdgeKey& edge)
{
    for (const auto& boundaryEdge : patch.boundaryEdges)
    {
        if (makeEdgeKey(boundaryEdge.first, boundaryEdge.second) == edge)
        {
            return true;
        }
    }
    return false;
}

float edgeWeight(const ProjectedPatch& patch, uint32_t a, uint32_t b, bool use3DEdgeLength)
{
    return std::sqrt(
        use3DEdgeLength ? distanceSquared3(patch.originalVertices[a], patch.originalVertices[b])
                        : distanceSquared2(patch.projectedVertices[a].pos, patch.projectedVertices[b].pos)
    );
}

std::vector<EdgeKey> collectVisibleCandidateEdges(const ProjectedPatch& patch)
{
    std::vector<EdgeKey> candidates;
    const uint32_t vertexCount = static_cast<uint32_t>(patch.projectedVertices.size());
    candidates.reserve(vertexCount * vertexCount / 2);

    for (uint32_t a = 0; a < vertexCount; ++a)
    {
        for (uint32_t b = a + 1; b < vertexCount; ++b)
        {
            const EdgeKey edge = makeEdgeKey(a, b);
            if (isBoundaryEdge(patch, edge) || candidateEdgeStaysInsideBoundary(patch, a, b))
            {
                candidates.push_back(edge);
            }
        }
    }

    return candidates;
}

bool edgeHasLmtWitness(const ProjectedPatch& patch, const EdgeKey& edge, bool use3DEdgeLength)
{
    if (isBoundaryEdge(patch, edge))
    {
        return true;
    }

    const uint32_t a = edge.a;
    const uint32_t b = edge.b;
    const Falcor::float2& pa = patch.projectedVertices[a].pos;
    const Falcor::float2& pb = patch.projectedVertices[b].pos;
    const float edgeLength = edgeWeight(patch, a, b, use3DEdgeLength);
    const uint32_t vertexCount = static_cast<uint32_t>(patch.projectedVertices.size());

    for (uint32_t c = 0; c < vertexCount; ++c)
    {
        if (c == a || c == b)
        {
            continue;
        }

        const float sideC = cross2(pa, pb, patch.projectedVertices[c].pos);
        if (std::abs(sideC) <= 1e-7f || !triangleIsValidEmptyPatchTriangle(patch, a, b, c))
        {
            continue;
        }

        for (uint32_t d = c + 1; d < vertexCount; ++d)
        {
            if (d == a || d == b)
            {
                continue;
            }

            const float sideD = cross2(pa, pb, patch.projectedVertices[d].pos);
            if (sideC * sideD >= -1e-7f || !triangleIsValidEmptyPatchTriangle(patch, a, b, d))
            {
                continue;
            }

            if (!candidateEdgeStaysInsideBoundary(patch, c, d))
            {
                continue;
            }

            const float oppositeDiagonalLength = edgeWeight(patch, c, d, use3DEdgeLength);
            if (edgeLength <= oppositeDiagonalLength + 1e-6f)
            {
                return true;
            }
        }
    }

    return false;
}

std::vector<uint32_t> triangulateLmtMinimumWeight(const ProjectedPatch& patch, bool use3DEdgeLength)
{
    const std::vector<EdgeKey> visibleCandidates = collectVisibleCandidateEdges(patch);

    std::vector<EdgeKey> lmtCandidates;
    lmtCandidates.reserve(visibleCandidates.size());
    for (const EdgeKey& edge : visibleCandidates)
    {
        if (edgeHasLmtWitness(patch, edge, use3DEdgeLength))
        {
            lmtCandidates.push_back(edge);
        }
    }

    std::unordered_set<EdgeKey, EdgeKeyHash> edges;
    edges.reserve(lmtCandidates.size());
    for (const auto& boundaryEdge : patch.boundaryEdges)
    {
        edges.insert(makeEdgeKey(boundaryEdge.first, boundaryEdge.second));
    }

    for (const EdgeKey& edge : lmtCandidates)
    {
        if (edges.find(edge) != edges.end())
        {
            continue;
        }

        bool crossesOtherCandidate = false;
        for (const EdgeKey& other : lmtCandidates)
        {
            if (edge == other || edgesShareEndpoint(edge, other))
            {
                continue;
            }
            if (segmentsIntersectOrTouch(
                    patch.projectedVertices[edge.a].pos,
                    patch.projectedVertices[edge.b].pos,
                    patch.projectedVertices[other.a].pos,
                    patch.projectedVertices[other.b].pos
                ))
            {
                crossesOtherCandidate = true;
                break;
            }
        }
        if (!crossesOtherCandidate && !edgeIntersectsExistingEdges(patch.projectedVertices, edges, edge))
        {
            edges.insert(edge);
        }
    }

    std::sort(lmtCandidates.begin(), lmtCandidates.end(), [&](const EdgeKey& a, const EdgeKey& b)
              { return edgeWeight(patch, a.a, a.b, use3DEdgeLength) < edgeWeight(patch, b.a, b.b, use3DEdgeLength); });

    for (const EdgeKey& edge : lmtCandidates)
    {
        if (edges.find(edge) != edges.end())
        {
            continue;
        }
        if (!edgeIntersectsExistingEdges(patch.projectedVertices, edges, edge))
        {
            edges.insert(edge);
        }
    }

    std::vector<uint32_t> indices = extractTrianglesFromPlanarGraph(patch, edges);
    if (indices.empty())
    {
        indices = triangulateGreedyPointSet(patch, use3DEdgeLength);
    }

    if (!indices.empty())
    {
        indices = Triangulation::optimizeByMinLengthFlips(patch.projectedVertices, std::move(indices));
    }
    return indices;
}

std::vector<uint32_t> triangulateGreedyPointSet(const ProjectedPatch& patch, bool use3DEdgeLength)
{
    std::unordered_set<EdgeKey, EdgeKeyHash> edges;
    edges.reserve(patch.projectedVertices.size() * 4);

    for (const auto& boundaryEdge : patch.boundaryEdges)
    {
        edges.insert(makeEdgeKey(boundaryEdge.first, boundaryEdge.second));
    }

    struct CandidateEdge
    {
        EdgeKey edge;
        float weight = 0.0f;
    };

    std::vector<CandidateEdge> candidates;
    const uint32_t vertexCount = static_cast<uint32_t>(patch.projectedVertices.size());

    for (uint32_t a = 0; a < vertexCount; ++a)
    {
        for (uint32_t b = a + 1; b < vertexCount; ++b)
        {
            const EdgeKey edge = makeEdgeKey(a, b);
            if (edges.find(edge) != edges.end() || !candidateEdgeStaysInsideBoundary(patch, a, b))
            {
                continue;
            }

            const float weight = use3DEdgeLength ? distanceSquared3(patch.originalVertices[a], patch.originalVertices[b])
                                                 : distanceSquared2(patch.projectedVertices[a].pos, patch.projectedVertices[b].pos);
            candidates.push_back({edge, weight});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const CandidateEdge& a, const CandidateEdge& b)
              { return a.weight < b.weight; });

    for (const CandidateEdge& candidate : candidates)
    {
        if (!edgeIntersectsExistingEdges(patch.projectedVertices, edges, candidate.edge))
        {
            edges.insert(candidate.edge);
        }
    }

    return extractTrianglesFromPlanarGraph(patch, edges);
}

std::vector<uint32_t> triangulatePclGreedyProjection(const ProjectedPatch& patch, const TriangulationOptions& options)
{
    constexpr float kPiLocal = 3.14159265358979323846f;
    const uint32_t vertexCount = static_cast<uint32_t>(patch.projectedVertices.size());
    if (vertexCount < 3)
    {
        return {};
    }

    std::unordered_set<EdgeKey, EdgeKeyHash> edges;
    edges.reserve(vertexCount * 4);
    for (const auto& boundaryEdge : patch.boundaryEdges)
    {
        edges.insert(makeEdgeKey(boundaryEdge.first, boundaryEdge.second));
    }

    struct Neighbor
    {
        uint32_t index = 0;
        float distance = 0.0f;
        float angle = 0.0f;
    };

    struct CandidateEdge
    {
        EdgeKey edge;
        float weight = 0.0f;
    };

    std::vector<CandidateEdge> candidateEdges;
    candidateEdges.reserve(vertexCount * std::max(options.pclMaximumNearestNeighbors, 3u));

    const float minAngle = options.pclMinimumAngleDegrees * kPiLocal / 180.0f;
    const float maxAngle = options.pclMaximumAngleDegrees * kPiLocal / 180.0f;

    for (uint32_t center = 0; center < vertexCount; ++center)
    {
        std::vector<Neighbor> neighbors;
        neighbors.reserve(vertexCount - 1);

        float nearestDistance = std::numeric_limits<float>::max();
        for (uint32_t other = 0; other < vertexCount; ++other)
        {
            if (other == center)
            {
                continue;
            }

            const float distance = edgeWeight(patch, center, other, options.use3DEdgeLengthForGreedy);
            if (distance <= 1e-8f)
            {
                continue;
            }
            nearestDistance = std::min(nearestDistance, distance);

            const Falcor::float2 delta = patch.projectedVertices[other].pos - patch.projectedVertices[center].pos;
            neighbors.push_back({other, distance, std::atan2(delta.y, delta.x)});
        }

        if (neighbors.size() < 2 || nearestDistance == std::numeric_limits<float>::max())
        {
            continue;
        }

        const float adaptiveRadius = nearestDistance * std::max(options.pclMu, 0.01f);
        const float effectiveRadius = options.pclSearchRadius > 0.0f ? std::min(options.pclSearchRadius, adaptiveRadius) : adaptiveRadius;

        neighbors.erase(
            std::remove_if(neighbors.begin(), neighbors.end(), [&](const Neighbor& neighbor) { return neighbor.distance > effectiveRadius; }),
            neighbors.end()
        );
        if (neighbors.size() < 2)
        {
            continue;
        }

        std::sort(neighbors.begin(), neighbors.end(), [](const Neighbor& a, const Neighbor& b) { return a.distance < b.distance; });
        if (neighbors.size() > options.pclMaximumNearestNeighbors)
        {
            neighbors.resize(options.pclMaximumNearestNeighbors);
        }
        std::sort(neighbors.begin(), neighbors.end(), [](const Neighbor& a, const Neighbor& b) { return a.angle < b.angle; });

        for (size_t i = 0; i < neighbors.size(); ++i)
        {
            const Neighbor& left = neighbors[i];
            const Neighbor& right = neighbors[(i + 1) % neighbors.size()];

            float wedgeAngle = right.angle - left.angle;
            if (wedgeAngle < 0.0f)
            {
                wedgeAngle += 2.0f * kPiLocal;
            }
            if (wedgeAngle > maxAngle || wedgeAngle <= 1e-5f)
            {
                continue;
            }

            if (!triangleIsValidEmptyPatchTriangle(patch, center, left.index, right.index))
            {
                continue;
            }

            float triangleMinAngle = 0.0f;
            float triangleMaxAngle = 0.0f;
            triangleAnglesRadians(patch.projectedVertices, center, left.index, right.index, triangleMinAngle, triangleMaxAngle);
            if (triangleMaxAngle > maxAngle + 1e-5f)
            {
                continue;
            }

            const float centerLeft = edgeWeight(patch, center, left.index, options.use3DEdgeLengthForGreedy);
            const float centerRight = edgeWeight(patch, center, right.index, options.use3DEdgeLengthForGreedy);
            const float leftRight = edgeWeight(patch, left.index, right.index, options.use3DEdgeLengthForGreedy);
            if (options.pclSearchRadius > 0.0f && (centerLeft > options.pclSearchRadius || centerRight > options.pclSearchRadius || leftRight > options.pclSearchRadius))
            {
                continue;
            }

            const float anglePenalty = triangleMinAngle < minAngle ? (minAngle - triangleMinAngle) * effectiveRadius : 0.0f;
            const float triangleWeight = centerLeft + centerRight + leftRight + anglePenalty;

            candidateEdges.push_back({makeEdgeKey(center, left.index), triangleWeight + centerLeft});
            candidateEdges.push_back({makeEdgeKey(center, right.index), triangleWeight + centerRight});
            candidateEdges.push_back({makeEdgeKey(left.index, right.index), triangleWeight + leftRight});
        }
    }

    std::sort(candidateEdges.begin(), candidateEdges.end(), [](const CandidateEdge& a, const CandidateEdge& b)
              { return a.weight < b.weight; });

    for (const CandidateEdge& candidate : candidateEdges)
    {
        if (edges.find(candidate.edge) != edges.end())
        {
            continue;
        }
        if (!candidateEdgeStaysInsideBoundary(patch, candidate.edge.a, candidate.edge.b))
        {
            continue;
        }
        if (!edgeIntersectsExistingEdges(patch.projectedVertices, edges, candidate.edge))
        {
            edges.insert(candidate.edge);
        }
    }

    std::vector<uint32_t> indices = extractTrianglesFromPlanarGraph(patch, edges);
    if (indices.empty())
    {
        indices = triangulateGreedyPointSet(patch, options.use3DEdgeLengthForGreedy);
    }
    return indices;
}

PatchTriangulationResult makePatchResultFromLocalIndices(
    const ProjectedPatch& patch,
    const std::vector<Vertex>& projectedVertices,
    std::vector<uint32_t> indices,
    const ProjectionFrame& frame
)
{
    PatchTriangulationResult result;
    result.indices = std::move(indices);
    result.vertices.reserve(projectedVertices.size());
    result.sourceVertexIndices.reserve(projectedVertices.size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(projectedVertices.size()); ++i)
    {
        if (i < patch.localToGlobal.size())
        {
            result.vertices.push_back(patch.originalVertices[i]);
            result.sourceVertexIndices.push_back(patch.localToGlobal[i]);
        }
        else
        {
            result.vertices.push_back(unprojectFromFrame(projectedVertices[i].pos, frame));
            result.sourceVertexIndices.push_back(kInvalidIndex);
        }
    }

    result.success = !result.indices.empty() && result.indices.size() % 3 == 0;
    result.message = result.success ? "Patch triangulation succeeded." : "Patch triangulation produced no triangles.";
    return result;
}

PatchTriangulationResult triangulatePatchWithTriangle(
    const ProjectedPatch& patch,
    const ProjectionFrame& frame,
    bool applyFlips
)
{
    PatchTriangulationResult result;
    const int vertexCount = static_cast<int>(patch.projectedVertices.size());
    const int edgeCount = static_cast<int>(patch.boundaryEdges.size());

    Eigen::Matrix<double, Eigen::Dynamic, 2> inputVertices(vertexCount, 2);
    for (int i = 0; i < vertexCount; ++i)
    {
        inputVertices(i, 0) = static_cast<double>(patch.projectedVertices[i].pos.x);
        inputVertices(i, 1) = static_cast<double>(patch.projectedVertices[i].pos.y);
    }

    Eigen::Matrix<int, Eigen::Dynamic, 2> boundaryEdges(edgeCount, 2);
    for (int i = 0; i < edgeCount; ++i)
    {
        boundaryEdges(i, 0) = static_cast<int>(patch.boundaryEdges[i].first);
        boundaryEdges(i, 1) = static_cast<int>(patch.boundaryEdges[i].second);
    }

    Eigen::Matrix<double, Eigen::Dynamic, 2> holes(0, 2);
    Eigen::Matrix<double, Eigen::Dynamic, 2> outputVertices;
    Eigen::Matrix<int, Eigen::Dynamic, 3> outputFaces;

    igl::triangle::triangulate(inputVertices, boundaryEdges, holes, "pQz", outputVertices, outputFaces);

    if (outputFaces.rows() == 0)
    {
        result.message = "Triangle CDT returned no faces.";
        return result;
    }

    std::vector<Vertex> projectedOutputVertices;
    projectedOutputVertices.reserve(static_cast<size_t>(outputVertices.rows()));
    for (int i = 0; i < outputVertices.rows(); ++i)
    {
        Vertex vertex;
        vertex.pos = Falcor::float2(static_cast<float>(outputVertices(i, 0)), static_cast<float>(outputVertices(i, 1)));
        projectedOutputVertices.push_back(vertex);
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(outputFaces.rows()) * 3);
    for (int faceIndex = 0; faceIndex < outputFaces.rows(); ++faceIndex)
    {
        const int a = outputFaces(faceIndex, 0);
        const int b = outputFaces(faceIndex, 1);
        const int c = outputFaces(faceIndex, 2);
        if (a < 0 || b < 0 || c < 0 || a >= outputVertices.rows() || b >= outputVertices.rows() || c >= outputVertices.rows())
        {
            continue;
        }
        appendTriangleCCW(indices, projectedOutputVertices, static_cast<uint32_t>(a), static_cast<uint32_t>(b), static_cast<uint32_t>(c));
    }

    if (applyFlips)
    {
        indices = Triangulation::optimizeByMinLengthFlips(projectedOutputVertices, std::move(indices));
    }

    return makePatchResultFromLocalIndices(patch, projectedOutputVertices, std::move(indices), frame);
}

PatchTriangulationResult triangulatePatchGreedy(const ProjectedPatch& patch, const ProjectionFrame& frame, bool use3DEdgeLength)
{
    std::vector<uint32_t> indices = triangulateGreedyPointSet(patch, use3DEdgeLength);
    return makePatchResultFromLocalIndices(patch, patch.projectedVertices, std::move(indices), frame);
}

PatchTriangulationResult triangulatePatchLmtMinimumWeight(const ProjectedPatch& patch, const ProjectionFrame& frame, bool use3DEdgeLength)
{
    std::vector<uint32_t> indices = triangulateLmtMinimumWeight(patch, use3DEdgeLength);
    return makePatchResultFromLocalIndices(patch, patch.projectedVertices, std::move(indices), frame);
}

PatchTriangulationResult triangulatePatchPclGreedyProjection(
    const ProjectedPatch& patch,
    const ProjectionFrame& frame,
    const TriangulationOptions& options
)
{
    std::vector<uint32_t> indices = triangulatePclGreedyProjection(patch, options);
    return makePatchResultFromLocalIndices(patch, patch.projectedVertices, std::move(indices), frame);
}

} // namespace

const char* getMethodName(Method method)
{
    switch (method)
    {
    case Method::ConstrainedDelaunay:
        return "CDT";
    case Method::ConstrainedDelaunayFlipped:
        return "CDT + Flips";
    case Method::GreedyPointSet:
        return "Greedy Point Set";
    case Method::LmtMinimumWeight:
        return "LMT MWT";
    case Method::PclGreedyProjection:
        return "PCL Greedy Projection";
    }

    return "Unknown";
}

std::vector<Patch> growPatches(const Mesh& mesh, const PatchGrowthOptions& options)
{
    std::vector<Patch> patches;
    if (mesh.vertices.empty() || mesh.faces.empty())
    {
        return patches;
    }

    const std::vector<std::vector<uint32_t>> adjacency = buildFaceAdjacency(mesh);
    std::vector<bool> assigned(mesh.faces.size(), false);

    for (uint32_t seedFace = 0; seedFace < static_cast<uint32_t>(mesh.faces.size()); ++seedFace)
    {
        if (assigned[seedFace] || !isFiniteFace(mesh, mesh.faces[seedFace]))
        {
            continue;
        }

        std::vector<uint32_t> patchFaces = {seedFace};
        assigned[seedFace] = true;

        std::queue<uint32_t> candidates;
        for (uint32_t neighbor : adjacency[seedFace])
        {
            candidates.push(neighbor);
        }

        while (!candidates.empty())
        {
            const uint32_t candidateFace = candidates.front();
            candidates.pop();

            if (candidateFace >= mesh.faces.size() || assigned[candidateFace] || !isFiniteFace(mesh, mesh.faces[candidateFace]))
            {
                continue;
            }
            if (patchFaces.size() >= options.maxFacesPerPatch)
            {
                continue;
            }

            std::vector<uint32_t> proposedFaces = patchFaces;
            proposedFaces.push_back(candidateFace);
            const PatchStats stats = computePatchStats(mesh, proposedFaces, options);

            if (!stats.valid)
            {
                continue;
            }

            patchFaces.push_back(candidateFace);
            assigned[candidateFace] = true;

            for (uint32_t neighbor : adjacency[candidateFace])
            {
                if (!assigned[neighbor])
                {
                    candidates.push(neighbor);
                }
            }
        }

        patches.push_back(makePatch(mesh, patchFaces, options));
    }

    return patches;
}

PatchTriangulationResult triangulatePatch(const Mesh& mesh, const Patch& patch, const TriangulationOptions& options)
{
    PatchTriangulationResult result;

    if (!patch.isDiskLike)
    {
        result.message = "Patch triangulation skipped because the boundary is not a single disk-like loop.";
        return result;
    }
    if (patch.vertexIndices.size() < 3)
    {
        result.message = "Patch triangulation skipped because the patch has fewer than three vertices.";
        return result;
    }

    const ProjectedPatch projectedPatch = projectPatch(mesh, patch);

    switch (options.method)
    {
    case Method::ConstrainedDelaunay:
        return triangulatePatchWithTriangle(projectedPatch, patch.frame, false);
    case Method::ConstrainedDelaunayFlipped:
        return triangulatePatchWithTriangle(projectedPatch, patch.frame, true);
    case Method::GreedyPointSet:
        return triangulatePatchGreedy(projectedPatch, patch.frame, options.use3DEdgeLengthForGreedy);
    case Method::LmtMinimumWeight:
        return triangulatePatchLmtMinimumWeight(projectedPatch, patch.frame, options.use3DEdgeLengthForGreedy);
    case Method::PclGreedyProjection:
        return triangulatePatchPclGreedyProjection(projectedPatch, patch.frame, options);
    }

    result.message = "Unknown triangulation method.";
    return result;
}

MeshTriangulationResult triangulateMesh(
    const Mesh& mesh,
    const PatchGrowthOptions& patchOptions,
    const TriangulationOptions& triangulationOptions
)
{
    MeshTriangulationResult result;
    result.vertices = mesh.vertices;
    result.patches = growPatches(mesh, patchOptions);

    for (size_t patchIndex = 0; patchIndex < result.patches.size(); ++patchIndex)
    {
        const PatchTriangulationResult patchResult = triangulatePatch(mesh, result.patches[patchIndex], triangulationOptions);
        if (!patchResult.success)
        {
            result.messages.push_back("Patch " + std::to_string(patchIndex) + ": " + patchResult.message);
            continue;
        }

        std::vector<uint32_t> localToOutput(patchResult.vertices.size(), kInvalidIndex);
        for (uint32_t localIndex = 0; localIndex < static_cast<uint32_t>(patchResult.vertices.size()); ++localIndex)
        {
            if (patchResult.sourceVertexIndices[localIndex] != kInvalidIndex)
            {
                localToOutput[localIndex] = patchResult.sourceVertexIndices[localIndex];
            }
            else
            {
                localToOutput[localIndex] = static_cast<uint32_t>(result.vertices.size());
                result.vertices.push_back(patchResult.vertices[localIndex]);
            }
        }

        for (uint32_t localIndex : patchResult.indices)
        {
            if (localIndex < localToOutput.size() && localToOutput[localIndex] != kInvalidIndex)
            {
                result.indices.push_back(localToOutput[localIndex]);
            }
        }
    }

    result.success = !result.indices.empty();
    return result;
}

MeshTriangulationResult useSourceMeshTriangulation(const Mesh& mesh)
{
    MeshTriangulationResult result;
    result.vertices = mesh.vertices;

    for (size_t faceIndex = 0; faceIndex < mesh.faces.size(); ++faceIndex)
    {
        const std::vector<uint32_t>& face = mesh.faces[faceIndex];
        if (face.size() < 3)
        {
            result.messages.push_back("Face " + std::to_string(faceIndex) + " has fewer than three vertices.");
            continue;
        }

        bool validFace = true;
        for (uint32_t vertexIndex : face)
        {
            if (vertexIndex >= mesh.vertices.size())
            {
                validFace = false;
                break;
            }
        }
        if (!validFace)
        {
            result.messages.push_back("Face " + std::to_string(faceIndex) + " references an invalid vertex.");
            continue;
        }

        for (size_t i = 1; i + 1 < face.size(); ++i)
        {
            result.indices.push_back(face[0]);
            result.indices.push_back(face[i]);
            result.indices.push_back(face[i + 1]);
        }

        if (face.size() > 3)
        {
            result.messages.push_back("Face " + std::to_string(faceIndex) + " was fan-triangulated from an OBJ polygon.");
        }
    }

    result.success = !result.indices.empty();
    return result;
}

} // namespace Triangulation3D
