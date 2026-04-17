/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 **************************************************************************/
#include "ExtendedMeasurementAssets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace
{
using TexelGenerator = std::function<float4(float, float)>;

float clamp01(float x)
{
    return std::max(0.f, std::min(1.f, x));
}

uint32_t packUnorm8(float4 value)
{
    value = clamp(value, float4(0.f), float4(1.f));
    const uint32_t r = static_cast<uint32_t>(std::round(value.x * 255.f));
    const uint32_t g = static_cast<uint32_t>(std::round(value.y * 255.f));
    const uint32_t b = static_cast<uint32_t>(std::round(value.z * 255.f));
    const uint32_t a = static_cast<uint32_t>(std::round(value.w * 255.f));
    return r | (g << 8) | (b << 16) | (a << 24);
}

float fract(float x)
{
    return x - std::floor(x);
}

float hash21(int x, int y, int seed)
{
    uint32_t n = static_cast<uint32_t>(x) * 0x1f123bb5u ^ static_cast<uint32_t>(y) * 0x5f356495u ^
                 static_cast<uint32_t>(seed) * 0x85ebca6bu;
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return static_cast<float>(n & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

float smooth(float x)
{
    return x * x * (3.f - 2.f * x);
}

float smooth01(float x)
{
    return smooth(clamp01(x));
}

float lerpScalar(float a, float b, float t)
{
    return a * (1.f - t) + b * t;
}

float3 lerpColor(float3 a, float3 b, float t)
{
    return a * (1.f - t) + b * t;
}

float valueNoise(float u, float v, int seed)
{
    const int ix = static_cast<int>(std::floor(u));
    const int iy = static_cast<int>(std::floor(v));
    const float fx = smooth(fract(u));
    const float fy = smooth(fract(v));

    const float a = hash21(ix, iy, seed);
    const float b = hash21(ix + 1, iy, seed);
    const float c = hash21(ix, iy + 1, seed);
    const float d = hash21(ix + 1, iy + 1, seed);
    const float x0 = lerpScalar(a, b, fx);
    const float x1 = lerpScalar(c, d, fx);
    return lerpScalar(x0, x1, fy);
}

float fbm(float u, float v, int seed, int octaves)
{
    float sum = 0.f;
    float amp = 0.5f;
    float freq = 1.f;
    float norm = 0.f;

    for (int i = 0; i < octaves; ++i)
    {
        sum += valueNoise(u * freq, v * freq, seed + i * 17) * amp;
        norm += amp;
        freq *= 2.13f;
        amp *= 0.52f;
    }
    return sum / std::max(norm, 1e-5f);
}

float ridge(float x)
{
    return 1.f - std::abs(2.f * x - 1.f);
}

float reliefField(float u, float v)
{
    const float grain = fbm(u * 9.5f + 3.1f, v * 9.5f - 1.7f, 41, 5);
    const float veins = ridge(std::sin((u * 17.3f + v * 7.1f + grain * 1.8f) * 3.14159265f) * 0.5f + 0.5f);
    const float tiles = ridge(fract(u * 6.f)) * ridge(fract(v * 6.f));
    return clamp01(grain * 0.62f + veins * 0.23f + tiles * 0.15f);
}

float4 normalFromRelief(float u, float v, float texelSize, float strength)
{
    const float hL = reliefField(u - texelSize, v);
    const float hR = reliefField(u + texelSize, v);
    const float hD = reliefField(u, v - texelSize);
    const float hU = reliefField(u, v + texelSize);
    const float3 n = normalize(float3((hL - hR) * strength, (hD - hU) * strength, 1.f));
    return float4(n * 0.5f + 0.5f, 1.f);
}

ref<Texture> createTexture(ref<Device> pDevice, const char* name, uint32_t size, const TexelGenerator& generator)
{
    std::vector<uint32_t> pixels(size * size);
    const float inv = 1.f / static_cast<float>(size);

    for (uint32_t y = 0; y < size; ++y)
    {
        for (uint32_t x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) * inv;
            const float v = (static_cast<float>(y) + 0.5f) * inv;
            pixels[y * size + x] = packUnorm8(generator(u, v));
        }
    }

    ref<Texture> pTexture = pDevice->createTexture2D(
        size, size, ResourceFormat::RGBA8Unorm, 1, Resource::kMaxPossible, pixels.data(), ResourceBindFlags::ShaderResource
    );
    pTexture->setName(name);
    return pTexture;
}

void bindTexture(const ref<ProgramVars>& pVars, const char* name, const ref<Texture>& pTexture)
{
    ShaderVar var = pVars->getRootVar().findMember(name);
    if (var.isValid())
        var.setTexture(pTexture);
}

void bindSampler(const ref<ProgramVars>& pVars, const char* name, const ref<Sampler>& pSampler)
{
    ShaderVar var = pVars->getRootVar().findMember(name);
    if (var.isValid())
        var.setSampler(pSampler);
}
} // namespace

namespace ExtendedMeasurementAssetFactory
{
ExtendedMeasurementAssets create(ref<Device> pDevice)
{
    ExtendedMeasurementAssets assets;

    assets.albedo = createTexture(pDevice, "ExtendedMeasurements.Albedo", kTextureSize, [](float u, float v)
    {
        const float base = fbm(u * 5.5f, v * 5.5f, 11, 5);
        const float cells = smooth01(ridge(fract(u * 8.f)) * ridge(fract(v * 8.f)));
        const float oxide = fbm(u * 33.f + 5.f, v * 33.f - 4.f, 12, 3);
        float3 color = lerpColor(float3(0.26f, 0.31f, 0.28f), float3(0.78f, 0.74f, 0.62f), base);
        color = lerpColor(color, float3(0.12f, 0.20f, 0.34f), cells * 0.26f);
        color += float3(0.17f, 0.06f, 0.03f) * smooth01(oxide) * 0.45f;
        return float4(clamp(color, float3(0.f), float3(1.f)), 1.f);
    });

    assets.normal = createTexture(pDevice, "ExtendedMeasurements.Normal", kTextureSize, [](float u, float v)
    {
        return normalFromRelief(u, v, 1.f / static_cast<float>(kTextureSize), 5.5f);
    });

    assets.roughnessMetalness = createTexture(pDevice, "ExtendedMeasurements.RoughnessMetalness", kTextureSize, [](float u, float v)
    {
        const float n = fbm(u * 7.5f + 10.f, v * 7.5f, 21, 5);
        const float scratches = ridge(std::sin((u * 113.f + v * 19.f) * 3.14159265f) * 0.5f + 0.5f);
        const float roughness = clamp01(0.28f + n * 0.55f + scratches * 0.12f);
        const float metalness = clamp01(0.04f + smooth01(fbm(u * 4.f, v * 4.f, 22, 4)) * 0.24f);
        return float4(roughness, metalness, n, 1.f);
    });

    assets.ambientOcclusion = createTexture(pDevice, "ExtendedMeasurements.AmbientOcclusion", kTextureSize, [](float u, float v)
    {
        const float grout = std::min(ridge(fract(u * 8.f)), ridge(fract(v * 8.f)));
        const float cavities = fbm(u * 22.f - 3.f, v * 22.f + 7.f, 31, 4);
        const float ao = clamp01(0.58f + grout * 0.31f + cavities * 0.14f);
        return float4(ao, ao, ao, 1.f);
    });

    assets.emissive = createTexture(pDevice, "ExtendedMeasurements.Emissive", kTextureSize, [](float u, float v)
    {
        const float line = smooth01(1.f - std::abs(fract((u + v * 0.37f) * 18.f) * 2.f - 1.f) * 18.f);
        const float pulse = fbm(u * 13.f + 2.f, v * 13.f, 52, 3);
        const float e = smooth01(line * pulse);
        return float4(float3(0.2f, 0.85f, 1.0f) * e * 0.65f, 1.f);
    });

    assets.detailAlbedo = createTexture(pDevice, "ExtendedMeasurements.DetailAlbedo", kTextureSize, [](float u, float v)
    {
        const float speckle = fbm(u * 96.f, v * 96.f, 61, 3);
        const float fiber = std::sin((u * 185.f + fbm(u * 18.f, v * 18.f, 62, 2) * 8.f) * 3.14159265f) * 0.5f + 0.5f;
        const float d = clamp01(0.46f + speckle * 0.35f + fiber * 0.19f);
        return float4(d, d * 0.94f, d * 0.82f, 1.f);
    });

    assets.detailNormal = createTexture(pDevice, "ExtendedMeasurements.DetailNormal", kTextureSize, [](float u, float v)
    {
        const float texel = 1.f / static_cast<float>(kTextureSize);
        const float hL = fbm((u - texel) * 64.f, v * 64.f, 71, 3);
        const float hR = fbm((u + texel) * 64.f, v * 64.f, 71, 3);
        const float hD = fbm(u * 64.f, (v - texel) * 64.f, 71, 3);
        const float hU = fbm(u * 64.f, (v + texel) * 64.f, 71, 3);
        const float3 n = normalize(float3((hL - hR) * 9.f, (hD - hU) * 9.f, 1.f));
        return float4(n * 0.5f + 0.5f, 1.f);
    });

    assets.mask = createTexture(pDevice, "ExtendedMeasurements.MaskLut", kTextureSize, [](float u, float v)
    {
        const float lookup = fbm(u * 3.1f + v * 1.7f, v * 5.3f - u * 0.7f, 81, 5);
        const float stripe = ridge(fract(u * 11.f + v * 5.f));
        const float edgeWear = smooth01(std::max(ridge(fract(u * 8.f)), ridge(fract(v * 8.f))));
        return float4(lookup, stripe, edgeWear, 1.f);
    });

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(TextureFilteringMode::Linear, TextureFilteringMode::Linear, TextureFilteringMode::Linear)
        .setAddressingMode(TextureAddressingMode::Wrap, TextureAddressingMode::Wrap, TextureAddressingMode::Wrap);
    assets.sampler = pDevice->createSampler(samplerDesc);

    return assets;
}

void bind(const ref<ProgramVars>& pVars, const ExtendedMeasurementAssets& assets)
{
    bindTexture(pVars, "gExtendedAlbedo", assets.albedo);
    bindTexture(pVars, "gExtendedNormal", assets.normal);
    bindTexture(pVars, "gExtendedRoughnessMetalness", assets.roughnessMetalness);
    bindTexture(pVars, "gExtendedAmbientOcclusion", assets.ambientOcclusion);
    bindTexture(pVars, "gExtendedEmissive", assets.emissive);
    bindTexture(pVars, "gExtendedDetailAlbedo", assets.detailAlbedo);
    bindTexture(pVars, "gExtendedDetailNormal", assets.detailNormal);
    bindTexture(pVars, "gExtendedMask", assets.mask);
    bindSampler(pVars, "gExtendedSampler", assets.sampler);
}
} // namespace ExtendedMeasurementAssetFactory
