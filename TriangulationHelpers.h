#pragma once

#include "ShaderTypes.h"
#include "Falcor.h"

#include <vector>
#include <cstdint>

using namespace Falcor;

namespace Triangulation
{

// MARK: - Polygon Triangulation Methods

// Simple ear clipping triangulation - O(n²), works for any simple polygon.
std::vector<uint32_t> earClippingTriangulation(const std::vector<Vertex>& vertices);

// Minimum Weight Triangulation using dynamic programming.
std::vector<uint32_t> minimumWeightTriangulation(const std::vector<Vertex>& vertices, bool shouldHandleConcave = false);

// Fan triangulation from the centroid.
// Note: Modifies vertices by appending the centroid vertex.
std::vector<uint32_t> centroidFanTriangulation(std::vector<Vertex>& vertices);

// Greedy triangulation selecting largest area triangles first (ear-clipping based).
std::vector<uint32_t> greedyMaxAreaTriangulation(const std::vector<Vertex>& vertices, bool shouldHandleConcave = false);

// Strip triangulation alternating from both ends.
std::vector<uint32_t> stripTriangulation(const std::vector<Vertex>& vertices);

// Maximizes the minimum triangle area (max-min optimization).
std::vector<uint32_t> maxMinAreaTriangulation(const std::vector<Vertex>& vertices, bool shouldHandleConcave = false);

// Minimizes the maximum triangle area (min-max optimization).
std::vector<uint32_t> minMaxAreaTriangulation(const std::vector<Vertex>& vertices, bool shouldHandleConcave = false);

// Constrained Delaunay Triangulation - handles any simple polygon including concave.
// Uses libigl-compatible interface with self-contained CDT implementation.
std::vector<uint32_t> constrainedDelaunay(const std::vector<Vertex>& vertices);

// Constrained Delaunay Triangulation with edge flip optimization.
std::vector<uint32_t> constrainedDelaunayFlipped(const std::vector<Vertex>& vertices);

// Ear clipping using mapbox earcut library - fast O(n log n) for simple polygons.
std::vector<uint32_t> earClippingMapbox(const std::vector<Vertex>& vertices);

// Ear clipping using mapbox earcut, then optimized with edge flips to minimize total edge length.
std::vector<uint32_t> earClippingMapboxFlipped(const std::vector<Vertex>& vertices);

// Optimize triangulation by flipping edges to minimize total edge length.
// Uses a priority queue based approach for efficiency.
std::vector<uint32_t> optimizeByMinLengthFlips(
    const std::vector<Vertex>& vertices,
    std::vector<uint32_t> indices,
    int maxFlips = -1,
    int maxPops = -1
);

// Create vertices for an ellipse
std::vector<Vertex> CreateVerticesForEllipse(uint32_t numSegments, float radiusX, float radiusY, const float2& center);

void ComputeEdgeMetrics(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    size_t& outUniqueEdgeCount,
    double& outTotalEdgeLength
);

} // namespace Triangulation
