#ifndef EXTENDED_MEASUREMENTS_SLANG
#define EXTENDED_MEASUREMENTS_SLANG

cbuffer ExtendedMeasurementParams
{
    uint gAluIterations;
    uint gExtendedReserved0;
    uint gExtendedReserved1;
    uint gExtendedReserved2;

    float gMaterialUvScale;
    float gDetailUvScale;
    float gNormalStrength;
    float gExtendedReserved3;
};

Texture2D<float4> gExtendedAlbedo;
Texture2D<float4> gExtendedNormal;
Texture2D<float4> gExtendedRoughnessMetalness;
Texture2D<float4> gExtendedAmbientOcclusion;
Texture2D<float4> gExtendedEmissive;
Texture2D<float4> gExtendedDetailAlbedo;
Texture2D<float4> gExtendedDetailNormal;
Texture2D<float4> gExtendedMask;
SamplerState gExtendedSampler;

struct ExtendedSurface
{
    float3 albedo;
    float3 normal;
    float roughness;
    float metalness;
    float ao;
    float3 emissive;
    float3 mask;
};

float3 unpackExtendedNormal(float4 encoded)
{
    float3 n = encoded.xyz * 2.0f - 1.0f;
    n.z = max(n.z, 0.05f);
    return normalize(n);
}

float3 blendExtendedNormals(float3 baseNormal, float3 detailNormal)
{
    float3 n;
    n.xy = baseNormal.xy + detailNormal.xy * 0.45f;
    n.z = baseNormal.z * detailNormal.z;
    return normalize(lerp(float3(0.0f, 0.0f, 1.0f), n, gNormalStrength));
}

ExtendedSurface sampleExtendedSurface(float2 uv)
{
    ExtendedSurface s;

    float2 detailUv = uv * gDetailUvScale;
    float4 albedo = gExtendedAlbedo.Sample(gExtendedSampler, uv);
    float4 detailAlbedo = gExtendedDetailAlbedo.Sample(gExtendedSampler, detailUv);
    float4 roughMetal = gExtendedRoughnessMetalness.Sample(gExtendedSampler, uv * 1.17f + 0.11f);
    float4 ao = gExtendedAmbientOcclusion.Sample(gExtendedSampler, uv * 0.93f - 0.07f);
    float4 emissive = gExtendedEmissive.Sample(gExtendedSampler, uv * 1.41f + 0.23f);
    float4 mask = gExtendedMask.Sample(gExtendedSampler, uv * 0.61f + float2(0.19f, 0.37f));
    float3 normal = unpackExtendedNormal(gExtendedNormal.Sample(gExtendedSampler, uv * 1.03f));
    float3 detailNormal = unpackExtendedNormal(gExtendedDetailNormal.Sample(gExtendedSampler, detailUv + mask.xy * 0.05f));

    s.albedo = saturate(albedo.rgb * lerp(float3(0.72f), detailAlbedo.rgb * 1.35f, 0.42f + mask.z * 0.18f));
    s.normal = blendExtendedNormals(normal, detailNormal);
    s.roughness = clamp(roughMetal.r, 0.045f, 0.98f);
    s.metalness = clamp(roughMetal.g, 0.0f, 0.85f);
    s.ao = clamp(ao.r, 0.2f, 1.0f);
    s.emissive = emissive.rgb;
    s.mask = mask.rgb;
    return s;
}

float3 evaluateExtendedLamp(ExtendedSurface s, float3 viewDir, float3 lightDir, float3 radiance)
{
    float3 halfDir = normalize(viewDir + lightDir);
    float nDotL = saturate(dot(s.normal, lightDir));
    float nDotV = saturate(dot(s.normal, viewDir));
    float nDotH = saturate(dot(s.normal, halfDir));

    float specPower = lerp(96.0f, 12.0f, s.roughness);
    float specular = pow(nDotH, specPower) * (1.0f - s.roughness) * lerp(0.08f, 0.65f, s.metalness);
    float rim = pow(saturate(1.0f - nDotV), 3.0f) * 0.12f;
    float3 diffuse = s.albedo * nDotL * (1.0f - s.metalness * 0.55f);

    return (diffuse + specular + rim * s.albedo) * radiance;
}

float3 shadeExtendedSurface(ExtendedSurface s, float2 uv)
{
    float3 viewDir = normalize(float3(0.18f, -0.25f, 1.0f));
    float3 color = float3(0.015f, 0.018f, 0.023f) * s.albedo * s.ao;

    color += evaluateExtendedLamp(s, viewDir, normalize(float3(-0.45f, 0.32f, 0.84f)), float3(3.9f, 3.5f, 3.0f));
    color += evaluateExtendedLamp(s, viewDir, normalize(float3(0.73f, -0.18f, 0.66f)), float3(1.1f, 1.5f, 1.9f));
    color += evaluateExtendedLamp(s, viewDir, normalize(float3(0.05f, 0.82f, 0.57f)), float3(0.75f, 0.66f, 0.52f));
    color += evaluateExtendedLamp(s, viewDir, normalize(float3(-0.18f, -0.91f, 0.38f)), float3(0.42f, 0.58f, 0.82f));

    float2 lookupUv = float2(s.roughness, saturate(dot(s.normal, viewDir)));
    float3 lookup = gExtendedMask.Sample(gExtendedSampler, lookupUv * 0.91f + 0.045f).rgb;
    color += lookup * lerp(float3(0.03f, 0.05f, 0.08f), s.albedo, 0.35f) * s.ao;
    color += s.emissive * (0.45f + s.mask.y * 0.55f);

    return color / (color + 1.0f);
}

float4 evaluateTextureMaterialStress(float2 materialUv, float2 screenUv, float4 posH)
{
    float2 uv = materialUv * gMaterialUvScale;
    ExtendedSurface s = sampleExtendedSurface(uv);
    return float4(shadeExtendedSurface(s, uv), 1.0f);
}

float4 evaluateDerivativeAnchoredAluStress(float2 materialUv, float2 screenUv, float4 posH)
{
    float2 screenDx = ddx(screenUv);
    float2 screenDy = ddy(screenUv);
    float2 uvDx = ddx(materialUv);
    float2 uvDy = ddy(materialUv);
    float anchor = dot(abs(screenDx) + abs(screenDy), float2(1.0f, 1.37f)) +
                   dot(abs(uvDx) + abs(uvDy), float2(0.71f, 0.53f));

    float4 a = float4(materialUv, screenUv) + anchor * float4(0.11f, 0.17f, 0.23f, 0.29f);
    float4 b = a.yzwx * 0.37f + float4(0.19f, 0.31f, 0.43f, 0.59f);
    uint iterations = min(gAluIterations, 4096u);

    // Uniform fixed-cost loop for a selected iteration count. The shader never branches on helper-lane state.
    [loop]
    for (uint i = 0u; i < iterations; ++i)
    {
        float4 mixed = a.zwxy * b.yzwx + float4(0.013f, 0.017f, 0.019f, 0.023f);
        a = frac(a * 1.6180339f + mixed * 0.071f + b.wxyz * 0.043f + anchor);
        b = frac(b * 1.3247179f + a.yzwx * 0.097f + mixed.wxyz * 0.029f + anchor * 0.37f);
    }

    float3 color = saturate(a.rgb * 0.63f + b.gba * 0.37f);
    return float4(color, 1.0f);
}

#endif
