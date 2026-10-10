#include "Triangulation/MeshValidation.h"
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace MeshValidation
{
using namespace Falcor;
using MeshResult = Triangulation3D::MeshTriangulationResult;
using Edge = std::pair<uint32_t, uint32_t>;
Edge edge(uint32_t a, uint32_t b)
{
    return {std::min(a, b), std::max(a, b)};
}
double length(float3 p)
{
    return std::sqrt(double(p.x) * p.x + double(p.y) * p.y + double(p.z) * p.z);
}
std::map<Edge, std::pair<uint32_t, int>> edgeMap(const MeshResult& m)
{
    std::map<Edge, std::pair<uint32_t, int>> edges;
    for (size_t i = 0; i < m.indices.size(); i += 3)
        for (size_t k = 0; k < 3; ++k)
        {
            uint32_t a = m.indices[i + k], b = m.indices[i + (k + 1) % 3];
            auto& e = edges[edge(a, b)];
            ++e.first;
            e.second += a < b ? 1 : -1;
        }
    return edges;
}
Audit audit(const MeshResult& m, const MeshResult& source)
{
    Audit a;
    auto check = [](const MeshResult& mesh)
    {
        if (mesh.indices.size() % 3)
            throw std::runtime_error("Incomplete triangle.");
        for (auto index : mesh.indices)
            if (index >= mesh.vertices.size())
                throw std::runtime_error("Invalid triangle index.");
        for (auto p : mesh.vertices)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                throw std::runtime_error("Nonfinite vertex.");
    };
    check(m);
    check(source);
    auto edges = edgeMap(m), original = edgeMap(source);
    std::set<uint32_t> used;
    std::set<std::array<uint32_t, 3>> tris;
    for (size_t i = 0; i < m.indices.size(); i += 3)
    {
        std::array<uint32_t, 3> t = {m.indices[i], m.indices[i + 1], m.indices[i + 2]};
        for (auto j : t)
        {
            if (j >= m.vertices.size())
                throw std::runtime_error("Invalid triangle index.");
            used.insert(j);
        }
        std::sort(t.begin(), t.end());
        if (!tris.insert(t).second)
            ++a.duplicates;
        float3 p = m.vertices[m.indices[i]], b = m.vertices[m.indices[i + 1]], c = m.vertices[m.indices[i + 2]];
        double area = 0.5 * length(math::cross(b - p, c - p));
        a.area += area;
        if (area < 1e-12)
            ++a.degenerate;
    }
    a.used = uint32_t(used.size());
    for (auto& [e, n] : edges)
    {
        if (n.first > 2)
            ++a.nonmanifold;
        if (n.first == 2 && n.second != 0)
            ++a.winding;
        if (n.first == 1 && (!original.count(e) || original.at(e).first != 1))
            ++a.unexpectedBoundary;
        a.edgeLength += length(m.vertices[e.first] - m.vertices[e.second]);
    }
    for (auto& [e, n] : original)
        if (n.first == 1 && !edges.count(e))
            ++a.missingBoundary;
    return a;
}
} // namespace MeshValidation
