//
//  SVGLoader.cpp
//  HelperLaneViz
//
//  Created by Robert Bene on 2025. 10. 11..
//

#include "SVGLoader.h"
#include "TriangulationHelpers.h"
#include "Falcor.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>

// Suppress warnings from nanosvg.h (variable shadowing)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4456) // declaration hides previous local declaration
#endif

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

using namespace Falcor;

namespace
{

float GetSignedArea(const std::vector<float2>& p)
{
    if (p.size() < 3)
        return 0.0f;
    double area = 0.0;
    size_t n = p.size();
    for (size_t i = 0; i < n; ++i)
    {
        size_t j = (i + 1) % n;
        area += static_cast<double>(p[i].x) * p[j].y;
        area -= static_cast<double>(p[j].x) * p[i].y;
    }
    return static_cast<float>(0.5 * area);
}

float PointLineDistance(const float2& p, const float2& a, const float2& b)
{
    const float2 ab = b - a;
    const float lenSq = dot(ab, ab);
    if (lenSq < 1e-12f)
        return length(p - a);
    const float t = std::clamp(dot(p - a, ab) / lenSq, 0.0f, 1.0f);
    return length(p - (a + t * ab));
}

void SampleCubicBezier(const float2& p0, const float2& p1, const float2& p2, const float2& p3, float maxDev, std::vector<float2>& out)
{
    float d1 = PointLineDistance(p1, p0, p3);
    float d2 = PointLineDistance(p2, p0, p3);
    if (std::max(d1, d2) <= maxDev)
    {
        out.push_back(p3);
        return;
    }
    float2 p01 = (p0 + p1) * 0.5f;
    float2 p12 = (p1 + p2) * 0.5f;
    float2 p23 = (p2 + p3) * 0.5f;
    float2 p012 = (p01 + p12) * 0.5f;
    float2 p123 = (p12 + p23) * 0.5f;
    float2 mid = (p012 + p123) * 0.5f;
    SampleCubicBezier(p0, p01, p012, mid, maxDev, out);
    SampleCubicBezier(mid, p123, p23, p3, maxDev, out);
}

std::vector<float2> TessellatePath(NSVGpath* path, float bezierMaxDev)
{
    std::vector<float2> poly;
    const float* pts = path->pts;
    const int npts = path->npts;

    if (npts < 4)
        return poly;

    float2 p0{pts[0], pts[1]};
    poly.push_back(p0);

    // Process each cubic Bezier segment
    int numSegments = (npts - 1) / 3;
    for (int seg = 0; seg < numSegments; seg++)
    {
        int i = 1 + seg * 3;
        float2 p1{pts[i * 2], pts[i * 2 + 1]};
        float2 p2{pts[(i + 1) * 2], pts[(i + 1) * 2 + 1]};
        float2 p3{pts[(i + 2) * 2], pts[(i + 2) * 2 + 1]};
        SampleCubicBezier(p0, p1, p2, p3, bezierMaxDev, poly);
        p0 = p3;
    }

    // Remove duplicate closing vertex if very close to first
    while (poly.size() > 3 && length(poly.front() - poly.back()) < 0.5f)
    {
        poly.pop_back();
    }

    return poly;
}

std::vector<Vertex> PolyToVertices(const std::vector<float2>& poly)
{
    std::vector<Vertex> verts;
    verts.reserve(poly.size());
    for (const auto& p : poly)
    {
        Vertex v;
        v.pos = p;
        verts.push_back(v);
    }
    return verts;
}

SVGLoader::AABB2 ComputeAABB2(const std::vector<Vertex>& vertices)
{
    SVGLoader::AABB2 bb;
    bb.min = float2(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
    bb.max = float2(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());
    for (const auto& v : vertices)
    {
        float2 p = v.pos;
        bb.min = math::min(bb.min, p);
        bb.max = math::max(bb.max, p);
    }
    return bb;
}

} // anonymous namespace

std::vector<SVGLoader::ShapeWithHoles> SVGLoader::ParseSvgToShapes(const std::string& filePath, float bezierMaxDeviationPx)
{
    std::vector<ShapeWithHoles> result;

    NSVGimage* image = nsvgParseFromFile(filePath.c_str(), "px", 96.0f);
    if (!image)
    {
        return result;
    }

    // Process each SVG shape and path - treat each path as a separate shape
    for (NSVGshape* shape = image->shapes; shape; shape = shape->next)
    {
        if ((shape->flags & NSVG_FLAGS_VISIBLE) == 0)
            continue;

        for (NSVGpath* path = shape->paths; path; path = path->next)
        {
            std::vector<float2> poly = TessellatePath(path, bezierMaxDeviationPx);
            if (poly.size() < 3)
                continue;

            // Check if closed (explicitly or implicitly)
            bool isClosed = path->closed;
            if (!isClosed)
            {
                float dist = length(poly.front() - poly.back());
                // Very generous threshold - if first/last are close, treat as closed
                if (dist < 50.0f)
                {
                    isClosed = true;
                }
            }

            if (!isClosed)
                continue;

            ShapeWithHoles shapeWithHoles;
            if (GetSignedArea(poly) < 0)
            {
                shapeWithHoles.holes.push_back(PolyToVertices(poly));
            }
            shapeWithHoles.outerBoundary = PolyToVertices(poly);
            result.push_back(std::move(shapeWithHoles));
        }
    }

    nsvgDelete(image);

    fprintf(stderr, "SVGLoader: Parsed %zu shapes from %s\n", result.size(), filePath.c_str());

    return result;
}

bool SVGLoader::TessellateSvgToMesh(
    const std::string& filePath,
    std::vector<Vertex>& outPositions,
    std::vector<uint32_t>& outIndices,
    Triangulator triangulator,
    float bezierMaxDeviationPx
)
{
    outPositions.clear();
    outIndices.clear();

    auto shapes = ParseSvgToShapes(filePath, bezierMaxDeviationPx);
    if (shapes.empty())
    {
        fprintf(stderr, "SVGLoader: No shapes found\n");
        return false;
    }

    uint32_t baseVertex = 0;

    for (auto& shape : shapes)
    {
        if (shape.outerBoundary.size() < 3)
            continue;

        // Use the provided triangulator directly
        std::vector<Vertex> verts = shape.outerBoundary;
        std::vector<uint32_t> indices = triangulator(verts);

        if (indices.empty())
        {
            fprintf(stderr, "SVGLoader: Triangulation failed for shape with %zu verts\n", shape.outerBoundary.size());
            continue;
        }

        // Append to output
        for (const auto& v : verts)
        {
            outPositions.push_back(v);
        }
        for (uint32_t idx : indices)
        {
            outIndices.push_back(baseVertex + idx);
        }
        baseVertex += static_cast<uint32_t>(verts.size());
    }

    fprintf(stderr, "SVGLoader: Output: %zu vertices, %zu triangles\n", outPositions.size(), outIndices.size() / 3);

    // Normalize to [0..1] range
    if (!outPositions.empty())
    {
        AABB2 bb = ComputeAABB2(outPositions);
        float2 size = bb.max - bb.min;
        if (length(size) > 1e-6f)
        {
            for (auto& v : outPositions)
            {
                // Center and scale to [0..1]
                v.pos = (v.pos - bb.min) / size;
            }
        }
    }

    return !outPositions.empty() && !outIndices.empty();
}

bool SVGLoader::TessellateSvgToMesh(
    const std::string& filePath,
    std::vector<Vertex>& outPositions,
    std::vector<uint32_t>& outIndices,
    float bezierMaxDeviationPx
)
{
    // Default: use ear clipping (CDT requires Eigen/libigl which may not be available)
    auto defaultTriangulator = [](std::vector<Vertex>& verts) { return Triangulation::earClippingTriangulation(verts); };
    return TessellateSvgToMesh(filePath, outPositions, outIndices, defaultTriangulator, bezierMaxDeviationPx);
}
