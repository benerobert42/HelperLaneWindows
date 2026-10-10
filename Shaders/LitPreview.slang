// Lighting equations adapted directly from GraphEx/Shaders/Example/Mesh/Lighting.ps.slang.
// POSITION/NORMAL transforms follow GraphEx's PosNormTex.vs.slang.
#ifndef DIAGNOSTICS
#define DIAGNOSTICS 0
#endif
#if DIAGNOSTICS
RWTexture2D<uint> gCounters;
#endif
struct VSIn { float3 pos : POSITION; float3 normal : NORMAL; };
struct V2P { float4 pos : SV_Position; float3 worldPos : POSITION; float3 normal : NORMAL; };
cbuffer Camera { float4x4 viewProj; };
cbuffer VolumeModel { float4x4 volumeModel; };
cbuffer VolumeGrid
{
    uint volumeInstanceCount; uint volumeGridCols; uint volumeGridRows; uint volumeGridLayers;
    uint volumeUse3DGrid; float volumeGridSpacing; float2 volumeGridPadding;
};
cbuffer Lighting
{
    float4x4 normalTransform;
    float3 cameraPosition;
    float3 materialAmbient; float3 materialDiffuse; float3 materialSpecular; float shininess;
    uint lightType; float3 lightPosition; float3 lightDirection;
    float3 lightAmbient; float3 lightDiffuse; float3 lightSpecular; float3 attenuationCoefficients;
};

V2P vsMain(VSIn v, uint instanceId : SV_InstanceID)
{
    uint cols = max(volumeGridCols, 1u), rows = max(volumeGridRows, 1u);
    uint layer = volumeUse3DGrid != 0u ? instanceId / (cols * rows) : 0u;
    uint indexInLayer = instanceId - layer * cols * rows;
    float depth = volumeUse3DGrid != 0u ? float(max(volumeGridLayers, 1u) - 1u) : 0.0;
    float3 center = 0.5 * float3(float(cols - 1u), float(rows - 1u), depth);
    float3 offset = (float3(float(indexInLayer % cols), float(indexInLayer / cols), float(layer)) - center) * volumeGridSpacing;
    V2P o;
    o.worldPos = mul(volumeModel, float4(v.pos, 1.0)).xyz + offset;
    o.pos = mul(viewProj, float4(o.worldPos, 1.0));
    o.normal = mul(normalTransform, float4(v.normal, 0.0)).xyz;
    return o;
}

[earlydepthstencil]
float4 psMain(V2P input) : SV_Target
{
#if DIAGNOSTICS
    // Existing quad counting convention; this variant is never timed.
    uint helper = IsHelperLane() ? 1u : 0u;
    uint helpers = QuadReadLaneAt(helper, 0) + QuadReadLaneAt(helper, 1) + QuadReadLaneAt(helper, 2) + QuadReadLaneAt(helper, 3);
    uint candidate = helper != 0 ? 4u : WaveGetLaneIndex() % 4;
    uint leader = min(candidate, QuadReadAcrossX(candidate));
    leader = min(leader, QuadReadAcrossY(candidate));
    leader = min(leader, QuadReadAcrossDiagonal(candidate));
    if (candidate == leader) {
        InterlockedAdd(gCounters[uint2(0, 0)], helpers);
        InterlockedAdd(gCounters[uint2(1, 0)], 4 - helpers);
    }
#endif
    float3 normal = normalize(input.normal);
    float3 toLight = lightType == 0u ? lightPosition - input.worldPos : lightDirection;
    float lightDistance = lightType == 0u ? length(toLight) : 0.0;
    toLight = normalize(toLight);
    float attenuation = 1.0 / (attenuationCoefficients.x + attenuationCoefficients.y * lightDistance
        + attenuationCoefficients.z * lightDistance * lightDistance);
    float3 ambient = lightAmbient * materialAmbient;
    float diffuseFactor = max(dot(toLight, normal), 0.0) * attenuation;
    float3 diffuse = diffuseFactor * lightDiffuse * materialDiffuse;
    float3 viewDir = normalize(cameraPosition - input.worldPos);
    float3 reflectDir = reflect(-toLight, normal);
    float specularFactor = pow(max(dot(viewDir, reflectDir), 0.0), shininess) * attenuation;
    float3 specular = specularFactor * lightSpecular * materialSpecular;
    return float4(ambient + diffuse + specular, 1.0) * float4(0.75);
}
