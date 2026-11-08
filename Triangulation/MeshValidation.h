#pragma once
#include "Triangulation/3D/Triangulation3D.h"
namespace MeshValidation
{
inline constexpr double kMinimumTriangleArea = 1e-12;
inline double triangleArea(Falcor::float3 a, Falcor::float3 b, Falcor::float3 c)
{
    return 0.5 * Falcor::math::length(Falcor::math::cross(b - a, c - a));
}
struct Audit
{
    uint32_t duplicates = 0, nonmanifold = 0, winding = 0, missingBoundary = 0, unexpectedBoundary = 0, degenerate = 0, used = 0;
    double edgeLength = 0, area = 0;
    uint32_t edges = 0, components = 0;
    int64_t euler = 0;
    uint32_t maxValence = 0;
    double valenceVariance = 0;
    bool topologyOK() const { return !(duplicates || nonmanifold || winding || missingBoundary || unexpectedBoundary || degenerate); }
};
Audit audit(const Triangulation3D::MeshTriangulationResult&, const Triangulation3D::MeshTriangulationResult&);
// Protect existing defective neighborhoods; preserve their exact triangle indices.
std::vector<uint8_t> protectedSourceVertices(const Triangulation3D::MeshTriangulationResult& source);
bool preservesSourceDefects(const Triangulation3D::MeshTriangulationResult& candidate,
                           const Triangulation3D::MeshTriangulationResult& source, const Audit& candidateAudit,
                           const Audit& sourceAudit, const std::vector<uint8_t>& protectedVertices);
} // namespace MeshValidation
