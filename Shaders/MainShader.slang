// Poly.slang

#ifndef READ_BACK_HELPER_LANE_COUNT
#define READ_BACK_HELPER_LANE_COUNT 0
#endif

#ifndef VIZ_MODE
#define VIZ_MODE 0
#endif

#ifndef USE_3D_GEOMETRY
#define USE_3D_GEOMETRY 0
#endif

struct VSIn
{
    float3 pos : POSITION;
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

cbuffer Camera
{
    float4x4 viewProj;
};

cbuffer VolumeModel
{
    float4x4 volumeModel;
};

cbuffer VolumeGrid
{
    uint volumeInstanceCount;
    uint volumeGridCols;
    uint volumeGridRows;
    uint volumeGridLayers;
    uint volumeUse3DGrid;
    float volumeGridSpacing;
    float2 volumeGridPadding;
};

RWTexture2D<uint> gHelperLaneCounter;

V2P vsMain(VSIn v, uint instanceId : SV_InstanceID)
{
#if USE_3D_GEOMETRY
    uint cols = max(volumeGridCols, 1u);
    uint rows = max(volumeGridRows, 1u);
    uint layers = max(volumeGridLayers, 1u);
    uint layer = 0u;
    uint indexInLayer = instanceId;
    if (volumeUse3DGrid != 0u)
    {
        uint cellsPerLayer = cols * rows;
        layer = instanceId / cellsPerLayer;
        indexInLayer = instanceId - layer * cellsPerLayer;
    }
    uint row = indexInLayer / cols;
    uint col = indexInLayer - row * cols;

    float gridDepth = volumeUse3DGrid != 0u ? float(layers - 1u) : 0.0;
    float3 gridCenter = 0.5 * float3(float(cols - 1u), float(rows - 1u), gridDepth);
    float3 instanceCoord = float3(float(col), float(row), float(layer));
    float3 instanceOffset = (instanceCoord - gridCenter) * volumeGridSpacing;
    float3 modelPos = mul(volumeModel, float4(v.pos, 1.0)).xyz;

    V2P o;
    o.pos = mul(viewProj, float4(modelPos + instanceOffset, 1.0));
    return o;
#else
    uint col = instanceId % cols;
    uint row = instanceId / cols;
    float2 cellOrigin = float2(col, row) * cellSize + origin;
    float2 local = v.pos.xy * scale;
    float2 worldPos = cellOrigin + local;
    float2 ndc = worldPos * 2.0 - 1.0;
    V2P o;
    o.pos = float4(ndc, 0.0, 1.0);
    return o;
#endif
}

float4 psMain(V2P input) : SV_Target
{
#if VIZ_MODE == 0
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
    uint quadLane = WaveGetLaneIndex() % 4; // ID within 2x2 quad - https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_7_QuadAny_QuadAll.html
    uint candidate = IsHelperLane() ? 4u : quadLane;
    uint m = candidate;
         m = min(m, QuadReadAcrossX(candidate));
         m = min(m, QuadReadAcrossY(candidate));
         m = min(m, QuadReadAcrossDiagonal(candidate));

    if (m < 4u && candidate == m) InterlockedAdd(gHelperLaneCounter[0], sum);
    if (!IsHelperLane()) InterlockedAdd(gHelperLaneCounter[uint2(1, 0)], 1);
#endif
    return kColors[clamp(sum, 0u, 3u)];
#elif VIZ_MODE == 1
    return float4(0, 0, 0, 1);
#else
    // Internal benchmark workload: opaque flat rasterization, no diagnostics.
    return float4(1, 1, 1, 1);
#endif
}
