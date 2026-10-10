#include "Triangulation/2D/ProjectedGeometry.h"
#include "Triangulation/2D/TriangulationHelpers.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
namespace ProjectedTriangulation::Detail
{
std::vector<uint32_t> extractTrianglesFromPlanarGraph(const ProjectedPatch& patch, const std::unordered_set<EdgeKey, EdgeKeyHash>& edges)
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
        std::sort(
            adjacency[vertexIndex].begin(),
            adjacency[vertexIndex].end(),
            [&](uint32_t lhs, uint32_t rhs)
            {
                const Falcor::float2& origin = patch.projectedVertices[vertexIndex].pos;
                const Falcor::float2& a = patch.projectedVertices[lhs].pos;
                const Falcor::float2& b = patch.projectedVertices[rhs].pos;
                return std::atan2(a.y - origin.y, a.x - origin.x) < std::atan2(b.y - origin.y, b.x - origin.x);
            }
        );
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

            if (!appendEarClippedFace(indices, patch.projectedVertices, face))
                return {};
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
    const auto& cached = use3DEdgeLength ? patch.lengths3D : patch.lengths2D;
    if (!cached.empty())
        return cached[size_t(a) * patch.projectedVertices.size() + b];
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

std::vector<uint32_t> triangulateLmtMinimumWeight(const ProjectedPatch& patch, bool use3DEdgeLength, bool useSurfaceFlips)
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

    std::sort(
        lmtCandidates.begin(),
        lmtCandidates.end(),
        [&](const EdgeKey& a, const EdgeKey& b)
        { return edgeWeight(patch, a.a, a.b, use3DEdgeLength) < edgeWeight(patch, b.a, b.b, use3DEdgeLength); }
    );

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
        indices = Triangulation::optimizeByMinLengthFlips(
            patch.projectedVertices, std::move(indices), -1, -1, useSurfaceFlips ? &patch.originalVertices : nullptr
        );
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

    std::sort(candidates.begin(), candidates.end(), [](const CandidateEdge& a, const CandidateEdge& b) { return a.weight < b.weight; });

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
            std::remove_if(
                neighbors.begin(), neighbors.end(), [&](const Neighbor& neighbor) { return neighbor.distance > effectiveRadius; }
            ),
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
            if (options.pclSearchRadius > 0.0f &&
                (centerLeft > options.pclSearchRadius || centerRight > options.pclSearchRadius || leftRight > options.pclSearchRadius))
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

    std::map<EdgeKey, float> bestWeight;
    for (const auto& candidate : candidateEdges)
    {
        auto [it, inserted] = bestWeight.emplace(candidate.edge, candidate.weight);
        if (!inserted)
            it->second = std::min(it->second, candidate.weight);
    }
    candidateEdges.clear();
    for (auto [edge, weight] : bestWeight)
        candidateEdges.push_back({edge, weight});
    std::sort(
        candidateEdges.begin(), candidateEdges.end(), [](const CandidateEdge& a, const CandidateEdge& b) { return a.weight < b.weight; }
    );

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

} // namespace ProjectedTriangulation::Detail
