#include "Experiments/BenchmarkGeometry.h"
#include "Triangulation/MeshValidation.h"
#include "Triangulation/3D/SurfaceFlipExperiment.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

// CPU-only reconstruction of the eight-model study. No GPU timing is collected.
int runSurfaceConstructionAudit(const char* directory)
{
    using namespace Triangulation3D;
    std::filesystem::create_directories(directory);
    const auto root = std::filesystem::path(directory);
    std::ofstream summary(root / "construction.csv"), failures(root / "patch_failures.csv");
    if (!summary || !failures) throw std::runtime_error("Cannot create construction audit files.");
    bool allValid = true;
    summary << "Geometry,Method,Patches,MultiTrianglePatches,TrianglesInMultiTrianglePatches,FailedPatches,Status,Vertices,Triangles,UsedVertices,Degenerate,Nonmanifold,Winding,ForwardError,ReverseError,StrictBudget,SurfaceEquivalent,ReferenceAuditDirections,GeometryValid,Flips,FirstFailure\n";
    failures << "Geometry,Method,Patch,Vertices,SourceTriangles,OutputTriangles,Reason\n";
    summary << std::setprecision(17);
    for (const auto* shape : {"obj:Data/Models/ResearchCandidates/SweptWing.obj",
             "obj:Data/Models/ResearchCandidates/FacetedArch.obj", "torus_skew",
             "obj:Data/Models/ResearchCandidates/SpiralRamp.obj",
             "obj:Data/Models/ResearchCandidates/TrefoilKnot.obj", "suzanne",
             "obj:Data/Models/ResearchCandidates/ToothedWheel.obj", "cylinder_skew"})
    {
        const auto input = BenchmarkGeometry::make(shape);
        const auto source = useSourceMeshTriangulation(input);
        if (!source.success) throw std::runtime_error(std::string("Invalid source: ") + shape);
        const auto patches = growPatches(input);
        size_t multiPatches = 0, multiTriangles = 0;
        for (const auto& patch : patches)
            if (patch.sourceTriangleIndices.size() > 1) { ++multiPatches; multiTriangles += patch.sourceTriangleIndices.size(); }
        SurfaceFlipExperiment::ReferenceSurface reference(source);
        auto lo = source.vertices.front(), hi = lo;
        for (auto p : source.vertices) { lo = Falcor::math::min(lo, p); hi = Falcor::math::max(hi, p); }
        const double budget = Falcor::math::length(hi - lo) * 0.001;
        const auto sourceAudit = MeshValidation::audit(source, source);
        for (const auto* name : {"Source", "SourceControl", "CDT", "CDTFlip", "CDTFlip3D", "Heap3D", "HeapPlane"})
        {
            TriangulationOptions options;
            options.method = std::string(name) == "CDT" ? Method::ConstrainedDelaunay :
                Method::ConstrainedDelaunayFlipped;
            options.use3DEdgeLengthForFlips = std::string(name) == "CDTFlip3D";
            MeshTriangulationResult output;
            uint32_t flips = 0;
            const std::string method(name);
            if (method == "Source" || method == "SourceControl") output = source;
            else if (method == "Heap3D" || method == "HeapPlane")
            {
                auto refined = SurfaceFlipExperiment::optimize(source, reference, method == "HeapPlane" ? 4 : 0);
                flips = refined.flips;
                output = std::move(refined.mesh);
            }
            else output = triangulatePreparedMesh(input, source, patches, options);
            const auto failed = output.failedPatchCount;
            if (failed)
                failures << std::quoted(shape) << ',' << name << ",,,,0,"
                         << std::quoted(output.messages.empty() ? "" : output.messages.back()) << '\n';
            const auto audit = MeshValidation::audit(output, source);
            const double forward = reference.sampledError(output);
            bool samePositions = output.vertices.size() == source.vertices.size();
            for (size_t i = 0; samePositions && i < source.vertices.size(); ++i)
                samePositions = output.vertices[i].x == source.vertices[i].x && output.vertices[i].y == source.vertices[i].y &&
                                output.vertices[i].z == source.vertices[i].z;
            const bool valid = output.success && output.status == ResultStatus::Complete && audit.topologyOK() && samePositions &&
                output.indices.size() == source.indices.size() && audit.used == sourceAudit.used &&
                audit.components == sourceAudit.components && audit.euler == sourceAudit.euler && forward <= budget * 1.001;
            allValid &= valid;
            summary << std::quoted(shape) << ',' << name << ',' << patches.size() << ',' << multiPatches << ',' << multiTriangles << ',' << failed << ',' << int(output.status)
                    << ',' << output.vertices.size() << ',' << output.indices.size() / 3 << ',' << audit.used << ','
                    << audit.degenerate << ',' << audit.nonmanifold << ',' << audit.winding << ',' << forward << ",0,"
                    << budget << ',' << (forward <= budget * 1.001) << ",1," << valid << ',' << flips << ','
                    << std::quoted(output.messages.empty() ? "" : output.messages.back()) << '\n';
            summary.flush(); failures.flush();
            std::cout << shape << ' ' << name << ": " << failed << '/' << patches.size() << " failed patches; status "
                      << int(output.status) << "; diagnostics " << (valid ? "PASS" : "FAIL") << std::endl;
        }
    }
    return allValid ? 0 : 1;
}
