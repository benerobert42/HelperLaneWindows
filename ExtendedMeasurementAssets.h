/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 **************************************************************************/
#pragma once

#include "Falcor.h"

using namespace Falcor;

struct ExtendedMeasurementAssets
{
    ref<Texture> albedo;
    ref<Texture> normal;
    ref<Texture> roughnessMetalness;
    ref<Texture> ambientOcclusion;
    ref<Texture> emissive;
    ref<Texture> detailAlbedo;
    ref<Texture> detailNormal;
    ref<Texture> mask;
    ref<Sampler> sampler;
};

namespace ExtendedMeasurementAssetFactory
{
static constexpr uint32_t kTextureSize = 1024;

ExtendedMeasurementAssets create(ref<Device> pDevice);
void bind(const ref<ProgramVars>& pVars, const ExtendedMeasurementAssets& assets);
} // namespace ExtendedMeasurementAssetFactory
