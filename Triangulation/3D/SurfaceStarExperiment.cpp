#include "SurfaceStarExperiment.h"
#include "Triangulation/SurfaceGeometryMath.h"
#include <array>
#include <map>
#include <set>
#include <limits>
#include <algorithm>
#include <functional>

namespace SurfaceStarExperiment
{
namespace
{
using namespace SurfaceGeometryMath;
using Triangle = std::array<uint32_t, 3>;
using Edge = std::pair<uint32_t, uint32_t>;
Edge edge(uint32_t a, uint32_t b) { return std::minmax(a, b); }
struct Cell
{
    double cost = std::numeric_limits<double>::infinity();
    int split = -1, left = 0, right = 0, center = 0;
};
} // namespace

Result optimize(const Triangulation3D::MeshTriangulationResult& seed,
                const SurfaceFlipExperiment::ReferenceSurface& reference,
                SurfaceFlipExperiment::Constraints constraints)
{
    Result result{seed};
    auto& indices = result.mesh.indices;
    const auto& positions = seed.vertices;
    if (indices.empty()) return result;
    V lo = positions.front(), hi = lo;
    for (auto p : positions) { lo = Falcor::math::min(lo, p); hi = Falcor::math::max(hi, p); }
    const double errorLimit = norm(sub(hi, lo)) * constraints.relativeError;
    std::vector<std::set<uint32_t>> incident(positions.size());
    std::map<Edge, uint32_t> edges;
    // Positions and reference remain fixed across all sweeps. Cache only these
    // immutable distance tests; adjacency and normal tests are always current.
    std::map<Edge, bool> midpointOK;
    std::map<Triangle, bool> centroidOK;
    auto referenceOK = [&](uint32_t a, uint32_t b, uint32_t c)
    {
        for (auto e : {edge(a, b), edge(b, c), edge(c, a)})
        {
            auto it = midpointOK.find(e);
            if (it == midpointOK.end())
                it = midpointOK.emplace(e, reference.distance(scale(add(positions[e.first], positions[e.second]), .5)) <= errorLimit).first;
            if (!it->second) return false;
        }
        Triangle key{a, b, c};
        std::sort(key.begin(), key.end());
        auto it = centroidOK.find(key);
        if (it == centroidOK.end())
            it = centroidOK.emplace(key, reference.distance(scale(add(add(positions[a], positions[b]), positions[c]), 1.0 / 3)) <= errorLimit).first;
        return it->second;
    };
    auto update = [&](uint32_t t, bool insert)
    {
        for (int j = 0; j < 3; ++j)
        {
            auto v = indices[t * 3 + j], w = indices[t * 3 + (j + 1) % 3];
            if (insert) { incident[v].insert(t); ++edges[edge(v, w)]; }
            else { incident[v].erase(t); --edges.at(edge(v, w)); }
        }
    };
    for (uint32_t t = 0; t < indices.size() / 3; ++t) update(t, true);
    // Bounded deterministic sweeps, rather than another persistent heap/state machine.
    for (uint32_t pass = 0; pass < 3; ++pass)
    {
        uint32_t changed = 0;
        for (uint32_t center = 0; center < positions.size(); ++center)
        {
            const std::vector<uint32_t> triangles(incident[center].begin(), incident[center].end());
            const int n = int(triangles.size());
            if (n < 4 || n > 12) continue;
            std::map<uint32_t, uint32_t> next;
            std::map<Edge, uint32_t> localEdges;
            V normal(0);
            double oldCost = 0;
            bool valid = true;
            for (auto t : triangles)
            {
                for (int j = 0; j < 3; ++j)
                {
                    auto a = indices[t * 3 + j], b = indices[t * 3 + (j + 1) % 3];
                    ++localEdges[edge(a, b)];
                    oldCost += norm(sub(positions[a], positions[b]));
                    if (a == center && !next.emplace(b, indices[t * 3 + (j + 2) % 3]).second) valid = false;
                }
                auto a = positions[indices[t * 3]], b = positions[indices[t * 3 + 1]], c = positions[indices[t * 3 + 2]];
                normal = add(normal, cross(sub(b, a), sub(c, a)));
            }
            if (!valid || next.size() != n) continue;
            std::vector<uint32_t> ring;
            uint32_t v = next.begin()->first;
            for (int i = 0; i < n && next.count(v); ++i) { ring.push_back(v); v = next.at(v); }
            if (ring.size() != n || v != ring.front() || std::set<uint32_t>(ring.begin(), ring.end()).size() != n) continue;
            normal = unit(normal);
            for (int i = 0; i < n; ++i)
            {
                auto a = positions[ring[i]], b = positions[ring[(i + 1) % n]], c = positions[ring[(i + 2) % n]];
                if (dot(cross(sub(b, a), sub(c, b)), normal) <= 1e-20) valid = false;
                auto oldNormal = cross(sub(a, positions[center]), sub(b, positions[center]));
                if (dot(unit(oldNormal), normal) < constraints.minNormalDot) valid = false;
            }
            if (!valid) continue; // Require a convex projected disk and protect creases.
            auto triangleCost = [&](uint32_t a, uint32_t b, uint32_t c)
            {
                auto x = positions[a], y = positions[b], z = positions[c];
                V newNormal = cross(sub(y, x), sub(z, x));
                if (!hasStableTriangleArea(x, y, z) || dot(unit(newNormal), normal) < constraints.minNormalDot)
                    return std::numeric_limits<double>::infinity();
                // New interior edges may not already belong to triangles outside this disk.
                for (auto e : {edge(a, b), edge(b, c), edge(c, a)})
                    if ((!localEdges.count(e) || localEdges.at(e) != 1) && edges.count(e) &&
                        edges.at(e) > (localEdges.count(e) ? localEdges.at(e) : 0))
                        return std::numeric_limits<double>::infinity();
                if (!referenceOK(a, b, c)) return std::numeric_limits<double>::infinity();
                return norm(sub(x, y)) + norm(sub(y, z)) + norm(sub(z, x));
            };
            std::array<std::array<std::array<Cell, 2>, 12>, 12> dp;
            for (int i = 0; i < n - 1; ++i) dp[i][i + 1][0].cost = 0;
            for (int width = 2; width < n; ++width)
                for (int i = 0, j = width; j < n; ++i, ++j)
                    for (int k = i + 1; k < j; ++k)
                    {
                        uint32_t a = ring[i], b = ring[k], c = ring[j];
                        double ordinary = triangleCost(a, b, c);
                        double inserted = triangleCost(a, b, center) + triangleCost(b, c, center) + triangleCost(c, a, center);
                        for (int l = 0; l <= 1; ++l)
                            for (int r = 0; r + l <= 1; ++r)
                                for (int middle = 0; middle + l + r <= 1; ++middle)
                                {
                                    double cost = dp[i][k][l].cost + dp[k][j][r].cost + (middle ? inserted : ordinary);
                                    auto& cell = dp[i][j][l + r + middle];
                                    if (cost < cell.cost) cell = {cost, k, l, r, middle};
                                }
                    }
            if (!(dp[0][n - 1][1].cost < oldCost - 1e-8 * oldCost)) continue;
            std::vector<Triangle> replacement;
            std::function<void(int, int, int)> emit = [&](int i, int j, int hasCenter)
            {
                if (j == i + 1) return;
                const auto& cell = dp[i][j][hasCenter];
                int k = cell.split;
                emit(i, k, cell.left); emit(k, j, cell.right);
                if (cell.center)
                {
                    replacement.push_back({ring[i], ring[k], center});
                    replacement.push_back({ring[k], ring[j], center});
                    replacement.push_back({ring[j], ring[i], center});
                }
                else replacement.push_back({ring[i], ring[k], ring[j]});
            };
            emit(0, n - 1, 1);
            if (replacement.size() != triangles.size()) continue;
            for (auto t : triangles) update(t, false);
            for (size_t i = 0; i < triangles.size(); ++i)
                for (int j = 0; j < 3; ++j) indices[triangles[i] * 3 + j] = replacement[i][j];
            for (auto t : triangles) update(t, true);
            ++result.replacements; ++changed;
        }
        if (!changed) break;
    }
    return result;
}
} // namespace SurfaceStarExperiment
