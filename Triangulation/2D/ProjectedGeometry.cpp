#include "Triangulation/2D/ProjectedGeometry.h"
#include <algorithm>
#include <cmath>
namespace ProjectedTriangulation::Detail
{
Falcor::float3 unprojectFromFrame(const Falcor::float2& point, const ProjectionFrame& frame)
{
    return add3(frame.origin, add3(multiply3(frame.tangent, point.x), multiply3(frame.bitangent, point.y)));
}

float cross2(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointOnSegment(const Falcor::float2& point, const Falcor::float2& a, const Falcor::float2& b, float eps)
{
    if (std::abs(cross2(a, b, point)) > eps)
    {
        return false;
    }
    return point.x >= std::min(a.x, b.x) - eps && point.x <= std::max(a.x, b.x) + eps && point.y >= std::min(a.y, b.y) - eps &&
           point.y <= std::max(a.y, b.y) + eps;
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

    return ((o1 > 0.0f && o2 < 0.0f) || (o1 < 0.0f && o2 > 0.0f)) && ((o3 > 0.0f && o4 < 0.0f) || (o3 < 0.0f && o4 > 0.0f));
}

bool segmentsIntersectOrTouch(const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c, const Falcor::float2& d)
{
    constexpr float eps = 1e-6f;
    const float o1 = cross2(a, b, c);
    const float o2 = cross2(a, b, d);
    const float o3 = cross2(c, d, a);
    const float o4 = cross2(c, d, b);

    if (((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps)) && ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps)))
    {
        return true;
    }

    return (std::abs(o1) <= eps && pointOnSegment(c, a, b, eps)) || (std::abs(o2) <= eps && pointOnSegment(d, a, b, eps)) ||
           (std::abs(o3) <= eps && pointOnSegment(a, c, d, eps)) || (std::abs(o4) <= eps && pointOnSegment(b, c, d, eps));
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

bool candidateEdgeStaysInsideBoundaryUncached(const ProjectedPatch& patch, uint32_t a, uint32_t b)
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

bool candidateEdgeStaysInsideBoundary(const ProjectedPatch& patch, uint32_t a, uint32_t b)
{
    if (patch.visibility.empty())
        return candidateEdgeStaysInsideBoundaryUncached(patch, a, b);
    auto& result = patch.visibility[size_t(std::min(a, b)) * patch.projectedVertices.size() + std::max(a, b)];
    if (result < 0)
        result = candidateEdgeStaysInsideBoundaryUncached(patch, a, b) ? 1 : 0;
    return result != 0;
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

bool pointStrictlyInsideTriangle(const Falcor::float2& point, const Falcor::float2& a, const Falcor::float2& b, const Falcor::float2& c)
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
        const auto begin = indices.size();
        appendTriangleCCW(indices, vertices, face[0], face[1], face[2]);
        return indices.size() == begin + 3;
    }

    std::vector<uint32_t> polygon = face;
    if (polygonSignedArea2(vertices, polygon) < 0.0f)
    {
        std::reverse(polygon.begin(), polygon.end());
    }

    const size_t begin = indices.size();
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

            clippedEar = true;
            break;
        }

        if (!clippedEar)
        {
            indices.resize(begin);
            return false;
        }
    }

    appendTriangleCCW(indices, vertices, polygon[0], polygon[1], polygon[2]);
    if (indices.size() - begin != (face.size() - 2) * 3)
    {
        indices.resize(begin);
        return false;
    }
    return true;
}

} // namespace ProjectedTriangulation::Detail
