#include "ObjMeshLoader.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace ObjMeshLoader
{
namespace
{

float length3(const Falcor::float3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Falcor::float3 add3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

Falcor::float3 subtract3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

Falcor::float3 multiply3(const Falcor::float3& v, float scale)
{
    return Falcor::float3(v.x * scale, v.y * scale, v.z * scale);
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

    int index = 0;
    try
    {
        index = std::stoi(indexText);
    }
    catch (...)
    {
        return false;
    }

    if (index > 0)
    {
        const uint32_t zeroBased = static_cast<uint32_t>(index - 1);
        if (zeroBased >= vertexCount)
        {
            return false;
        }
        outIndex = zeroBased;
        return true;
    }

    if (index < 0)
    {
        const int resolved = static_cast<int>(vertexCount) + index;
        if (resolved < 0 || resolved >= static_cast<int>(vertexCount))
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

    const Falcor::float3 center = multiply3(add3(minPoint, maxPoint), 0.5f);
    float radius = 0.0f;
    for (const Falcor::float3& vertex : mesh.vertices)
    {
        radius = std::max(radius, length3(subtract3(vertex, center)));
    }

    if (radius <= 1e-8f)
    {
        return;
    }

    const float scale = std::max(targetRadius, 1e-6f) / radius;
    for (Falcor::float3& vertex : mesh.vertices)
    {
        vertex = multiply3(subtract3(vertex, center), scale);
    }
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
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
            if (!(stream >> x >> y >> z))
            {
                result.warnings.push_back("Line " + std::to_string(lineNumber) + ": invalid vertex.");
                continue;
            }
            if (stream >> w)
            {
                if (std::abs(w) > 1e-8f && std::abs(w - 1.0f) > 1e-8f)
                {
                    x /= w;
                    y /= w;
                    z /= w;
                }
            }
            result.mesh.vertices.push_back(Falcor::float3(x, y, z));
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
                    result.warnings.push_back("Line " + std::to_string(lineNumber) + ": invalid face index '" + token + "'.");
                    face.clear();
                    break;
                }
                face.push_back(vertexIndex);
            }

            if (face.size() < 3)
            {
                if (!face.empty())
                {
                    result.warnings.push_back("Line " + std::to_string(lineNumber) + ": skipped face with fewer than three vertices.");
                }
                continue;
            }

            ++result.sourceFaceCount;
            result.sourceTriangleCount += static_cast<uint32_t>(face.size() - 2);
            if (face.size() > 3)
            {
                ++result.fanTriangulatedFaceCount;
            }
            result.mesh.faces.push_back(std::move(face));
        }
    }

    if (options.centerAndScale)
    {
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
        result.message = "OBJ did not contain usable vertices and faces.";
    }

    return result;
}

} // namespace ObjMeshLoader
