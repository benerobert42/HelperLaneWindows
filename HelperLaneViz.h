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
#include "Core/API/GpuTimer.h"
#include "Triangulation/3D/Triangulation3D.h"
#include <optional>
#include "Rendering/LitPreview.h"
#include "Triangulation/3D/ReferenceSurface.h"

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
    ref<GpuTimer> mpDrawTimer;
    double mLastDrawMs = 0;
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
    uint32_t mVolumeObjPolygonFaceCount = 0;
    float mVolumeMaxNormalDeviationDegrees = 15.0f;
    float mVolumeMaxRelativePlaneDeviation = 0.005f;
    uint32_t mVolumeMaxFacesPerPatch = 64;
    bool mVolumeSurfaceLocalFlips = false;
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
    bool mVolumeResultValid = false;
    double mVolumeReferenceError = 0;
    Triangulation3D::Mesh mVolumeCachedSource;
    Triangulation3D::MeshTriangulationResult mVolumeCachedBaseline;
    std::vector<Triangulation3D::Patch> mVolumeCachedPatches;
    std::string mVolumeSourceKey, mVolumePatchKey;
    std::unique_ptr<SurfaceFlipExperiment::ReferenceSurface> mVolumeReference;

    // SVG loading
    std::string mSvgPath;
    uint32_t mTriangulationType = 0;
    float mMaxBezierDeviation = 1.0f;

    // Helper lane count
    uint32_t mHelperLaneCount{};
    uint32_t mActiveLaneCount{};
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
        MaterialStress = 3,
        LitPreview = 4
    };

    VizMode mVizMode = VizMode::HelperLanes;

    bool benchmarkRunning() const { return mPlanarBenchmark.active || mVolumeBenchmark.active || mFolderBenchmark.active; }
    struct SavedInteractiveState
    {
        GeometryDimensionality dimensionality;
        VizMode viz;
        bool readback, circle;
        uint32_t msaa, cols, rows, planarMethod, volumeMethod, circleVertices;
        float ellipseX, ellipseY;
        std::string svgPath;
        float4x4 model;
    };
    std::optional<SavedInteractiveState> mBenchmarkSavedState;
    void beginBenchmarkSession();
    void writeBenchmarkMetadata(const std::string& path);
    friend int runInteractiveSmoke();
    friend int runScreenshot(int argc, char** argv);
    bool advanceInteractiveSmoke();
    int mSmokeStage = -1;
    uint32_t mSmokeFrames = 0;
    bool mSmokeFailed = false;
    void finishBenchmarkSession(const std::string& message);

    struct VolumeBenchmarkDetailRow
    {
        uint32_t orientation = 0;
        uint32_t method = 0;
        double gpuMedianMs = 0.0;
        double projectedEdgeLength = 0.0;
        double helperLaneCount = 0.0;
        double cpuMs = 0.0, edgeLength3D = 0.0, referenceError = 0.0;
        uint32_t failedPatches = 0, samples = 0;
        bool valid = false;
        uint32_t activeLanes = 0;
    };
    std::vector<std::string> mBenchmarkLog;
    std::ofstream mBenchmarkFile;
    VizMode mSavedVizMode = VizMode::HelperLanes;
    enum class BenchmarkPhase
    {
        Prepare,
        Warmup,
        Timing,
        Diagnostics
    };
    struct BenchmarkConfig
    {
        uint32_t warmup = 50, planarSamples = 100, volumeSamples = 200, orientations = 32;
    };
    BenchmarkConfig mBenchmarkConfig, mBenchmarkConfigSnapshot;

    struct PlanarBenchmarkState
    {
        bool active = false;
        BenchmarkPhase phase = BenchmarkPhase::Prepare;
        int currentMethod = 0;
        std::vector<int> methods;
        uint32_t waitFrameCounter = 0;
        double cpuTimeMs = 0.0;
        std::vector<float> gpuTimes;

    } mPlanarBenchmark;

    struct VolumeBenchmarkState
    {
        bool active = false;
        BenchmarkPhase phase = BenchmarkPhase::Prepare;
        int method = 0;
        std::vector<uint32_t> methods;
        uint32_t waitFrameCounter = 0;
        double cpuTimeMs = 0.0;
        float medianGpuMs = 0.0f;
        uint32_t orientation = 0;
        uint32_t visibleOrientations = 0;
        double gpuMedianSum = 0.0;
        double gpuMedianMin = 0.0;
        double gpuMedianMax = 0.0;
        double projectedEdgeLengthSum = 0.0;
        double helperLaneSum = 0.0;
        std::vector<float> gpuTimes;
        std::string outputPath;
        std::vector<VolumeBenchmarkDetailRow> detailRows;

    } mVolumeBenchmark;

    struct FolderBenchmarkState
    {
        bool active = false;
        std::vector<std::string> folderFiles;
        int currentFolderFile = 0;
        std::string outputPath;
        int syntheticShapePhase = 0; // 0 = SVG files, 1 = circle, 2 = ellipse
        int configPhase = 0;         // 0-3 for MSAA/Grid combinations

    } mFolderBenchmark;

    void startBenchmarkConfig(int configPhase);

    void uploadMesh(const void*, size_t, size_t, const std::vector<uint32_t>&, const ref<VertexLayout>&);
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
    void renderLitPreview(RenderContext* context, const ref<Fbo>& target);
    void saveScreenshot(const ref<Fbo>& target);
    LitPreview mLitPreview;
    LitPreviewSettings mLitSettings;
    std::string mScreenshotPath = "capture.png";
    bool mCaptureAndExit = false;
    bool mRequestScreenshot = false;

    // Window resize functionality
    void resizeWindow(uint32_t width, uint32_t height);
    uint32_t mDesiredWindowWidth = 1920;
    uint32_t mDesiredWindowHeight = 1080;
};

int runScreenshot(int argc, char** argv);
