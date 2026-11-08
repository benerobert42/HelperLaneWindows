#include "Experiments/BenchmarkOptions.h"
#include <sstream>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <charconv>
#include <cmath>
namespace BenchmarkOptions
{
uint32_t parseCount(const std::string& value)
{
    uint32_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("Invalid numeric option: " + value);
    return result;
}
std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string v;
    while (std::getline(ss, v, ','))
        if (!v.empty())
            result.push_back(v);
    return result;
}
Options parse(int argc, char** argv)
{
    Options o;
    bool explicitShapes = false, explicitMethods = false;
    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];
        auto value = [&]()
        {
            if (i + 1 >= argc)
                throw std::runtime_error("Missing value for " + a);
            return std::string(argv[++i]);
        };
        if (a == "--output")
            o.output = value();
        else if (a == "--samples")
            o.samples = parseCount(value());
        else if (a == "--warmup")
            o.warmup = parseCount(value());
        else if (a == "--views")
            o.views = parseCount(value());
        else if (a == "--rotation-axes")
        {
            if (!o.axisAngles.empty())
                throw std::runtime_error("Choose either --rotation-axes or --rotation-angles.");
            o.axisSteps = parseCount(value());
            if (!o.axisSteps || o.axisSteps > 360)
                throw std::runtime_error("Rotation axis steps must be 1..360.");
        }
        else if (a == "--blocks")
            o.blocks = parseCount(value());
        else if (a == "--rotation-angles")
        {
            if (o.axisSteps)
                throw std::runtime_error("Choose either --rotation-axes or --rotation-angles.");
            for (const auto& angle : split(value()))
            {
                const auto degrees = parseCount(angle);
                if (degrees >= 360) throw std::runtime_error("Rotation angles must be 0..359 degrees.");
                o.axisAngles.push_back(degrees);
            }
            if (o.axisAngles.empty()) throw std::runtime_error("Rotation angles must be nonempty.");
            o.axisSteps = uint32_t(o.axisAngles.size());
        }
        else if (a == "--instances")
            o.instances = parseCount(value());
        else if (a == "--width")
            o.width = parseCount(value());
        else if (a == "--height")
            o.height = parseCount(value());
        else if (a == "--geometries")
        {
            o.shapes = split(value());
            explicitShapes = true;
        }
        else if (a == "--methods")
        {
            o.methods = split(value());
            explicitMethods = true;
        }
        else if (a == "--preserve-source-defects")
            o.preserveSourceDefects = true;
        else if (a == "--paper-planar")
            o.paperPlanar = true;
        else if (a == "--bezier-deviation")
        {
            const auto text = value();
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), o.bezierDeviation);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !(o.bezierDeviation > 0) || !std::isfinite(o.bezierDeviation))
                throw std::runtime_error("Bezier deviation must be a positive finite number.");
        }
        else if (a == "--capture-method")
            o.captureMethod = value();
        else if (a == "--msaa")
        {
            o.msaa.clear();
            for (auto s : split(value()))
                o.msaa.push_back(parseCount(s));
        }
        else if (a == "--no-depth")
            o.depth = false;
        else if (a == "--meshopt")
            o.ordering = "cache-overdraw";
        else if (a == "--ordering")
            o.ordering = value();
        else if (a == "--explicit-lod")
            o.explicitLod = true;
        else if (a == "--raster-metrics")
            o.rasterMetrics = true;
        else if (a == "--lit")
            o.lit = true;
        else if (a == "--queue-depth")
            o.queueDepth = parseCount(value());
        else if (a == "--quick")
            o.quick = true;
        else
            throw std::runtime_error("Unknown performance option: " + a);
    }
    if (o.quick)
    {
        o.samples = 8;
        o.warmup = 8;
        o.views = 1;
        o.blocks = 1;
        o.msaa = {1};
        o.shapes = {"sphere"};
        o.methods = {"Source", "CDT", "CDTFlip", "Heap3D", "HeapPlane"};
    }
    if (o.paperPlanar)
    {
        if (o.axisSteps || o.lit || o.explicitLod || o.quick || o.instances != 1)
            throw std::runtime_error("Paper planar mode fixes untransformed 1x1/100x100 grids; no lit, LOD, quick or instance overrides.");
        o.views = 1;
        o.ordering = "cache-overdraw";
        if (!explicitShapes) o.shapes = {"circle", "ellipse"};
        if (o.captureMethod == "Source") o.captureMethod = "Earcut";
        const std::vector<std::string> methods = {"Earcut", "SourceControl", "EarcutFlip", "CDT", "CDTFlip", "GMA", "MWT"};
        if (!explicitMethods) o.methods = methods;
        for (const auto& method : o.methods)
            if (std::find(methods.begin(), methods.end(), method) == methods.end())
                throw std::runtime_error("Unknown paper planar method: " + method);
    }
    if (o.axisSteps)
    {
        o.views = 3 * o.axisSteps;
        const std::vector<std::string> planar = {"planar", "jittered_planar", "skew1", "skew2", "skew3", "ellipse2d"};
        o.shapes.erase(std::remove_if(o.shapes.begin(), o.shapes.end(), [&](const std::string& name)
        {
            if (std::find(planar.begin(), planar.end(), name) == planar.end()) return false;
            std::cout << "Skipping planar rotation-sweep input: " << name << std::endl;
            return true;
        }), o.shapes.end());
    }
    for (const auto& method : o.methods)
        if (isViewMethod(method) && !o.axisSteps)
            throw std::runtime_error("Single-pose projected methods require --rotation-axes.");
    if (!o.samples || !o.views || !o.blocks || !o.instances || !o.width || !o.height || o.shapes.empty() || o.methods.empty() ||
        o.msaa.empty())
        throw std::runtime_error("Performance options must be nonzero/nonempty.");
    for (auto s : o.msaa)
        if (s != 1 && s != 4)
            throw std::runtime_error("This study supports MSAA 1 or 4.");
    if (!o.queueDepth || o.queueDepth > 32)
        throw std::runtime_error("Queue depth must be 1..32.");
    if (o.lit && !o.depth)
        throw std::runtime_error("Lit comparisons require depth.");
    if (o.ordering != "native" && o.ordering != "cache" && o.ordering != "cache-overdraw")
        throw std::runtime_error("Ordering must be native, cache or cache-overdraw.");
    o.meshopt = o.ordering != "native";
    if (o.lit && o.explicitLod)
        throw std::runtime_error("Explicit LOD applies only to the texture benchmark.");
    return o;
}
std::array<double, 3> rotationDegrees(const Options& o, uint32_t view)
{
    if (o.paperPlanar) return {0, 0, 0};
    if (!o.axisSteps)
        return {22.5 + 45 * (view % 2), 11.25 + 90 * (view / 2), 0};
    std::array<double, 3> angles{};
    angles.at(view / o.axisSteps) = o.axisAngles.empty() ? 360.0 * (view % o.axisSteps) / o.axisSteps
                                                       : o.axisAngles.at(view % o.axisSteps);
    return angles;
}
bool isViewMethod(const std::string& method)
{
    return method == "HeapQuadView" || method == "HeapQuadLag" || method == "HeapQuadWarm" || method == "HeapQuadWarmLag";
}
std::string describeRun(const Options& o)
{
    std::string explicitAngles;
    for (auto angle : o.axisAngles)
        explicitAngles += (explicitAngles.empty() ? "" : ",") + std::to_string(angle);
    return "Frontend: CLI; visible window; debug layer disabled"
    "\nPresentation: onscreen; borderless fullscreen; one runner step per displayed frame; VSync disabled; UI/profiler disabled"
    "\nDraw scope unchanged; output copy and swapchain presentation are outside Draw and Pass"
    "\nPlanar inputs omitted from separate-axis sweeps"
    "\nSeed: 20261010; randomized method order per block/view/MSAA/shader"
    "\nGPU priming: all timed/diagnostic/MSAA pipelines, then 1 second of presented, unmeasured draws per geometry"
    "\nCPU construction and projected metrics run on a worker; all graphics API calls remain on the window thread."
    "\nPreparation displays an untimed one-instance source preview; measurements retain their configured instance count."
    "\nSurface color: RGBA8Unorm; depth: D32Float matching MSAA; culling: None"
    "\nCPU scope: construction and per-flip guards; shared reference setup, final topology/reference audit and upload excluded."
    "\nSetupCPU_ms: CLI preparation through audit, index ordering and VAO/buffer creation; excludes shared source/reference "
    "setup, lazy lighting/PSO initialization, GPU upload completion and diagnostics. GUI reports zero (unmeasured)."
    "\nReferenceSetupCPU_ms: shared fixed-source reference construction once per geometry; reuse across view updates."
    "\nSourceSetupCPU_ms/PatchSetupCPU_ms: shared canonical source/patch growth once per geometry; backend construction reuses both."
    "\nReferenceAuditDirections: CLI/GUI volume 1 (candidate-to-source); 0 unmeasured. GUI zero setup/reverse-error fields are unmeasured."
    "\nGeometryValidationPolicy: ForwardOnlyAdmissionV1"
    "\nReference budget: 0.1% of fixed source bounds diagonal; vertices, midpoints and centroids sampled."
    "\nCLI final audit samples candidate-to-source only; ReverseSampledReferenceError=0 is unmeasured, not zero error. This is not a Hausdorff guarantee."
    "\nPatch and strict heap methods share the 0.1% final surface budget; status 0 failed, 1 complete, 2 source fallback."
    "\nCDTFlip3D/LMT3D opt into existing world-edge flip scoring; default CDTFlip/LMT use projected patch lengths."
    "\nHeapLoose/HeapWide: explicit 0.5%/2% bounds-relative error budgets; same 30 degree normal guards."
    "\nStar3D/Loose/Wide: world heap, then up to three convex vertex-star DP sweeps, then world heap; fixed counts."
    "\nTopologySplit: torus fixture only; reclose two half-tubes as two tori, fixed vertex/triangle counts, changed surface."
    "\nGridAligned/GridOpposite: whole procedural grid rewiring/opposite cell diagonals; fixed positions/counts, 2% error budget."
    "\nSurfaceEquivalent records the original 0.1% sampled budget; non-equivalent cases excluded from ValidForRanking."
    "\nHeapPlane: diagonal length reduction minus 4 * quad plane residual."
    "\nSurface texture workload: deterministic nonconstant 512x512 RGBA8, one mip, linear/wrap."
    "\nTexture shaders: 1 sample or 16 dependent samples; implicit LOD or explicit LOD 0 as selected."
    "\nEdge length: unique geometric edges per mesh; projected Euclidean pixel lengths summed over all instances, including "
    "occluded edges."
    "\nLit: existing GraphEx raster preview, fixed source normals, default single point light; early depth for opaque timed and "
    "diagnostic variants."
    "\nHeapScreen/HeapQuad: 25% world length + 75% normalized mean projected Euclidean/L1 length, fixed view set, central "
    "representative instance."
    "\nHeapQuadSet: pure normalized mean projected L1 length over the fixed view set and all actual instances."
    "\nHeapQuadView/HeapQuadLag: pure projected L1 length for the current/previous axis pose; variants prepared before "
    "GPU timing, averaging all actual instances. Lag tests one angular step of stale connectivity. OptimizationView "
    "records the training pose; ObjectiveInstances records the projection count. Fixed edge scores cached per run."
    "\nHeapQuadWarm/WarmLag: corresponding current/previous-pose pure cost initialized from the prepared Heap3D "
    "indices; fixed original reference/guards retained. CPU times exclude cached seed preparation; Flips count "
    "additional flips. Requires Heap3D before warm methods."
    "\nSourceControl: identical source geometry, independently uploaded, randomized with other methods."
    "\nMeshopt: surface index ordering only; planar paper additionally reorders GPU vertex buffers for vertex fetch. Audits retain canonical indices."
    "\nLit helpers: diagnostic raster quad-waste proxy; the timed lit shader has no derivatives/quad operations."
    "\nHardware pipeline statistics: separate uninstrumented draw after timing, same clear/depth; no query around timed draws."
    "\nPSInvocations are API-reported invocations, not a guarantee of useful pixels or helper-lane instruction work."
    "\nRaster metrics: pre-depth, unclipped projected triangles; area/shape include occluded faces."
    "\nCache simulation: meshoptimizer cache=16, warp=32, primitive group=128; not hardware counters."
    "\nOrdering: " + o.ordering + "\nExplicit texture LOD 0: " + std::to_string(o.explicitLod) +
    "\nSeparate-axis rotation steps: " + std::to_string(o.axisSteps) +
    "; model = Rx * Ry * Rz; randomized view order for axis sweeps."
    "\nExplicit axis angles: " + (explicitAngles.empty() ? "uniform full-circle spacing" : explicitAngles) +
    "\nPaper planar mode: " + std::to_string(o.paperPlanar) +
    "\nPaper planar: static original grid shader; 1x1 and 100x100; 512 vertices; center .5,.5; radii .4,.4 / .4,.2; "
    "2048x2048 constant 0xFF0000FF RGBA8 texture, default sampler, screen-position UVs; no transformations."
    "\nPaper planar targets: MSAA1 BGRA8UnormSrgb + D32Float; MSAA4 RGBA16Float without depth, as in original project."
    "\nPaper planar display conversion is outside Draw and Pass; Pass includes clear/draw/resolve only."
    "\nPaper planar SourceControl: independently uploaded Earcut copy."
    "\nPaper planar meshoptimizer: vertex cache, overdraw threshold 1.05, then vertex fetch; all methods and controls."
    "\nBezier deviation: " + std::to_string(o.bezierDeviation) +
    "\nPreserve existing source defects: " + std::to_string(o.preserveSourceDefects) +
    "\nSource-defect mode leaves input indices/positions/counts unchanged; freezes defective neighborhoods; "
    "requires the same defect signatures/counts, components, Euler characteristic and boundaries. "
    "Unsupported patches retain their source triangles; FailedPatches records these local retentions. "
    "The 0.1% candidate-to-source sampled surface budget and local flip validity guards remain enabled.";
}
} // namespace BenchmarkOptions
