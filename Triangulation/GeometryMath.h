#pragma once
#include "Falcor.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace TriangulationCommon
{
constexpr float kPi = 3.14159265358979323846f;
constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
struct EdgeKey
{
    uint32_t a = 0;
    uint32_t b = 0;

    bool operator==(const EdgeKey& other) const { return a == other.a && b == other.b; }
    bool operator<(const EdgeKey& other) const { return a < other.a || (a == other.a && b < other.b); }
};

struct EdgeKeyHash
{
    size_t operator()(const EdgeKey& key) const noexcept { return (static_cast<size_t>(key.a) << 32) ^ static_cast<size_t>(key.b); }
};

inline float dot3(const Falcor::float3& a, const Falcor::float3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Falcor::float3 add3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline Falcor::float3 subtract3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline Falcor::float3 multiply3(const Falcor::float3& v, float scale)
{
    return Falcor::float3(v.x * scale, v.y * scale, v.z * scale);
}

inline Falcor::float3 cross3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline float length3(const Falcor::float3& v)
{
    return std::sqrt(dot3(v, v));
}

inline float distanceSquared2(const Falcor::float2& a, const Falcor::float2& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline float distanceSquared3(const Falcor::float3& a, const Falcor::float3& b)
{
    const Falcor::float3 delta = subtract3(a, b);
    return dot3(delta, delta);
}

inline Falcor::float3 normalize3(const Falcor::float3& v)
{
    const float len = length3(v);
    if (!std::isfinite(len) || len <= 1e-12f)
    {
        return Falcor::float3(0.0f, 0.0f, 0.0f);
    }
    return multiply3(v, 1.0f / len);
}

inline EdgeKey makeEdgeKey(uint32_t a, uint32_t b)
{
    if (a > b)
    {
        std::swap(a, b);
    }
    return EdgeKey{a, b};
}

} // namespace TriangulationCommon
