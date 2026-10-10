#include "Import/ObjMeshLoader.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace ObjMeshLoader
{
namespace
{

LoadResult failure(std::string message)
{
    LoadResult result;
    result.message = std::move(message);
    return result;
}

std::string trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool parseObjPositionIndex(const std::string& token, size_t vertexCount, uint32_t& outIndex)
{
    const size_t slash = token.find('/');
    const std::string indexText = slash == std::string::npos ? token : token.substr(0, slash);
    if (indexText.empty())
    {
        return false;
    }

    int64_t index = 0;
    const char* begin = indexText.data();
    if (*begin == '+')
        ++begin;
    const auto parsed = std::from_chars(begin, indexText.data() + indexText.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != indexText.data() + indexText.size())
        return false;

    if (index > 0)
    {
        const uint64_t zeroBased = static_cast<uint64_t>(index - 1);
        if (zeroBased >= vertexCount || zeroBased > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
        outIndex = static_cast<uint32_t>(zeroBased);
        return true;
    }

    if (index < 0)
    {
        const int64_t resolved = static_cast<int64_t>(vertexCount) + index;
        if (resolved < 0 || resolved >= static_cast<int64_t>(vertexCount))
        {
            return false;
        }
        outIndex = static_cast<uint32_t>(resolved);
        return true;
    }

    return false;
}

void centerAndScale(Triangulation3D::Mesh& mesh, float targetRadius)
{
    if (mesh.vertices.empty())
    {
        return;
    }

    Falcor::float3 minPoint = mesh.vertices.front();
    Falcor::float3 maxPoint = mesh.vertices.front();
    for (const Falcor::float3& vertex : mesh.vertices)
    {
        minPoint.x = std::min(minPoint.x, vertex.x);
        minPoint.y = std::min(minPoint.y, vertex.y);
        minPoint.z = std::min(minPoint.z, vertex.z);
        maxPoint.x = std::max(maxPoint.x, vertex.x);
        maxPoint.y = std::max(maxPoint.y, vertex.y);
        maxPoint.z = std::max(maxPoint.z, vertex.z);
    }

    const double cx = 0.5 * (double(minPoint.x) + maxPoint.x);
    const double cy = 0.5 * (double(minPoint.y) + maxPoint.y);
    const double cz = 0.5 * (double(minPoint.z) + maxPoint.z);
    double radius = 0;
    for (auto p : mesh.vertices)
    {
        double x = p.x - cx, y = p.y - cy, z = p.z - cz;
        radius = std::max(radius, std::sqrt(x * x + y * y + z * z));
    }
    if (radius <= 1e-20)
        return;
    const double scale = targetRadius / radius;
    for (auto& p : mesh.vertices)
        p = Falcor::float3(float((p.x - cx) * scale), float((p.y - cy) * scale), float((p.z - cz) * scale));
}

} // namespace

LoadResult load(const std::string& path, const LoadOptions& options)
{
    LoadResult result;
    if (path.empty())
    {
        result.message = "OBJ path is empty.";
        return result;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        result.message = "Could not open OBJ file: " + path;
        return result;
    }

    std::string line;
    uint32_t lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;

        const size_t comment = line.find('#');
        if (comment != std::string::npos)
        {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty())
        {
            continue;
        }

        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;

        if (keyword == "v")
        {
            std::vector<float> components;
            std::string token;
            while (stream >> token)
            {
                const char* begin = token.data();
                if (*begin == '+')
                    ++begin;
                float value = 0;
                auto parsed = std::from_chars(begin, token.data() + token.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !std::isfinite(value))
                {
                    return failure("Line " + std::to_string(lineNumber) + ": invalid vertex component.");
                }
                components.push_back(value);
            }
            // Six/seven components are the common vertex-color extension;
            // colors are ignored by this position-only importer.
            if (components.size() != 3 && components.size() != 4 && components.size() != 6 && components.size() != 7)
            {
                return failure("Line " + std::to_string(lineNumber) + ": invalid vertex component count.");
            }
            const float w = components.size() == 4 || components.size() == 7 ? components[3] : 1;
            if (std::abs(w) <= 1e-8f)
            {
                return failure("Line " + std::to_string(lineNumber) + ": zero homogeneous weight.");
            }
            auto position = Falcor::float3(components[0] / w, components[1] / w, components[2] / w);
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            {
                return failure("Line " + std::to_string(lineNumber) + ": homogeneous vertex overflow.");
            }
            result.mesh.vertices.push_back(position);
        }
        else if (keyword == "f")
        {
            std::vector<uint32_t> face;
            std::string token;
            while (stream >> token)
            {
                uint32_t vertexIndex = 0;
                if (!parseObjPositionIndex(token, result.mesh.vertices.size(), vertexIndex))
                {
                    return failure("Line " + std::to_string(lineNumber) + ": invalid face index '" + token + "'.");
                }
                face.push_back(vertexIndex);
            }

            if (face.size() < 3)
            {
                return failure("Line " + std::to_string(lineNumber) + ": face has fewer than three vertices.");
            }

            ++result.sourceFaceCount;
            result.sourceTriangleCount += static_cast<uint32_t>(face.size() - 2);
            if (face.size() > 3)
            {
                ++result.polygonFaceCount;
            }
            result.mesh.faces.push_back(std::move(face));
        }
    }

    if (file.bad())
    {
        return failure("Failed while reading OBJ: " + path);
    }
    if (options.centerAndScale)
    {
        if (!(options.targetRadius > 0 && std::isfinite(options.targetRadius)))
        {
            return failure("Invalid OBJ normalization radius.");
        }
        centerAndScale(result.mesh, options.targetRadius);
    }

    result.success = !result.mesh.vertices.empty() && !result.mesh.faces.empty();
    if (result.success)
    {
        result.message = "Loaded OBJ with " + std::to_string(result.mesh.vertices.size()) + " vertices and " +
                         std::to_string(result.sourceFaceCount) + " faces.";
    }
    else
    {
        return failure("OBJ did not contain usable vertices and faces.");
    }

    return result;
}

} // namespace ObjMeshLoader
