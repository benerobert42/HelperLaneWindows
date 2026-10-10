#pragma once
#include "Falcor.h"
#include "SampleSupport.h"

// The material and single-light fields used by GraphEx's raster Lighting.ps.slang.
struct LitPreviewSettings
{
    Falcor::float3 background = Falcor::float3(0.15f);
    Falcor::float3 materialAmbient = Falcor::float3(0.5f);
    Falcor::float3 materialDiffuse = {0.55f, 0.65f, 0.8f};
    Falcor::float3 materialSpecular = Falcor::float3(0.4f);
    float shininess = 32.f;
    uint32_t lightType = 0; // GraphEx: point = 0, directional = 1.
    Falcor::float3 lightPosition = {0.f, 1.f, 1.f};
    Falcor::float3 lightDirection = {0.f, 1.f, 1.f};
    Falcor::float3 lightAmbient = Falcor::float3(0.125f);
    Falcor::float3 lightDiffuse = Falcor::float3(1.f);
    Falcor::float3 lightSpecular = Falcor::float3(1.f);
    Falcor::float3 attenuation = {1.f, 0.f, 0.f}; // constant, linear, quadratic
    void drawGui(Falcor::Gui::Widgets& widgets);
};

class LitPreview
{
public:
    void draw(Falcor::RenderContext* context, const Falcor::ref<Falcor::Fbo>& target,
        const Falcor::ref<Falcor::Buffer>& positions, const Falcor::ref<Falcor::Buffer>& indices,
        const std::vector<Falcor::float3>& vertices, const std::vector<uint32_t>& sourceIndices,
        uint32_t indexCount, const CameraParams& camera, const VolumeModelParams& model,
        const VolumeGridParams& grid, const Falcor::float3& cameraPosition, const LitPreviewSettings& settings);
private:
    Falcor::ref<Falcor::Program> mpProgram;
    Falcor::ref<Falcor::ProgramVars> mpVars;
    Falcor::ref<Falcor::GraphicsState> mpState;
    Falcor::ref<Falcor::Vao> mpVao;
    Falcor::ref<Falcor::Buffer> mpPositions;
};
