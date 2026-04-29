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

#include <fstream>

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
    ref<VertexLayout> mpLayout3D;
    ref<FullScreenPass> mpPass;
    ref<Texture> mpHelperLaneCounter;
    ref<Texture> mpDummyTexture;
    ref<RasterizerState> defaultRsState;
    ref<RasterizerState> wireframeRsState;

    // MSAA setup
    void CreateMSAATargets();
    ref<Fbo> mpFbo;
    ref<Texture> mpResolvedTexture;
    uint32_t cntMSAA = 4;

    // current polygon (editable)
    std::vector<Vertex> mVertices;
    std::vector<uint32_t> mIndices;
    std::vector<float3> mVolumeVertices;
    std::vector<uint32_t> mVolumeIndices;

    // Rendering mode
    enum class GeometryDimensionality : uint32_t
    {
        Planar = 0,
        Volumetric = 1
    };

    GeometryDimensionality mGeometryDimensionality = GeometryDimensionality::Planar;
    bool mUseCircle = false;
    uint32_t mCircleVertexCount = 32;
    float mEllipseRadiusX = 0.4f;
    float mEllipseRadiusY = 0.4f;
    uint32_t mVolumeGeometryType = 0;
    uint32_t mVolumeTriangulationType = 0;
    uint32_t mVolumeTargetVertexCount = 512;
    uint32_t mVolumeLatitudeSegments = 31;
    uint32_t mVolumeLongitudeSegments = 17;
    float mVolumeRadius = 0.5f;
    float mVolumeBoundsRadius = 0.5f;
    uint32_t mVolumeSourceVertexCount = 512;
    uint32_t mVolumeSourceFaceCount = 0;
    uint32_t mVolumeSourceTriangleCount = 0;
    std::string mVolumeGeometryName = "Sphere";
    std::string mVolumeObjPath;
    std::string mVolumeObjStatus;
    bool mVolumeNormalizeObj = true;
    uint32_t mVolumeObjFanTriangulatedFaceCount = 0;
    float mVolumeMaxNormalDeviationDegrees = 15.0f;
    float mVolumeMaxRelativePlaneDeviation = 0.005f;
    uint32_t mVolumeMaxFacesPerPatch = 64;
    uint32_t mVolumePclMaximumNearestNeighbors = 100;
    float mVolumePclMu = 2.5f;
    float mVolumePclSearchRadius = 0.0f;
    float mVolumePclMinimumAngleDegrees = 10.0f;
    float mVolumePclMaximumAngleDegrees = 120.0f;
    uint32_t mVolumeInstanceCount = 1;
    bool mVolumeUse3DInstancing = false;
    uint32_t mVolumeGridCols = 1;
    uint32_t mVolumeGridRows = 1;
    uint32_t mVolumeGridLayers = 1;
    float mVolumeGridSpacing = 1.25f;
    float4x4 mVolumeModelTransform = float4x4::identity();
    uint32_t mVolumePatchCount = 0;
    uint32_t mVolumeFailedPatchCount = 0;

    // SVG loading
    std::string mSvgPath;
    uint32_t mTriangulationType = 0;
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

    bool mUseDummyTexture = false;

    bool mMeasureTriangulationTime = false;
    double mLastTriangulationMs = 0.0;
    
    // Mesh optimization (meshoptimizer)
    bool mUseMeshOptimizer = false;
    void optimizeMesh();

    enum class VizMode : uint32_t
    {
        HelperLanes = 0,
        Wireframe = 1,
        Texture = 2,
        MaterialStress = 3
    };

    VizMode mVizMode = VizMode::HelperLanes;

    // Benchmarking state
    bool mBenchmarkActive = false;
    int mBenchmarkStep = 0; // 0 = CPU triang, 1 = wait frames, 2 = GPU frames
    int mCurrentMethod = 0;
    std::vector<int> mBenchmarkMethods;
    int mWaitFrameCounter = 0;
    int mGpuFrameCounter = 0;
    double mCpuTimeMs = 0.0;
    std::vector<float> mGpuTimes;
    std::vector<std::string> mBenchmarkLog;
    std::ofstream mBenchmarkFile;
    VizMode mSavedVizMode = VizMode::HelperLanes; // saved during helper lane measurement

    bool mVolumeBenchmarkActive = false;
    int mVolumeBenchmarkStep = 0;
    int mVolumeBenchmarkMethod = 0;
    std::vector<uint32_t> mVolumeBenchmarkMethods;
    int mVolumeBenchmarkWaitFrameCounter = 0;
    int mVolumeBenchmarkGpuFrameCounter = 0;
    double mVolumeBenchmarkCpuTimeMs = 0.0;
    float mVolumeBenchmarkMedianGpuMs = 0.0f;
    uint32_t mVolumeBenchmarkOrientation = 0;
    uint32_t mVolumeBenchmarkCompletedOrientations = 0;
    double mVolumeBenchmarkGpuMedianSum = 0.0;
    double mVolumeBenchmarkGpuMedianMin = 0.0;
    double mVolumeBenchmarkGpuMedianMax = 0.0;
    double mVolumeBenchmarkProjectedEdgeLengthSum = 0.0;
    double mVolumeBenchmarkHelperLaneSum = 0.0;
    std::vector<float> mVolumeBenchmarkGpuTimes;
    std::string mVolumeBenchmarkOutputPath;
    uint32_t mSavedVolumeTriangulationType = 0;
    bool mSavedReadBackHelperLaneCount = false;
    struct VolumeBenchmarkDetailRow
    {
        uint32_t orientation = 0;
        uint32_t method = 0;
        double gpuMedianMs = 0.0;
        double projectedEdgeLength = 0.0;
        double helperLaneCount = 0.0;
    };
    std::vector<VolumeBenchmarkDetailRow> mVolumeBenchmarkDetailRows;

    bool mBenchmarkFolderActive = false;
    std::vector<std::string> mFolderFiles;
    int mCurrentFolderFile = 0;
    std::string mBenchmarkOutputPath;
    int mSyntheticShapePhase = 0; // 0 = SVG files, 1 = circle, 2 = ellipse
    int mBenchmarkConfigPhase = 0; // 0-3 for MSAA/Grid combinations
    
    void startBenchmarkConfig(int configPhase);

    void uploadGeometry();
    void uploadVolumeGeometry();
    void loadSvg(const std::string& path);
    void generateCircle();
    void generateVolumeGeometry();
    void applyVolumeTargetVertexCount();
    void updateGridParams();
    void updateVolumeGridParams();
    void updateVolumeModelParams();
    void updateCameraParams(uint32_t width = 0, uint32_t height = 0);

    void processBenchmarkStep(RenderContext* pRenderContext);
    void processBenchmarkFolderStep(RenderContext* pRenderContext);
    void startVolumeBenchmark();
    void processVolumeBenchmarkStep(RenderContext* pRenderContext);
    void writeVolumeBenchmarkHeader();
    void writeVolumeBenchmarkRow(
        const std::string& methodName,
        double averageMedianGpuMs,
        double minMedianGpuMs,
        double maxMedianGpuMs,
        double averageProjectedEdgeLength,
        double averageHelperLaneCount
    );
    void writeVolumeBenchmarkDetails();
    void benchmarkFolder(const std::string& folderPath);
    
    // Screenshot functionality
    void saveScreenshot(RenderContext* pRenderContext, const ref<Fbo>& pTargetFbo);
    bool mRequestScreenshot = false;
    
    // Window resize functionality
    void resizeWindow(uint32_t width, uint32_t height);
    uint32_t mDesiredWindowWidth = 1920;
    uint32_t mDesiredWindowHeight = 1080;
};
