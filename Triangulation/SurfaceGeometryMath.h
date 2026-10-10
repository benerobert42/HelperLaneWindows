#pragma once
#include "Falcor.h"
#include <cmath>
// Double accumulation is intentional: these functions preserve the heap score precision.
namespace SurfaceGeometryMath
{
using V = Falcor::float3;
inline V add(V a, V b)
{
    return V(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline V sub(V a, V b)
{
    return V(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline V scale(V a, double s)
{
    return V(float(a.x * s), float(a.y * s), float(a.z * s));
}
inline double dot(V a, V b)
{
    return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
}
inline V cross(V a, V b)
{
    return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline double norm(V a)
{
    return std::sqrt(dot(a, a));
}
inline V unit(V a)
{
    double n = norm(a);
    return n > 1e-20 ? scale(a, 1 / n) : V(0);
}
inline double coord(V a, int k)
{
    return k == 0 ? a.x : k == 1 ? a.y : a.z;
}
} // namespace SurfaceGeometryMath
