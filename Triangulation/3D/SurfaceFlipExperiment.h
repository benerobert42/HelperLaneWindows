#pragma once
#include "Triangulation/3D/Triangulation3D.h"
#include "Triangulation/3D/ReferenceSurface.h"
#include <functional>

namespace SurfaceFlipExperiment
{
struct Result
{
    Triangulation3D::MeshTriangulationResult mesh;
    uint32_t flips = 0;
    uint32_t pops = 0;
};

// Experimental fixed-vertex optimizer. Both variants require manifold local
// topology, convexity, orientation, feature-angle and sampled reference-error
// checks. A finite set of error samples is not a Hausdorff-distance guarantee.
Result optimize(const Triangulation3D::MeshTriangulationResult& source, double planePenalty);
// Optional fixed edge cost changes the objective, never the surface safeguards.
Result optimize(
    const Triangulation3D::MeshTriangulationResult& source,
    const ReferenceSurface& reference,
    double planePenalty,
    const std::function<double(uint32_t, uint32_t)>& edgeCost = {}
);
double sampledReferenceError(
    const Triangulation3D::MeshTriangulationResult& source,
    const Triangulation3D::MeshTriangulationResult& candidate
);
} // namespace SurfaceFlipExperiment
