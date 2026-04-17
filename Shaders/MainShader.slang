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
    float2 materialUv : TEXCOORD0;
    float2 screenUv : TEXCOORD1;
};

#include "ExtendedMeasurements.slang"

cbuffer Grid
{
    uint cols;
    uint rows;
    float2 cellSize;
    float2 origin;
    float scale;
};

cbuffer SceneTransform
{
    float2 translation; // NDC units
    float2 rotCosSin;   // (cos, sin) of rotation angle
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
    float2 rotated = float2(ndc.x * rotCosSin.x - ndc.y * rotCosSin.y,
                            ndc.x * rotCosSin.y + ndc.y * rotCosSin.x);
    V2P o;
    o.pos = float4(rotated + translation, 0.0, 1.0);
    o.materialUv = v.pos;
    o.screenUv = worldPos;
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
#elif VIZ_MODE == 2
    return gDummyTexture.Sample(gDummySampler, input.pos.xy * 0.5 + 0.5);
#elif VIZ_MODE == 3
    return evaluateTextureMaterialStress(input.materialUv, input.screenUv, input.pos);
#elif VIZ_MODE == 4
    return evaluateDerivativeAnchoredAluStress(input.materialUv, input.screenUv, input.pos);
#else
    return float4(1.0, 0.0, 1.0, 1.0);
#endif
}
