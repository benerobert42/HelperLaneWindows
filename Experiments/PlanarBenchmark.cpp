#include "PlanarBenchmark.h"
#include "Import/SVGLoader.h"
#include "Triangulation/2D/TriangulationHelpers.h"
#include "meshoptimizer.h"
#include <stdexcept>

namespace PlanarBenchmark
{
VertexStream prepareVertexStream(const Triangulation3D::MeshTriangulationResult& mesh)
{
    std::vector<Falcor::float2> original;
    for (auto p : mesh.vertices) original.emplace_back(p.x, p.y);
    VertexStream stream;
    stream.vertices.resize(original.size());
    stream.indices = mesh.indices;
    const auto used = meshopt_optimizeVertexFetch(stream.vertices.data(), stream.indices.data(), stream.indices.size(),
                                                 original.data(), original.size(), sizeof(Falcor::float2));
    if (used != original.size())
        throw std::runtime_error("Planar fetch optimization must retain every input vertex.");
    for (size_t i = 0; i < stream.indices.size(); ++i)
    {
        const auto a = stream.vertices.at(stream.indices[i]), b = original.at(mesh.indices[i]);
        if (a.x != b.x || a.y != b.y)
            throw std::runtime_error("Vertex-fetch remapping changed planar geometry.");
    }
    return stream;
}

Triangulation3D::MeshTriangulationResult make(const std::string& shape, const std::string& method, float bezierDeviation)
{
    const bool procedural = shape == "circle" || shape == "ellipse";
    auto triangulate = [&](std::vector<Vertex>& vertices)
    {
        if (method == "Earcut") return Triangulation::earClippingMapbox(vertices);
        if (method == "EarcutFlip") return Triangulation::earClippingMapboxFlipped(vertices);
        if (method == "CDT") return Triangulation::constrainedDelaunay(vertices);
        if (method == "CDTFlip") return Triangulation::constrainedDelaunayFlipped(vertices);
        if (method == "GMA") return Triangulation::greedyMaxAreaTriangulation(vertices, !procedural);
        if (method == "MWT") return Triangulation::minimumWeightTriangulation(vertices, !procedural);
        throw std::runtime_error("Unknown planar paper method: " + method);
    };
    std::vector<Vertex> vertices;
    Triangulation3D::MeshTriangulationResult mesh;
    if (procedural)
    {
        vertices = Triangulation::CreateVerticesForEllipse(512, 0.4f, shape == "circle" ? 0.4f : 0.2f, Falcor::float2(0.5f));
        mesh.indices = triangulate(vertices);
    }
    else
    {
        if (shape.rfind("svg:", 0) != 0)
            throw std::runtime_error("Planar input must be circle, ellipse or svg:<original file path>: " + shape);
        if (!SVGLoader::TessellateSvgToMesh(shape.substr(4), vertices, mesh.indices, triangulate, bezierDeviation))
            throw std::runtime_error("Could not load planar paper input: " + shape);
    }
    for (const auto& vertex : vertices)
        mesh.vertices.emplace_back(vertex.pos.x, vertex.pos.y, 0.f);
    mesh.success = !mesh.indices.empty();
    mesh.status = mesh.success ? Triangulation3D::ResultStatus::Complete : Triangulation3D::ResultStatus::Failed;
    return mesh;
}
}
