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

void HelperLaneViz::onLoad(RenderContext* pRenderContext)
{
    ProgramDesc d;
    d.addShaderLibrary("Samples/HelperLaneViz/MainShader.slang").vsEntry("vsMain").psEntry("psMain");

    mpPass = FullScreenPass::create(getDevice(), d);

    mpProgram = Program::create(getDevice(), d);
    mpVars = ProgramVars::create(getDevice(), mpProgram.get());
    mpState = GraphicsState::create(getDevice());
    mpState->setProgram(mpProgram);

    RasterizerState::Desc rsDesc;
    rsDesc.setCullMode(RasterizerState::CullMode::None);
    auto pRsState = RasterizerState::create(rsDesc);
    mpState->setRasterizerState(pRsState);

    mpLayout = VertexLayout::create();
    auto pVbLayout = VertexBufferLayout::create();
    pVbLayout->addElement("POSITION", 0, ResourceFormat::RG32Float, 1, 0);
    mpLayout->addBufferLayout(0, pVbLayout);

    // Load SVG file (default path, can be changed via GUI)
    mSvgPath = "C:/Users/ShaprIntel/Downloads/1920560.svg";
    loadSvg(mSvgPath);

    // Enable profiler for GPU frametime measurement
    getDevice()->getProfiler()->setEnabled(true);

    updateGridParams();

    mpHelperLaneCounter = getDevice()->createTexture2D(
        getTargetFbo()->getWidth(),
        getTargetFbo()->getHeight(),
        ResourceFormat::R32Uint,
        1,
        1,
        nullptr,
        ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);
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
        default:
            return Triangulation::earClippingTriangulation(verts);
        }
    };

    if (SVGLoader::TessellateSvgToMesh(path, mVertices, mIndices, triangulator, mMaxBezierDeviation))
    {
        uploadGeometry();
    }
}

void HelperLaneViz::generateCircle()
{
    mVertices.clear();
    mIndices.clear();

    mVertices = Triangulation::CreateVerticesForEllipse(mCircleVertexCount, mEllipseRadiusX, mEllipseRadiusY, float2(0.5f, 0.5f));

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
    default:
        mIndices = Triangulation::earClippingTriangulation(mVertices);
        break;
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
    mpVars->getRootVar()["grid"].setBlob(gridParams);
}

void HelperLaneViz::onShutdown() {}
void HelperLaneViz::onResize(uint32_t width, uint32_t height)
{
    updateGridParams();
    mpHelperLaneCounter = getDevice()->createTexture2D(
        width, height, ResourceFormat::R32Uint, 1, 1, nullptr, ResourceBindFlags::UnorderedAccess | ResourceBindFlags::ShaderResource
    );
    mpVars->setTexture("gHelperLaneCounter", mpHelperLaneCounter);
}

void HelperLaneViz::onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo)
{
    mpState->setFbo(pTargetFbo);
    pRenderContext->clearFbo(pTargetFbo.get(), float4(0.f), 1.f, 0);

    if (mpHelperLaneCounter)
    {
        pRenderContext->clearUAV(mpHelperLaneCounter->getUAV().get(), uint4(0));
    }

    if (mIndexCount)
    {
        uint32_t instanceCount = mGridCols * mGridRows;
        pRenderContext->drawIndexedInstanced(mpState.get(), mpVars.get(), mIndexCount, instanceCount, 0, 0, 0);
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

    bool modeChanged = w.checkbox("Use Circle", mUseCircle);

    Gui::DropdownList triangTypes = {
        {0, "Ear Clipping"}, {1, "MWT"}, {2, "Centroid Fan"}, {3, "Greedy"}, {4, "Strip"}, {5, "MaxMin"}, {6, "MinMax"}, {7, "CDT"}};
    bool triangChanged = w.dropdown("Triangulation", triangTypes, mTriangulationType);

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
