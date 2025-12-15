// Poly.slang

#ifndef READ_BACK_HELPER_LANE_COUNT
#define READ_BACK_HELPER_LANE_COUNT 0
#endif

struct VSIn
{
    float2 pos : POSITION;    // expected in [0..1] UV space
};

struct V2P
{
    float4 pos : SV_Position;
};

cbuffer Grid
{
    uint cols;
    uint rows;
    float2 cellSize;
    float2 origin;
    float scale;
};

RWTexture2D<uint> gHelperLaneCounter;

V2P vsMain(VSIn v, uint instanceId : SV_InstanceID)
{
    uint col = instanceId % cols;
    uint row = instanceId / cols;
    float2 cellOrigin = float2(col, row) * cellSize + origin;
    float2 local = v.pos * scale;
    float2 worldPos = cellOrigin + local;
    float2 ndc = worldPos * 2.0 - 1.0;
    V2P o;
    o.pos = float4(ndc, 0.0, 1.0);
    return o;
}

float4 psMain(V2P input) : SV_Target
{
    uint isHelperLane = IsHelperLane() ? 1u : 0u;
    uint sum =
        QuadReadLaneAt(isHelperLane, 0) +
        QuadReadLaneAt(isHelperLane, 1) +
        QuadReadLaneAt(isHelperLane, 2) +
        QuadReadLaneAt(isHelperLane, 3);
    static const float4 kColors[4] = {
        float4(0.0, 0.2, 0.0, 1.0),   // 0 helpers
        float4(1.0, 1.0, 0.0, 1.0),   // 1
        float4(1.0, 0.5, 0.1, 1.0),   // 2
        float4(1.0, 0.0, 0.0, 1.0)    // 3
    };
#if READ_BACK_HELPER_LANE_COUNT
    InterlockedAdd(gHelperLaneCounter[0], sum);
#endif
    return kColors[clamp(sum, 0u, 3u)];
}
