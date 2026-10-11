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
    std::vector<std::string> shapes = {"sphere", "sphere_dense", "jittered", "planar", "jittered_planar", "folded"};
    std::vector<std::string> methods = {"Source", "CDT", "CDTFlip", "Greedy", "LMT", "GP", "Heap3D", "HeapPlane"};
    bool depth = true, meshopt = false, quick = false;
    bool lit = false;
    bool explicitLod = false, rasterMetrics = false;
    std::string ordering = "native";
    uint32_t queueDepth = 1;
    uint32_t axisSteps = 0;
};
Options parse(int argc, char** argv);
std::array<double, 3> rotationDegrees(const Options& options, uint32_t view);
} // namespace BenchmarkOptions
