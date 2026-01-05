/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "HelperLaneViz.h"

#include "TriangulationHelpers.h"
#include "SVGLoader.h"

#include "Falcor.h"
#include "Core/Program/ProgramManager.h"

#include <chrono>
#include <filesystem>
#include <windows.h>
#include <shlobj.h>
#include <sstream>

using namespace Falcor;

FALCOR_EXPORT_D3D12_AGILITY_SDK

struct GridParams
{
    uint32_t cols;
    uint32_t rows;
    float2 cellSize;
    float2 origin;
    float scale;
};

uint32_t mSampleGuiWidth = 250;
uint32_t mSampleGuiHeight = 200;
uint32_t mSampleGuiPositionX = 20;
uint32_t mSampleGuiPositionY = 40;

HelperLaneViz::HelperLaneViz(const SampleAppConfig& config) : SampleApp(config) {}
HelperLaneViz::~HelperLaneViz() {}

void HelperLaneViz::CreateMSAATargets()
{
    mpFbo = Fbo::create(getDevice());
    ref<Texture> tex = getDevice()->createTexture2DMS(
        getTargetFbo()->getWidth(), getTargetFbo()->getHeight(),
        ResourceFormat::RGBA16Float,
        cntMSAA,
        1,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::RenderTarget
    );
    mpFbo->attachColorTarget(tex, 0);

    mpResolvedTexture = getDevice()->createTexture2D(
        getTargetFbo()->getWidth(),
        getTargetFbo()->getHeight(),
        ResourceFormat::RGBA16Float,
        1,
        1);
}

void HelperLaneViz::onLoad(RenderContext* pRenderContext)
{
    ProgramDesc d;
    d.addShaderLibrary("Samples/HelperLaneWindows/Shaders/MainShader.slang").vsEntry("vsMain").psEntry("psMain");

    pRenderContext->getDevice()->getProgramManager()->setGenerateDebugInfoEnabled(true);

    mpPass = FullScreenPass::create(getDevice(), d);

    mpProgram = Program::create(getDevice(), d);
    mpVars = ProgramVars::create(getDevice(), mpProgram.get());
    mpState = GraphicsState::create(getDevice());
    mpState->setProgram(mpProgram);

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);
    defaultRsState = RasterizerState::create(rsDesc);
    mpState->setRasterizerState(defaultRsState);

    RasterizerState::Desc wireDesc = rsDesc;
    wireDesc.setFillMode(RasterizerState::FillMode::Wireframe);
    wireframeRsState = RasterizerState::create(wireDesc);

    mpLayout = VertexLayout::create();
    auto pVbLayout = VertexBufferLayout::create();
    pVbLayout->addElement("POSITION", 0, ResourceFormat::RG32Float, 1, 0);
    mpLayout->addBufferLayout(0, pVbLayout);

    mSvgPath = "C:/Users/ShaprIntel/Downloads/1920560.svg";
    loadSvg(mSvgPath);

    getDevice()->getProfiler()->setEnabled(true);

    updateGridParams();

    mpHelperLaneCounter = getDevice()->createTexture2D(
        1,
        1,
        ResourceFormat::R32Uint,
        1,
        1,
        nullptr,
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);

    uint32_t texWidth = 2048;
    uint32_t texHeight = 2048;
    std::vector<uint32_t> pixels(texWidth * texHeight, 0xFF0000FF); // ARGB: A=255, R=0, G=0, B=255
    mpDummyTexture = getDevice()->createTexture2D(
        texWidth,
        texHeight,
        ResourceFormat::RGBA8Unorm,
        1,
        1,
        pixels.data(),
        ResourceBindFlags::ShaderResource
    );

    // MSAA render target
    CreateMSAATargets();
}

void HelperLaneViz::loadSvg(const std::string& path)
{
    mVertices.clear();
    mIndices.clear();

    SVGLoader::Triangulator triangulator = [this](std::vector<Vertex>& verts) -> std::vector<uint32_t>
    {
        switch (mTriangulationType)
        {
        case 0:
            return Triangulation::earClippingTriangulation(verts);
        case 1:
            return Triangulation::minimumWeightTriangulation(verts, true);
        case 2:
            return Triangulation::centroidFanTriangulation(verts);
        case 3:
            return Triangulation::greedyMaxAreaTriangulation(verts, true);
        case 4:
            return Triangulation::stripTriangulation(verts);
        case 5:
            return Triangulation::maxMinAreaTriangulation(verts, true);
        case 6:
            return Triangulation::minMaxAreaTriangulation(verts, true);
        case 7:
            return Triangulation::constrainedDelaunay(verts);
        case 8:
            return Triangulation::earClippingMapbox(verts);
        case 9:
            return Triangulation::earClippingMapboxFlipped(verts);
        case 10:
            return Triangulation::constrainedDelaunayFlipped(verts);
        default:
            return Triangulation::earClippingTriangulation(verts);
        }
    };

    auto start = std::chrono::high_resolution_clock::now();

    if (SVGLoader::TessellateSvgToMesh(path, mVertices, mIndices, triangulator, mMaxBezierDeviation))
    {
        uploadGeometry();
    }

    if (mMeasureTriangulationTime)
    {
        auto end = std::chrono::high_resolution_clock::now();
        mLastTriangulationMs = std::chrono::duration<double, std::milli>(end - start).count();
    }
}

void HelperLaneViz::generateCircle()
{
    mVertices.clear();
    mIndices.clear();

    mVertices = Triangulation::CreateVerticesForEllipse(mCircleVertexCount, mEllipseRadiusX, mEllipseRadiusY, float2(0.5f, 0.5f));

    auto start = std::chrono::high_resolution_clock::now();

    switch (mTriangulationType)
    {
    case 0:
        mIndices = Triangulation::earClippingTriangulation(mVertices);
        break;
    case 1:
        mIndices = Triangulation::minimumWeightTriangulation(mVertices, false);
        break;
    case 2:
        mIndices = Triangulation::centroidFanTriangulation(mVertices);
        break;
    case 3:
        mIndices = Triangulation::greedyMaxAreaTriangulation(mVertices, false);
        break;
    case 4:
        mIndices = Triangulation::stripTriangulation(mVertices);
        break;
    case 5:
        mIndices = Triangulation::maxMinAreaTriangulation(mVertices, false);
        break;
    case 6:
        mIndices = Triangulation::minMaxAreaTriangulation(mVertices, false);
        break;
    case 7:
        mIndices = Triangulation::constrainedDelaunay(mVertices);
        break;
    case 8:
        mIndices = Triangulation::earClippingMapbox(mVertices);
        break;
    case 9:
        mIndices = Triangulation::earClippingMapboxFlipped(mVertices);
        break;
    case 10:
        mIndices = Triangulation::constrainedDelaunayFlipped(mVertices);
        break;
    default:
        mIndices = Triangulation::earClippingTriangulation(mVertices);
        break;
    }

    if (mMeasureTriangulationTime)
    {
        auto end = std::chrono::high_resolution_clock::now();
        mLastTriangulationMs = std::chrono::duration<double, std::milli>(end - start).count();
    }

    uploadGeometry();
}

void HelperLaneViz::uploadGeometry()
{
    if (mVertices.size() < 3 || mIndices.empty())
    {
        mIndexCount = 0;
        mpVao = nullptr;
        mpState->setVao(nullptr);
        return;
    }

    const uint64_t vbSize = (uint64_t)mVertices.size() * sizeof(Vertex);
    const uint64_t ibSize = (uint64_t)mIndices.size() * sizeof(uint32_t);

    mpVB = make_ref<Buffer>(getDevice(), vbSize, ResourceBindFlags::Vertex, MemoryType::DeviceLocal, mVertices.data());
    getDevice()->getRenderContext()->updateBuffer(mpVB.get(), mVertices.data(), 0, vbSize);

    mpIB = make_ref<Buffer>(getDevice(), ibSize, ResourceBindFlags::Index, MemoryType::DeviceLocal, mIndices.data());
    getDevice()->getRenderContext()->updateBuffer(mpIB.get(), mIndices.data(), 0, ibSize);

    Vao::BufferVec vbuffers = {mpVB};
    mpVao = Vao::create(Vao::Topology::TriangleList, mpLayout, vbuffers, mpIB, ResourceFormat::R32Uint);
    mpState->setVao(mpVao);

    mIndexCount = (uint32_t)mIndices.size();
}

void HelperLaneViz::updateGridParams()
{
    mGridCellSize = float2(1.0f / mGridCols, 1.0f / mGridRows);
    mGridOrigin = float2(0.0f, 0.0f);
    mGridScale = std::min(mGridCellSize.x, mGridCellSize.y);

    GridParams gridParams;
    gridParams.cols = mGridCols;
    gridParams.rows = mGridRows;
    gridParams.cellSize = mGridCellSize;
    gridParams.origin = mGridOrigin;
    gridParams.scale = mGridScale;
    mpVars->getRootVar()["Grid"].setBlob(gridParams);
}

std::string selectFolder()
{
    std::string folderPath;
    BROWSEINFOA bi = {0};
    bi.lpszTitle = "Select folder to benchmark";
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != nullptr)
    {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path))
        {
            folderPath = path;
        }
        CoTaskMemFree(pidl);
    }
    return folderPath;
}

void HelperLaneViz::benchmarkFolder(const std::string& folderPath)
{
    if (folderPath.empty())
    {
        return;
    }

    mFolderFiles.clear();
    for (const auto& entry : std::filesystem::directory_iterator(folderPath))
    {
        if (entry.is_regular_file())
        {
            std::string filePath = entry.path().string();
            std::string ext = filePath.substr(filePath.find_last_of(".") + 1);
            if (ext == "svg")
            {
                mFolderFiles.push_back(filePath);
            }
        }
    }

    if (mFolderFiles.empty())
    {
        return;
    }

    mBenchmarkOutputPath = folderPath + "/benchmark_results.txt";
    mBenchmarkFile.open(mBenchmarkOutputPath, std::ios::out | std::ios::trunc);
    if (!mBenchmarkFile.is_open())
    {
        return;
    }

    mBenchmarkFile << "File,Method,CPU_Time_ms,GPU_Median_ms,GPU_Mean_ms,GPU_StdDev_ms,HelperLaneCount\n";

    mBenchmarkFolderActive = true;
    mCurrentFolderFile = 0;
    mBenchmarkMethods = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10}; // Skip centroid fan
    mCurrentMethod = 0;
    mBenchmarkStep = 0;
    mWaitFrameCounter = 0;
    mGpuFrameCounter = 0;
    mUseCircle = false;
    mSvgPath = mFolderFiles[0];
}

void HelperLaneViz::processBenchmarkStep(RenderContext* pRenderContext)
{
    const std::unordered_map<int, std::string> kTriangulationMethodNames = {
        {0, "Ear Clipping"},
        {1, "MWT"},
        {2, "Centroid Fan"},
        {3, "Greedy"},
        {4, "Strip"},
        {5, "MaxMin"},
        {6, "MinMax"},
        {7, "CDT"},
        {8, "Earcut (Mapbox)"},
        {9, "Earcut + Flip"},
        {10, "CDT + Flip"}
    };
    Profiler* pProfiler = getDevice()->getProfiler();
    if (pProfiler)
        pProfiler->setEnabled(true);

    int method = mBenchmarkMethods[mCurrentMethod];
    std::string methodName = kTriangulationMethodNames.at(method);

    switch (mBenchmarkStep)
    {
    case 0: // CPU triangulation
    {
        mTriangulationType = method;
        auto start = std::chrono::high_resolution_clock::now();
        if (mUseCircle)
        {
            generateCircle();
        }
        else
        {
            loadSvg(mSvgPath);
        }
        auto end = std::chrono::high_resolution_clock::now();
        mCpuTimeMs = std::chrono::duration<double, std::milli>(end - start).count();

        mWaitFrameCounter = 0;
        mBenchmarkStep = 1;
        break;
    }
    case 1: // Wait frames for stability
    {
        const int waitFrames = 50;
        mWaitFrameCounter++;
        if (mWaitFrameCounter >= waitFrames)
        {
            mGpuFrameCounter = 0;
            mGpuTimes.clear();
            mBenchmarkStep = 2;
        }
        break;
    }
    case 2: // Measure GPU frames
    {
        const int gpuFrames = 100;

        if (pProfiler && pProfiler->isEnabled())
        {
            const auto& events = pProfiler->getEvents();
            for (auto* pEvent : events)
            {
                if (pEvent->getName() == "/onFrameRender")
                {
                    float gpuTime = pEvent->getGpuTime(); // in ms
                    mGpuTimes.push_back(gpuTime);
                    break;
                }
            }
        }

        mGpuFrameCounter++;
        if (mGpuFrameCounter == gpuFrames )
        {
            mReadBackHelperLaneCount = true;
            mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "1");
        }
        if (mGpuFrameCounter > gpuFrames)
        {
            // Compute stats
            std::sort(mGpuTimes.begin(), mGpuTimes.end());
            float median = mGpuTimes[mGpuTimes.size() / 2];
            float mean = std::accumulate(mGpuTimes.begin(), mGpuTimes.end(), 0.0f) / mGpuTimes.size();
            float sqSum = 0.0f;
            for (float t : mGpuTimes)
                sqSum += (t - mean) * (t - mean);
            float stddev = std::sqrt(sqSum / mGpuTimes.size());

            // Read helper lane counter
            uint32_t helperCount = mHelperLaneCount;
            mReadBackHelperLaneCount = false;
            mpProgram->removeDefine("READ_BACK_HELPER_LANE_COUNT");

            size_t totalEdgeLength;
            double uniqueEdgeLength;
            Triangulation::ComputeEdgeMetrics(mVertices, mIndices, totalEdgeLength, uniqueEdgeLength);

            std::ostringstream oss;
            if (mBenchmarkFolderActive)
            {
                std::string fileName = std::filesystem::path(mFolderFiles[mCurrentFolderFile]).filename().string();
                oss << fileName << "," << methodName << "," << mCpuTimeMs << "," 
                    << median << "," << mean << "," << stddev << ","
                    << helperCount << "," << uniqueEdgeLength;
            }
            else
            {
                oss << methodName << "," << mCpuTimeMs << "," << median << "," 
                    << mean << "," << stddev << "," << helperCount << "," << uniqueEdgeLength;
            }
            std::string logLine = oss.str();

            mBenchmarkLog.push_back(logLine);

            if (mBenchmarkFile.is_open())
            {
                mBenchmarkFile << logLine << "\n";
                mBenchmarkFile.flush();
            }

            // Move to next method
            mCurrentMethod++;
            if (mCurrentMethod >= (int)mBenchmarkMethods.size())
            {
                if (mBenchmarkFolderActive)
                {
                    mBenchmarkStep = 0;
                }
                else
                {
                    mBenchmarkActive = false;
                    mBenchmarkStep = 0;
                    if (mBenchmarkFile.is_open())
                        mBenchmarkFile.close();
                    mBenchmarkLog.push_back("Benchmark complete!");
                }
            }
            else
            {
                mBenchmarkStep = 0;
            }
        }
        break;
    }
    }
}

void HelperLaneViz::processBenchmarkFolderStep(RenderContext* pRenderContext)
{
    if (mCurrentFolderFile < (int)mFolderFiles.size())
    {
        processBenchmarkStep(pRenderContext);

        if (mCurrentMethod >= (int)mBenchmarkMethods.size())
        {
            mCurrentFolderFile++;
            if (mCurrentFolderFile < (int)mFolderFiles.size())
            {
                mSvgPath = mFolderFiles[mCurrentFolderFile];
                mCurrentMethod = 0;
                mBenchmarkStep = 0;
                mWaitFrameCounter = 0;
                mGpuFrameCounter = 0;
            }
            else
            {
                mBenchmarkFolderActive = false;
                if (mBenchmarkFile.is_open())
                {
                    mBenchmarkFile.flush();
                    mBenchmarkFile.close();
                }
                mBenchmarkLog.push_back("Folder benchmark complete!");
            }
        }
    }
}
void HelperLaneViz::onShutdown() {}
void HelperLaneViz::onResize(uint32_t width, uint32_t height)
{
    updateGridParams();
    mpHelperLaneCounter = getDevice()->createTexture2D(
        1, 1, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);
    CreateMSAATargets();
}

void HelperLaneViz::onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    if (mBenchmarkFolderActive)
    {
        processBenchmarkFolderStep(pRenderContext);
    }
    else if (mBenchmarkActive)
    {
        processBenchmarkStep(pRenderContext);
    }

    mpState->setFbo(mpFbo);
    float4 bgColor = (mVizMode == VizMode::Wireframe) ? float4(1, 1, 1, 1) : float4(0, 0, 0, 0);

    if (cntMSAA > 1)
    {
        pRenderContext->clearFbo(mpFbo.get(), bgColor, 1.f, 0);
        mpState->setFbo(mpFbo);
    }
    else
    {
        pRenderContext->clearFbo(pTargetFbo.get(), bgColor, 1.f, 0);
        mpState->setFbo(pTargetFbo);
    }

    if (mpHelperLaneCounter)
    {
        pRenderContext->clearUAV(mpHelperLaneCounter->getUAV().get(), uint4(0));
    }

    if (mVizMode == VizMode::Wireframe)
    {
        mpState->setRasterizerState(wireframeRsState);
    }
    else
    {
        mpState->setRasterizerState(defaultRsState);
    }


    if (mIndexCount)
    {
        uint32_t instanceCount = mGridCols * mGridRows;
        pRenderContext->drawIndexedInstanced(mpState.get(), mpVars.get(), mIndexCount, instanceCount, 0, 0, 0);
    }

    if (mpHelperLaneCounter && mIndexCount && mReadBackHelperLaneCount)
    {
        std::vector<uint8_t> counterData = pRenderContext->readTextureSubresource(mpHelperLaneCounter->asTexture().get(), 0);
        mHelperLaneCount = *(uint32_t*)counterData.data();
    }

    if (cntMSAA > 1)
    {
        pRenderContext->resolveResource(mpFbo->getColorTexture(0), mpResolvedTexture);
        pRenderContext->blit(mpResolvedTexture->getSRV(), pTargetFbo->getRenderTargetView(0));
    }
}

void HelperLaneViz::onGuiRender(Gui* pGui)
{
    Gui::Window w(pGui, "SVG Loader", {300, 150});

    // Display GPU frametime
    Profiler* pProfiler = getDevice()->getProfiler();
    if (pProfiler && pProfiler->isEnabled())
    {
        const auto& events = pProfiler->getEvents();
        for (auto* pEvent : events)
        {
            if (pEvent->getName() == "/onFrameRender")
            {
                float gpuTime = pEvent->getGpuTime(); // in milliseconds
                w.text("GPU Frame Time: %.2f ms", gpuTime);
                break;
            }
        }
    }

    // MSAA
    Gui::DropdownList msaaTypes = { {1, "None"}, { 2, "2x" }, {4, "4x"}, {8, "8x"}, {16, "16x"} };
    bool msaaChanged = w.dropdown("MSAA", msaaTypes, cntMSAA);
    if (msaaChanged && cntMSAA > 1u) CreateMSAATargets();

    // Use built-in circle
    bool modeChanged = w.checkbox("Use Circle", mUseCircle);

    // Visualization mode
    Gui::DropdownList vizModes = {{0, "Helper Lanes"}, {1, "Wireframe"}, {2, "Texture"}};
    w.dropdown("Visualization", vizModes, *(uint32_t*)&mVizMode);

    mpProgram->removeDefine("VIZ_MODE");
    mpProgram->removeDefine("USE_DUMMY_TEXTURE");

    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));

    if (mVizMode == VizMode::HelperLanes && mUseDummyTexture)
    {
        mpProgram->addDefine("USE_DUMMY_TEXTURE", "1");
        mpVars->setTexture("gDummyTexture", mpDummyTexture);
    }

    if (mVizMode == VizMode::Texture)
    {
        mpVars->setTexture("gDummyTexture", mpDummyTexture);
    }

    // Triangulation type
    Gui::DropdownList triangTypes = {
        {0, "Ear Clipping"}, {1, "MWT"}, {2, "Centroid Fan"}, {3, "Greedy"}, {4, "Strip"}, {5, "MaxMin"}, {6, "MinMax"}, {7, "CDT"},
        {8, "Earcut (Mapbox)"}, {9, "Earcut + Flip"}, {10, "CDT + Flip"}};
    bool triangChanged = w.dropdown("Triangulation", triangTypes, mTriangulationType);

    w.separator();

    w.checkbox("Read back helper lane count", mReadBackHelperLaneCount);

    if (mReadBackHelperLaneCount)
    {
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "1");
        std::string helperLaneCountStr = "Helper lane count: " + std::to_string(mHelperLaneCount);
        w.text(helperLaneCountStr);
    }
    else
    {
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
    }

    mpProgram->removeDefine("USE_DUMMY_TEXTURE");

    w.checkbox("Bind dummy texture (force helper lanes)", mUseDummyTexture);

    if (mUseDummyTexture)
    {
        mpProgram->addDefine("USE_DUMMY_TEXTURE", "1");
        mpVars->setTexture("gDummyTexture", mpDummyTexture);
    }

    w.checkbox("Measure triangulation time", mMeasureTriangulationTime);

    if (mMeasureTriangulationTime)
    {
        w.text("Last triangulation: %.3f ms", mLastTriangulationMs);
    }

    w.separator();

    if (mUseCircle)
    {
        bool circleChanged = false;
        circleChanged |= w.var("Vertex Count", mCircleVertexCount, 3u, 256u);
        circleChanged |= w.var("Radius X", mEllipseRadiusX, 0.01f, 0.5f);
        circleChanged |= w.var("Radius Y", mEllipseRadiusY, 0.01f, 0.5f);
        if (circleChanged || modeChanged || triangChanged)
        {
            generateCircle();
        }
    }
    else
    {
        bool bezierChanged = w.var("Max Bezier Deviation", mMaxBezierDeviation, 0.1f, 100.0f);
        if (w.textbox("SVG Path", mSvgPath) || modeChanged)
        {
            loadSvg(mSvgPath);
        }
        if (triangChanged || bezierChanged)
        {
            loadSvg(mSvgPath);
        }
    }

    w.separator();
    bool gridChanged = false;
    gridChanged |= w.var("Grid Cols", mGridCols, 1u, 100u);
    gridChanged |= w.var("Grid Rows", mGridRows, 1u, 100u);
    if (gridChanged)
    {
        updateGridParams();
    }

    w.separator();
    if (w.button("Run Benchmark"))
    {
        mBenchmarkActive = true;
        mBenchmarkStep = 0;
        mCurrentMethod = 0;
        mWaitFrameCounter = 0;
        mGpuFrameCounter = 0;
        mBenchmarkMethods = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10};
        mBenchmarkLog.clear();

        mBenchmarkFile.open("C:/Users/User/Downloads/triangulation_benchmark.txt", std::ios::out | std::ios::trunc);
        if (mBenchmarkFile.is_open())
            mBenchmarkFile << "Method  CPU_Time_ms  GPU_Median_ms   GPU_Mean_ms GPU_StdDev_ms   HelperLaneCount EdgeLength\n";
    }

    if (w.button("Benchmark Folder"))
    {
        std::string folderPath = selectFolder();
        if (!folderPath.empty())
        {
            benchmarkFolder(folderPath);
        }
    }
}

bool HelperLaneViz::onKeyEvent(const KeyboardEvent& keyEvent)
{
    return false;
}
bool HelperLaneViz::onMouseEvent(const MouseEvent& mouseEvent)
{
    return false;
}
void HelperLaneViz::onHotReload(HotReloadFlags reloaded) {}

int runMain(int argc, char** argv)
{
    SampleAppConfig config;
    config.windowDesc.title = "Falcor Project Template";
    config.windowDesc.resizableWindow = true;

    HelperLaneViz project(config);
    return project.run();
}

int main(int argc, char** argv)
{
    return catchAndReportAllExceptions([&]() { return runMain(argc, argv); });
}
