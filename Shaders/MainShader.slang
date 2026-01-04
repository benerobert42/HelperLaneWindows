// Poly.slang

#ifndef READ_BACK_HELPER_LANE_COUNT
#define READ_BACK_HELPER_LANE_COUNT 0
#endif

#ifndef USE_DUMMY_TEXTURE
#define USE_DUMMY_TEXTURE 0
#endif

#ifndef VIZ_MODE
#define VIZ_MODE 0
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
Texture2D<float4> gDummyTexture;
SamplerState gDummySampler;

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
    float dummy = 0.0;
#if (VIZ_MODE == 0) && USE_DUMMY_TEXTURE
    uint lane = WaveGetLaneIndex();
    float2 uv = frac(float2(lane * 0.37, lane * 0.73));
    dummy = gDummyTexture.Sample(gDummySampler, uv).r;
#endif
#if VIZ_MODE == 0
    uint isHelperLane = IsHelperLane() ? 1u : 0u;
    isHelperLane ^= (dummy > -1.0f ? 0u : 0u); // prevents dead-code elimination
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
#endif
    return kColors[clamp(sum, 0u, 3u)];
#elif VIZ_MODE == 1
    return float4(0, 0, 0, 1);
#else
    return gDummyTexture.Sample(gDummySampler, input.pos.xy * 0.5 + 0.5);
#endif
}
