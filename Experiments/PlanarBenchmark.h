#pragma once
#include "Triangulation/3D/Triangulation3D.h"
#include <string>

namespace PlanarBenchmark
{
// Original paper inputs and algorithms. SVG triangulation precedes the
// importer's historical per-axis normalization, just as in the GUI.
Triangulation3D::MeshTriangulationResult make(const std::string& shape, const std::string& method, float bezierDeviation);
struct VertexStream
{
    std::vector<Falcor::float2> vertices;
    std::vector<uint32_t> indices;
};
// Remap GPU storage without changing the canonical mesh used by audits.
VertexStream prepareVertexStream(const Triangulation3D::MeshTriangulationResult& mesh);
}
