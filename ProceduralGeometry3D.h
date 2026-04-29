#pragma once

#include "Triangulation3D.h"

#include "Falcor.h"

#include <cstdint>

namespace ProceduralGeometry3D
{

Triangulation3D::Mesh makeSphere(
    uint32_t latitudeSegments,
    uint32_t longitudeSegments,
    float radius,
    const Falcor::float3& center = Falcor::float3(0.0f, 0.0f, 0.0f)
);

Triangulation3D::Mesh makeEllipsoid(
    uint32_t latitudeSegments,
    uint32_t longitudeSegments,
    const Falcor::float3& radii,
    const Falcor::float3& center = Falcor::float3(0.0f, 0.0f, 0.0f)
);

Triangulation3D::Mesh makeJitteredSphere(
    uint32_t latitudeSegments,
    uint32_t longitudeSegments,
    float radius,
    float radialJitter,
    float tangentialJitter,
    const Falcor::float3& center = Falcor::float3(0.0f, 0.0f, 0.0f)
);

Triangulation3D::Mesh makeLumpyEllipsoid(
    uint32_t latitudeSegments,
    uint32_t longitudeSegments,
    const Falcor::float3& radii,
    float radialAmplitude,
    const Falcor::float3& center = Falcor::float3(0.0f, 0.0f, 0.0f)
);

Triangulation3D::Mesh makeSaddlePatch(
    uint32_t widthSegments,
    uint32_t heightSegments,
    float size,
    float height,
    float jitter,
    const Falcor::float3& center = Falcor::float3(0.0f, 0.0f, 0.0f)
);

Triangulation3D::Mesh makeFoldedSheet(
    uint32_t widthSegments,
    uint32_t heightSegments,
    float size,
    float foldHeight,
    float ripple,
    float jitter,
    const Falcor::float3& center = Falcor::float3(0.0f, 0.0f, 0.0f)
);

Triangulation3D::Mesh makeClusteredWavePatch(
    uint32_t widthSegments,
    uint32_t heightSegments,
    float size,
    float height,
    float clusterStrength,
    const Falcor::float3& center = Falcor::float3(0.0f, 0.0f, 0.0f)
);

} // namespace ProceduralGeometry3D
