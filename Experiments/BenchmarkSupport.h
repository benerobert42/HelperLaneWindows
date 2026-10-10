#pragma once
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace BenchmarkSupport
{
inline std::string csvField(const std::string& text)
{
    std::string escaped = "\"";
    for (char c : text)
    {
        if (c == '"')
            escaped += '"';
        escaped += c;
    }
    return escaped + '"';
}

struct Statistics
{
    double median, mean, standardDeviation, minimum, maximum;
};

template<typename T>
Statistics summarize(const std::vector<T>& samples)
{
    if (samples.empty())
        throw std::runtime_error("No valid GPU samples.");
    auto sorted = samples;
    for (auto value : sorted)
        if (!(value > 0 && std::isfinite(value)))
            throw std::runtime_error("Invalid GPU sample.");
    std::sort(sorted.begin(), sorted.end());
    double median = sorted.size() % 2 ? sorted[sorted.size() / 2] : 0.5 * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
    double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size(), variance = 0;
    for (auto value : samples)
        variance += (value - mean) * (value - mean);
    return {median, mean, std::sqrt(variance / samples.size()), double(sorted.front()), double(sorted.back())};
}
} // namespace BenchmarkSupport
