#include "LitPreview.h"
#include "Triangulation/SurfaceGeometryMath.h"

using namespace Falcor;

void LitPreviewSettings::drawGui(Gui::Widgets& w)
{
    auto group = w.group("GraphEx Raster Lighting");
    if (!group) return;
    group.rgbColor("Background", background);
    group.rgbColor("Material Ambient", materialAmbient);
    group.rgbColor("Material Diffuse", materialDiffuse);
    group.rgbColor("Material Specular", materialSpecular);
    group.var("Shininess", shininess, 1.f, 256.f);
    group.dropdown("Light Type", {{0, "Point"}, {1, "Directional"}}, lightType);
    if (lightType == 0) group.var("Light Position", lightPosition);
    else group.var("Light Direction", lightDirection);
    group.rgbColor("Light Ambient", lightAmbient);
    group.rgbColor("Light Diffuse", lightDiffuse);
    group.rgbColor("Light Specular", lightSpecular);
    group.var("Attenuation (constant, linear, quadratic)", attenuation, 0.f, 100.f);
}

void LitPreview::draw(RenderContext* context, const ref<Fbo>& target,
    const ref<Buffer>& positions, const ref<Buffer>& indices,
    const std::vector<float3>& vertices, const std::vector<uint32_t>& sourceIndices,
    uint32_t indexCount, const CameraParams& camera, const VolumeModelParams& model,
    const VolumeGridParams& grid, const float3& cameraPosition, const LitPreviewSettings& s)
{
    if (!indexCount) return;
    auto device = context->getDevice();
    if (!mpProgram)
    {
        ProgramDesc desc;
        desc.addShaderLibrary("Samples/HelperLaneWindows/Shaders/LitPreview.slang").vsEntry("vsMain").psEntry("psMain");
        mpProgram = Program::create(device, desc);
        mpVars = ProgramVars::create(device, mpProgram.get());
        mpState = GraphicsState::create(device);
        mpState->setProgram(mpProgram);
        DepthStencilState::Desc depth;
        depth.setDepthEnabled(true).setDepthWriteMask(true).setDepthFunc(ComparisonFunc::Less);
        mpState->setDepthStencilState(DepthStencilState::create(depth));
        RasterizerState::Desc raster;
        raster.setCullMode(RasterizerState::CullMode::None);
        mpState->setRasterizerState(RasterizerState::create(raster));
    }
    if (mpPositions != positions)
    {
        // Area-weighted source normals stay fixed when only the triangulation changes.
        // Normal upload is lazy: diagnostic and performance modes incur no extra work.
        std::vector<float3> normals(vertices.size(), float3(0.f));
        for (size_t i = 0; i + 2 < sourceIndices.size(); i += 3)
        {
            const auto a = sourceIndices[i], b = sourceIndices[i + 1], c = sourceIndices[i + 2];
            const auto normal = SurfaceGeometryMath::cross(vertices[b] - vertices[a], vertices[c] - vertices[a]);
            normals[a] += normal; normals[b] += normal; normals[c] += normal;
        }
        for (auto& normal : normals)
        {
            const double length = length3(normal);
            normal = length > 1e-20 ? normal / static_cast<float>(length) : float3(0.f, 1.f, 0.f);
        }
        auto buffer = device->createBuffer(normals.size() * sizeof(float3), ResourceBindFlags::Vertex,
            MemoryType::DeviceLocal, normals.data());
        auto layout = VertexLayout::create();
        auto positionLayout = VertexBufferLayout::create();
        positionLayout->addElement("POSITION", 0, ResourceFormat::RGB32Float, 1, 0);
        auto normalLayout = VertexBufferLayout::create();
        normalLayout->addElement("NORMAL", 0, ResourceFormat::RGB32Float, 1, 1);
        layout->addBufferLayout(0, positionLayout);
        layout->addBufferLayout(1, normalLayout);
        mpVao = Vao::create(Vao::Topology::TriangleList, layout, {positions, buffer}, indices, ResourceFormat::R32Uint);
        mpPositions = positions;
    }
    auto root = mpVars->getRootVar();
    root["Camera"].setBlob(camera);
    root["VolumeModel"].setBlob(model);
    root["VolumeGrid"].setBlob(grid);
    auto lighting = root["Lighting"];
    lighting["normalTransform"] = math::transpose(math::inverse(model.transform));
    lighting["cameraPosition"] = cameraPosition;
    lighting["materialAmbient"] = s.materialAmbient;
    lighting["materialDiffuse"] = s.materialDiffuse;
    lighting["materialSpecular"] = s.materialSpecular;
    lighting["shininess"] = s.shininess;
    lighting["lightType"] = s.lightType;
    lighting["lightPosition"] = s.lightPosition;
    lighting["lightDirection"] = s.lightDirection;
    lighting["lightAmbient"] = s.lightAmbient;
    lighting["lightDiffuse"] = s.lightDiffuse;
    lighting["lightSpecular"] = s.lightSpecular;
    lighting["attenuationCoefficients"] = s.attenuation;
    mpState->setFbo(target);
    mpState->setVao(mpVao);
    context->drawIndexedInstanced(mpState.get(), mpVars.get(), indexCount, grid.instanceCount, 0, 0, 0);
}
