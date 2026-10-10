#include "Triangulation/2D/ProjectedPatchTriangulation.h"
#include "Triangulation/GeometryMath.h"
#include "Triangulation/2D/ProjectedGeometry.h"

#include "Triangulation/ShaderTypes.h"
#include "Triangulation/2D/TriangulationHelpers.h"

#include <Eigen/Dense>
#include <igl/triangle/triangulate.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace ProjectedTriangulation
{
using namespace Triangulation3D;
using namespace TriangulationCommon;
using namespace Detail;
namespace
{
bool simpleBoundary(const ProjectedPatch& patch)
{
    const auto& v = patch.projectedVertices;
    const auto& loop = patch.boundaryLoop;
    if (loop.size() < 3 || std::abs(polygonSignedArea2(v, loop)) <= 1e-10f)
        return false;
    std::set<uint32_t> seen;
    for (uint32_t i : loop)
        if (i >= v.size() || !seen.insert(i).second)
            return false;
    for (size_t i = 0; i < loop.size(); ++i)
        for (size_t j = i + 1; j < loop.size(); ++j)
        {
            size_t ni = (i + 1) % loop.size(), nj = (j + 1) % loop.size();
            if (ni == j || nj == i)
                continue;
            if (segmentsIntersectOrTouch(v[loop[i]].pos, v[loop[ni]].pos, v[loop[j]].pos, v[loop[nj]].pos))
                return false;
        }
    for (const auto& p : v)
        if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || !pointInsideOrOnPolygon(v, loop, p.pos))
            return false;
    return true;
}

bool completeDisk(const ProjectedPatch& patch, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    // Keeping all input vertices and a simple oriented boundary allows checking
    // disk coverage without an all-pairs triangle intersection pass.
    if (vertices.size() != patch.projectedVertices.size() || indices.empty() || indices.size() % 3)
        return false;
    if (indices.size() / 3 != 2 * vertices.size() - patch.boundaryLoop.size() - 2)
        return false;
    std::map<EdgeKey, std::pair<uint32_t, int>> edges;
    std::set<std::array<uint32_t, 3>> triangles;
    std::set<uint32_t> used;
    double area = 0;
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        std::array<uint32_t, 3> t = {indices[i], indices[i + 1], indices[i + 2]};
        for (auto j : t)
            if (j >= vertices.size())
                return false;
        double signedArea = triangleSignedArea2(vertices, t[0], t[1], t[2]);
        if (!(signedArea > 1e-10))
            return false;
        area += signedArea;
        for (size_t k = 0; k < 3; ++k)
        {
            auto a = t[k], b = t[(k + 1) % 3];
            used.insert(a);
            auto& e = edges[makeEdgeKey(a, b)];
            ++e.first;
            e.second += a < b ? 1 : -1;
        }
        std::sort(t.begin(), t.end());
        if (!triangles.insert(t).second)
            return false;
    }
    std::set<EdgeKey> boundary;
    for (auto [a, b] : patch.boundaryEdges)
        boundary.insert(makeEdgeKey(a, b));
    for (auto [edge, count] : edges)
    {
        if (boundary.count(edge))
        {
            if (count.first != 1)
                return false;
        }
        else if (count.first != 2 || count.second != 0)
            return false;
    }
    for (auto edge : boundary)
        if (!edges.count(edge))
            return false;
    const double targetArea = std::abs(polygonSignedArea2(vertices, patch.boundaryLoop));
    return used.size() == vertices.size() && std::abs(area - targetArea) <= std::max(1e-9, targetArea * 1e-4);
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
            if (projectedVertices[i].pos.x != patch.projectedVertices[i].pos.x || projectedVertices[i].pos.y != patch.projectedVertices[i].pos.y)
            { result.message = "Backend changed the input vertex ordering."; return result; }
            result.vertices.push_back(patch.originalVertices[i]);
            result.sourceVertexIndices.push_back(patch.localToGlobal[i]);
        }
        else
        {
            result.vertices.push_back(unprojectFromFrame(projectedVertices[i].pos, frame));
            result.sourceVertexIndices.push_back(kInvalidIndex);
        }
    }

    result.success = completeDisk(patch, projectedVertices, result.indices);
    result.message =
        result.success ? "Patch triangulation succeeded." : "Patch triangulation failed disk coverage, winding, or topology checks.";
    return result;
}

PatchTriangulationResult triangulatePatchWithTriangle(
    const ProjectedPatch& patch,
    const ProjectionFrame& frame,
    bool applyFlips,
    bool surfaceFlips
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
        if (surfaceFlips && projectedOutputVertices.size() != patch.originalVertices.size())
        {
            result.message = "Surface metric requires unchanged vertices.";
            return result;
        }
        indices = Triangulation::optimizeByMinLengthFlips(
            projectedOutputVertices, std::move(indices), -1, -1, surfaceFlips ? &patch.originalVertices : nullptr
        );
    }

    return makePatchResultFromLocalIndices(patch, projectedOutputVertices, std::move(indices), frame);
}

} // namespace

bool triangulatePolygon(std::vector<uint32_t>& indices, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& boundary)
{
    ProjectedPatch patch;
    patch.projectedVertices = vertices;
    patch.boundaryLoop = boundary;
    for (size_t i = 0; i < boundary.size(); ++i)
        patch.boundaryEdges.emplace_back(boundary[i], boundary[(i + 1) % boundary.size()]);
    if (!simpleBoundary(patch))
        return false;
    std::vector<uint32_t> candidate;
    // Preserve the historical diagonal on convex faces, including curved quads.
    for (size_t i = 1; i + 1 < boundary.size(); ++i)
        appendTriangleCCW(candidate, vertices, boundary[0], boundary[i], boundary[i + 1]);
    if (!completeDisk(patch, vertices, candidate))
    {
        candidate.clear();
        if (!appendEarClippedFace(candidate, vertices, boundary) || !completeDisk(patch, vertices, candidate))
            return false;
    }
    indices.insert(indices.end(), candidate.begin(), candidate.end());
    return true;
}

PatchTriangulationResult triangulate(const ProjectedPatch& input, const ProjectionFrame& frame, const TriangulationOptions& options)
{
    const auto& patch = input;
    if (!simpleBoundary(patch))
    {
        PatchTriangulationResult result;
        result.message = "Projected boundary is not simple or does not enclose the vertices.";
        return result;
    }
    // These research heuristics have superlinear search cost. Use a predictable
    // limit and let the surface layer retain source triangles on larger patches.
    if ((options.method == Method::LmtMinimumWeight && patch.projectedVertices.size() > 64) ||
        ((options.method == Method::GreedyPointSet || options.method == Method::PclGreedyProjection) && patch.projectedVertices.size() > 256
        ))
    {
        PatchTriangulationResult result;
        result.message = "Experimental backend vertex budget exceeded.";
        return result;
    }
    if (options.method == Method::ConstrainedDelaunay || options.method == Method::ConstrainedDelaunayFlipped)
        return triangulatePatchWithTriangle(
            patch, frame, options.method == Method::ConstrainedDelaunayFlipped, options.use3DEdgeLengthForFlips
        );
    ProjectedPatch cached = input;
    const auto n = cached.projectedVertices.size();
    cached.visibility.assign(n * n, -1);
    if (options.method == Method::LmtMinimumWeight)
    {
        cached.lengths2D.resize(n * n);
        cached.lengths3D.resize(n * n);
        for (uint32_t a = 0; a < n; ++a)
            for (uint32_t b = 0; b < n; ++b)
            {
                cached.lengths2D[a * n + b] = std::sqrt(distanceSquared2(cached.projectedVertices[a].pos, cached.projectedVertices[b].pos));
                cached.lengths3D[a * n + b] = std::sqrt(distanceSquared3(cached.originalVertices[a], cached.originalVertices[b]));
            }
    }
    switch (options.method)
    {
    case Method::ConstrainedDelaunay:
        return triangulatePatchWithTriangle(patch, frame, false, options.use3DEdgeLengthForFlips);
    case Method::ConstrainedDelaunayFlipped:
        return triangulatePatchWithTriangle(patch, frame, true, options.use3DEdgeLengthForFlips);
    case Method::GreedyPointSet:
        return makePatchResultFromLocalIndices(
            cached, cached.projectedVertices, triangulateGreedyPointSet(cached, options.use3DEdgeLengthForGreedy), frame
        );
    case Method::LmtMinimumWeight:
        return makePatchResultFromLocalIndices(
            cached,
            cached.projectedVertices,
            triangulateLmtMinimumWeight(cached, options.use3DEdgeLengthForGreedy, options.use3DEdgeLengthForFlips),
            frame
        );
    case Method::PclGreedyProjection:
        return makePatchResultFromLocalIndices(cached, cached.projectedVertices, triangulatePclGreedyProjection(cached, options), frame);
    }
    PatchTriangulationResult result;
    result.message = "Unknown triangulation method.";
    return result;
}
} // namespace ProjectedTriangulation
