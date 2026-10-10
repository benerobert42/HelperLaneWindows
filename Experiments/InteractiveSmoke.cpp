#include "HelperLaneViz.h"
#include "BenchmarkSupport.h"
#include <map>
#include <iostream>
#include <fstream>

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
    if (mSmokeStage > 0 && mSmokeStage <= 3 &&
        (mBenchmarkSavedState || mBenchmarkStatus.empty() || mBenchmarkStatus.find("complete") == std::string::npos))
    {
        std::cerr << "Interactive benchmark failed: " << mBenchmarkStatus << "\n";
        mSmokeFailed = true;
        shutdown();
        return false;
    }
    if (mSmokeStage >= 4 && mSmokeStage <= 6)
    {
        // Warm up each fixed view, then compare draw and callback scopes without
        // mixing geometry construction/shader compilation into the statistics.
        static std::map<std::string, std::vector<float>> samples;
        if (mSmokeFrames == 16)
            samples.clear();
        if (mSmokeFrames > 16)
        {
            for (auto* event : getDevice()->getProfiler()->getEvents())
                if (event->getName().find("/onFrameRender") == 0 && event->getGpuTime() > 0)
                    samples[event->getName()].push_back(event->getGpuTime());
        }
        if (mSmokeFrames < 96)
            return true;
        for (const auto& [name, values] : samples)
        {
            const auto stats = BenchmarkSupport::summarize(values);
            std::cout << "Steady "
                      << (mSmokeStage == 4   ? "lit"
                          : mSmokeStage == 5 ? "helpers"
                                             : "helpers-readback")
                      << " " << name << ": mean=" << stats.mean << " min=" << stats.minimum << " max=" << stats.maximum
                      << " stddev=" << stats.standardDeviation << " ms\n";
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
        std::cout << "Interactive smoke " << (mSmokeFailed ? "failed" : "passed")
                  << ": 1x and 4x volume, planar, lit preview, UI layout, state restoration.\n";
        shutdown();
        return false;
    }
    if (mSmokeStage == 3)
    {
        auto require = [](bool ok, const char* message)
        {
            if (!ok)
                throw std::runtime_error(message);
        };
        const auto fixture = std::filesystem::path(HELPERLANE_SOURCE_DIR) / "analysis/review/benchmark-svg";
        std::filesystem::create_directories(fixture);
        std::ofstream(fixture / "triangle.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
                                                   "<path d=\"M 0 0 L 100 0 L 0 100 Z\"/></svg>";
        benchmarkFolder(fixture.string());
        require(benchmarkRunning() && mBenchmarkCases.size() == 72, "Folder sweep lost cases");
        require(
            mBenchmarkCases[0].measurement.msaa == 1 && mBenchmarkCases[18].cols == 100 && mBenchmarkCases[36].measurement.msaa == 4 &&
                mBenchmarkCases[12].ellipseY == 0.2f,
            "Folder sweep changed MSAA/grid/ellipse settings"
        );
        // Measure one real SVG case; the other cases exercise the same runner.
        mBenchmarkCases.resize(1);
        while (benchmarkRunning())
        {
            processBenchmarkFrame(getRenderContext(), getTargetFbo());
            getDevice()->endFrame();
        }
        require(
            mBenchmarkStatus.find("complete") != std::string::npos && mTriangulationType == 8 && mUseCircle && mCircleVertexCount == 32 &&
                cntMSAA == 4 && mVizMode == VizMode::Wireframe,
            "SVG measurement did not restore interactive settings"
        );
        startPlanarBenchmark({8});
        while (!mBenchmarkRunner->started() || mBenchmarkRunner->result().samples.size() < 2)
        {
            processBenchmarkFrame(getRenderContext(), getTargetFbo());
            getDevice()->endFrame();
        }
        finishBenchmarkSession("Smoke test cancellation.");
        require(
            !benchmarkRunning() && mUseCircle && mTriangulationType == 8 && mVizMode == VizMode::Wireframe,
            "Cancellation did not restore interactive settings"
        );
        std::cout << "Partial cancellation output: " << mBenchmarkOutputPath << '\n';
        // A regular file used as the parent directory forces an output error.
        const auto blocker = fixture / "output-blocker";
        std::ofstream(blocker) << "fixture";
        InteractiveCase testCase;
        testCase.volume = false;
        testCase.method = 8;
        testCase.measurement.msaa = 4;
        startInteractiveBenchmark({testCase}, (blocker / "bad.csv").string());
        require(
            !benchmarkRunning() && mBenchmarkStatus.find("failed") != std::string::npos && mTriangulationType == 8 &&
                mVizMode == VizMode::Wireframe,
            "Output failure did not restore interactive settings"
        );
        std::filesystem::remove(blocker);
        std::filesystem::remove(fixture / "triangle.svg");
        std::filesystem::remove(fixture);
        std::cout << "Folder sweep, SVG measurement, partial cancellation and output failure checks passed.\n";
        mBenchmarkStatus.clear();
        mBenchmarkOutputPath.clear();
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
        startVolumeBenchmark({3, 0, 2});
        std::cout << "Interactive volume " << cntMSAA << "x: " << mBenchmarkOutputPath << "\n";
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
        startPlanarBenchmark({8, 10});
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
    app.getTargetFbo()->getColorTexture(0)->captureToFile(
        0, 0, capture, Falcor::Bitmap::FileFormat::PngFile, Falcor::Bitmap::ExportFlags::None, false
    );
    return app.mSmokeFailed ? 1 : result;
}
