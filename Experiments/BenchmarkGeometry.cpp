#include "BenchmarkGeometry.h"
#include "Triangulation/3D/ProceduralGeometry3D.h"
#include "Import/ModelPresets.h"
#include "Import/ObjMeshLoader.h"
#include <cmath>
#include <stdexcept>

namespace BenchmarkGeometry
{
using Falcor::float3;

Triangulation3D::Mesh make(const std::string& name)
{
    if (name == "ellipse2d")
    {
        Triangulation3D::Mesh mesh;
        mesh.faces.emplace_back();
        for (uint32_t i = 0; i < 512; ++i)
        {
            const float angle = 6.28318530718f * float(i) / 512;
            mesh.vertices.emplace_back(0.4f * std::cos(angle), 0.2f * std::sin(angle), 0.f);
            mesh.faces.back().push_back(i);
        }
        return mesh;
    }
    if (name.rfind("obj:", 0) == 0 || name == "suzanne" || name == "bunny" || name == "teapot")
    {
        const auto path = name.rfind("obj:", 0) == 0 ? std::filesystem::path(name.substr(4))
            : getModelPresetPath(name == "suzanne" ? 8 : name == "bunny" ? 9 : 10);
        auto result = ObjMeshLoader::load(path.string());
        if (!result.success)
            throw std::runtime_error(result.message);
        return std::move(result.mesh);
    }
    if (name == "ellipsoid" || name == "ellipsoid_small")
        return ProceduralGeometry3D::makeEllipsoid(
            name == "ellipsoid" ? 63 : 31, name == "ellipsoid" ? 33 : 17, float3(0.825f, 0.275f, 0.475f), float3(0)
        );
    if (name == "blob" || name == "blob_small")
        return ProceduralGeometry3D::makeLumpyEllipsoid(
            name == "blob" ? 63 : 31, name == "blob" ? 33 : 17, float3(0.725f, 0.325f, 0.525f), 0.35f, float3(0)
        );
    if (name == "saddle_branch")
        return ProceduralGeometry3D::makeSaddlePatch(33, 63, 1.5f, 0.425f, 1.0f, float3(0));
    if (name == "clustered")
        return ProceduralGeometry3D::makeClusteredWavePatch(33, 63, 1.5f, 0.4f, 0.8f, float3(0));
    if (name == "skew1" || name == "skew2" || name == "skew3" || name == "skew_fold" || name == "skew_saddle")
    {
        auto mesh = ProceduralGeometry3D::makeSaddlePatch(63, 32, 1.5f, 0, 0, float3(0));
        float shear = name == "skew1" ? 1.0f : name == "skew2" ? 2.0f : 3.0f;
        for (auto& p : mesh.vertices)
        {
            float u = p.x, v = p.y;
            if (name == "skew_fold")
                p.z = 0.7f * std::abs(v);
            if (name == "skew_saddle")
                p.z = 0.18f * (u * u - v * v);
            p.x += shear * v;
        }
        return mesh;
    }
    if (name == "torus" || name == "torus_skew" || name == "cylinder" || name == "cylinder_skew")
    {
        Triangulation3D::Mesh mesh;
        constexpr float pi = 3.14159265358979323846f;
        bool torus = name.find("torus") == 0, skew = name.find("skew") != std::string::npos;
        uint32_t columns = torus ? 128 : 192, rows = torus ? 64 : 96;
        uint32_t vertexRows = torus ? rows : rows + 1;
        // Integer shifts give exactly the same vertex set for aligned/skewed
        // inputs. Two columns per row close the torus seam after 64 rows.
        for (uint32_t y = 0; y < vertexRows; ++y)
            for (uint32_t x = 0; x < columns; ++x)
            {
                float theta = 2 * pi * float((x + (skew ? 2 * y : 0)) % columns) / columns;
                float t = float(y) / rows, phi = 2 * pi * t;
                if (torus)
                {
                    float tube = 0.18f, radius = 0.45f + tube * std::cos(phi);
                    mesh.vertices.push_back(float3(radius * std::cos(theta), radius * std::sin(theta), tube * std::sin(phi)));
                }
                else
                {
                    float radius = 0.32f * (1 + 0.22f * std::sin(3 * theta + 2 * t) + 0.16f * std::sin(5 * theta - 3 * t));
                    mesh.vertices.push_back(float3(
                        0.12f * std::sin(phi) + radius * std::cos(theta),
                        0.08f * std::sin(1.5f * phi) + radius * std::sin(theta),
                        1.4f * (t - 0.5f)
                    ));
                }
            }
        for (uint32_t y = 0; y < rows; ++y)
            for (uint32_t x = 0; x < columns; ++x)
            {
                uint32_t nx = (x + 1) % columns, ny = torus ? (y + 1) % rows : y + 1;
                mesh.faces.push_back({y * columns + x, y * columns + nx, ny * columns + nx, ny * columns + x});
            }
        return mesh;
    }
    if (name == "sphere")
        return ProceduralGeometry3D::makeSphere(31, 17, 0.5f, float3(0));
    if (name == "sphere_dense")
        return ProceduralGeometry3D::makeSphere(63, 33, 0.5f, float3(0));
    if (name == "jittered")
        return ProceduralGeometry3D::makeJitteredSphere(63, 33, 0.5f, 0.22f, 0.18f, float3(0));
    if (name == "planar")
        return ProceduralGeometry3D::makeSaddlePatch(33, 63, 1.5f, 0, 0, float3(0));
    if (name == "jittered_planar")
        return ProceduralGeometry3D::makeSaddlePatch(33, 63, 1.5f, 0, 0.05f, float3(0));
    if (name == "folded")
        return ProceduralGeometry3D::makeFoldedSheet(33, 63, 1.6f, 0.45f, 0.06f, 1.0f, float3(0));
    throw std::runtime_error("Unknown geometry: " + name);
}
} // namespace BenchmarkGeometry
