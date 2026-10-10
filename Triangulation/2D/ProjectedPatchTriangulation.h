#pragma once
#include "Triangulation/3D/Triangulation3D.h"
#include "Triangulation/ShaderTypes.h"
#include <unordered_map>

namespace ProjectedTriangulation
{
struct ProjectedPatch
{
    std::vector<uint32_t> localToGlobal;
    std::unordered_map<uint32_t, uint32_t> globalToLocal;
    std::vector<Vertex> projectedVertices;
    std::vector<Falcor::float3> originalVertices;
    std::vector<uint32_t> boundaryLoop;
    std::vector<std::pair<uint32_t, uint32_t>> boundaryEdges;
    mutable std::vector<int8_t> visibility;
    std::vector<float> lengths2D, lengths3D;
};

Triangulation3D::PatchTriangulationResult
triangulate(const ProjectedPatch&, const Triangulation3D::ProjectionFrame&, const Triangulation3D::TriangulationOptions&);
bool triangulatePolygon(std::vector<uint32_t>& indices, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& boundary);
} // namespace ProjectedTriangulation
