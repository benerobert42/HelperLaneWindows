#pragma once
#include "Triangulation/3D/Triangulation3D.h"
namespace MeshValidation
{
struct Audit
{
    uint32_t duplicates = 0, nonmanifold = 0, winding = 0, missingBoundary = 0, unexpectedBoundary = 0, degenerate = 0, used = 0;
    double edgeLength = 0, area = 0;
    bool topologyOK() const { return !(duplicates || nonmanifold || winding || missingBoundary || unexpectedBoundary || degenerate); }
};
Audit audit(const Triangulation3D::MeshTriangulationResult&, const Triangulation3D::MeshTriangulationResult&);
} // namespace MeshValidation
