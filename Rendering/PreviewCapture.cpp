#include "HelperLaneViz.h"
#include "Import/ModelPresets.h"
#include "Import/SVGLoader.h"
#include "Triangulation/ShaderTypes.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>
#include <limits>
#include <map>
#include <set>

void HelperLaneViz::renderLitPreview(RenderContext* context, const ref<Fbo>& target)
{
    context->clearFbo(target.get(), float4(mLitSettings.background, 1.f), 1.f, 0);
    float3 cameraPosition;
    CameraParams camera{makeVolumeViewProj(target->getWidth(), target->getHeight(), mVolumeGridCols,
        mVolumeGridRows, mVolumeGridLayers, mVolumeUse3DInstancing, mVolumeGridSpacing, mVolumeBoundsRadius, &cameraPosition)};
    VolumeGridParams grid{std::max(mVolumeInstanceCount, 1u), mVolumeGridCols, mVolumeGridRows,
        mVolumeGridLayers, mVolumeUse3DInstancing ? 1u : 0u, mVolumeGridSpacing, float2(0.f)};
    mLitPreview.draw(context, target, mpVB, mpIB, mVolumeVertices, mVolumeCachedBaseline.indices,
        mIndexCount, camera, VolumeModelParams{mVolumeModelTransform}, grid, cameraPosition, mLitSettings);
}

void HelperLaneViz::frameSvgForCapture(const std::string& path)
{
    // The interactive loader normalizes each axis independently. Restore
    // SVG proportions for capture, with 5% margins and SVG's downward Y.
    float2 lo(std::numeric_limits<float>::infinity()), hi(-std::numeric_limits<float>::infinity());
    for (const auto& shape : SVGLoader::ParseSvgToShapes(path, mMaxBezierDeviation))
        for (const auto& v : shape.outerBoundary) { lo = math::min(lo, v.pos); hi = math::max(hi, v.pos); }
    const float2 size = hi - lo;
    if (!(size.x > 0 && size.y > 0)) throw std::runtime_error("SVG screenshot has invalid bounds");
    float2 fit = size / float2(float(getTargetFbo()->getWidth()), float(getTargetFbo()->getHeight()));
    fit *= 0.9f / std::max(fit.x, fit.y);
    for (auto& v : mVertices) { v.pos = (v.pos - float2(0.5f)) * fit + float2(0.5f); v.pos.y = 1.f - v.pos.y; }
}

void HelperLaneViz::saveScreenshot(const ref<Fbo>& target)
{
    const std::filesystem::path path(mScreenshotPath);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    // Capture final display bytes, after MSAA resolve/blit and before UI. No resize,
    // gamma conversion, vertical flip, timestamps, or asynchronous file writes.
    target->getColorTexture(0)->captureToFile(0, 0, path, Bitmap::FileFormat::PngFile, Bitmap::ExportFlags::None, false);
}

namespace
{
using Json = nlohmann::json;
float3 readVector(const Json& value)
{
    if (!value.is_array() || value.size() != 3) throw std::runtime_error("Expected an array of three finite numbers");
    float3 v(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) throw std::runtime_error("Nonfinite vector");
    return v;
}
}

int runScreenshot(int argc, char** argv)
{
    try
    {
        if (argc != 4) throw std::runtime_error("Usage: HelperLaneWindows --screenshot recipe.json output.png");
        std::ifstream input(argv[2]);
        if (!input) throw std::runtime_error("Cannot open screenshot recipe");
        const Json recipe = Json::parse(input);
        const std::set<std::string> keys{"model", "width", "height", "msaa", "method", "visualization", "rotation", "lighting", "bezierDeviation"};
        if (!recipe.is_object()) throw std::runtime_error("Screenshot recipe must be an object");
        for (const auto& item : recipe.items())
            if (!keys.count(item.key())) throw std::runtime_error("Unknown recipe field: " + item.key());
        SampleAppConfig config;
        config.headless = true;
        config.showUI = false;
        config.pauseTime = true;
        config.windowDesc.width = recipe.value("width", 1920u);
        config.windowDesc.height = recipe.value("height", 1080u);
        config.deviceDesc.enableDebugLayer = false;
        if (!config.windowDesc.width || config.windowDesc.width > 7680 || !config.windowDesc.height || config.windowDesc.height > 4320)
            throw std::runtime_error("Dimensions must be 1..7680 by 1..4320");
        HelperLaneViz app(config);
        app.cntMSAA = recipe.value("msaa", 4u);
        if (app.cntMSAA != 1 && app.cntMSAA != 2 && app.cntMSAA != 4 && app.cntMSAA != 8)
            throw std::runtime_error("MSAA must be 1, 2, 4, or 8");
        const std::string model = recipe.value("model", "Suzanne");
        const bool planar = std::filesystem::path(model).extension() == ".svg";
        app.mGeometryDimensionality = planar ? HelperLaneViz::GeometryDimensionality::Planar : HelperLaneViz::GeometryDimensionality::Volumetric;
        if (planar)
        {
            app.mUseCircle = false;
            app.mSvgPath = (std::filesystem::path(argv[2]).parent_path() / model).string();
            app.mTriangulationType = recipe.value("method", 7u);
            app.mMaxBezierDeviation = recipe.value("bezierDeviation", 1.f);
            app.mUseMeshOptimizer = true;
            if (app.mTriangulationType > 10 || !std::isfinite(app.mMaxBezierDeviation) || app.mMaxBezierDeviation <= 0)
                throw std::runtime_error("SVG method must be 0..10 and Bezier deviation must be positive");
        }
        app.mVolumeGeometryType = 7;
        app.mVolumeObjPath = (std::filesystem::path(argv[2]).parent_path() / model).string();
        for (uint32_t i = 0; i < kObjModelPresets.size(); ++i)
            if (model == kObjModelPresets[i].name) app.mVolumeGeometryType = 8 + i;
        for (uint32_t i = 0; i < kStudyGeometryPresets.size(); ++i)
            if (model == std::string("Study: ") + kStudyGeometryPresets[i].name) app.mVolumeGeometryType = kStudyGeometryFirst + i;
        app.mVolumeTriangulationType = recipe.value("method", 3u);
        if (!planar && app.mVolumeTriangulationType > 5) throw std::runtime_error("Method must be 0..5 (3 = Source Faces)");
        const std::map<std::string, HelperLaneViz::VizMode> modes{
            {"lit", HelperLaneViz::VizMode::LitPreview}, {"wireframe", HelperLaneViz::VizMode::Wireframe},
            {"helpers", HelperLaneViz::VizMode::HelperLanes}};
        app.mVizMode = modes.at(recipe.value("visualization", planar ? "wireframe" : "lit"));
        if (planar && app.mVizMode == HelperLaneViz::VizMode::LitPreview)
            throw std::runtime_error("Lit preview requires a surface model");
        const float3 rotation = readVector(recipe.value("rotation", Json::array({0.f, 0.f, 0.f})));
        app.mVolumeModelTransform = mul(math::matrixFromRotationZ(math::radians(rotation.z)),
            mul(math::matrixFromRotationY(math::radians(rotation.y)), math::matrixFromRotationX(math::radians(rotation.x))));
        auto& s = app.mLitSettings;
        const Json lighting = recipe.value("lighting", Json::object());
        if (!lighting.is_object()) throw std::runtime_error("lighting must be an object");
        const std::map<std::string, float3*> vectors{
            {"background", &s.background}, {"materialAmbient", &s.materialAmbient}, {"materialDiffuse", &s.materialDiffuse},
            {"materialSpecular", &s.materialSpecular}, {"lightPosition", &s.lightPosition}, {"lightDirection", &s.lightDirection},
            {"lightAmbient", &s.lightAmbient}, {"lightDiffuse", &s.lightDiffuse}, {"lightSpecular", &s.lightSpecular},
            {"attenuation", &s.attenuation}};
        for (const auto& item : lighting.items())
        {
            if (item.key() == "shininess") s.shininess = item.value().get<float>();
            else if (item.key() == "lightType") s.lightType = item.value().get<uint32_t>();
            else if (const auto field = vectors.find(item.key()); field != vectors.end()) *field->second = readVector(item.value());
            else throw std::runtime_error("Unknown lighting field: " + item.key());
        }
        if (s.lightType > 1 || !std::isfinite(s.shininess) || s.shininess <= 0.f || s.attenuation.x <= 0.f
            || s.attenuation.y < 0.f || s.attenuation.z < 0.f || (s.lightType == 1 && length3(s.lightDirection) == 0.0))
            throw std::runtime_error("Invalid light type, shininess, attenuation, or directional light vector");
        app.mScreenshotPath = argv[3];
        if (std::filesystem::path(app.mScreenshotPath).extension() != ".png") throw std::runtime_error("Output must end in .png");
        app.mRequestScreenshot = true;
        app.mCaptureAndExit = true;
        return app.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Screenshot failed: " << e.what() << '\n';
        return 1;
    }
}
