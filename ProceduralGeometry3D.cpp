#include "ProceduralGeometry3D.h"

#include <algorithm>
#include <cmath>

namespace ProceduralGeometry3D
{
namespace
{

constexpr float kPi = 3.14159265358979323846f;

Falcor::float3 add3(const Falcor::float3& a, const Falcor::float3& b)
{
    return Falcor::float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

Falcor::float3 multiply3(const Falcor::float3& v, float scale)
{
    return Falcor::float3(v.x * scale, v.y * scale, v.z * scale);
}

float dot3(const Falcor::float3& a, const Falcor::float3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length3(const Falcor::float3& v)
{
    return std::sqrt(dot3(v, v));
}

Falcor::float3 normalize3(const Falcor::float3& v)
{
    const float length = length3(v);
    if (length <= 1e-8f)
    {
        return Falcor::float3(0.0f, 0.0f, 0.0f);
    }

    return multiply3(v, 1.0f / length);
}

float hash01(uint32_t x, uint32_t y, uint32_t salt)
{
    uint32_t h = x * 374761393u + y * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h = h ^ (h >> 16u);
    return static_cast<float>(h & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
}

float signedHash(uint32_t x, uint32_t y, uint32_t salt)
{
    return hash01(x, y, salt) * 2.0f - 1.0f;
}

Falcor::float3 makeUnitSpherePoint(float theta, float phi)
{
    const float sinTheta = std::sin(theta);
    const float cosTheta = std::cos(theta);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);

    return Falcor::float3(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
}

Falcor::float3 makePointOnEllipsoid(float theta, float phi, const Falcor::float3& radii, const Falcor::float3& center)
{
    const Falcor::float3 unit = makeUnitSpherePoint(theta, phi);

    return Falcor::float3(
        center.x + radii.x * unit.x,
        center.y + radii.y * unit.y,
        center.z + radii.z * unit.z
    );
}

uint32_t ringVertexIndex(uint32_t ring, uint32_t longitude, uint32_t longitudeSegments)
{
    return 1 + ring * longitudeSegments + longitude;
}

void appendLatitudeLongitudeFaces(Triangulation3D::Mesh& mesh, uint32_t latitudeSegments, uint32_t longitudeSegments)
{
    const uint32_t topVertex = 0;
    const uint32_t bottomVertex = static_cast<uint32_t>(mesh.vertices.size() - 1);

    for (uint32_t lon = 0; lon < longitudeSegments; ++lon)
    {
        const uint32_t nextLon = (lon + 1) % longitudeSegments;
        mesh.faces.push_back({topVertex, ringVertexIndex(0, nextLon, longitudeSegments), ringVertexIndex(0, lon, longitudeSegments)});
    }

    for (uint32_t ring = 0; ring + 1 < latitudeSegments - 1; ++ring)
    {
        for (uint32_t lon = 0; lon < longitudeSegments; ++lon)
        {
            const uint32_t nextLon = (lon + 1) % longitudeSegments;
            const uint32_t upperCurrent = ringVertexIndex(ring, lon, longitudeSegments);
            const uint32_t upperNext = ringVertexIndex(ring, nextLon, longitudeSegments);
            const uint32_t lowerNext = ringVertexIndex(ring + 1, nextLon, longitudeSegments);
            const uint32_t lowerCurrent = ringVertexIndex(ring + 1, lon, longitudeSegments);
            mesh.faces.push_back({upperCurrent, upperNext, lowerNext, lowerCurrent});
        }
    }

    const uint32_t lastRing = latitudeSegments - 2;
    for (uint32_t lon = 0; lon < longitudeSegments; ++lon)
    {
        const uint32_t nextLon = (lon + 1) % longitudeSegments;
        mesh.faces.push_back({ringVertexIndex(lastRing, lon, longitudeSegments), ringVertexIndex(lastRing, nextLon, longitudeSegments), bottomVertex});
    }
}

uint32_t gridVertexIndex(uint32_t x, uint32_t y, uint32_t widthSegments)
{
    return y * (widthSegments + 1) + x;
}

void appendGridFaces(Triangulation3D::Mesh& mesh, uint32_t widthSegments, uint32_t heightSegments)
{
    for (uint32_t y = 0; y < heightSegments; ++y)
    {
        for (uint32_t x = 0; x < widthSegments; ++x)
        {
            const uint32_t lowerLeft = gridVertexIndex(x, y, widthSegments);
            const uint32_t lowerRight = gridVertexIndex(x + 1, y, widthSegments);
            const uint32_t upperRight = gridVertexIndex(x + 1, y + 1, widthSegments);
            const uint32_t upperLeft = gridVertexIndex(x, y + 1, widthSegments);
            mesh.faces.push_back({lowerLeft, lowerRight, upperRight, upperLeft});
        }
    }
}

float clusteredCoordinate(float t, float strength)
{
    strength = std::max(strength, 0.0f);
    const float centered = t * 2.0f - 1.0f;
    const float curved = std::tanh(centered * (1.0f + strength * 3.0f)) / std::tanh(1.0f + strength * 3.0f);
    return curved * 0.5f + 0.5f;
}

} // namespace

Triangulation3D::Mesh makeSphere(uint32_t latitudeSegments, uint32_t longitudeSegments, float radius, const Falcor::float3& center)
{
    return makeEllipsoid(latitudeSegments, longitudeSegments, Falcor::float3(radius, radius, radius), center);
}

Triangulation3D::Mesh makeEllipsoid(
    uint32_t latitudeSegments,
    uint32_t longitudeSegments,
    const Falcor::float3& radii,
    const Falcor::float3& center
)
{
    latitudeSegments = std::max(latitudeSegments, 3u);
    longitudeSegments = std::max(longitudeSegments, 3u);

    Triangulation3D::Mesh mesh;
    mesh.vertices.reserve(2 + (latitudeSegments - 1) * longitudeSegments);
    mesh.faces.reserve(longitudeSegments * 2 + (latitudeSegments - 2) * longitudeSegments);

    mesh.vertices.push_back(Falcor::float3(center.x, center.y + radii.y, center.z));

    for (uint32_t lat = 1; lat < latitudeSegments; ++lat)
    {
        const float theta = kPi * static_cast<float>(lat) / static_cast<float>(latitudeSegments);
        for (uint32_t lon = 0; lon < longitudeSegments; ++lon)
        {
            const float phi = 2.0f * kPi * static_cast<float>(lon) / static_cast<float>(longitudeSegments);
            mesh.vertices.push_back(makePointOnEllipsoid(theta, phi, radii, center));
        }
    }

    mesh.vertices.push_back(Falcor::float3(center.x, center.y - radii.y, center.z));

    appendLatitudeLongitudeFaces(mesh, latitudeSegments, longitudeSegments);

    return mesh;
}

Triangulation3D::Mesh makeJitteredSphere(
    uint32_t latitudeSegments,
    uint32_t longitudeSegments,
    float radius,
    float radialJitter,
    float tangentialJitter,
    const Falcor::float3& center
)
{
    latitudeSegments = std::max(latitudeSegments, 3u);
    longitudeSegments = std::max(longitudeSegments, 3u);
    radialJitter = std::max(radialJitter, 0.0f);
    tangentialJitter = std::max(tangentialJitter, 0.0f);

    Triangulation3D::Mesh mesh;
    mesh.vertices.reserve(2 + (latitudeSegments - 1) * longitudeSegments);
    mesh.faces.reserve(longitudeSegments * 2 + (latitudeSegments - 2) * longitudeSegments);

    mesh.vertices.push_back(add3(center, Falcor::float3(0.0f, radius * (1.0f + 0.35f * radialJitter), 0.0f)));

    for (uint32_t lat = 1; lat < latitudeSegments; ++lat)
    {
        const float theta = kPi * static_cast<float>(lat) / static_cast<float>(latitudeSegments);
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (uint32_t lon = 0; lon < longitudeSegments; ++lon)
        {
            const float phi = 2.0f * kPi * static_cast<float>(lon) / static_cast<float>(longitudeSegments);
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);

            const Falcor::float3 normal = makeUnitSpherePoint(theta, phi);
            const Falcor::float3 thetaTangent = normalize3(Falcor::float3(cosTheta * cosPhi, -sinTheta, cosTheta * sinPhi));
            const Falcor::float3 phiTangent = Falcor::float3(-sinPhi, 0.0f, cosPhi);
            const float radialScale = radius * (1.0f + radialJitter * signedHash(lat, lon, 17u));
            const float tangentialScale = radius * tangentialJitter * sinTheta;
            Falcor::float3 point = add3(center, multiply3(normal, radialScale));
            point = add3(point, multiply3(thetaTangent, tangentialScale * signedHash(lat, lon, 23u)));
            point = add3(point, multiply3(phiTangent, tangentialScale * signedHash(lat, lon, 29u)));
            mesh.vertices.push_back(point);
        }
    }

    mesh.vertices.push_back(add3(center, Falcor::float3(0.0f, -radius * (1.0f - 0.25f * radialJitter), 0.0f)));
    appendLatitudeLongitudeFaces(mesh, latitudeSegments, longitudeSegments);
    return mesh;
}

Triangulation3D::Mesh makeLumpyEllipsoid(
    uint32_t latitudeSegments,
    uint32_t longitudeSegments,
    const Falcor::float3& radii,
    float radialAmplitude,
    const Falcor::float3& center
)
{
    latitudeSegments = std::max(latitudeSegments, 3u);
    longitudeSegments = std::max(longitudeSegments, 3u);
    radialAmplitude = std::max(radialAmplitude, 0.0f);

    Triangulation3D::Mesh mesh;
    mesh.vertices.reserve(2 + (latitudeSegments - 1) * longitudeSegments);
    mesh.faces.reserve(longitudeSegments * 2 + (latitudeSegments - 2) * longitudeSegments);

    mesh.vertices.push_back(Falcor::float3(center.x, center.y + radii.y, center.z));

    for (uint32_t lat = 1; lat < latitudeSegments; ++lat)
    {
        const float theta = kPi * static_cast<float>(lat) / static_cast<float>(latitudeSegments);
        for (uint32_t lon = 0; lon < longitudeSegments; ++lon)
        {
            const float phi = 2.0f * kPi * static_cast<float>(lon) / static_cast<float>(longitudeSegments);
            const Falcor::float3 unit = makeUnitSpherePoint(theta, phi);
            const float lobes =
                0.55f * std::sin(3.0f * theta + 1.7f * phi) +
                0.35f * std::cos(5.0f * phi - 2.0f * theta) +
                0.25f * std::sin(7.0f * theta + 0.5f * phi);
            const float scale = std::max(0.15f, 1.0f + radialAmplitude * lobes);
            mesh.vertices.push_back(Falcor::float3(
                center.x + radii.x * unit.x * scale,
                center.y + radii.y * unit.y * scale,
                center.z + radii.z * unit.z * scale
            ));
        }
    }

    mesh.vertices.push_back(Falcor::float3(center.x, center.y - radii.y, center.z));
    appendLatitudeLongitudeFaces(mesh, latitudeSegments, longitudeSegments);
    return mesh;
}

Triangulation3D::Mesh makeSaddlePatch(
    uint32_t widthSegments,
    uint32_t heightSegments,
    float size,
    float height,
    float jitter,
    const Falcor::float3& center
)
{
    widthSegments = std::max(widthSegments, 2u);
    heightSegments = std::max(heightSegments, 2u);
    jitter = std::max(jitter, 0.0f);

    Triangulation3D::Mesh mesh;
    mesh.vertices.reserve((widthSegments + 1) * (heightSegments + 1));
    mesh.faces.reserve(widthSegments * heightSegments);

    for (uint32_t y = 0; y <= heightSegments; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(heightSegments);
        for (uint32_t x = 0; x <= widthSegments; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(widthSegments);
            float px = (u * 2.0f - 1.0f) * size * 0.5f;
            float py = (v * 2.0f - 1.0f) * size * 0.5f;
            const float nx = px / std::max(size * 0.5f, 1e-4f);
            const float ny = py / std::max(size * 0.5f, 1e-4f);
            const float edgeFade = std::sin(kPi * u) * std::sin(kPi * v);
            px += jitter * size * 0.08f * edgeFade * signedHash(x, y, 41u);
            py += jitter * size * 0.08f * edgeFade * signedHash(x, y, 43u);
            const float pz =
                height * (0.65f * nx * nx - 0.45f * ny * ny) +
                height * 0.25f * std::sin(4.0f * nx + 1.5f) * std::cos(3.0f * ny - 0.7f) +
                height * 0.12f * edgeFade * signedHash(x, y, 47u);

            mesh.vertices.push_back(add3(center, Falcor::float3(px, py, pz)));
        }
    }

    appendGridFaces(mesh, widthSegments, heightSegments);

    return mesh;
}

Triangulation3D::Mesh makeFoldedSheet(
    uint32_t widthSegments,
    uint32_t heightSegments,
    float size,
    float foldHeight,
    float ripple,
    float jitter,
    const Falcor::float3& center
)
{
    widthSegments = std::max(widthSegments, 2u);
    heightSegments = std::max(heightSegments, 2u);
    jitter = std::max(jitter, 0.0f);

    Triangulation3D::Mesh mesh;
    mesh.vertices.reserve((widthSegments + 1) * (heightSegments + 1));
    mesh.faces.reserve(widthSegments * heightSegments);

    for (uint32_t y = 0; y <= heightSegments; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(heightSegments);
        for (uint32_t x = 0; x <= widthSegments; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(widthSegments);
            float px = (u * 2.0f - 1.0f) * size * 0.5f;
            float py = (v * 2.0f - 1.0f) * size * 0.5f;
            const float fold = std::abs(px) / std::max(size * 0.5f, 1e-4f);
            const float edgeFade = std::sin(kPi * u) * std::sin(kPi * v);
            px += jitter * size * 0.05f * edgeFade * signedHash(x, y, 61u);
            py += jitter * size * 0.05f * edgeFade * signedHash(x, y, 67u);
            const float pz =
                foldHeight * fold +
                ripple * std::sin(7.0f * u + 1.3f) * std::sin(5.0f * v - 0.4f) +
                jitter * foldHeight * 0.08f * edgeFade * signedHash(x, y, 71u);

            mesh.vertices.push_back(add3(center, Falcor::float3(px, py, pz)));
        }
    }

    appendGridFaces(mesh, widthSegments, heightSegments);

    return mesh;
}

Triangulation3D::Mesh makeClusteredWavePatch(
    uint32_t widthSegments,
    uint32_t heightSegments,
    float size,
    float height,
    float clusterStrength,
    const Falcor::float3& center
)
{
    widthSegments = std::max(widthSegments, 2u);
    heightSegments = std::max(heightSegments, 2u);

    Triangulation3D::Mesh mesh;
    mesh.vertices.reserve((widthSegments + 1) * (heightSegments + 1));
    mesh.faces.reserve(widthSegments * heightSegments);

    for (uint32_t y = 0; y <= heightSegments; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(heightSegments);
        const float cv = clusteredCoordinate(v, clusterStrength);
        for (uint32_t x = 0; x <= widthSegments; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(widthSegments);
            const float cu = clusteredCoordinate(u, clusterStrength);
            const float px = (cu * 2.0f - 1.0f) * size * 0.5f;
            const float py = (cv * 2.0f - 1.0f) * size * 0.5f;
            const float pz =
                height * 0.45f * std::sin(5.0f * cu + 0.3f) * std::cos(6.0f * cv - 0.8f) +
                height * 0.25f * std::sin(11.0f * cu + 4.0f * cv);

            mesh.vertices.push_back(add3(center, Falcor::float3(px, py, pz)));
        }
    }

    appendGridFaces(mesh, widthSegments, heightSegments);

    return mesh;
}

} // namespace ProceduralGeometry3D
