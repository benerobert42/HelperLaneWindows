#include "Triangulation/MeshValidation.h"
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <numeric>
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
    a.edges = uint32_t(edges.size());
    a.euler = int64_t(a.used) - a.edges + int64_t(m.indices.size() / 3);
    std::vector<uint32_t> degree(m.vertices.size());
    std::vector<uint32_t> parent(m.vertices.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto root = [&](uint32_t v)
    {
        while (parent[v] != v) { parent[v] = parent[parent[v]]; v = parent[v]; }
        return v;
    };
    for (auto& [e, n] : edges)
    {
        parent[root(e.second)] = root(e.first);
        ++degree[e.first]; ++degree[e.second];
        if (n.first > 2)
            ++a.nonmanifold;
        if (n.first == 2 && n.second != 0)
            ++a.winding;
        if (n.first == 1 && (!original.count(e) || original.at(e).first != 1))
            ++a.unexpectedBoundary;
        a.edgeLength += length(m.vertices[e.first] - m.vertices[e.second]);
    }
    std::set<uint32_t> components;
    for (auto v : used) components.insert(root(v));
    a.components = uint32_t(components.size());
    const double meanDegree = a.used ? 2.0 * a.edges / a.used : 0;
    for (auto v : used)
    {
        a.maxValence = std::max(a.maxValence, degree[v]);
        a.valenceVariance += (degree[v] - meanDegree) * (degree[v] - meanDegree);
    }
    if (a.used) a.valenceVariance /= a.used;
    for (auto& [e, n] : original)
        if (n.first == 1 && !edges.count(e))
            ++a.missingBoundary;
    return a;
}
} // namespace MeshValidation
