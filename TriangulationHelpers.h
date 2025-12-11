#pragma once

#include "ShaderTypes.h"
#include "Falcor.h"

#include <vector>
#include <cstdint>

using namespace Falcor;

namespace Triangulation
{

// Calculate total edge length of a triangulation (sum of all triangle perimeters)
double calculateTotalEdgeLength(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

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
// Uses libigl's Triangle library wrapper (falls back to ear clipping if not available).
std::vector<uint32_t> constrainedDelaunay(const std::vector<Vertex>& vertices);

// Create vertices for an ellipse
std::vector<Vertex> CreateVerticesForEllipse(uint32_t numSegments, float radiusX, float radiusY, const float2& center);

// Create convex minimum weight triangulation (wrapper for minimumWeightTriangulation)
std::vector<uint32_t> CreateConvexMWT(const std::vector<Vertex>& vertices, double& outEdgeLength);

} // namespace Triangulation
