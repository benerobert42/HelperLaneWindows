#include "Triangulation/3D/ReferenceSurface.h"
#include "Triangulation/SurfaceGeometryMath.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_set>
#include <stdexcept>
namespace SurfaceFlipExperiment
{
using namespace SurfaceGeometryMath;
double pointTriangleDistance2(V p, V a, V b, V c)
{
    V ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return dot(ap, ap);
    V bp = sub(p, b);
    double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return dot(bp, bp);
    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
    {
        V q = sub(p, add(a, scale(ab, d1 / (d1 - d3))));
        return dot(q, q);
    }
    V cp = sub(p, c);
    double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return dot(cp, cp);
    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
    {
        V q = sub(p, add(a, scale(ac, d2 / (d2 - d6))));
        return dot(q, q);
    }
    double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
    {
        V q = sub(p, add(b, scale(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)))));
        return dot(q, q);
    }
    double denominator = va + vb + vc;
    if (std::abs(denominator) < 1e-30)
        return std::min({dot(ap, ap), dot(bp, bp), dot(cp, cp)});
    V q = sub(p, add(a, add(scale(ab, vb / denominator), scale(ac, vc / denominator))));
    return dot(q, q);
}

struct ReferenceSurface::Impl
{
    struct Node
    {
        V lo = V(0), hi = V(0);
        uint32_t begin = 0, end = 0;
        int left = -1, right = -1;
    };
    Triangulation3D::MeshTriangulationResult m;
    std::vector<uint32_t> order;
    std::vector<Node> nodes;
    int build(uint32_t begin, uint32_t end)
    {
        int index = int(nodes.size());
        nodes.push_back({});
        V lo = V(std::numeric_limits<float>::max()), hi = V(-std::numeric_limits<float>::max());
        for (uint32_t i = begin; i < end; ++i)
            for (int k = 0; k < 3; ++k)
            {
                V p = m.vertices[m.indices[3 * order[i] + k]];
                lo = V(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
                hi = V(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
            }
        nodes[index].lo = lo;
        nodes[index].hi = hi;
        nodes[index].begin = begin;
        nodes[index].end = end;
        if (end - begin > 8)
        {
            V extent = sub(hi, lo);
            int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
            uint32_t mid = (begin + end) / 2;
            auto centroid = [&](uint32_t t)
            {
                return coord(
                    add(add(m.vertices[m.indices[3 * t]], m.vertices[m.indices[3 * t + 1]]), m.vertices[m.indices[3 * t + 2]]), axis
                );
            };
            std::nth_element(
                order.begin() + begin,
                order.begin() + mid,
                order.begin() + end,
                [&](uint32_t a, uint32_t b) { return centroid(a) < centroid(b); }
            );
            int left = build(begin, mid), right = build(mid, end);
            nodes[index].left = left;
            nodes[index].right = right;
        }
        return index;
    }
    double bound(V p, const Node& n) const
    {
        double x = std::max({double(n.lo.x - p.x), 0.0, double(p.x - n.hi.x)});
        double y = std::max({double(n.lo.y - p.y), 0.0, double(p.y - n.hi.y)});
        double z = std::max({double(n.lo.z - p.z), 0.0, double(p.z - n.hi.z)});
        return x * x + y * y + z * z;
    }
    void query(V p, int i, double& best) const
    {
        const auto& n = nodes[i];
        if (bound(p, n) > best)
            return;
        if (n.left < 0)
        {
            for (uint32_t j = n.begin; j < n.end; ++j)
            {
                uint32_t t = order[j];
                best = std::min(
                    best,
                    pointTriangleDistance2(
                        p, m.vertices[m.indices[3 * t]], m.vertices[m.indices[3 * t + 1]], m.vertices[m.indices[3 * t + 2]]
                    )
                );
            }
        }
        else
        {
            int a = n.left, b = n.right;
            if (bound(p, nodes[a]) > bound(p, nodes[b]))
                std::swap(a, b);
            query(p, a, best);
            query(p, b, best);
        }
    }

public:
    Impl(const Triangulation3D::MeshTriangulationResult& source) : m(source)
    {
        order.resize(m.indices.size() / 3);
        std::iota(order.begin(), order.end(), 0u);
        nodes.reserve(order.size() * 2);
        build(0, uint32_t(order.size()));
    }
    double distance(V p) const
    {
        double best = std::numeric_limits<double>::max();
        query(p, 0, best);
        return std::sqrt(best);
    }
};

ReferenceSurface::ReferenceSurface(const Triangulation3D::MeshTriangulationResult& source)
{
    if (source.indices.empty() || source.indices.size() % 3)
        throw std::runtime_error("Invalid reference triangle list.");
    for (auto i : source.indices)
        if (i >= source.vertices.size())
            throw std::runtime_error("Invalid reference vertex index.");
    mImpl = std::make_shared<Impl>(source);
}
double ReferenceSurface::distance(Falcor::float3 point) const
{
    return mImpl->distance(point);
}
double ReferenceSurface::sampledError(const Triangulation3D::MeshTriangulationResult& candidate) const
{
    if (candidate.indices.empty() || candidate.indices.size() % 3)
        return std::numeric_limits<double>::infinity();
    double error = 0;
    std::unordered_set<uint32_t> vertices;
    std::unordered_set<uint64_t> edges;
    for (size_t i = 0; i < candidate.indices.size(); i += 3)
    {
        uint32_t triangle[3] = {candidate.indices[i], candidate.indices[i + 1], candidate.indices[i + 2]};
        for (size_t k = 0; k < 3; ++k)
        {
            uint32_t a = triangle[k], b = triangle[(k + 1) % 3];
            if (a >= candidate.vertices.size() || b >= candidate.vertices.size())
                return std::numeric_limits<double>::infinity();
            if (vertices.insert(a).second)
                error = std::max(error, distance(candidate.vertices[a]));
            uint64_t edge = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
            if (edges.insert(edge).second)
                error = std::max(error, distance(scale(add(candidate.vertices[a], candidate.vertices[b]), 0.5)));
        }
        error = std::max(
            error,
            distance(
                scale(add(add(candidate.vertices[triangle[0]], candidate.vertices[triangle[1]]), candidate.vertices[triangle[2]]), 1.0 / 3)
            )
        );
    }
    return error;
}
} // namespace SurfaceFlipExperiment
