#include "HelperLaneViz.h"
#include "BenchmarkSupport.h"
#include <map>
#include <iostream>

// Exercise the real interactive render path without automating a desktop window.
// Short runs cover 1x/4x depth, the diagnostic phase, CSV output and restoration.
bool HelperLaneViz::advanceInteractiveSmoke()
{
    if (++mSmokeFrames > 500)
    {
        std::cerr << "Interactive smoke timed out.\n";
        mSmokeFailed = true;
        shutdown();
        return false;
    }
    if (benchmarkRunning())
        return true;
    if (mSmokeStage && (mBenchmarkSavedState || mBenchmarkLog.empty() || mBenchmarkLog.back().find("complete") == std::string::npos))
    {
        std::cerr << "Interactive benchmark failed: " << (mBenchmarkLog.empty() ? "no result" : mBenchmarkLog.back()) << "\n";
        mSmokeFailed = true;
        shutdown();
        return false;
    }
    if (mSmokeStage >= 4 && mSmokeStage <= 6)
    {
        // Warm up each fixed view, then compare draw and callback scopes without
        // mixing geometry construction/shader compilation into the statistics.
        static std::map<std::string, std::vector<float>> samples;
        if (mSmokeFrames == 16) samples.clear();
        if (mSmokeFrames > 16)
        {
            for (auto* event : getDevice()->getProfiler()->getEvents())
                if (event->getName().find("/onFrameRender") == 0 && event->getGpuTime() > 0)
                    samples[event->getName()].push_back(event->getGpuTime());
        }
        if (mSmokeFrames < 96) return true;
        for (const auto& [name, values] : samples)
        {
            const auto stats = BenchmarkSupport::summarize(values);
            std::cout << "Steady " << (mSmokeStage == 4 ? "lit" : mSmokeStage == 5 ? "helpers" : "helpers-readback")
                      << " " << name << ": mean=" << stats.mean << " min=" << stats.minimum
                      << " max=" << stats.maximum << " stddev=" << stats.standardDeviation << " ms\n";
        }
        ++mSmokeStage;
        mSmokeFrames = 0;
        mReadBackHelperLaneCount = mSmokeStage == 6;
        mVizMode = mSmokeStage == 7 ? VizMode::LitPreview : VizMode::HelperLanes;
        mpProgram->addDefine("VIZ_MODE", "0");
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", mReadBackHelperLaneCount ? "1" : "0");
        return true;
    }
    if (mSmokeStage == 7)
    {
        ++mSmokeStage;
        return true;
    }
    if (mSmokeStage == 8)
    {
        if (mTriangulationType != 8 || !mUseCircle || mVizMode != VizMode::LitPreview || !mUiLayoutInitialized)
            mSmokeFailed = true;
        std::cout << "Interactive smoke " << (mSmokeFailed ? "failed" : "passed") << ": 1x and 4x volume, planar, lit preview, UI layout, state restoration.\n";
        shutdown();
        return false;
    }
    if (mSmokeStage == 3)
    {
        mGeometryDimensionality = GeometryDimensionality::Volumetric;
        mVolumeGeometryType = 8;
        mVolumeTriangulationType = 3;
        mVizMode = VizMode::LitPreview;
        generateVolumeGeometry();
        ++mSmokeStage;
        mSmokeFrames = 0;
        return true;
    }
    mBenchmarkConfig = {2, 4, 4, 2};
    if (mSmokeStage < 2)
    {
        mGeometryDimensionality = GeometryDimensionality::Volumetric;
        cntMSAA = mSmokeStage ? 4 : 1;
        mVizMode = mSmokeStage ? VizMode::Wireframe : VizMode::HelperLanes;
        mVolumeTriangulationType = 3;
        CreateMSAATargets();
        if (!mpFbo->getDepthStencilTexture() || mpFbo->getDepthStencilTexture()->getSampleCount() != cntMSAA)
        {
            mSmokeFailed = true;
            shutdown();
            return false;
        }
        startVolumeBenchmark();
        mVolumeBenchmark.methods = {3, 0, 2};
        std::cout << "Interactive volume " << cntMSAA << "x: " << mVolumeBenchmark.outputPath << "\n";
    }
    else
    {
        if (mVolumeTriangulationType != 3 || cntMSAA != 4)
        {
            mSmokeFailed = true;
            shutdown();
            return false;
        }
        mGeometryDimensionality = GeometryDimensionality::Planar;
        mUseCircle = true;
        mCircleVertexCount = 32;
        mTriangulationType = 8;
        mVizMode = VizMode::Wireframe;
        generateCircle();
        beginBenchmarkSession();
        mPlanarBenchmark.active = true;
        mPlanarBenchmark.currentMethod = 0;
        mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
        mPlanarBenchmark.methods = {8, 10};
    }
    ++mSmokeStage;
    return true;
}

int runInteractiveSmoke()
{
    Falcor::SampleAppConfig config;
    config.headless = true;
    config.showUI = true;
    config.windowDesc.width = 1920;
    config.windowDesc.height = 1080;
    config.deviceDesc.type = Falcor::Device::Type::D3D12;
    config.deviceDesc.enableDebugLayer = false;
    HelperLaneViz app(config);
    if (app.mGeometryDimensionality != HelperLaneViz::GeometryDimensionality::Volumetric || app.mVolumeTriangulationType != 3)
        throw std::runtime_error("Default must be volumetric Source Faces");
    app.mSmokeStage = 0;
    int result = app.run();
    const auto capture = std::filesystem::path(HELPERLANE_SOURCE_DIR) / "analysis/screenshots/ui-layout.png";
    std::filesystem::create_directories(capture.parent_path());
    app.getTargetFbo()->getColorTexture(0)->captureToFile(0, 0, capture, Falcor::Bitmap::FileFormat::PngFile, Falcor::Bitmap::ExportFlags::None, false);
    return app.mSmokeFailed ? 1 : result;
}
