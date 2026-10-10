#include "HelperLaneViz.h"
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
    if (mSmokeStage == 3)
    {
        if (mTriangulationType != 8 || !mUseCircle)
            mSmokeFailed = true;
        std::cout << "Interactive smoke " << (mSmokeFailed ? "failed" : "passed") << ": 1x and 4x volume, planar, state restoration.\n";
        shutdown();
        return false;
    }
    mBenchmarkConfig = {2, 4, 4, 2};
    if (mSmokeStage < 2)
    {
        mGeometryDimensionality = GeometryDimensionality::Volumetric;
        cntMSAA = mSmokeStage ? 4 : 1;
        mVizMode = mSmokeStage ? VizMode::MaterialStress : VizMode::Texture;
        mVolumeTriangulationType = 3;
        mUseDummyTexture = true;
        mpProgram->addDefine("USE_DUMMY_TEXTURE", "1");
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
        mVizMode = VizMode::Texture;
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
    config.showUI = false;
    config.windowDesc.width = 640;
    config.windowDesc.height = 480;
    config.deviceDesc.type = Falcor::Device::Type::D3D12;
    config.deviceDesc.enableDebugLayer = false;
    HelperLaneViz app(config);
    app.mSmokeStage = 0;
    int result = app.run();
    return app.mSmokeFailed ? 1 : result;
}
