#include "Import/ObjMeshLoader.h"
#include "Import/ModelPresets.h"
#include "Triangulation/MeshValidation.h"
#include "Triangulation/2D/TriangulationHelpers.h"
#include "Experiments/BenchmarkGeometry.h"
#include "Experiments/RasterMetrics.h"
#include "Experiments/BenchmarkOptions.h"
#include "Triangulation/3D/SurfaceFlipExperiment.h"
#include "Triangulation/3D/SurfaceStarExperiment.h"
#include "Triangulation/GeometryMath.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>

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
        RasterMetrics::regressionChecks();
        BenchmarkOptions::Options sweep;
        sweep.axisSteps = 8;
        for (uint32_t axis = 0; axis < 3; ++axis)
            for (uint32_t step = 0; step < 8; ++step)
            {
                const auto angles = BenchmarkOptions::rotationDegrees(sweep, axis * 8 + step);
                for (uint32_t component = 0; component < 3; ++component)
                    require(angles[component] == (component == axis ? step * 45.0 : 0), "Axis sweep must rotate just the requested axis");
            }
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
        Mesh quad;
        quad.vertices = {{0, 0, 0}, {2, 0, 0}, {3, 1, 0}, {1, 1, 0}};
        quad.faces = {{0, 1, 2}, {0, 2, 3}};
        auto quadSource = useSourceMeshTriangulation(quad);
        auto shortQuad = SurfaceFlipExperiment::optimize(quadSource, 0).mesh;
        require(shortQuad.indices != quadSource.indices, "World objective shortens a planar diagonal");
        SurfaceFlipExperiment::ReferenceSurface quadReference(quadSource);
        auto custom = SurfaceFlipExperiment::optimize(
            shortQuad, quadReference, 0, [&quad](uint32_t a, uint32_t b)
            {
                const auto d = quad.vertices[a] - quad.vertices[b];
                // Foreshortening reverses the diagonals' world-length order.
                return std::abs(double(d.x) - 2 * d.y);
            }
        );
        require(
            custom.flips == 1 && MeshValidation::audit(custom.mesh, quadSource).topologyOK(),
            "Projected edge cost can reverse a world-length flip while retaining topology"
        );
        require(quadReference.sampledError(custom.mesh) < 1e-6, "Alternative cost retains the fixed reference surface guard");
        Mesh fan;
        fan.vertices.push_back({0.3f, 0.03f, 0});
        for (uint32_t i = 0; i < 8; ++i)
        {
            double angle = 6.28318530718 * i / 8;
            fan.vertices.emplace_back(float(5 * std::cos(angle)), float(.3 * std::sin(angle)), 0);
        }
        for (uint32_t i = 0; i < 8; ++i) fan.faces.push_back({0, i + 1, (i + 1) % 8 + 1});
        auto fanSource = useSourceMeshTriangulation(fan);
        SurfaceFlipExperiment::ReferenceSurface fanReference(fanSource);
        auto star = SurfaceStarExperiment::optimize(fanSource, fanReference);
        auto fanAudit = MeshValidation::audit(fanSource, fanSource), starAudit = MeshValidation::audit(star.mesh, fanSource);
        require(star.replacements > 0 && star.mesh.indices.size() == fanSource.indices.size() &&
                star.mesh.vertices.size() == fanSource.vertices.size() && starAudit.used == fanAudit.used &&
                starAudit.topologyOK() && starAudit.edgeLength < fanAudit.edgeLength &&
                fanReference.sampledError(star.mesh) < 1e-6,
                "Star replacement must improve a fan while preserving every vertex, triangle and boundary");
        auto torusSource = useSourceMeshTriangulation(BenchmarkGeometry::make("torus"));
        Mesh sampledPanel;
        const std::array<Falcor::float3, 3> corners{{{.123f,.432f,.234f},{.811f,.317f,.552f},{.492f,.982f,.712f}}};
        sampledPanel.vertices.push_back(corners[0]*.321f+corners[1]*.417f+corners[2]*.262f);
        for (uint32_t side=0; side<3; ++side)
            for (uint32_t j=0; j<32; ++j)
                sampledPanel.vertices.push_back(corners[side]*(1.f-j/32.f)+corners[(side+1)%3]*(j/32.f));
        for (uint32_t j=0; j<96; ++j) sampledPanel.faces.push_back({0,j+1,(j+1)%96+1});
        const auto panelSource=useSourceMeshTriangulation(sampledPanel);
        const auto panelFlip=SurfaceFlipExperiment::optimize(panelSource,0);
        require(panelSource.success && panelFlip.flips>0 &&
                MeshValidation::audit(panelFlip.mesh,panelSource).topologyOK() &&
                panelFlip.mesh.indices.size()==panelSource.indices.size(),
                "Sampled straight panel edges must not create roundoff-degenerate flip triangles");
        auto splitTorus = BenchmarkGeometry::splitTorus(torusSource);
        auto torusAudit = MeshValidation::audit(torusSource, torusSource), splitAudit = MeshValidation::audit(splitTorus, torusSource);
        require(torusAudit.components == 1 && splitAudit.components == 2 && torusAudit.euler == 0 && splitAudit.euler == 0 &&
                splitAudit.topologyOK() && splitAudit.used == torusAudit.used && splitTorus.indices.size() == torusSource.indices.size(),
                "Seam surgery must produce two closed components with unchanged counts and Euler characteristic");
        auto skewGrid = BenchmarkGeometry::make("torus_skew");
        auto skewSource = useSourceMeshTriangulation(skewGrid);
        for (bool align : {false, true})
        {
            auto rewired = BenchmarkGeometry::rewriteGrid(skewGrid, skewSource, align);
            auto rewiredAudit = MeshValidation::audit(rewired, skewSource);
            require(rewiredAudit.topologyOK() && rewiredAudit.components == 1 && rewiredAudit.euler == 0 &&
                    rewiredAudit.used == skewSource.vertices.size() && rewired.indices.size() == skewSource.indices.size(),
                    "Global grid rewiring preserves used vertices, faces and surface topology");
        }
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
        Mesh tiny;
        tiny.vertices = {{0.3f, 0.4f, 0.5f}, {0.30001f, 0.4f, 0.5f}, {0.3f, 0.40001f, 0.5f}};
        tiny.faces = {{2, 1, 0}};
        const auto tinySource = useSourceMeshTriangulation(tiny);
        require(tinySource.success && tinySource.indices == tiny.faces[0] &&
                MeshValidation::audit(tinySource, tinySource).topologyOK(), "Small explicit triangles preserve winding and pass the 3D audit");
        tiny.vertices[2] = {0.30002f, 0.4f, 0.5f};
        require(!useSourceMeshTriangulation(tiny).success, "Collinear explicit triangles must still fail");
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
            require(
                !loaded.success && loaded.mesh.faces.empty() && loaded.sourceFaceCount == 0,
                "Malformed OBJ must fail without shifting position IDs"
            );
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
        for (const auto& model : {std::array<uint32_t, 3>{8, 7958, 15744}, {9, 2503, 4968}, {10, 1292, 2464},
                                 {11, 1538, 3072}, {12, 3072, 6144}, {13, 2114, 4224},
                                 {14, 24576, 49152}, {15, 7913, 15360}})
        {
            const auto loaded = ObjMeshLoader::load(getModelPresetPath(model[0]).string());
            require(loaded.success && loaded.mesh.vertices.size() == model[1], "Preset must load through the OBJ pipeline");
            const auto source = useSourceMeshTriangulation(loaded.mesh);
            require(source.success && source.indices.size() == model[2] * 3, "Preset Source Faces must preserve triangles");
            require(MeshValidation::audit(source, source).topologyOK(), "Preset source must pass the topology audit");
            std::cout << getModelPresetPath(model[0]).filename().string() << ": " << model[1] << " vertices, " << model[2]
                      << " triangles\n";
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
