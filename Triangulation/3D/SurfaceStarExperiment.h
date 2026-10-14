#pragma once
#include "SurfaceFlipExperiment.h"

namespace SurfaceStarExperiment
{
struct Result
{
    Triangulation3D::MeshTriangulationResult mesh;
    uint32_t replacements = 0;
};
// Replace a convex interior vertex star as one transaction. Dynamic programming
// triangulates its boundary and reinserts the original center into one triangle.
// Every vertex, boundary edge and triangle count is retained; genus is unchanged.
Result optimize(const Triangulation3D::MeshTriangulationResult& seed,
                const SurfaceFlipExperiment::ReferenceSurface& reference,
                SurfaceFlipExperiment::Constraints constraints = {});
} // namespace SurfaceStarExperiment
