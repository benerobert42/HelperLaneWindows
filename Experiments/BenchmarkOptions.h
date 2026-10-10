#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace BenchmarkOptions
{
struct Options
{
    std::filesystem::path output = "analysis/gpu/latest.csv";
    uint32_t samples = 40, warmup = 20, views = 8, blocks = 3, instances = 1, width = 1280, height = 720;
    std::vector<uint32_t> msaa = {1, 4};
    std::vector<std::string> shapes = {"sphere", "sphere_dense", "jittered", "planar", "jittered_planar", "folded"};
    std::vector<std::string> methods = {"Source", "CDT", "CDTFlip", "Greedy", "LMT", "GP", "Heap3D", "HeapPlane"};
    bool depth = true, meshopt = false, quick = false;
};
Options parse(int argc, char** argv);
} // namespace BenchmarkOptions
