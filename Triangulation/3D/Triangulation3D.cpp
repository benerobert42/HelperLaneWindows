#include "Triangulation/3D/Triangulation3D.h"

#include "Triangulation/ShaderTypes.h"
#include "Triangulation/2D/TriangulationHelpers.h"

#include "Triangulation/2D/ProjectedPatchTriangulation.h"
#include "Triangulation/GeometryMath.h"
#include "Triangulation/MeshValidation.h"

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

using namespace TriangulationCommon;
using ProjectedTriangulation::ProjectedPatch;

struct FaceGeometry
{
    Falcor::float3 normal;
    float weight = 0;
    bool valid = false;
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
    if (!std::isfinite(rawLength) || rawLength <= 1e-8f)
    {
        return false;
    }

    outNormal = multiply3(rawNormal, 1.0f / rawLength);
    outWeight = rawLength;
    return true;
}

float angleDegreesBetweenUnitVectors(const Falcor::float3& a, const Falcor::float3& b)
{
    const float dotValue = std::clamp(dot3(a, b), -1.0f, 1.0f);
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

PatchStats computePatchStats(
    const Mesh& mesh,
    const std::vector<uint32_t>& faceIndices,
    const PatchGrowthOptions& options,
    const std::vector<FaceGeometry>& faces
)
{
    PatchStats stats;
    stats.vertexIndices = collectVertexIndices(mesh, faceIndices);
    if (faceIndices.empty() || stats.vertexIndices.size() < 3)
    {
        return stats;
    }

    const auto seedNormal = faces[faceIndices.front()].normal;
    if (!faces[faceIndices.front()].valid)
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
        const auto faceNormal = faces[faceIndex].normal;
        const auto faceWeight = faces[faceIndex].weight;
        if (!faces[faceIndex].valid)
            return stats;

        if (dot3(faceNormal, seedNormal) < 0.0f)
        {
            return stats;
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
        const auto faceNormal = faces[faceIndex].normal;
        const auto faceWeight = faces[faceIndex].weight;
        if (!faces[faceIndex].valid)
            return stats;

        stats.maxNormalDeviationDegrees =
            std::max(stats.maxNormalDeviationDegrees, angleDegreesBetweenUnitVectors(faceNormal, stats.frame.normal));
    }

    const float diagonal = computeBoundsDiagonal(mesh, stats.vertexIndices);
    stats.planeTolerance = std::max(options.minPlaneDeviation, options.maxRelativePlaneDeviation * diagonal);
    stats.valid = stats.maxPlaneDistance <= stats.planeTolerance && stats.maxNormalDeviationDegrees <= options.maxNormalDeviationDegrees;
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

    const uint32_t start =
        std::min_element(adjacency.begin(), adjacency.end(), [](const auto& a, const auto& b) { return a.first < b.first; })->first;

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

Patch makePatch(
    const Mesh& mesh,
    const std::vector<uint32_t>& faceIndices,
    const PatchGrowthOptions& options,
    const std::vector<FaceGeometry>& faces
)
{
    Patch patch;
    patch.faceIndices = faceIndices;

    const PatchStats stats = computePatchStats(mesh, faceIndices, options, faces);
    patch.vertexIndices = stats.vertexIndices;
    patch.frame = stats.frame;
    patch.maxPlaneDistance = stats.maxPlaneDistance;
    patch.maxNormalDeviationDegrees = stats.maxNormalDeviationDegrees;

    patch.boundaryEdges = extractBoundaryEdges(mesh, faceIndices);
    std::map<EdgeKey, std::pair<uint32_t, int>> edges;
    for (auto faceIndex : faceIndices)
    {
        const auto& face = mesh.faces[faceIndex];
        for (size_t i = 0; i < face.size(); ++i)
        {
            auto a = face[i], b = face[(i + 1) % face.size()];
            auto& count = edges[makeEdgeKey(a, b)];
            ++count.first;
            count.second += a < b ? 1 : -1;
        }
    }
    bool manifold = int64_t(patch.vertexIndices.size()) - int64_t(edges.size()) + int64_t(faceIndices.size()) == 1;
    for (auto [edge, count] : edges)
        if (count.first > 2 || (count.first == 2 && count.second != 0))
            manifold = false;
    patch.isDiskLike = stats.valid && manifold && orderBoundaryLoop(patch.boundaryEdges, patch.boundaryLoop);
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
        if (vertexIndex >= mesh.vertices.size() || !std::isfinite(mesh.vertices[vertexIndex].x) ||
            !std::isfinite(mesh.vertices[vertexIndex].y) || !std::isfinite(mesh.vertices[vertexIndex].z))
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
        return "LMT Heuristic";
    case Method::PclGreedyProjection:
        return "Custom Greedy Projection";
    }

    return "Unknown";
}

static std::vector<Patch> growFacePatches(const Mesh& mesh, const PatchGrowthOptions& options, const std::vector<uint32_t>& sourceFaces)
{
    std::vector<Patch> patches;
    if (mesh.vertices.empty() || mesh.faces.empty())
    {
        return patches;
    }

    for (const auto& face : mesh.faces)
        if (!isFiniteFace(mesh, face))
            return {};
    const std::vector<std::vector<uint32_t>> adjacency = buildFaceAdjacency(mesh);
    std::vector<bool> assigned(mesh.faces.size(), false);
    std::vector<FaceGeometry> faces(mesh.faces.size());
    for (uint32_t i = 0; i < faces.size(); ++i)
        faces[i].valid = computeFaceNormal(mesh, i, faces[i].normal, faces[i].weight);

    for (uint32_t seedFace = 0; seedFace < static_cast<uint32_t>(mesh.faces.size()); ++seedFace)
    {
        if (assigned[seedFace] || !isFiniteFace(mesh, mesh.faces[seedFace]))
        {
            continue;
        }

        std::vector<uint32_t> patchFaces = {seedFace};
        std::set<uint32_t> patchSourceFaces = {sourceFaces[seedFace]};
        assigned[seedFace] = true;

        const bool seedValid = computePatchStats(mesh, patchFaces, options, faces).valid;
        std::queue<uint32_t> candidates;
        std::vector<bool> queued(mesh.faces.size(), false);
        for (uint32_t neighbor : adjacency[seedFace])
        {
            if (seedValid && !queued[neighbor])
            {
                candidates.push(neighbor);
                queued[neighbor] = true;
            }
        }

        while (!candidates.empty())
        {
            const uint32_t candidateFace = candidates.front();
            candidates.pop();
            queued[candidateFace] = false;

            if (candidateFace >= mesh.faces.size() || assigned[candidateFace] || !isFiniteFace(mesh, mesh.faces[candidateFace]))
            {
                continue;
            }
            if (!patchSourceFaces.count(sourceFaces[candidateFace]) && patchSourceFaces.size() >= options.maxFacesPerPatch)
            {
                continue;
            }

            patchFaces.push_back(candidateFace);
            const PatchStats stats = computePatchStats(mesh, patchFaces, options, faces);
            patchFaces.pop_back();

            if (!stats.valid)
            {
                continue;
            }

            patchFaces.push_back(candidateFace);
            assigned[candidateFace] = true;
            patchSourceFaces.insert(sourceFaces[candidateFace]);

            for (uint32_t neighbor : adjacency[candidateFace])
            {
                if (!assigned[neighbor] && !queued[neighbor])
                {
                    queued[neighbor] = true;
                    candidates.push(neighbor);
                }
            }
        }

        patches.push_back(makePatch(mesh, patchFaces, options, faces));
    }

    return patches;
}

std::vector<Patch> growPatches(const Mesh& mesh, const PatchGrowthOptions& options)
{
    // Split seeds that cannot be projected within the requested plane tolerance
    // into their already defined source triangles. They can then grow normally,
    // without bypassing seed eligibility or dropping a curved source face.
    Mesh prepared;
    prepared.vertices = mesh.vertices;
    std::vector<std::vector<uint32_t>> sourceTriangles;
    std::vector<uint32_t> originalFaces;
    std::vector<FaceGeometry> originalGeometry(mesh.faces.size());
    for (uint32_t i = 0; i < mesh.faces.size(); ++i)
    {
        if (!isFiniteFace(mesh, mesh.faces[i]))
            return {};
        originalGeometry[i].valid = computeFaceNormal(mesh, i, originalGeometry[i].normal, originalGeometry[i].weight);
    }
    uint32_t firstTriangle = 0;
    for (uint32_t f = 0; f < mesh.faces.size(); ++f)
    {
        const auto& face = mesh.faces[f];
        const uint32_t triangleCount = uint32_t(face.size() - 2);
        if (computePatchStats(mesh, {f}, options, originalGeometry).valid)
        {
            prepared.faces.push_back(face);
            originalFaces.push_back(f);
            sourceTriangles.emplace_back();
            for (uint32_t i = 0; i < triangleCount; ++i)
                sourceTriangles.back().push_back(firstTriangle + i);
        }
        else
        {
            Mesh local;
            local.faces.emplace_back();
            for (auto i : face)
            {
                local.faces.back().push_back(uint32_t(local.vertices.size()));
                local.vertices.push_back(mesh.vertices[i]);
            }
            auto source = useSourceMeshTriangulation(local);
            if (!source.success)
                return {};
            for (uint32_t t = 0; t < triangleCount; ++t)
            {
                prepared.faces.push_back({face[source.indices[3 * t]], face[source.indices[3 * t + 1]], face[source.indices[3 * t + 2]]});
                originalFaces.push_back(f);
                sourceTriangles.push_back({firstTriangle + t});
            }
        }
        firstTriangle += triangleCount;
    }
    auto patches = growFacePatches(prepared, options, originalFaces);
    for (auto& patch : patches)
    {
        std::set<uint32_t> faces;
        for (auto f : patch.faceIndices)
        {
            faces.insert(originalFaces[f]);
            patch.sourceTriangleIndices.insert(patch.sourceTriangleIndices.end(), sourceTriangles[f].begin(), sourceTriangles[f].end());
        }
        patch.faceIndices.assign(faces.begin(), faces.end());
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

    return ProjectedTriangulation::triangulate(projectedPatch, patch.frame, options);
}

MeshTriangulationResult useSourceMeshTriangulation(const Mesh& mesh)
{
    MeshTriangulationResult result;
    result.vertices = mesh.vertices;
    for (size_t faceIndex = 0; faceIndex < mesh.faces.size(); ++faceIndex)
    {
        const auto& face = mesh.faces[faceIndex];
        if (!isFiniteFace(mesh, face))
        {
            result.messages.push_back("Invalid source face " + std::to_string(faceIndex));
            result.indices.clear();
            return result;
        }
        if (face.size() == 3)
        {
            // Explicit triangles need no polygon projection. Its fixed 2D
            // tolerance can reject small triangles that pass the 3D audit.
            const float area = 0.5f * length3(cross3(
                subtract3(mesh.vertices[face[1]], mesh.vertices[face[0]]),
                subtract3(mesh.vertices[face[2]], mesh.vertices[face[0]])));
            if (!std::isfinite(area) || area < 1e-12f)
            {
                result.messages.push_back("Degenerate source face " + std::to_string(faceIndex));
                result.indices.clear();
                return result;
            }
            result.indices.insert(result.indices.end(), face.begin(), face.end());
            continue;
        }
        ProjectionFrame frame;
        if (!buildFrameFromNormal(mesh.vertices[face[0]], computeFaceNormalRaw(mesh, face), frame))
        {
            result.messages.push_back("Degenerate source face " + std::to_string(faceIndex));
            result.indices.clear();
            return result;
        }
        float deviation = 0, diameter = 0;
        for (auto i : face)
        {
            const auto delta = subtract3(mesh.vertices[i], frame.origin);
            deviation = std::max(deviation, std::abs(dot3(delta, frame.normal)));
            diameter = std::max(diameter, length3(delta));
        }
        // A curved quad has no unique polygon interior. Its source surface is
        // explicitly defined by the historical face[0] diagonal; projection
        // methods must either meet their eligibility checks or retain it.
        if (face.size() == 4 && deviation > std::max(1e-7f, diameter * 1e-5f))
        {
            for (size_t i = 1; i + 1 < face.size(); ++i)
            {
                if (length3(cross3(
                        subtract3(mesh.vertices[face[i]], mesh.vertices[face[0]]),
                        subtract3(mesh.vertices[face[i + 1]], mesh.vertices[face[0]])
                    )) <= 1e-12f)
                {
                    result.messages.push_back("Degenerate source quad " + std::to_string(faceIndex));
                    result.indices.clear();
                    return result;
                }
                result.indices.insert(result.indices.end(), {face[0], face[i], face[i + 1]});
            }
            continue;
        }
        if (face.size() > 4 && deviation > std::max(1e-7f, diameter * 1e-5f))
        {
            result.messages.push_back("Nonplanar n-gon requires an explicit triangulation: " + std::to_string(faceIndex));
            result.indices.clear();
            return result;
        }
        std::vector<Vertex> vertices;
        std::vector<uint32_t> boundary, localIndices;
        for (auto i : face)
        {
            Vertex vertex;
            vertex.pos = projectToFrame(mesh.vertices[i], frame);
            boundary.push_back(uint32_t(vertices.size()));
            vertices.push_back(vertex);
        }
        if (!ProjectedTriangulation::triangulatePolygon(localIndices, vertices, boundary))
        {
            result.messages.push_back("Source face is not a simple triangulable polygon: " + std::to_string(faceIndex));
            result.indices.clear();
            return result;
        }
        for (auto i : localIndices)
            result.indices.push_back(face[i]);
    }
    result.success = !result.indices.empty();
    result.status = result.success ? ResultStatus::Complete : ResultStatus::Failed;
    return result;
}

MeshTriangulationResult triangulatePreparedMesh(
    const Mesh& mesh,
    const MeshTriangulationResult& baseline,
    const std::vector<Patch>& patches,
    const TriangulationOptions& options
)
{
    const auto& source = baseline;
    if (!source.success)
        return source;
    MeshTriangulationResult result;
    result.vertices = mesh.vertices;
    result.patches = patches;
    std::vector<bool> covered(source.indices.size() / 3, false);
    auto rollback = [&](const std::string& message)
    {
        auto retained = source;
        retained.patches = patches;
        retained.messages.push_back(message);
        retained.failedPatchCount = 1;
        retained.status = ResultStatus::SourceFallback;
        return retained;
    };
    for (const auto& patch : patches)
    {
        for (auto face : patch.sourceTriangleIndices)
        {
            if (face >= covered.size() || covered[face])
                return rollback("Patch coverage is invalid.");
            covered[face] = true;
        }
        auto output = triangulatePatch(mesh, patch, options);
        if (!output.success)
            return rollback(output.message);
        if (output.vertices.size() != output.sourceVertexIndices.size())
            return rollback("Invalid vertex mapping.");
        std::vector<uint32_t> mapping(output.vertices.size(), kInvalidIndex);
        for (uint32_t i = 0; i < mapping.size(); ++i)
        {
            auto original = output.sourceVertexIndices[i];
            if (original == kInvalidIndex)
                return rollback("Unexpected Steiner vertex.");
            if (original >= mesh.vertices.size())
                return rollback("Invalid source vertex mapping.");
            mapping[i] = original;
        }
        for (auto i : output.indices)
        {
            if (i >= mapping.size())
                return rollback("Invalid local triangle index.");
            result.indices.push_back(mapping[i]);
        }
    }
    if (std::find(covered.begin(), covered.end(), false) != covered.end())
        return rollback("Source faces were not covered.");
    if (result.indices.size() != source.indices.size() || !MeshValidation::audit(result, source).topologyOK())
        return rollback("Assembled surface failed coverage or topology checks.");
    result.success = !result.indices.empty();
    result.status = result.success ? ResultStatus::Complete : ResultStatus::Failed;
    return result;
}

MeshTriangulationResult triangulatePreparedMesh(const Mesh& mesh, const std::vector<Patch>& patches, const TriangulationOptions& options)
{
    return triangulatePreparedMesh(mesh, useSourceMeshTriangulation(mesh), patches, options);
}

MeshTriangulationResult triangulateMesh(const Mesh& mesh, const PatchGrowthOptions& patchOptions, const TriangulationOptions& options)
{
    return triangulatePreparedMesh(mesh, growPatches(mesh, patchOptions), options);
}

} // namespace Triangulation3D
