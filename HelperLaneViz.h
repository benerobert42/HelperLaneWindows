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
#pragma once
#include "Falcor.h"
#include "Core/SampleApp.h"
#include "Core/Pass/FullScreenPass.h"

struct Vertex;

using namespace Falcor;

class HelperLaneViz : public SampleApp
{
public:
    HelperLaneViz(const SampleAppConfig& config);
    ~HelperLaneViz() override;

    void onLoad(RenderContext* pRenderContext) override;
    void onFrameRender(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo) override;
    void onGuiRender(Gui* pGui) override;
    void onResize(uint32_t width, uint32_t height) override;
    void onHotReload(HotReloadFlags reloaded) override;
    void onShutdown() override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;

private:
    // === NEW: graphics objects
    ref<Program> mpProgram;
    ref<GraphicsState> mpState;
    ref<ProgramVars> mpVars;
    ref<Vao> mpVao;
    ref<Buffer> mpVB;
    ref<Buffer> mpIB;
    uint32_t mIndexCount = 0;
    ref<VertexLayout> mpLayout;
    ref<FullScreenPass> mpPass;
    ref<Texture> mpHelperLaneCounter;

    // MSAA setup
    void CreateMSAATargets();
    ref<Fbo> mpFbo;
    ref<Texture> mpResolvedTexture;
    uint32_t cntMSAA = 4;

    // === NEW: current polygon (editable)
    std::vector<Vertex> mVertices;
    std::vector<uint32_t> mIndices;

    // Rendering mode
    bool mUseCircle = false;
    uint32_t mCircleVertexCount = 32;
    float mEllipseRadiusX = 0.4f;
    float mEllipseRadiusY = 0.4f;

    // SVG loading
    std::string mSvgPath;
    uint32_t mTriangulationType = 0; // 0=EarClipping, 1=MWT, 2=CentroidFan, 3=Greedy, 4=Strip, 5=MaxMin, 6=MinMax, 7=CDT
    float mMaxBezierDeviation = 1.0f;

    // Helper lane count
    uint32_t mHelperLaneCount{};
    bool mReadBackHelperLaneCount = false;

    // Grid instancing
    uint32_t mGridCols = 1;
    uint32_t mGridRows = 1;
    float2 mGridCellSize = float2(1.0f, 1.0f);
    float2 mGridOrigin = float2(0.0f, 0.0f);
    float mGridScale = 1.0f;

    void uploadGeometry();
    void loadSvg(const std::string& path);
    void generateCircle();
    void updateGridParams();
};
