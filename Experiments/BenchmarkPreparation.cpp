#include "BenchmarkPreparation.h"
#include "BenchmarkGeometry.h"
#include "PlanarBenchmark.h"
#include "SampleSupport.h"
#include "Triangulation/3D/SurfaceFlipExperiment.h"
#include "Triangulation/3D/SurfaceStarExperiment.h"
#include "meshoptimizer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace BenchmarkPreparation
{
using namespace Falcor;
using BenchmarkOptions::Options;
float4x4 modelForView(const Options& o, uint32_t view)
{
    const auto angles = BenchmarkOptions::rotationDegrees(o, view);
    return mul(mul(math::matrixFromRotationX(math::radians(float(angles[0]))),
                   math::matrixFromRotationY(math::radians(float(angles[1])))),
               math::matrixFromRotationZ(math::radians(float(angles[2]))));
}
Params paramsForCase(const Options& o, Params p, uint32_t view, uint32_t instances)
{
    p.model = modelForView(o, view);
    if (o.paperPlanar)
    {
        p.instanceCount = instances;
        p.gridCols = instances == 1 ? 1 : 100;
        p.gridSpacing = 1.f;
        // Maps the centered instance grid to the original [0,1] cells.
        p.viewProj = float4x4::identity();
        p.viewProj[0][0] = p.viewProj[1][1] = 2.f / p.gridCols;
        p.viewProj.setCol(3, float4(-1.f / p.gridCols, -1.f / p.gridCols, 0.f, 1.f));
    }
    return p;
}

namespace
{
using Clock = std::chrono::steady_clock;
using MeshValidation::audit;
double length(float3 p)
{
    return std::sqrt(double(p.x) * p.x + double(p.y) * p.y + double(p.z) * p.z);
}

class Builder
{
    const Options& o;
    size_t shapeIndex;
    const std::atomic<bool>& cancelPreparation;
    const std::function<void(const MeshResult&)>& publishSource;
    std::vector<Prepared> prepared;
    Params geometryParams{};
    std::vector<uint32_t> sourceIndices;
    float3 cameraPosition;
    float geometryRadius = 0;
    double referenceSetupMs = 0, sourceSetupMs = 0, patchSetupMs = 0;
    std::vector<Triangulation3D::Patch> cachedPatches;
    uint32_t sourceUsed = 0;
    MeshValidation::Audit sourceAudit;
    std::vector<uint8_t> sourceProtected;
    std::set<std::array<uint32_t, 3>> sourceTriangleSet;
    float4x4 modelForView(uint32_t view) const { return BenchmarkPreparation::modelForView(o, view); }
    Prepared prepare(
        const Triangulation3D::Mesh& input,
        const MeshResult& source,
        const std::string& name,
        double errorLimit,
        const SurfaceFlipExperiment::ReferenceSurface& reference,
        int32_t view = -1,
        const MeshResult* seed = nullptr
    )
    {
        Prepared p;
        std::cout << "Preparing " << name << " CPU triangulation" << std::endl;
        p.name = name;
        p.view = view;
        p.optimizationView = view;
        if (name == "HeapQuadLag" || name == "HeapQuadWarmLag")
            p.optimizationView = (view / o.axisSteps) * o.axisSteps + (view + o.axisSteps - 1) % o.axisSteps;
        auto start = Clock::now();
        SurfaceFlipExperiment::Constraints constraints;
        if (name == "HeapLoose" || name == "StarLoose") constraints.relativeError = 0.005;
        if (name == "HeapWide" || name == "StarWide") constraints.relativeError = 0.02;
        p.metrics.referenceBudget = errorLimit * constraints.relativeError / 0.001;
        if (constraints.relativeError > 0.001) p.metrics.experimentClass = "RelaxedGeometry";
        if (o.paperPlanar)
            p.mesh = name == "SourceControl" ? source : PlanarBenchmark::make(o.shapes.at(shapeIndex), name, o.bezierDeviation);
        else if (name == "Source" || name == "SourceControl")
            p.mesh = source;
        else if (name == "TopologySplit")
        {
            p.mesh = BenchmarkGeometry::splitTorus(source);
            p.metrics.experimentClass = "TopologyChange";
            p.metrics.referenceBudget = errorLimit * 1000;
        }
        else if (name == "GridAligned" || name == "GridOpposite")
        {
            p.mesh = BenchmarkGeometry::rewriteGrid(input, source, name == "GridAligned");
            p.metrics.experimentClass = "GlobalConnectivity";
            p.metrics.referenceBudget = errorLimit * 20;
        }
        else if (name == "Star3D" || name == "StarLoose" || name == "StarWide")
        {
            auto heap = SurfaceFlipExperiment::optimize(source, reference, 0, {}, constraints);
            auto star = SurfaceStarExperiment::optimize(heap.mesh, reference, constraints);
            auto settled = SurfaceFlipExperiment::optimize(star.mesh, reference, 0, {}, constraints);
            p.mesh = std::move(settled.mesh);
            p.metrics.flips = heap.flips + settled.flips;
            p.metrics.starReplacements = star.replacements;
        }
        else if (name == "Heap3D" || name == "HeapPlane" || name == "HeapScreen" || name == "HeapQuad" ||
                 name == "HeapQuadSet" || name == "HeapLoose" || name == "HeapWide" || BenchmarkOptions::isViewMethod(name))
        {
            std::function<double(uint32_t, uint32_t)> edgeCost;
            if (name == "HeapScreen" || name == "HeapQuad" || name == "HeapQuadSet" || view >= 0)
            {
                // Static methods average the view set; View/Lag use one pose.
                // Normalize pixels to world units for the optimizer's tolerance.
                const uint32_t cols = uint32_t(std::ceil(std::sqrt(double(o.instances))));
                const uint32_t rows = (o.instances + cols - 1) / cols;
                const float radius = computeMaxDistanceFromOrigin(source.vertices);
                const auto vp = makeVolumeViewProj(o.width, o.height, cols, rows, 1, false, radius * 2.5f, radius);
                std::vector<std::vector<float2>> screens;
                p.objectiveInstances = name == "HeapScreen" || name == "HeapQuad" ? 1 : o.instances;
                const uint32_t begin = view < 0 ? 0 : uint32_t(p.optimizationView);
                const uint32_t end = view < 0 ? o.views : begin + 1;
                for (uint32_t objectiveView = begin; objectiveView < end; ++objectiveView)
                {
                    const auto model = modelForView(objectiveView);
                    for (uint32_t instance = 0; instance < p.objectiveInstances; ++instance)
                    {
                        const auto offset = p.objectiveInstances == 1 ? float3(0) :
                            getVolumeInstanceOffset(instance, cols, rows, 1, false, radius * 2.5f);
                        screens.emplace_back(source.vertices.size());
                        for (size_t i = 0; i < source.vertices.size(); ++i)
                            if (!projectToScreenSpace(
                                    vp, transformPointByMatrix(model, source.vertices[i]) + offset, o.width, o.height, screens.back()[i]
                                ))
                                throw std::runtime_error("Optimization camera must keep every source vertex in front.");
                    }
                }
                std::unordered_map<uint64_t, double> costs;
                costs.reserve(source.indices.size());
                auto projectedCost = [screens = std::move(screens), costs = std::move(costs), manhattan = name != "HeapScreen"]
                                     (uint32_t a, uint32_t b) mutable
                {
                    const uint64_t key = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
                    if (const auto it = costs.find(key); it != costs.end())
                        return it->second;
                    double cost = 0;
                    for (const auto& screen : screens)
                    {
                        const auto d = screen[a] - screen[b];
                        cost += manhattan ? std::abs(d.x) + std::abs(d.y) : std::sqrt(double(d.x) * d.x + double(d.y) * d.y);
                    }
                    return costs.emplace(key, cost / screens.size()).first->second;
                };
                double worldSum = 0, pixelSum = 0;
                for (auto [a, b] : makeEdgeSet(source.indices))
                {
                    worldSum += length(source.vertices[a] - source.vertices[b]);
                    pixelSum += projectedCost(a, b);
                }
                if (!(pixelSum > 0) || !std::isfinite(pixelSum))
                    throw std::runtime_error("Projected objective must have finite positive source edge length.");
                const double worldWeight = name == "HeapScreen" || name == "HeapQuad" ? 0.25 : 0.0;
                edgeCost = [projectedCost = std::move(projectedCost), scale = worldSum / pixelSum, worldWeight, &source]
                           (uint32_t a, uint32_t b) mutable
                { return worldWeight * length(source.vertices[a] - source.vertices[b]) + (1 - worldWeight) * scale * projectedCost(a, b); };
            }
            auto r = SurfaceFlipExperiment::optimize(seed ? *seed : source, reference, name == "HeapPlane" ? 4.0 : 0.0, edgeCost, constraints,
                                                     o.preserveSourceDefects ? &sourceProtected : nullptr);
            p.mesh = std::move(r.mesh);
            p.metrics.flips = r.flips;
        }
        else
        {
            Triangulation3D::TriangulationOptions options;
            p.metrics.experimentClass = "PatchRetriangulation";
            options.preserveSourceDefects = o.preserveSourceDefects;
            options.use3DEdgeLengthForFlips = name == "CDTFlip3D" || name == "LMT3D";
            if (name == "CDT")
                options.method = Triangulation3D::Method::ConstrainedDelaunay;
            else if (name == "CDTFlip" || name == "CDTFlip3D")
                options.method = Triangulation3D::Method::ConstrainedDelaunayFlipped;
            else if (name == "Greedy")
                options.method = Triangulation3D::Method::GreedyPointSet;
            else if (name == "LMT" || name == "LMT3D")
                options.method = Triangulation3D::Method::LmtMinimumWeight;
            else if (name == "GP")
                options.method = Triangulation3D::Method::PclGreedyProjection;
            else
                throw std::runtime_error("Unknown method: " + name);
            p.mesh = Triangulation3D::triangulatePreparedMesh(input, source, cachedPatches, options);
            p.metrics.failedPatches = p.mesh.failedPatchCount;
        }
        p.metrics.cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        p.metrics.audit = audit(p.mesh, source);
        p.metrics.referenceError = o.preserveSourceDefects && p.mesh.indices == source.indices ? 0 : reference.sampledError(p.mesh);
        p.metrics.reverseReferenceError = 0; // Unmeasured; ReferenceAuditDirections is 1.
        p.metrics.surfaceEquivalent = p.metrics.referenceError <= errorLimit * 1.001;
        const bool topologyValid = o.preserveSourceDefects ? MeshValidation::preservesSourceDefects(
            p.mesh, source, p.metrics.audit, sourceAudit, sourceProtected) : p.metrics.audit.topologyOK();
        p.metrics.structuralValid = p.mesh.success && p.mesh.indices.size() == source.indices.size() && topologyValid && p.metrics.audit.used == sourceUsed;
        if (o.preserveSourceDefects) p.metrics.experimentClass = "SourceDefectsPreserved:" + p.metrics.experimentClass;
        p.metrics.valid = p.metrics.structuralValid && p.mesh.status != Triangulation3D::ResultStatus::SourceFallback &&
                  (o.preserveSourceDefects || !p.metrics.failedPatches) &&
                  p.metrics.referenceError <= p.metrics.referenceBudget * 1.001;
        if (o.paperPlanar && !p.metrics.valid)
            throw std::runtime_error("Invalid planar construction; aborting before timing: " + name);
        for (size_t i = 0; i < p.mesh.indices.size(); i += 3)
        {
            std::array<uint32_t, 3> triangle{p.mesh.indices[i], p.mesh.indices[i + 1], p.mesh.indices[i + 2]};
            std::sort(triangle.begin(), triangle.end());
            p.metrics.changedTriangles += !sourceTriangleSet.count(triangle);
        }
        if (o.meshopt && !p.mesh.indices.empty())
        {
            meshopt_optimizeVertexCache(p.mesh.indices.data(), p.mesh.indices.data(), p.mesh.indices.size(), p.mesh.vertices.size());
            if (o.ordering == "cache-overdraw")
                meshopt_optimizeOverdraw(
                    p.mesh.indices.data(),
                    p.mesh.indices.data(),
                    p.mesh.indices.size(),
                    &p.mesh.vertices[0].x,
                    p.mesh.vertices.size(),
                    sizeof(float3),
                    1.05f
                );
            // Vertex fetch is applied only to the planar GPU stream, after audits.
        }
        if ((o.lit || o.paperPlanar) && !p.mesh.indices.empty())
        {
            if (p.mesh.vertices.size() != source.vertices.size() ||
                !std::equal(
                    p.mesh.vertices.begin(),
                    p.mesh.vertices.end(),
                    source.vertices.begin(),
                    [](float3 a, float3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
                ))
                throw std::runtime_error("Lit comparisons require identical source vertex ordering.");
        }
        p.metrics.setupMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        std::cout << "Prepared " << name << " view=" << view << ": " << p.metrics.cpuMs << " ms, setup=" << p.metrics.setupMs
                  << " ms, " << p.mesh.indices.size() / 3 << " triangles, valid=" << p.metrics.valid
                  << ", flips=" << p.metrics.flips << std::endl;
        p.metrics.vertices = uint32_t(p.mesh.vertices.size());
        p.metrics.triangles = uint32_t(p.mesh.indices.size() / 3);
        p.metrics.sourceTriangles = uint32_t(source.indices.size() / 3);
        p.metrics.referenceSetupMs = referenceSetupMs;
        p.metrics.sourceSetupMs = sourceSetupMs;
        p.metrics.patchSetupMs = patchSetupMs;
        p.metrics.referenceAuditDirections = 1;
        p.metrics.triangulationStatus = int32_t(p.mesh.status);
        p.metrics.patchCount = uint32_t(p.mesh.patches.size());
        p.metrics.constructionMessage = p.mesh.messages.empty() ? "" : p.mesh.messages.back();
        return p;
    }

public:
    Builder(const Options& options, size_t index, const std::atomic<bool>& cancel,
            const std::function<void(const MeshResult&)>& publish)
        : o(options), shapeIndex(index), cancelPreparation(cancel), publishSource(publish) {}
    Geometry run()
    {
        const auto& shape = o.shapes.at(shapeIndex);
        auto input = o.paperPlanar ? Triangulation3D::Mesh{} : BenchmarkGeometry::make(shape);
        const auto sourceStart = Clock::now();
        auto source = o.paperPlanar ? PlanarBenchmark::make(shape, "Earcut", o.bezierDeviation) :
                                     Triangulation3D::useSourceMeshTriangulation(input, o.preserveSourceDefects);
        sourceSetupMs = std::chrono::duration<double, std::milli>(Clock::now() - sourceStart).count();
        if (!source.success) throw std::runtime_error("Source conversion failed: " + shape);
        if (o.lit) publishSource(source);
        sourceAudit = audit(source, source);
        sourceProtected = o.preserveSourceDefects ? MeshValidation::protectedSourceVertices(source) : std::vector<uint8_t>{};
        std::cout << "Source audit: " << sourceAudit.degenerate << " degenerate triangles, " << sourceAudit.nonmanifold
                  << " nonmanifold edges, " << sourceAudit.duplicates << " duplicates; protected vertices "
                  << std::count(sourceProtected.begin(), sourceProtected.end(), uint8_t(1)) << std::endl;
        cachedPatches.clear();
        patchSetupMs = 0;
        if (!o.paperPlanar && std::any_of(o.methods.begin(), o.methods.end(), [](const std::string& method)
                       { return method.rfind("CDT", 0) == 0 || method.rfind("LMT", 0) == 0 || method == "Greedy" || method == "GP"; }))
        {
            const auto patchStart = Clock::now();
            Triangulation3D::PatchGrowthOptions growth;
            growth.protectedVertices = o.preserveSourceDefects ? &sourceProtected : nullptr;
            cachedPatches = Triangulation3D::growPatches(input, growth);
            patchSetupMs = std::chrono::duration<double, std::milli>(Clock::now() - patchStart).count();
        }
        sourceIndices = source.indices;
        sourceUsed = sourceAudit.used;
        sourceTriangleSet.clear();
        for (size_t i = 0; i < source.indices.size(); i += 3)
        {
            std::array<uint32_t, 3> triangle{source.indices[i], source.indices[i + 1], source.indices[i + 2]};
            std::sort(triangle.begin(), triangle.end());
            sourceTriangleSet.insert(triangle);
        }
        const auto referenceStart = Clock::now();
        SurfaceFlipExperiment::ReferenceSurface reference(source);
        referenceSetupMs = std::chrono::duration<double, std::milli>(Clock::now() - referenceStart).count();
        float3 lo = source.vertices.front(), hi = lo;
        geometryRadius = 0;
        for (auto p : source.vertices)
        {
            lo = math::min(lo, p);
            hi = math::max(hi, p);
            geometryRadius = std::max(geometryRadius, float(length(p)));
        }
        double errorLimit = length(hi - lo) * 0.001;
        std::cout << "Geometry " << shape << ": " << source.vertices.size() << " vertices, error budget " << errorLimit << std::endl;
        prepared.clear();
        for (const auto& name : o.methods)
        {
            if (cancelPreparation.load()) return {};
            const bool warmStart = name == "HeapQuadWarm" || name == "HeapQuadWarmLag";
            size_t seedIndex = 0;
            if (warmStart)
            {
                const auto seed = std::find_if(prepared.begin(), prepared.end(), [](const Prepared& p) { return p.name == "Heap3D"; });
                if (seed == prepared.end() || !seed->metrics.structuralValid || seed->metrics.failedPatches)
                    throw std::runtime_error("Warm projected methods require a structurally valid Heap3D earlier in --methods.");
                seedIndex = size_t(seed - prepared.begin());
            }
            if (BenchmarkOptions::isViewMethod(name))
                for (uint32_t view = 0; view < o.views; ++view)
                    prepared.push_back(prepare(input, source, name, errorLimit, reference, int32_t(view),
                                               warmStart ? &prepared[seedIndex].mesh : nullptr));
            else
                prepared.push_back(prepare(input, source, name, errorLimit, reference));
        }
        uint32_t cols = uint32_t(std::ceil(std::sqrt(double(o.instances)))), rows = (o.instances + cols - 1) / cols;
        float spacing = geometryRadius * 2.5f;
        const auto vp = makeVolumeViewProj(o.width, o.height, cols, rows, 1, false, spacing, geometryRadius, &cameraPosition);
        const Params prime{
            vp,
            modelForView(0),
            cols,
            spacing,
            0.5f * float(cols - 1),
            o.instances};
        geometryParams = prime;
        if (o.paperPlanar) geometryParams = paramsForCase(o, geometryParams, 0, 1);
        // Geometry diagnostics run once per method/view before GPU priming.
        // No CPU projection work is inserted between timed blocks.
        if (o.rasterMetrics)
            for (auto& p : prepared)
                if (!p.mesh.indices.empty())
                    for (uint32_t view = 0; view < o.views; ++view)
                    {
                        if (cancelPreparation.load()) return {};
                        if (p.view >= 0 && uint32_t(p.view) != view)
                            continue;
                        if (o.paperPlanar)
                            for (uint32_t instances : {1u, 10000u})
                            {
                                const auto params = paramsForCase(o, geometryParams, 0, instances);
                                p.rasterMetrics.emplace(instances, RasterMetrics::measure(p.mesh, params.model, params.viewProj,
                                    o.width, o.height, instances, params.gridCols, 1.f));
                            }
                        else
                        {
                            const auto model = modelForView(view);
                            p.rasterMetrics.emplace(view, RasterMetrics::measure(p.mesh, model, vp, o.width, o.height, o.instances, cols, spacing));
                        }
                    }
        std::cout << "CPU preparation and projected metrics complete" << std::endl;
        return {std::move(prepared), geometryParams, std::move(sourceIndices), cameraPosition, geometryRadius};
    }

};
} // namespace

Geometry prepare(const Options& options, size_t shapeIndex, const std::atomic<bool>& cancel,
                 const std::function<void(const MeshResult&)>& publishSource)
{
    return Builder(options, shapeIndex, cancel, publishSource).run();
}
} // namespace BenchmarkPreparation
