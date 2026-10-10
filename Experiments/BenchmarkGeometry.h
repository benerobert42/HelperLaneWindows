#pragma once
#include "Triangulation/3D/Triangulation3D.h"
#include <string>

namespace BenchmarkGeometry
{
// Experimental fixtures only. Existing generators remain in ProceduralGeometry3D.
// The *_skew inputs deliberately start with long edges; they are not claims
// about the typical connectivity of imported production assets.
Triangulation3D::Mesh make(const std::string& name);
} // namespace BenchmarkGeometry
