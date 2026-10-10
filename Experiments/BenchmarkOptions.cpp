#include "Experiments/BenchmarkOptions.h"
#include <sstream>
#include <stdexcept>
#include <limits>
#include <charconv>
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
        else if (a == "--blocks")
            o.blocks = parseCount(value());
        else if (a == "--instances")
            o.instances = parseCount(value());
        else if (a == "--width")
            o.width = parseCount(value());
        else if (a == "--height")
            o.height = parseCount(value());
        else if (a == "--geometries")
            o.shapes = split(value());
        else if (a == "--methods")
            o.methods = split(value());
        else if (a == "--msaa")
        {
            o.msaa.clear();
            for (auto s : split(value()))
                o.msaa.push_back(parseCount(s));
        }
        else if (a == "--no-depth")
            o.depth = false;
        else if (a == "--meshopt")
            o.meshopt = true;
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
    return o;
}
} // namespace BenchmarkOptions
