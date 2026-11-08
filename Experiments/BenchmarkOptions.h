#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace BenchmarkOptions
{
struct Options
{
    std::filesystem::path output = "analysis/gpu/latest.csv";
    uint32_t samples = 40, warmup = 20, views = 8, blocks = 3, instances = 1, width = 1920, height = 1080;
    std::vector<uint32_t> msaa = {1, 4};
    std::vector<std::string> shapes = {"sphere", "sphere_dense", "jittered", "folded"};
    std::vector<std::string> methods = {"Source", "SourceControl", "CDT", "CDTFlip", "CDTFlip3D", "Heap3D", "HeapPlane"};
    bool depth = true, meshopt = false, quick = false;
    bool paperPlanar = false;
    bool preserveSourceDefects = false;
    float bezierDeviation = 1.f;
    bool lit = false;
    bool explicitLod = false, rasterMetrics = false;
    std::string ordering = "native";
    uint32_t queueDepth = 1;
    uint32_t axisSteps = 0;
    std::vector<uint32_t> axisAngles;
    std::string captureMethod = "Source";
    float cameraDistanceScale = 1.f;
    std::string exportMeshes; // Directory for per-method OBJ exports of prepared triangulations; empty = off.
    uint32_t litQuadWork = 0; // 0 none, 1 ddx/ddy normals, 2 mip-mapped 2K texture (see LitPreview).
};
Options parse(int argc, char** argv);
std::string describeRun(const Options& options);
std::array<double, 3> rotationDegrees(const Options& options, uint32_t view);
bool isViewMethod(const std::string& method);
} // namespace BenchmarkOptions
