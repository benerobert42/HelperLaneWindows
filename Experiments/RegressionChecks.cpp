#include "Import/ObjMeshLoader.h"
#include "Triangulation/2D/TriangulationHelpers.h"
#include "Experiments/BenchmarkGeometry.h"
#include "Triangulation/3D/SurfaceFlipExperiment.h"
#include "Triangulation/GeometryMath.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int runRegressionChecks()
{
    using namespace Triangulation3D;
    using namespace TriangulationCommon;
    try
    {
        std::vector<Vertex> projected(4);
        projected[0].pos = Falcor::float2(0, 0);
        projected[1].pos = Falcor::float2(2, 0);
        projected[2].pos = Falcor::float2(2, 1);
        projected[3].pos = Falcor::float2(0, 1);
        std::vector<Falcor::float3> surface = {{0, 0, 1}, {2, 0, 0}, {2, 1, -1}, {0, 1, 0}};
        const std::vector<uint32_t> diagonal = {0, 1, 2, 0, 2, 3};
        require(Triangulation::optimizeByMinLengthFlips(projected, diagonal) == diagonal, "Historical local flip metric preserved");
        auto surfaceFlip = Triangulation::optimizeByMinLengthFlips(projected, diagonal, -1, -1, &surface);
        require(surfaceFlip.size() == diagonal.size() && surfaceFlip != diagonal, "Explicit 3D metric flips the shorter surface diagonal");
        Mesh concave;
        for (auto p : std::vector<Falcor::float2>{{0, 0}, {3, 0}, {3, 3}, {2, 3}, {2, 1}, {1, 1}, {1, 3}, {0, 3}})
            concave.vertices.emplace_back(p.x, p.y, 0);
        concave.faces = {{0, 1, 2, 3, 4, 5, 6, 7}};
        auto triangles = useSourceMeshTriangulation(concave);
        require(triangles.success && triangles.indices.size() == 18, "Concave source polygon coverage");
        double area = 0;
        for (size_t i = 0; i < triangles.indices.size(); i += 3)
            area += 0.5 * length3(cross3(
                              subtract3(triangles.vertices[triangles.indices[i + 1]], triangles.vertices[triangles.indices[i]]),
                              subtract3(triangles.vertices[triangles.indices[i + 2]], triangles.vertices[triangles.indices[i]])
                          ));
        require(std::abs(area - 7) < 1e-6, "Concave polygon must not fill its notch");
        auto invalid = concave;
        invalid.faces.push_back({0, 1, 99});
        require(!useSourceMeshTriangulation(invalid).success, "Invalid face must fail whole source");
        invalid = concave;
        invalid.vertices[0].x = std::numeric_limits<float>::quiet_NaN();
        require(!triangulateMesh(invalid).success, "Nonfinite coordinates rejected before projection");
        auto fallback = triangulatePreparedMesh(concave, {});
        require(
            fallback.success && fallback.status == ResultStatus::SourceFallback && fallback.indices == triangles.indices,
            "Missing patches must retain complete source and report fallback"
        );
        const auto fixture = std::filesystem::path(HELPERLANE_SOURCE_DIR) / "analysis/review/helper_lane_obj_regression.obj";
        for (const auto* text :
             {"v 0 0 0\nv broken\nv 1 0 0\nv 0 1 0\nf 1 3 4\n",
              "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1junk 2 3\n",
              "v 0 0 1junk\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"})
        {
            std::ofstream(fixture) << text;
            auto loaded = ObjMeshLoader::load(fixture.string());
            require(!loaded.success && loaded.mesh.faces.empty() && loaded.sourceFaceCount == 0, "Malformed OBJ must fail without shifting position IDs");
        }
        std::ofstream(fixture) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3/1 -2/2 -1/3\n";
        require(ObjMeshLoader::load(fixture.string()).success, "OBJ negative position indices");
        std::filesystem::remove(fixture);
        for (const auto* name : {"sphere", "jittered", "torus_skew"})
        {
            auto mesh = BenchmarkGeometry::make(name);
            auto source = useSourceMeshTriangulation(mesh);
            if (!source.success)
            {
                std::cerr << name << ": " << (source.messages.empty() ? "no message" : source.messages.front()) << "\n";
            }
            require(source.success, "Procedural source polygon regression");
            auto output = triangulateMesh(mesh);
            require(output.success && output.indices.size() == source.indices.size(), "Surface coverage regression");
            auto flipped = SurfaceFlipExperiment::optimize(source, 0);
            require(
                flipped.mesh.indices.size() == source.indices.size() && flipped.mesh.vertices.size() == source.vertices.size(),
                "Guarded heap must preserve triangle and vertex counts"
            );
            std::cout << name << ": " << source.indices.size() / 3 << " triangles, " << flipped.flips << " flips, patch status "
                      << int(output.status) << "\n";
        }
        std::cout << "Regression checks passed.\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Regression check failed: " << e.what() << "\n";
        return 1;
    }
}
