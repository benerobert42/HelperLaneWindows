#pragma once
#include "Falcor.h"
#include "Triangulation/3D/Triangulation3D.h"
#include <ostream>

namespace RasterMetrics
{
// Pre-depth, unclipped geometry statistics, not hardware invocation counts.
struct Metrics
{
    uint64_t triangles = 0, outside = 0, subpixel = 0, belowFour = 0, thin = 0, sliver = 0;
    double area = 0, bboxArea = 0, edgeL1 = 0, edgeLength = 0, acmr = 0, atvr = 0;
};
Metrics measure(
    const Triangulation3D::MeshTriangulationResult& mesh,
    const Falcor::float4x4& model,
    const Falcor::float4x4& viewProj,
    uint32_t width, uint32_t height, uint32_t instances, uint32_t cols, float spacing
);
void writeHeader(std::ostream& out);
void write(std::ostream& out, uint32_t caseId, const std::string& ordering, const Metrics& metrics);
void regressionChecks();
} // namespace RasterMetrics
