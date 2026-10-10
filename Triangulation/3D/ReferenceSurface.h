#pragma once
#include "Triangulation/3D/Triangulation3D.h"
#include <memory>
namespace SurfaceFlipExperiment
{
class ReferenceSurface
{
    struct Impl;
    std::shared_ptr<const Impl> mImpl;

public:
    explicit ReferenceSurface(const Triangulation3D::MeshTriangulationResult& source);
    double distance(Falcor::float3 point) const;
    double sampledError(const Triangulation3D::MeshTriangulationResult& candidate) const;
};
} // namespace SurfaceFlipExperiment
