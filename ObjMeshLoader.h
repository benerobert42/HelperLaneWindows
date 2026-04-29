#pragma once

#include "Triangulation3D.h"

#include "Falcor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ObjMeshLoader
{

struct LoadOptions
{
    bool centerAndScale = true;
    float targetRadius = 0.5f;
};

struct LoadResult
{
    Triangulation3D::Mesh mesh;
    uint32_t sourceFaceCount = 0;
    uint32_t sourceTriangleCount = 0;
    uint32_t fanTriangulatedFaceCount = 0;
    bool success = false;
    std::string message;
    std::vector<std::string> warnings;
};

LoadResult load(const std::string& path, const LoadOptions& options = {});

} // namespace ObjMeshLoader
