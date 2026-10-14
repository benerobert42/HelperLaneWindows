#pragma once
#include "Triangulation/3D/Triangulation3D.h"
#include <string>

namespace BenchmarkGeometry
{
Triangulation3D::MeshTriangulationResult splitTorus(const Triangulation3D::MeshTriangulationResult& source);
Triangulation3D::MeshTriangulationResult rewriteGrid(const Triangulation3D::Mesh& input,
    const Triangulation3D::MeshTriangulationResult& source, bool align);
// Experimental fixtures only. Existing generators remain in ProceduralGeometry3D.
// The *_skew inputs deliberately start with long edges; they are not claims
// about the typical connectivity of imported production assets.
Triangulation3D::Mesh make(const std::string& name);
} // namespace BenchmarkGeometry
