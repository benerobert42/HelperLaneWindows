#ifndef MATERIAL_STRESS
#define MATERIAL_STRESS 0
#endif
#ifndef DIAGNOSTICS
#define DIAGNOSTICS 0
#endif
#ifndef EXPLICIT_LOD
#define EXPLICIT_LOD 0
#endif
cbuffer Params
{
    float4x4 viewProj;
    float4x4 model;
    uint gridCols;
    float gridSpacing;
    float gridCenter;
    uint instanceCount;
};
Texture2D<float4> gTexture;
SamplerState gSampler;
float4 sampleTexture(float2 uv)
{
#if EXPLICIT_LOD
    return gTexture.SampleLevel(gSampler, uv, 0);
#else
    return gTexture.Sample(gSampler, uv);
#endif
}
#if DIAGNOSTICS
RWTexture2D<uint> gCounters;
#endif
struct VSIn { float3 pos : POSITION; };
struct V2P { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V2P vsMain(VSIn v,uint instance : SV_InstanceID)
{
    float3 p=mul(model,float4(v.pos,1)).xyz;
    float2 center=float2(gridCenter,0.5*float((instanceCount+gridCols-1)/gridCols-1));
    p.xy+=(float2(instance%gridCols,instance/gridCols)-center)*gridSpacing;
    V2P o; o.pos=mul(viewProj,float4(p,1)); o.uv=v.pos.xy*2.17+v.pos.z*0.37;
    return o;
}
[earlydepthstencil]
float4 psMain(V2P input) : SV_Target
{
    float4 color=sampleTexture(input.uv);
#if MATERIAL_STRESS
    float2 uv=input.uv;
    [unroll] for(uint i=1;i<16;++i) {
        float fi=float(i);
        uv+=float2(sin(color.r+fi*2.17),cos(color.g+fi*1.71))*0.027;
        color+=sampleTexture(uv+float2(fi*0.019,fi*0.037));
    }
    color*=1.0/16.0;
#endif
#if DIAGNOSTICS
    uint helper=IsHelperLane()?1u:0u;
    uint helpers=QuadReadLaneAt(helper,0)+QuadReadLaneAt(helper,1)+QuadReadLaneAt(helper,2)+QuadReadLaneAt(helper,3);
    uint candidate=helper!=0?4u:WaveGetLaneIndex()%4;
    uint leader=candidate;
    leader=min(leader,QuadReadAcrossX(candidate));
    leader=min(leader,QuadReadAcrossY(candidate));
    leader=min(leader,QuadReadAcrossDiagonal(candidate));
    uint quadHelpers=candidate==leader?helpers:0;
    uint quadActive=candidate==leader?4-helpers:0;
    uint waveHelpers=WaveActiveSum(quadHelpers);
    uint waveActive=WaveActiveSum(quadActive);
    if(WaveIsFirstLane()) {
        InterlockedAdd(gCounters[uint2(0,0)],waveHelpers);
        InterlockedAdd(gCounters[uint2(1,0)],waveActive);
    }
#endif
    return float4(color.rgb,1);
}
