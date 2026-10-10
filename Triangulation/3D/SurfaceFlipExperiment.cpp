#include "Triangulation/3D/SurfaceFlipExperiment.h"
#include "Triangulation/SurfaceGeometryMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>

namespace SurfaceFlipExperiment
{
namespace
{
using namespace SurfaceGeometryMath;
uint64_t key(uint32_t a, uint32_t b)
{
    if (a > b)
        std::swap(a, b);
    return (uint64_t(a) << 32) | b;
}

struct Adjacency
{
    int t[2] = {-1, -1};
    uint32_t opposite[2] = {0, 0};
    uint32_t version = 0;
};
struct Candidate
{
    double gain;
    uint64_t edge;
    uint32_t version;
    bool operator<(const Candidate& b) const { return gain < b.gain; }
};
struct Flip
{
    uint32_t a = 0, b = 0, c = 0, d = 0;
    int t0 = -1, t1 = -1;
    double gain = 0;
};
} // namespace

Result optimize(const Triangulation3D::MeshTriangulationResult& source, const ReferenceSurface& reference, double planePenalty)
{
    Result result;
    result.mesh = source;
    auto& indices = result.mesh.indices;
    const auto& vertices = source.vertices;
    if (indices.empty())
        return result;
    V lo = vertices.front(), hi = lo;
    for (V p : vertices)
    {
        lo = V(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
        hi = V(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    }
    const double errorLimit = norm(sub(hi, lo)) * 0.001; // 0.1% of fixed source bounds diagonal
    std::unordered_map<uint64_t, Adjacency> adjacency;
    adjacency.reserve(indices.size() * 2);
    auto updateTriangle = [&](int t, bool insert)
    {
        for (int j = 0; j < 3; ++j)
        {
            auto& e = adjacency[key(indices[3 * t + j], indices[3 * t + (j + 1) % 3])];
            ++e.version;
            if (insert)
            {
                int s = e.t[0] < 0 ? 0 : 1;
                e.t[s] = t;
                e.opposite[s] = indices[3 * t + (j + 2) % 3];
            }
            else
                for (int s = 0; s < 2; ++s)
                    if (e.t[s] == t)
                        e.t[s] = -1;
        }
    };
    for (int t = 0; t < int(indices.size() / 3); ++t)
        updateTriangle(t, true);
    auto evaluate = [&](uint64_t k, Flip& f, bool checkReference)
    {
        // First reject topology and local geometry failures. These checks are
        // cheap enough to run while inserting and refreshing heap candidates.
        const auto& e = adjacency.at(k);
        if (e.t[0] < 0 || e.t[1] < 0)
            return false;
        f.a = uint32_t(k >> 32);
        f.b = uint32_t(k);
        f.c = e.opposite[0];
        f.d = e.opposite[1];
        f.t0 = e.t[0];
        f.t1 = e.t[1];
        if (f.a == f.c || f.a == f.d || f.b == f.c || f.b == f.d || f.c == f.d)
            return false;
        auto existing = adjacency.find(key(f.c, f.d));
        if (existing != adjacency.end() && (existing->second.t[0] >= 0 || existing->second.t[1] >= 0))
            return false;
        bool forward = false;
        for (int j = 0; j < 3; ++j)
            if (indices[3 * f.t0 + j] == f.a && indices[3 * f.t0 + (j + 1) % 3] == f.b)
                forward = true;
        if (!forward)
            std::swap(f.a, f.b);
        bool reverse = false;
        for (int j = 0; j < 3; ++j)
            if (indices[3 * f.t1 + j] == f.b && indices[3 * f.t1 + (j + 1) % 3] == f.a)
                reverse = true;
        if (!reverse)
            return false;
        V a = vertices[f.a], b = vertices[f.b], c = vertices[f.c], d = vertices[f.d];
        V n0 = cross(sub(b, a), sub(c, a)), n1 = cross(sub(a, b), sub(d, b));
        if (norm(n0) < 1e-12 || norm(n1) < 1e-12 || dot(unit(n0), unit(n1)) < 0.8660254)
            return false; // protect >30 degree crease
        V normal = unit(add(n0, n1));
        double sideA = dot(cross(sub(d, c), sub(a, c)), normal), sideB = dot(cross(sub(d, c), sub(b, c)), normal);
        if (sideA * sideB >= -1e-20)
            return false;
        V new0 = cross(sub(d, c), sub(b, c)), new1 = cross(sub(c, d), sub(a, d));
        if (norm(new0) < 1e-12 || norm(new1) < 1e-12 || dot(unit(new0), normal) < 0.8660254 || dot(unit(new1), normal) < 0.8660254)
            return false;
        double oldLength = norm(sub(a, b)), newLength = norm(sub(c, d));
        V center = scale(add(add(a, b), add(c, d)), 0.25);
        double residual = std::max(
            {std::abs(dot(sub(a, center), normal)),
             std::abs(dot(sub(b, center), normal)),
             std::abs(dot(sub(c, center), normal)),
             std::abs(dot(sub(d, center), normal))}
        );
        f.gain = oldLength - newLength - planePenalty * residual;
        if (f.gain <= 1e-8 * std::max(oldLength, newLength))
            return false;
        if (checkReference)
        {
            // Query the fixed original surface only when accepting a popped
            // candidate. Comparing only with the previous triangles would
            // permit small per-flip errors to accumulate over many flips.
            for (V p : {scale(add(c, d), 0.5), scale(add(add(c, d), a), 1.0 / 3), scale(add(add(c, d), b), 1.0 / 3)})
                if (reference.distance(p) > errorLimit)
                    return false;
        }
        return true;
    };
    std::priority_queue<Candidate> queue;
    auto enqueue = [&](uint64_t k)
    {
        Flip f;
        if (evaluate(k, f, false))
            queue.push({f.gain, k, adjacency.at(k).version});
    };
    for (const auto& [k, e] : adjacency)
        enqueue(k);
    const uint32_t maxFlips = uint32_t(indices.size()), maxPops = uint32_t(indices.size() * 10);
    while (!queue.empty() && result.flips < maxFlips && result.pops < maxPops)
    {
        auto candidate = queue.top();
        queue.pop();
        ++result.pops;
        if (adjacency.at(candidate.edge).version != candidate.version)
            continue;
        // Adjacent flips can invalidate a queued edge's score or geometry.
        // Versions reject old entries; evaluation checks the current state.
        Flip f;
        if (!evaluate(candidate.edge, f, true))
            continue;
        updateTriangle(f.t0, false);
        updateTriangle(f.t1, false);
        std::array<uint32_t, 3> t0 = {f.c, f.d, f.b}, t1 = {f.d, f.c, f.a};
        for (int j = 0; j < 3; ++j)
        {
            indices[3 * f.t0 + j] = t0[j];
            indices[3 * f.t1 + j] = t1[j];
        }
        updateTriangle(f.t0, true);
        updateTriangle(f.t1, true);
        ++result.flips;
        for (int t : {f.t0, f.t1})
            for (int j = 0; j < 3; ++j)
                enqueue(key(indices[3 * t + j], indices[3 * t + (j + 1) % 3]));
    }
    return result;
}
Result optimize(const Triangulation3D::MeshTriangulationResult& source, double planePenalty)
{
    if (source.indices.empty())
        return {source, 0, 0};
    return optimize(source, ReferenceSurface(source), planePenalty);
}
double sampledReferenceError(
    const Triangulation3D::MeshTriangulationResult& source,
    const Triangulation3D::MeshTriangulationResult& candidate
)
{
    if (source.indices.empty())
        return std::numeric_limits<double>::infinity();
    return ReferenceSurface(source).sampledError(candidate);
}
} // namespace SurfaceFlipExperiment
