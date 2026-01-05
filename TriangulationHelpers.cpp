#include "TriangulationHelpers.h"
#include "Falcor.h"

#include <include/mapbox/earcut.hpp>
#include <Eigen/Dense>
#include <igl/triangle/triangulate.h>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>

using namespace Falcor;

namespace Triangulation
{

// MARK: - Helpers Namespace

namespace Helpers
{

struct DiagonalTable
{
    std::vector<std::vector<bool>> isDiagonal; // [n][n]
    bool polygonIsCCW = true;
};

inline double Cross2D(const float2& p1, const float2& p2, const float2& p3)
{
    return static_cast<double>((p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x));
}

inline double EdgeLength(const Vertex& vertexA, const Vertex& vertexB)
{
    float2 diff = vertexB.pos - vertexA.pos;
    return std::sqrt(static_cast<double>(dot(diff, diff)));
}

inline double TriangleArea(const std::vector<Vertex>& vertices, int i, int j, int k)
{
    const float2 ab = vertices[j].pos - vertices[i].pos;
    const float2 ac = vertices[k].pos - vertices[i].pos;
    return std::abs(static_cast<double>(ab.x * ac.y - ab.y * ac.x)) * 0.5;
}

inline double PolygonSignedArea(const std::vector<Vertex>& vertices)
{
    const size_t vertexCount = vertices.size();
    double signedAreaSum = 0.0;

    for (size_t i = 0; i < vertexCount; ++i)
    {
        const auto& current = vertices[i].pos;
        const auto& next = vertices[(i + 1) % vertexCount].pos;
        signedAreaSum += static_cast<double>(current.x) * static_cast<double>(next.y) - static_cast<double>(current.y) * static_cast<double>(next.x);
    }

    return 0.5 * signedAreaSum;
}

bool PointInsidePolygon(const float2& p, const std::vector<Vertex>& vertices)
{
    const size_t n = vertices.size();
    if (n < 3)
    {
        return false;
    }

    int crossings = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const auto& a = vertices[i].pos;
        const auto& b = vertices[(i + 1) % n].pos;
        if ((a.y <= p.y && b.y > p.y) || (b.y <= p.y && a.y > p.y))
        {
            float t = (p.y - a.y) / (b.y - a.y);
            if (p.x < a.x + t * (b.x - a.x))
            {
                crossings++;
            }
        }
    }
    return (crossings % 2) == 1;
}

bool SegmentsIntersect(const float2& p1, const float2& p2, const float2& q1, const float2& q2)
{
    const double d1 = Cross2D(p1, p2, q1);
    const double d2 = Cross2D(p1, p2, q2);
    const double d3 = Cross2D(q1, q2, p1);
    const double d4 = Cross2D(q1, q2, p2);

    // Segments intersect if points are on opposite sides
    if ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0))
    {
        if ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))
        {
            return true;
        }
    }
    return false;
}

bool IsAdjacent(int vertexA, int vertexB, size_t vertexCount)
{
    return (vertexA + 1) % vertexCount == vertexB || (vertexB + 1) % vertexCount == vertexA;
}

DiagonalTable BuildDiagonalTable(const std::vector<Vertex>& poly)
{
    DiagonalTable out;
    const size_t n = poly.size();
    out.isDiagonal.assign(n, std::vector<bool>(n, false));
    out.polygonIsCCW = PolygonSignedArea(poly) > 0.0;

    auto isValidDiagonal = [&](int i, int j)
    {
        if (i == j || IsAdjacent(i, j, n))
        {
            return false;
        }

        const float2& a = poly[i].pos;
        const float2& b = poly[j].pos;

        // Midpoint must be inside polygon
        float2 mid = 0.5f * (a + b);
        if (!PointInsidePolygon(mid, poly))
        {
            return false;
        }

        // Must not properly intersect any polygon edge (except shared endpoints)
        for (int v = 0; v < static_cast<int>(n); ++v)
        {
            const int vNext = (v + 1) % static_cast<int>(n);
            // skip edges incident to i or j
            if (v == i || vNext == i || v == j || vNext == j)
            {
                continue;
            }

            const float2& c = poly[v].pos;
            const float2& d = poly[vNext].pos;

            if (SegmentsIntersect(a, b, c, d))
            {
                return false;
            }
        }

        return true;
    };

    for (int i = 0; i < static_cast<int>(n); ++i)
    {
        for (int j = i + 1; j < static_cast<int>(n); ++j)
        {
            if (isValidDiagonal(i, j))
            {
                out.isDiagonal[i][j] = out.isDiagonal[j][i] = true;
            }
        }
    }

    return out;
}

bool IsTriangleInsidePolygon(const std::vector<Vertex>& vertices, int i, int j, int k, const DiagonalTable& diagTable)
{
    // Check if all edges of the triangle are either polygon edges or valid diagonals
    const int n = static_cast<int>(vertices.size());
    
    auto isEdgeOrDiagonal = [&](int a, int b) -> bool
    {
        if (IsAdjacent(a, b, n))
            return true;
        if (a < b)
            return diagTable.isDiagonal[a][b];
        else
            return diagTable.isDiagonal[b][a];
    };

    if (!isEdgeOrDiagonal(i, j) || !isEdgeOrDiagonal(j, k) || !isEdgeOrDiagonal(k, i))
    {
        return false;
    }

    // Check triangle center is inside polygon
    const float2& pA = vertices[i].pos;
    const float2& pB = vertices[j].pos;
    const float2& pC = vertices[k].pos;
    float2 center = {(pA.x + pB.x + pC.x) / 3.0f, (pA.y + pB.y + pC.y) / 3.0f};
    if (!PointInsidePolygon(center, vertices))
    {
        return false;
    }

    return true;
}

std::vector<uint32_t> BuildCCWOrder(const std::vector<Vertex>& vertices)
{
    const size_t vertexCount = vertices.size();
    std::vector<uint32_t> order(vertexCount);

    const bool isAlreadyCCW = PolygonSignedArea(vertices) >= 0.0;

    for (size_t i = 0; i < vertexCount; ++i)
    {
        order[i] = isAlreadyCCW ? static_cast<uint32_t>(i) : static_cast<uint32_t>(vertexCount - 1 - i);
    }

    return order;
}

} // namespace Helpers

// MARK: - Geometry Helper Functions

namespace
{

inline double edgeLength(const Vertex& vertexA, const Vertex& vertexB)
{
    float2 diff = vertexB.pos - vertexA.pos;
    return static_cast<double>(dot(diff, diff));
}

inline double trianglePerimeter(const std::vector<Vertex>& vertices, uint32_t indexA, uint32_t indexB, uint32_t indexC)
{
    return edgeLength(vertices[indexA], vertices[indexB]) + edgeLength(vertices[indexB], vertices[indexC]) +
           edgeLength(vertices[indexC], vertices[indexA]);
}

inline double triangleArea(const std::vector<Vertex>& vertices, uint32_t indexA, uint32_t indexB, uint32_t indexC)
{
    const float2 ab = vertices[indexB].pos - vertices[indexA].pos;
    const float2 ac = vertices[indexC].pos - vertices[indexA].pos;

    // Cross product's z-component
    return std::abs(static_cast<double>(ab.x * ac.y - ab.y * ac.x)) * 0.5;
}

inline double polygonSignedArea(const std::vector<Vertex>& vertices)
{
    const size_t vertexCount = vertices.size();
    double signedAreaSum = 0.0;

    for (size_t i = 0; i < vertexCount; ++i)
    {
        const auto& current = vertices[i].pos;
        const auto& next = vertices[(i + 1) % vertexCount].pos;
        signedAreaSum +=
            static_cast<double>(current.x) * static_cast<double>(next.y) - static_cast<double>(current.y) * static_cast<double>(next.x);
    }

    return 0.5 * signedAreaSum; // positive = CCW, negative = CW
}

// Check if triangle (A, B, C) has counter-clockwise orientation
inline bool isCounterClockwise(const std::vector<Vertex>& vertices, uint32_t indexA, uint32_t indexB, uint32_t indexC)
{
    const auto& posA = vertices[indexA].pos;
    const auto& posB = vertices[indexB].pos;
    const auto& posC = vertices[indexC].pos;

    const float crossProduct = (posB.x - posA.x) * (posC.y - posA.y) - (posB.y - posA.y) * (posC.x - posA.x);
    return crossProduct > 0.0;
}

// Signed cross product of vectors (p1-p0) and (p2-p0)
inline float cross2D(const float2& p0, const float2& p1, const float2& p2)
{
    return (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
}

// Check if point P is strictly inside triangle ABC (not on edges)
inline bool pointInTriangle(const float2& p, const float2& a, const float2& b, const float2& c)
{
    const float d1 = cross2D(p, a, b);
    const float d2 = cross2D(p, b, c);
    const float d3 = cross2D(p, c, a);

    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(hasNeg && hasPos);
}

// Ray casting point-in-polygon test
inline bool pointInsidePolygon(const float2& p, const std::vector<Vertex>& vertices)
{
    const size_t n = vertices.size();
    if (n < 3)
        return false;

    int crossings = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const auto& a = vertices[i].pos;
        const auto& b = vertices[(i + 1) % n].pos;

        if ((a.y <= p.y && b.y > p.y) || (b.y <= p.y && a.y > p.y))
        {
            float t = (p.y - a.y) / (b.y - a.y);
            if (p.x < a.x + t * (b.x - a.x))
            {
                crossings++;
            }
        }
    }
    return (crossings % 2) == 1;
}

// Check if two line segments intersect (excluding endpoints)
inline bool segmentsIntersect(const float2& p1, const float2& p2, const float2& q1, const float2& q2)
{
    const float d1 = cross2D(p1, p2, q1);
    const float d2 = cross2D(p1, p2, q2);
    const float d3 = cross2D(q1, q2, p1);
    const float d4 = cross2D(q1, q2, p2);

    // Segments intersect if points are on opposite sides
    if ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0))
    {
        if ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))
        {
            return true;
        }
    }
    return false;
}

// Simplified: Check if triangle is inside polygon (for concave polygons)
// Assumes triangle vertices are on polygon boundary
// Based on computational geometry best practices: check center + vertex containment + edge validity
inline bool isTriangleInsidePolygon(const std::vector<Vertex>& vertices, int i, int j, int k)
{
    const auto& pA = vertices[i].pos;
    const auto& pB = vertices[j].pos;
    const auto& pC = vertices[k].pos;

    // Check triangle center is inside polygon (single point-in-polygon test)
    float2 center = {(pA.x + pB.x + pC.x) / 3.0f, (pA.y + pB.y + pC.y) / 3.0f};
    if (!pointInsidePolygon(center, vertices))
    {
        return false; // Triangle center outside = triangle outside
    }

    // Check no other boundary vertices inside triangle
    const int n = static_cast<int>(vertices.size());
    for (int v = 0; v < n; ++v)
    {
        if (v == i || v == j || v == k)
            continue;
        if (pointInTriangle(vertices[v].pos, pA, pB, pC))
        {
            return false; // Another vertex inside = invalid
        }
    }

    // Check that triangle edges don't cross polygon boundary
    // Only check edges that are not consecutive (diagonals)
    auto isConsecutive = [n](int a, int b) -> bool { return (a + 1) % n == b || (b + 1) % n == a; };

    // Check edge AB
    if (!isConsecutive(i, j))
    {
        for (int e = 0; e < n; ++e)
        {
            int nextE = (e + 1) % n;
            // Skip if this edge is one of our triangle edges
            if ((e == i && nextE == j) || (e == j && nextE == i))
                continue;
            if ((e == j && nextE == k) || (e == k && nextE == j))
                continue;
            if ((e == k && nextE == i) || (e == i && nextE == k))
                continue;

            if (segmentsIntersect(pA, pB, vertices[e].pos, vertices[nextE].pos))
            {
                return false; // Edge AB crosses polygon boundary
            }
        }
    }

    // Check edge BC
    if (!isConsecutive(j, k))
    {
        for (int e = 0; e < n; ++e)
        {
            int nextE = (e + 1) % n;
            if ((e == i && nextE == j) || (e == j && nextE == i))
                continue;
            if ((e == j && nextE == k) || (e == k && nextE == j))
                continue;
            if ((e == k && nextE == i) || (e == i && nextE == k))
                continue;

            if (segmentsIntersect(pB, pC, vertices[e].pos, vertices[nextE].pos))
            {
                return false; // Edge BC crosses polygon boundary
            }
        }
    }

    // Check edge CA
    if (!isConsecutive(k, i))
    {
        for (int e = 0; e < n; ++e)
        {
            int nextE = (e + 1) % n;
            if ((e == i && nextE == j) || (e == j && nextE == i))
                continue;
            if ((e == j && nextE == k) || (e == k && nextE == j))
                continue;
            if ((e == k && nextE == i) || (e == i && nextE == k))
                continue;

            if (segmentsIntersect(pC, pA, vertices[e].pos, vertices[nextE].pos))
            {
                return false; // Edge CA crosses polygon boundary
            }
        }
    }

    return true;
}

// Check if an ear (formed by polygon[prev], polygon[curr], polygon[next]) is valid
// An ear is valid if:
// 1. It has correct winding (convex at curr vertex for CCW polygon)
// 2. No other polygon vertices are inside the triangle
// 3. The diagonal doesn't intersect any polygon edges
inline bool isValidEar(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& polygon,
    size_t prevIdx,
    size_t currIdx,
    size_t nextIdx,
    bool polygonIsCCW
)
{
    const size_t n = polygon.size();
    if (n < 3)
        return false;

    const auto& pPrev = vertices[polygon[prevIdx]].pos;
    const auto& pCurr = vertices[polygon[currIdx]].pos;
    const auto& pNext = vertices[polygon[nextIdx]].pos;

    // Check winding: for CCW polygon, ear must be CCW (convex vertex)
    const double cross = cross2D(pPrev, pCurr, pNext);
    if (polygonIsCCW)
    {
        if (cross <= 0)
            return false; // Reflex vertex, not an ear
    }
    else
    {
        if (cross >= 0)
            return false; // For CW polygon, ear must be CW
    }

    // Check that no other polygon vertices are inside this triangle
    for (size_t i = 0; i < n; ++i)
    {
        if (i == prevIdx || i == currIdx || i == nextIdx)
            continue;

        const auto& testPoint = vertices[polygon[i]].pos;
        if (pointInTriangle(testPoint, pPrev, pCurr, pNext))
        {
            return false;
        }
    }

    return true;
}

// Build vertex ordering for CCW traversal (reverses if input is CW)
inline std::vector<uint32_t> buildCCWOrder(const std::vector<Vertex>& vertices)
{
    const size_t vertexCount = vertices.size();
    std::vector<uint32_t> order(vertexCount);

    const bool isAlreadyCCW = polygonSignedArea(vertices) >= 0.0;

    for (size_t i = 0; i < vertexCount; ++i)
    {
        order[i] = isAlreadyCCW ? static_cast<uint32_t>(i) : static_cast<uint32_t>(vertexCount - 1 - i);
    }

    return order;
}

} // anonymous namespace

// MARK: - Edge Length Calculation

double calculateTotalEdgeLength(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    double total = 0.0;
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        total += trianglePerimeter(vertices, indices[i], indices[i + 1], indices[i + 2]);
    }
    return total;
}

// MARK: - Triangulation Implementations

std::vector<uint32_t> earClippingTriangulation(const std::vector<Vertex>& vertices)
{
    std::vector<uint32_t> indices;
    const size_t n = vertices.size();

    if (n < 3)
        return indices;

    const bool isCCW = polygonSignedArea(vertices) >= 0.0;

    // Working list of remaining vertex indices
    std::vector<uint32_t> polygon(n);
    std::iota(polygon.begin(), polygon.end(), 0);

    indices.reserve((n - 2) * 3);

    // Clip ears until only 3 vertices remain
    while (polygon.size() > 3)
    {
        const size_t size = polygon.size();
        bool earFound = false;

        for (size_t i = 0; i < size; ++i)
        {
            const size_t prev = (i + size - 1) % size;
            const size_t next = (i + 1) % size;

            if (isValidEar(vertices, polygon, prev, i, next, isCCW))
            {
                // Emit triangle
                indices.push_back(polygon[prev]);
                indices.push_back(polygon[i]);
                indices.push_back(polygon[next]);

                // Remove the ear vertex
                polygon.erase(polygon.begin() + static_cast<ptrdiff_t>(i));
                earFound = true;
                break;
            }
        }

        if (!earFound)
            break; // No valid ear found, polygon may be degenerate
    }

    // Add final triangle
    if (polygon.size() == 3)
    {
        indices.push_back(polygon[0]);
        indices.push_back(polygon[1]);
        indices.push_back(polygon[2]);
    }

    return indices;
}

std::vector<uint32_t> minimumWeightTriangulation(const std::vector<Vertex>& vertices, bool shouldHandleConcave)
{
    std::vector<uint32_t> indices;
    const int vertexCount = static_cast<int>(vertices.size());

    if (vertexCount < 3)
    {
        return indices;
    }

    Helpers::DiagonalTable diagTable;
    if (shouldHandleConcave)
    {
        diagTable = Helpers::BuildDiagonalTable(vertices);
    }

    // DP tables:
    // dp[i][j] = minimum edge weight to triangulate polygon from i to j
    // split[i][j] = optimal split point k for the interval [i, j]
    std::vector<double> dpTable(vertexCount * vertexCount, 0.0);
    std::vector<int> splitTable(vertexCount * vertexCount, -1);

    auto dp = [&](int i, int j) -> double&
    {
        return dpTable[i * vertexCount + j];
    };

    auto split = [&](int i, int j) -> int&
    {
        return splitTable[i * vertexCount + j];
    };

    // Initialize: adjacent vertices need no triangulation
    for (int i = 0; i < vertexCount - 1; ++i)
    {
        dp(i, i + 1) = 0.0;
    }

    // Fill DP table for increasing chain lengths
    for (int chainLength = 2; chainLength <= vertexCount - 1; ++chainLength)
    {
        for (int startIndex = 0; startIndex + chainLength < vertexCount; ++startIndex)
        {
            const int endIndex = startIndex + chainLength;

            double minimumCost = std::numeric_limits<double>::infinity();
            int optimalSplit = -1;

            for (int splitPoint = startIndex + 1; splitPoint < endIndex; ++splitPoint)
            {
                if (shouldHandleConcave)
                {
                    if (!Helpers::IsTriangleInsidePolygon(vertices, startIndex, splitPoint, endIndex, diagTable))
                    {
                        continue;
                    }
                }
                else
                {
                    const double area = Helpers::TriangleArea(vertices, startIndex, splitPoint, endIndex);
                    if (area <= 0.0)
                    {
                        continue;
                    }
                }

                // Cost = left subproblem + right subproblem + new internal edges
                const double internalEdgeCost = Helpers::EdgeLength(vertices[startIndex], vertices[splitPoint])
                               + Helpers::EdgeLength(vertices[splitPoint], vertices[endIndex]);
                const double totalCost = dp(startIndex, splitPoint)
                            + dp(splitPoint, endIndex)
                            + internalEdgeCost;

                if (totalCost < minimumCost)
                {
                    minimumCost = totalCost;
                    optimalSplit = splitPoint;
                }
            }

            dp(startIndex, endIndex) = minimumCost;
            split(startIndex, endIndex) = optimalSplit;
        }
    }

    // Reconstruct triangles via recursive traversal
    indices.reserve(3 * (vertexCount - 2));
    std::function<void(int, int)> emitTriangles = [&](int startIndex, int endIndex)
    {
        const int splitPoint = split(startIndex, endIndex);
        if (splitPoint < 0)
        {
            // No split point found
            return;
        }

        indices.push_back(static_cast<uint32_t>(startIndex));
        indices.push_back(static_cast<uint32_t>(splitPoint));
        indices.push_back(static_cast<uint32_t>(endIndex));

        // Recurse on sub-chains
        if (splitPoint > startIndex + 1)
        {
            emitTriangles(startIndex, splitPoint);
        }
        if (endIndex > splitPoint + 1)
        {
            emitTriangles(splitPoint, endIndex);
        }
    };

    // For closed polygons, we need to triangulate the full chain [0, vertexCount-1]
    emitTriangles(0, vertexCount - 1);

    const size_t expectedTriangles = vertexCount - 2;
    if (indices.size() / 3 < expectedTriangles)
    {
        static_assert(true, "Triangulation not sussecful");
        return {};
    }

    return indices;
}

std::vector<uint32_t> centroidFanTriangulation(std::vector<Vertex>& vertices)
{
    std::vector<uint32_t> indices;
    const size_t originalVertexCount = vertices.size();

    if (originalVertexCount < 3)
    {
        return indices;
    }

    // Compute centroid by averaging all vertex positions
    float2 centroid{0.0f, 0.0f};
    for (const auto& vertex : vertices)
    {
        centroid.x += vertex.pos.x;
        centroid.y += vertex.pos.y;
    }
    centroid.x /= static_cast<float>(originalVertexCount);
    centroid.y /= static_cast<float>(originalVertexCount);

    // Append centroid as new vertex
    Vertex centroidVertex;
    centroidVertex.pos = centroid;
    vertices.push_back(centroidVertex);
    const uint32_t centroidIndex = static_cast<uint32_t>(vertices.size() - 1);

    // Create fan triangles connecting each edge to the centroid
    indices.reserve(originalVertexCount * 3);

    for (uint32_t i = 0; i < originalVertexCount; ++i)
    {
        const uint32_t nextIndex = (i + 1) % static_cast<uint32_t>(originalVertexCount);

        indices.push_back(i);
        indices.push_back(nextIndex);
        indices.push_back(centroidIndex);
    }

    return indices;
}

std::vector<uint32_t> greedyMaxAreaTriangulation(const std::vector<Vertex>& vertices, bool shouldHandleConcave)
{
    std::vector<uint32_t> indices;
    const size_t vertexCount = vertices.size();

    const auto diagTable = Helpers::BuildDiagonalTable(vertices);
    if (vertexCount < 3)
    {
        return indices;
    }

    // For convex polygons, skip CCW order rebuild
    std::vector<uint32_t> ccwOrder;

    if (shouldHandleConcave)
    {
        ccwOrder = Helpers::BuildCCWOrder(vertices);
    }
    else
    {
        ccwOrder.resize(vertexCount);
        std::iota(ccwOrder.begin(), ccwOrder.end(), 0);
    }

    // Recursive solver: triangulates a sub-polygon by selecting the largest triangle
    // polygon is indices into ccwOrder array (which gives actual vertex indices)
    std::function<void(const std::vector<size_t>&)> triangulateSubPolygon =
        [&](const std::vector<size_t>& polygon)
    {
        const size_t polygonSize = polygon.size();

        if (polygonSize < 3)
        {
            return;
        }

        if (polygonSize == 3)
        {
            uint32_t va = ccwOrder[polygon[0]];
            uint32_t vb = ccwOrder[polygon[1]];
            uint32_t vc = ccwOrder[polygon[2]];
            indices.insert(indices.end(), {va, vb, vc});
            return;
        }

        // Find the largest-area triangle among all valid triples
        // polygon array is in boundary order (contiguous arc), so try all combinations
        double largestArea = -1.0;
        size_t bestI = 0, bestJ = 1, bestK = 2;

        for (size_t i = 0; i + 2 < polygonSize; ++i)
        {
            for (size_t j = i + 1; j + 1 < polygonSize; ++j)
            {
                for (size_t k = j + 1; k < polygonSize; ++k)
                {
                    uint32_t vi = ccwOrder[polygon[i]];
                    uint32_t vj = ccwOrder[polygon[j]];
                    uint32_t vk = ccwOrder[polygon[k]];

                    const double area = Helpers::TriangleArea(vertices, static_cast<int>(vi), static_cast<int>(vj), static_cast<int>(vk));

                    if (shouldHandleConcave)
                    {
                        if (!Helpers::IsTriangleInsidePolygon(vertices, static_cast<int>(vi), static_cast<int>(vj), static_cast<int>(vk), diagTable))
                        {
                            continue;
                        }
                    }
                    else
                    {
                        // For convex polygons, still ensure triangle has positive area (degenerate check)
                        if (area <= 0.0)
                        {
                            continue;
                        }
                    }

                    if (area > largestArea)
                    {
                        largestArea = area;
                        bestI = i;
                        bestJ = j;
                        bestK = k;
                    }
                }
            }
        }

        if (largestArea < 0)
        {
            // No valid triangle found - this shouldn't happen for valid polygons
            // Fallback: use first three vertices
            if (polygonSize >= 3)
            {
                uint32_t va = ccwOrder[polygon[0]];
                uint32_t vb = ccwOrder[polygon[1]];
                uint32_t vc = ccwOrder[polygon[2]];
                indices.insert(indices.end(), {va, vb, vc});
            }
            return;
        }

        // Emit the selected triangle
        const uint32_t vertexA = ccwOrder[polygon[bestI]];
        const uint32_t vertexB = ccwOrder[polygon[bestJ]];
        const uint32_t vertexC = ccwOrder[polygon[bestK]];

        indices.insert(indices.end(), {vertexA, vertexB, vertexC});

        // Build sub-polygons from the arcs between selected vertices (respecting boundary order)
        // The polygon array represents a contiguous arc, so we need to handle wrap-around correctly
        auto buildArc = [&](size_t startIdx, size_t endIdx) -> std::vector<size_t>
        {
            std::vector<size_t> arc;
            if (startIdx < endIdx)
            {
                // Normal case: arc from startIdx to endIdx (inclusive)
                for (size_t i = startIdx; i <= endIdx; ++i)
                {
                    arc.push_back(polygon[i]);
                }
            }
            else if (startIdx > endIdx)
            {
                // Wrap-around case: from startIdx to end of array, then from start to endIdx
                for (size_t i = startIdx; i < polygonSize; ++i)
                {
                    arc.push_back(polygon[i]);
                }
                for (size_t i = 0; i <= endIdx; ++i)
                {
                    arc.push_back(polygon[i]);
                }
            }
            else
            {
                // startIdx == endIdx: single vertex, return empty (will be filtered)
                arc.push_back(polygon[startIdx]);
            }
            return arc;
        };

        // Three arcs: A→B, B→C, C→A (respecting boundary order)
        // Note: bestI < bestJ < bestK in the polygon array (since we iterate in order)
        // Each arc includes both endpoints to form closed sub-polygons
        const auto arcAB = buildArc(bestI, bestJ);
        const auto arcBC = buildArc(bestJ, bestK);

        // arcCA wraps from bestK back to bestI (closed polygon)
        std::vector<size_t> arcCA;
        arcCA.push_back(polygon[bestK]); // Include endpoint
        for (size_t i = bestK + 1; i < polygonSize; ++i)
        {
            arcCA.push_back(polygon[i]);
        }
        for (size_t i = 0; i <= bestI; ++i)
        {
            arcCA.push_back(polygon[i]); // Include endpoint
        }

        // Recurse on arcs with 3+ vertices
        if (arcAB.size() >= 3)
            triangulateSubPolygon(arcAB);
        if (arcBC.size() >= 3)
            triangulateSubPolygon(arcBC);
        if (arcCA.size() >= 3)
            triangulateSubPolygon(arcCA);
    };

    // Initialize with full polygon (indices into ccwOrder)
    std::vector<size_t> fullPolygon(vertexCount);
    std::iota(fullPolygon.begin(), fullPolygon.end(), 0);

    indices.reserve((vertexCount - 2) * 3);
    triangulateSubPolygon(fullPolygon);

    return indices;
}

std::vector<uint32_t> stripTriangulation(const std::vector<Vertex>& vertices)
{
    std::vector<uint32_t> indices;
    const size_t vertexCount = vertices.size();

    if (vertexCount < 3)
    {
        return indices;
    }

    // For convex polygons, skip expensive CCW order rebuild
    // Check if polygon is likely convex (simple heuristic: CCW area check)
    const bool likelyConvex = polygonSignedArea(vertices) >= 0.0;

    // Build strip order: alternating from start and end
    std::vector<uint32_t> stripOrder;
    stripOrder.reserve(vertexCount);

    size_t leftIdx = 0;
    size_t rightIdx = vertexCount - 1;

    if (likelyConvex)
    {
        // For convex: use direct indices (assume already in order)
        while (leftIdx <= rightIdx)
        {
            stripOrder.push_back(static_cast<uint32_t>(leftIdx++));
            if (leftIdx > rightIdx)
                break;
            stripOrder.push_back(static_cast<uint32_t>(rightIdx--));
        }
    }
    else
    {
        // For concave: ensure CCW order first
        const std::vector<uint32_t> ccwOrder = buildCCWOrder(vertices);
        while (leftIdx <= rightIdx)
        {
            stripOrder.push_back(ccwOrder[leftIdx++]);
            if (leftIdx > rightIdx)
                break;
            stripOrder.push_back(ccwOrder[rightIdx--]);
        }
    }

    // Generate triangles from consecutive strip triplets
    indices.reserve((vertexCount - 2) * 3);

    for (size_t i = 0; i + 2 < stripOrder.size(); ++i)
    {
        uint32_t indexA = stripOrder[i];
        uint32_t indexB = stripOrder[i + 1];
        uint32_t indexC = stripOrder[i + 2];

        // Alternate winding to maintain consistent orientation (only needed for concave)
        if (!likelyConvex)
        {
            const bool isClockwise = polygonSignedArea(vertices) < 0.0;
            const bool shouldSwap = isClockwise ? ((i % 2) == 0) : ((i % 2) == 1);
            if (shouldSwap)
            {
                std::swap(indexA, indexB);
            }
        }

        indices.push_back(indexA);
        indices.push_back(indexB);
        indices.push_back(indexC);
    }

    return indices;
}

std::vector<uint32_t> maxMinAreaTriangulation(const std::vector<Vertex>& vertices, bool shouldHandleConcave)
{
    std::vector<uint32_t> indices;
    const int vertexCount = static_cast<int>(vertices.size());

    if (vertexCount < 3)
    {
        return indices;
    }

    Helpers::DiagonalTable diagTable;
    if (shouldHandleConcave)
    {
        diagTable = Helpers::BuildDiagonalTable(vertices);
    }

    // DP tables: dp[i][j] = maximum achievable minimum triangle area for chain [i, j]
    std::vector<double> dpTable(vertexCount * vertexCount, 0.0);
    std::vector<int> splitTable(vertexCount * vertexCount, -1);

    auto dp = [&](int i, int j) -> double& { return dpTable[i * vertexCount + j]; };
    auto split = [&](int i, int j) -> int& { return splitTable[i * vertexCount + j]; };

    // Initialize: adjacent pairs have infinite "minimum" (no triangles to constrain)
    constexpr double INFINITY_VALUE = std::numeric_limits<double>::infinity();
    for (int i = 0; i + 1 < vertexCount; ++i)
    {
        dp(i, i + 1) = INFINITY_VALUE;
    }

    // Fill DP table for increasing chain lengths
    for (int chainLength = 2; chainLength < vertexCount; ++chainLength)
    {
        for (int start = 0; start + chainLength < vertexCount; ++start)
        {
            const int end = start + chainLength;

            double bestMinArea = 0.0;
            int bestSplit = -1;

            for (int mid = start + 1; mid < end; ++mid)
            {
                if (shouldHandleConcave)
                {
                    if (!Helpers::IsTriangleInsidePolygon(vertices, start, mid, end, diagTable))
                    {
                        continue;
                    }
                }
                else
                {
                    if (Helpers::TriangleArea(vertices, start, mid, end) <= 0.0)
                    {
                        continue;
                    }
                }

                const double triArea = Helpers::TriangleArea(vertices, start, mid, end);

                // Bottleneck for the chain = min(left, right, this triangle)
                const double bottleneck = std::min({dp(start, mid), dp(mid, end), triArea});

                if (bottleneck > bestMinArea)
                {
                    bestMinArea = bottleneck;
                    bestSplit = mid;
                }
            }

            dp(start, end) = bestMinArea;
            split(start, end) = bestSplit;
        }
    }

    // Reconstruct triangles (respect CCW orientation)
    indices.reserve(3 * (vertexCount - 2));
    std::function<void(int, int)> emitTriangles = [&](int start, int end)
    {
        const int mid = split(start, end);
        if (mid < 0)
            return;

        indices.push_back(start);
        indices.push_back(mid);
        indices.push_back(end);

        if (mid > start + 1)
        {
            emitTriangles(start, mid);
        }
        if (end > mid + 1)
        {
            emitTriangles(mid, end);
        }
    };

    emitTriangles(0, vertexCount - 1);

    return indices;
}

std::vector<uint32_t> minMaxAreaTriangulation(const std::vector<Vertex>& vertices, bool shouldHandleConcave)
{
    std::vector<uint32_t> indices;
    const int vertexCount = static_cast<int>(vertices.size());

    if (vertexCount < 3)
    {
        return indices;
    }

    Helpers::DiagonalTable diagTable;
    if (shouldHandleConcave)
    {
        diagTable = Helpers::BuildDiagonalTable(vertices);
    }

    // DP tables: dp[i][j] = minimum achievable maximum triangle area for chain [i, j]
    std::vector<double> dpTable(vertexCount * vertexCount, 0.0);
    std::vector<int> splitTable(vertexCount * vertexCount, -1);

    auto dp = [&](int i, int j) -> double& { return dpTable[i * vertexCount + j]; };
    auto split = [&](int i, int j) -> int& { return splitTable[i * vertexCount + j]; };

    // Initialize: adjacent pairs have zero cost (no triangles)
    for (int i = 0; i + 1 < vertexCount; ++i)
    {
        dp(i, i + 1) = 0.0;
    }

    // Fill DP table for increasing chain lengths
    for (int chainLength = 2; chainLength < vertexCount; ++chainLength)
    {
        for (int start = 0; start + chainLength < vertexCount; ++start)
        {
            const int end = start + chainLength;

            double bestMaxArea = std::numeric_limits<double>::infinity();
            int bestSplit = -1;

            for (int mid = start + 1; mid < end; ++mid)
            {
                if (shouldHandleConcave)
                {
                    if (!Helpers::IsTriangleInsidePolygon(vertices, start, mid, end, diagTable))
                    {
                        continue;
                    }
                }
                else
                {
                    if (Helpers::TriangleArea(vertices, start, mid, end) <= 0.0)
                    {
                        continue;
                    }
                }

                const double triArea = Helpers::TriangleArea(vertices, start, mid, end);

                // Cost = maximum of {left subproblem, right subproblem, this triangle}
                const double cost = std::max({dp(start, mid), dp(mid, end), triArea});

                if (cost < bestMaxArea)
                {
                    bestMaxArea = cost;
                    bestSplit = mid;
                }
            }

            dp(start, end) = bestMaxArea;
            split(start, end) = bestSplit;
        }
    }

    // Reconstruct triangles with CCW orientation
    indices.reserve(3 * (vertexCount - 2));
    std::function<void(int, int)> emitTriangles = [&](int start, int end)
    {
        const int mid = split(start, end);
        if (mid < 0)
            return;

        indices.push_back(start);
        indices.push_back(mid);
        indices.push_back(end);

        if (mid > start + 1)
            emitTriangles(start, mid);
        if (end > mid + 1)
            emitTriangles(mid, end);
    };

    emitTriangles(0, vertexCount - 1);

    return indices;
}

std::vector<uint32_t> constrainedDelaunay(const std::vector<Vertex>& vertices)
{
    const int vertexCount = static_cast<int>(vertices.size());
    if (vertexCount < 3)
    {
        return std::vector<uint32_t>();
    }

    // Prepare vertex matrix for libigl
    Eigen::Matrix<double, Eigen::Dynamic, 2> inputVertices(vertexCount, 2);
    for (int i = 0; i < vertexCount; ++i)
    {
        inputVertices(i, 0) = static_cast<double>(vertices[i].pos.x);
        inputVertices(i, 1) = static_cast<double>(vertices[i].pos.y);
    }

    // Define boundary edges (closed polygon)
    Eigen::Matrix<int, Eigen::Dynamic, 2> boundaryEdges(vertexCount, 2);
    for (int i = 0; i < vertexCount; ++i)
    {
        boundaryEdges(i, 0) = i;
        boundaryEdges(i, 1) = (i + 1) % vertexCount;
    }

    // No interior holes
    Eigen::Matrix<double, Eigen::Dynamic, 2> holes(0, 2);

    // Triangle flags: p = PSLG mode (respects boundary segments), Q = quiet, z = zero-indexed
    const std::string triangleFlags = "pQz";

    Eigen::Matrix<double, Eigen::Dynamic, 2> outputVertices;
    Eigen::Matrix<int, Eigen::Dynamic, 3> outputFaces;

    igl::triangle::triangulate(inputVertices, boundaryEdges, holes, triangleFlags, outputVertices, outputFaces);

    // Convert face matrix to flat index array with CCW winding order
    std::vector<uint32_t> triangleIndices;
    triangleIndices.reserve(static_cast<size_t>(outputFaces.rows()) * 3);

    for (int faceIndex = 0; faceIndex < outputFaces.rows(); ++faceIndex)
    {
        int idx0 = outputFaces(faceIndex, 0);
        int idx1 = outputFaces(faceIndex, 1);
        int idx2 = outputFaces(faceIndex, 2);

        // Validate indices are within bounds
        if (idx0 < 0 || idx0 >= vertexCount || idx1 < 0 || idx1 >= vertexCount || idx2 < 0 || idx2 >= vertexCount)
        {
            continue; // Skip invalid triangles
        }

        // Ensure CCW winding order
        if (!isCounterClockwise(vertices, static_cast<uint32_t>(idx0), static_cast<uint32_t>(idx1), static_cast<uint32_t>(idx2)))
        {
            // Swap two vertices to make it CCW
            std::swap(idx1, idx2);
        }

        triangleIndices.push_back(static_cast<uint32_t>(idx0));
        triangleIndices.push_back(static_cast<uint32_t>(idx1));
        triangleIndices.push_back(static_cast<uint32_t>(idx2));
    }

    return triangleIndices;
}

std::vector<uint32_t> constrainedDelaunayFlipped(const std::vector<Vertex>& vertices)
{
    // First get CDT triangulation
    std::vector<uint32_t> indices = constrainedDelaunay(vertices);

    // Then optimize with edge flips
    if (!indices.empty())
    {
        indices = optimizeByMinLengthFlips(vertices, indices);
    }

    return indices;
}

// Helper functions for edge flip optimization
namespace
{

inline double orient2D(const Vertex& a, const Vertex& b, const Vertex& c)
{
    const double ax = a.pos.x, ay = a.pos.y;
    const double bx = b.pos.x, by = b.pos.y;
    const double cx = c.pos.x, cy = c.pos.y;
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

inline bool isConvexQuad(const std::vector<Vertex>& V, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    // a and b must be on opposite sides of cd, and c and d opposite sides of ab
    const double o1 = orient2D(V[a], V[b], V[c]);
    const double o2 = orient2D(V[a], V[b], V[d]);
    if (o1 == 0.0 || o2 == 0.0 || ((o1 > 0) == (o2 > 0)))
        return false;
    const double o3 = orient2D(V[c], V[d], V[a]);
    const double o4 = orient2D(V[c], V[d], V[b]);
    if (o3 == 0.0 || o4 == 0.0 || ((o3 > 0) == (o4 > 0)))
        return false;
    return true;
}

struct EdgeKey
{
    uint32_t a;
    uint32_t b;
    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
};

struct EdgeKeyHash
{
    size_t operator()(const EdgeKey& k) const noexcept
    {
        // 64-bit mix of two 32-bit ints
        return (size_t(k.a) << 32) ^ size_t(k.b);
    }
};

struct EdgeAdj
{
    int t0 = -1;
    int t1 = -1;
    uint32_t opp0 = 0;
    uint32_t opp1 = 0;
    uint32_t ver = 0; // bump on any mutation
};

inline EdgeKey makeKey(uint32_t u, uint32_t v)
{
    return EdgeKey{std::min(u, v), std::max(u, v)};
}

inline std::array<uint32_t, 3> makeCCW(const std::vector<Vertex>& v, uint32_t i0, uint32_t i1, uint32_t i2)
{
    // If (i0,i1,i2) is CCW keep, else swap i1/i2
    if (cross2D(v[i0].pos, v[i1].pos, v[i2].pos) >= 0.0f)
    {
        return {i0, i1, i2};
    }
    else
    {
        return {i0, i2, i1};
    }
}

} // anonymous namespace

std::vector<uint32_t> optimizeByMinLengthFlips(
    const std::vector<Vertex>& vertices,
    std::vector<uint32_t> indices,
    int maxFlips,
    int maxPops
)
{
    const int triCount = int(indices.size() / 3);
    if (triCount <= 0)
    {
        return indices;
    }

    auto tri = [&](int t, int k) -> uint32_t& { return indices[3 * t + k]; };
    auto tric = [&](int t, int k) -> uint32_t { return indices[3 * t + k]; };

    std::unordered_map<EdgeKey, EdgeAdj, EdgeKeyHash> adj;
    adj.reserve(indices.size() * 2);

    // u, v are indices of the edge endpoints
    // triangle is the triangle index in which the edge lies
    // ov is the index of the vertex opposite of the edge in the respective triangle
    auto addEdge = [&](uint32_t u, uint32_t v, int triangle, uint32_t ov)
    {
        EdgeKey key = makeKey(u, v);
        auto& edge = adj[key];
        // any write mutates bump version
        ++edge.ver;
        if (edge.t0 == -1)
        {
            edge.t0 = triangle;
            edge.opp0 = ov;
        }
        else
        {
            edge.t1 = triangle;
            edge.opp1 = ov;
        }
    };

    auto rebuildTriangle = [&](int t)
    {
        const uint32_t a = tric(t, 0);
        const uint32_t b = tric(t, 1);
        const uint32_t c = tric(t, 2);
        addEdge(a, b, t, c);
        addEdge(b, c, t, a);
        addEdge(c, a, t, b);
    };

    auto clearTriangle = [&](int t)
    {
        const uint32_t a = tric(t, 0);
        const uint32_t b = tric(t, 1);
        const uint32_t c = tric(t, 2);
        const EdgeKey e0 = makeKey(a, b);
        const EdgeKey e1 = makeKey(b, c);
        const EdgeKey e2 = makeKey(c, a);
        auto clearEdge = [&](const EdgeKey& k)
        {
            auto it = adj.find(k);
            if (it == adj.end())
            {
                return;
            }
            auto& E = it->second;
            // any write mutates bump version
            ++E.ver;
            if (E.t0 == t)
            {
                E.t0 = -1;
                E.opp0 = 0;
            }
            if (E.t1 == t)
            {
                E.t1 = -1;
                E.opp1 = 0;
            }
        };
        clearEdge(e0);
        clearEdge(e1);
        clearEdge(e2);
    };

    for (int t = 0; t < triCount; ++t)
    {
        rebuildTriangle(t);
    }

    // Priority queue of candidate flips (max gain first)
    struct Candidate
    {
        double gain; // >0 is improving
        EdgeKey key;
        uint32_t ver;
    };

    struct CandidateLess
    {
        bool operator()(const Candidate& a, const Candidate& b) const { return a.gain < b.gain; } // max-heap
    };

    std::priority_queue<Candidate, std::vector<Candidate>, CandidateLess> pq;

    auto len2 = [&](uint32_t i, uint32_t j) -> float
    {
        const float2 diff = vertices[i].pos - vertices[j].pos;
        return dot(diff, diff);
    };

    auto tryPushEdge = [&](const EdgeKey& k)
    {
        auto it = adj.find(k);
        if (it == adj.end())
        {
            return;
        }
        const auto& E = it->second;
        if (E.t0 == -1 || E.t1 == -1)
        {
            return; // Edge is not 2 sided (likely boundary)
        }
        const uint32_t a = k.a;
        const uint32_t b = k.b;
        const uint32_t c = E.opp0;
        const uint32_t d = E.opp1;
        if (!isConvexQuad(vertices, a, b, c, d))
        {
            return;
        }
        // improving if new diagonal shorter
        const double oldD = len2(a, b);
        const double newD = len2(c, d);
        const double gain = oldD - newD;
        if (gain <= 0.0)
        {
            return;
        }
        pq.push(Candidate{gain, k, E.ver});
    };

    for (const auto& [k, e] : adj)
    {
        (void)e;
        tryPushEdge(k);
    }

    auto pushTriEdges = [&](int t)
    {
        const uint32_t a = tric(t, 0);
        const uint32_t b = tric(t, 1);
        const uint32_t c = tric(t, 2);
        tryPushEdge(makeKey(a, b));
        tryPushEdge(makeKey(b, c));
        tryPushEdge(makeKey(c, a));
    };

    int flips = 0;
    int pops = 0;
    while (!pq.empty())
    {
        if (maxPops >= 0 && pops >= maxPops)
        {
            break;
        }
        ++pops;
        const Candidate candidate = pq.top();
        pq.pop();

        auto it = adj.find(candidate.key);
        if (it == adj.end())
        {
            continue;
        }
        auto& E = it->second;
        if (E.ver != candidate.ver)
        {
            continue;
        }
        if (E.t0 == -1 || E.t1 == -1)
        {
            continue; // not interior
        }
        const uint32_t a = candidate.key.a;
        const uint32_t b = candidate.key.b;
        const uint32_t c = E.opp0;
        const uint32_t d = E.opp1;
        const int t0 = E.t0;
        const int t1 = E.t1;

        // Re-check (neighbors may have moved but ver guard usually catches)
        if (!isConvexQuad(vertices, a, b, c, d))
        {
            continue;
        }
        const double oldD = len2(a, b);
        const double newD = len2(c, d);
        if (newD >= oldD)
        {
            continue; // no longer improving
        }
        // Optional hard cap on number of flips
        if (maxFlips >= 0 && flips >= maxFlips)
        {
            break;
        }

        // Build new triangles using the other diagonal (c-d)
        const auto T0 = makeCCW(vertices, c, d, a);
        const auto T1 = makeCCW(vertices, d, c, b);

        // Update mesh
        clearTriangle(t0);
        clearTriangle(t1);
        tri(t0, 0) = T0[0];
        tri(t0, 1) = T0[1];
        tri(t0, 2) = T0[2];
        tri(t1, 0) = T1[0];
        tri(t1, 1) = T1[1];
        tri(t1, 2) = T1[2];
        rebuildTriangle(t0);
        rebuildTriangle(t1);

        // Re-enqueue affected neighborhood (only local)
        pushTriEdges(t0);
        pushTriEdges(t1);

        ++flips;
    }

    return indices;
}

std::vector<uint32_t> earClippingMapbox(const std::vector<Vertex>& vertices)
{
    std::vector<uint32_t> indices;
    const size_t n = vertices.size();
    if (n < 3)
    {
        return indices;
    }

    // Convert Vertex format to earcut format (std::vector<std::vector<Point>>)
    // earcut expects: std::vector<std::vector<std::array<Coord, 2>>>
    using Point = std::array<double, 2>;
    std::vector<std::vector<Point>> polygon;

    // Create the main polygon contour
    std::vector<Point> contour;
    contour.reserve(n);
    for (const auto& v : vertices)
    {
        contour.push_back({static_cast<double>(v.pos.x), static_cast<double>(v.pos.y)});
    }
    polygon.push_back(std::move(contour));

    // Run earcut triangulation
    // Returns array of indices that refer to the vertices of the input polygon
    indices = mapbox::earcut<uint32_t>(polygon);

    return indices;
}

std::vector<uint32_t> earClippingMapboxFlipped(const std::vector<Vertex>& vertices)
{
    std::vector<uint32_t> indices;
    const size_t n = vertices.size();
    if (n < 3)
    {
        return indices;
    }

    // Convert Vertex format to earcut format
    using Point = std::array<double, 2>;
    std::vector<std::vector<Point>> polygon;

    std::vector<Point> contour;
    contour.reserve(n);
    for (const auto& v : vertices)
    {
        contour.push_back({static_cast<double>(v.pos.x), static_cast<double>(v.pos.y)});
    }
    polygon.push_back(std::move(contour));

    // Run earcut triangulation
    indices = mapbox::earcut<uint32_t>(polygon);

    // Optimize with edge flips
    indices = optimizeByMinLengthFlips(vertices, indices);

    return indices;
}

std::vector<Vertex> CreateVerticesForEllipse(uint32_t numSegments, float radiusX, float radiusY, const float2& center)
{
    std::vector<Vertex> vertices;
    vertices.reserve(numSegments);

    const float angleStep = 2.0f * 3.14159265359f / static_cast<float>(numSegments);

    for (uint32_t i = 0; i < numSegments; ++i)
    {
        const float angle = static_cast<float>(i) * angleStep;
        const float x = center.x + radiusX * std::cos(angle);
        const float y = center.y + radiusY * std::sin(angle);

        Vertex v;
        v.pos = float2(x, y);
        vertices.push_back(v);
    }

    return vertices;
}

std::vector<uint32_t> CreateConvexMWT(const std::vector<Vertex>& vertices, double& outEdgeLength)
{
    std::vector<uint32_t> indices = minimumWeightTriangulation(vertices, false);
    outEdgeLength = calculateTotalEdgeLength(vertices, indices);
    return indices;
}

} // namespace Triangulation
