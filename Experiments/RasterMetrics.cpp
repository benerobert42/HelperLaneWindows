#include "RasterMetrics.h"
#include "SampleSupport.h"
#include "meshoptimizer.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <stdexcept>

namespace RasterMetrics
{
using namespace Falcor;
Metrics measure(
    const Triangulation3D::MeshTriangulationResult& mesh,
    const float4x4& model, const float4x4& viewProj,
    uint32_t width, uint32_t height, uint32_t instances, uint32_t cols, float spacing
)
{
    Metrics m;
    // Existing meshoptimizer FIFO/warp simulation; no claim of hardware counters.
    const auto cache = meshopt_analyzeVertexCache(mesh.indices.data(), mesh.indices.size(), mesh.vertices.size(), 16, 32, 128);
    m.acmr = cache.acmr;
    m.atvr = cache.atvr;
    const auto edges = makeEdgeSet(mesh.indices);
    const uint32_t rows = (instances + cols - 1) / cols;
    std::vector<float2> screen(mesh.vertices.size());
    for (uint32_t instance = 0; instance < instances; ++instance)
    {
        const auto offset = getVolumeInstanceOffset(instance, cols, rows, 1, false, spacing);
        for (size_t i = 0; i < screen.size(); ++i)
            if (!projectToScreenSpace(viewProj, transformPointByMatrix(model, mesh.vertices[i]) + offset, width, height, screen[i]))
                throw std::runtime_error("Raster metrics require vertices in front of the camera.");
        for (auto [a, b] : edges)
        {
            const auto d = screen[a] - screen[b];
            m.edgeL1 += std::abs(double(d.x)) + std::abs(double(d.y));
            m.edgeLength += std::sqrt(double(d.x) * d.x + double(d.y) * d.y);
        }
        for (size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            const auto a = screen[mesh.indices[i]], b = screen[mesh.indices[i + 1]], c = screen[mesh.indices[i + 2]];
            const double area = 0.5 * std::abs(double(b.x - a.x) * (c.y - a.y) - double(b.y - a.y) * (c.x - a.x));
            auto squared = [](float2 d) { return double(d.x) * d.x + double(d.y) * d.y; };
            const double longest2 = std::max({squared(b - a), squared(c - b), squared(a - c)});
            const double minX = std::min({a.x, b.x, c.x}), maxX = std::max({a.x, b.x, c.x});
            const double minY = std::min({a.y, b.y, c.y}), maxY = std::max({a.y, b.y, c.y});
            ++m.triangles;
            m.outside += minX < 0 || minY < 0 || maxX > width || maxY > height;
            m.area += area;
            m.bboxArea += (maxX - minX) * (maxY - minY);
            m.subpixel += area < 1;
            m.belowFour += area < 4;
            m.thin += longest2 <= 1e-20 || 2 * area < std::sqrt(longest2); // minimum altitude below one pixel
            m.sliver += longest2 <= 1e-20 || 2 * area < 0.1 * longest2; // scale-independent shape ratio
        }
    }
    return m;
}
static void writeRow(std::ostream& out, uint32_t caseId, const std::string& ordering, const Metrics& m, bool header)
{
    out << std::setprecision(12);
    bool first = true;
    auto field = [&](const char* name, auto value)
    {
        if (!first) out << ',';
        if (header) out << name;
        else out << value;
        first = false;
    };
    field("Case", caseId);
    field("Ordering", ordering);
    field("ProjectedTriangles", m.triangles);
    field("OutsideViewportTriangles", m.outside);
    field("ProjectedArea_px2", m.area);
    field("ProjectedBBoxArea_px2", m.bboxArea);
    field("ProjectedEdgeL1_px", m.edgeL1);
    field("SubpixelTriangles", m.subpixel);
    field("BelowFourPxTriangles", m.belowFour);
    field("ThinTriangles", m.thin);
    field("SliverTriangles", m.sliver);
    field("CacheSimACMR", m.acmr);
    field("CacheSimATVR", m.atvr);
    out << '\n';
}
void writeHeader(std::ostream& out) { writeRow(out, 0, "", {}, true); }
void write(std::ostream& out, uint32_t caseId, const std::string& ordering, const Metrics& m) { writeRow(out, caseId, ordering, m, false); }
void regressionChecks()
{
    Triangulation3D::MeshTriangulationResult triangle;
    triangle.vertices = {{0, 0, 0}, {0.5f, 0, 0}, {0, 0.5f, 0}};
    triangle.indices = {0, 1, 2};
    const auto identity = float4x4::identity();
    const auto a = measure(triangle, identity, identity, 8, 8, 1, 1, 0);
    const auto b = measure(triangle, identity, identity, 16, 16, 2, 2, 0);
    if (a.triangles != 1 || a.area != 2 || a.bboxArea != 4 || a.edgeL1 != 8 || a.belowFour != 1 || a.subpixel != 0 ||
        a.outside != 0 || std::abs(a.acmr - 3) > 1e-6 || b.triangles != 2 || b.area != 8 * a.area || b.edgeL1 != 4 * a.edgeL1 ||
        std::abs(b.edgeLength - 4 * a.edgeLength) > 1e-9)
        throw std::runtime_error("Projected metrics must scale with resolution and instance count.");
}
} // namespace RasterMetrics
