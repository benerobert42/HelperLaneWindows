#pragma once

#include "Falcor.h"

using namespace Falcor;

struct Vertex
{
    float2 pos; // in UV space [0..1]
    
    // Compatibility: allow access via .position for code that expects it
    float3 position() const { return float3(pos.x, pos.y, 1.0f); }
    void setPosition(const float3& p) { pos = float2(p.x, p.y); }
};
