#include "HelperLaneViz.h"
#include "Experiments/BenchmarkSupport.h"
#include "SampleSupport.h"
#include "Triangulation/ShaderTypes.h"
#include "Triangulation/2D/TriangulationHelpers.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <unordered_map>

void HelperLaneViz::startBenchmarkConfig(int configPhase)
{
    mFolderBenchmark.configPhase = configPhase;

    // Set MSAA and Grid based on config phase
    // Config 0: MSAA=1, Grid=1x1
    // Config 1: MSAA=1, Grid=100x100
    // Config 2: MSAA=4, Grid=1x1
    // Config 3: MSAA=4, Grid=100x100
    switch (configPhase)
    {
    case 0:
        cntMSAA = 1;
        mGridCols = 1;
        mGridRows = 1;
        break;
    case 1:
        cntMSAA = 1;
        mGridCols = 100;
        mGridRows = 100;
        break;
    case 2:
        cntMSAA = 4;
        mGridCols = 1;
        mGridRows = 1;
        break;
    case 3:
        cntMSAA = 4;
        mGridCols = 100;
        mGridRows = 100;
        break;
    }

    // Apply settings
    CreateMSAATargets();
    updateGridParams();

    // Reset iteration state
    mFolderBenchmark.currentFolderFile = 0;
    mFolderBenchmark.syntheticShapePhase = 0;
    mPlanarBenchmark.currentMethod = 0;
    mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
    mPlanarBenchmark.waitFrameCounter = 0;

    mUseCircle = false;

    if (!mFolderBenchmark.folderFiles.empty())
    {
        mSvgPath = mFolderBenchmark.folderFiles[0];
    }
}

void HelperLaneViz::benchmarkFolder(const std::string& folderPath)
{
    if (folderPath.empty())
    {
        return;
    }

    mFolderBenchmark.folderFiles.clear();
    for (const auto& entry : std::filesystem::directory_iterator(folderPath))
    {
        if (entry.is_regular_file())
        {
            std::string filePath = entry.path().string();
            std::string ext = filePath.substr(filePath.find_last_of(".") + 1);
            if (ext == "svg")
            {
                mFolderBenchmark.folderFiles.push_back(filePath);
            }
        }
    }

    if (mFolderBenchmark.folderFiles.empty())
    {
        return;
    }

    beginBenchmarkSession();
    mFolderBenchmark.outputPath = makeTimestampedCsvPath("helper_lane_folder_benchmark");
    std::filesystem::create_directories(std::filesystem::path(mFolderBenchmark.outputPath).parent_path());
    mBenchmarkFile.open(mFolderBenchmark.outputPath, std::ios::out | std::ios::trunc);
    if (!mBenchmarkFile.is_open())
    {
        finishBenchmarkSession("Could not open benchmark output.");
        return;
    }
    mBenchmarkFile << "File,Method,CPU_Time_ms,GPU_DrawMedian_ms,GPU_DrawMean_ms,GPU_DrawStdDev_ms,HelperLaneCount,EdgeLength,MSAA,"
                      "GridCols,GridRows,Shader\n";

    writeBenchmarkMetadata(mFolderBenchmark.outputPath);
    mFolderBenchmark.active = true;
    mPlanarBenchmark.methods = {1, 3, 7, 8, 9, 10}; // Skip centroid fan

    // Start with first configuration (MSAA Off, Grid 1x1)
    startBenchmarkConfig(0);
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

    int method = mPlanarBenchmark.methods[mPlanarBenchmark.currentMethod];
    std::string methodName = kTriangulationMethodNames.at(method);

    switch (mPlanarBenchmark.phase)
    {
    case BenchmarkPhase::Prepare: // CPU triangulation
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
        mPlanarBenchmark.cpuTimeMs = std::chrono::duration<double, std::milli>(end - start).count();

        mPlanarBenchmark.waitFrameCounter = 0;
        mPlanarBenchmark.phase = BenchmarkPhase::Warmup;
        break;
    }
    case BenchmarkPhase::Warmup: // Wait frames for stability
    {
        const auto waitFrames = mBenchmarkConfigSnapshot.warmup;
        mPlanarBenchmark.waitFrameCounter++;
        if (mPlanarBenchmark.waitFrameCounter >= waitFrames)
        {
            mPlanarBenchmark.gpuTimes.clear();
            mPlanarBenchmark.phase = BenchmarkPhase::Timing;
        }
        break;
    }
    case BenchmarkPhase::Timing: // Measure GPU frames
    {
        const auto gpuFrames = mBenchmarkConfigSnapshot.planarSamples;

        if (!(mLastDrawMs > 0 && std::isfinite(mLastDrawMs)))
        {
            finishBenchmarkSession("Missing GPU draw sample; benchmark stopped.");
            return;
        }
        mPlanarBenchmark.gpuTimes.push_back(float(mLastDrawMs));
        if (mPlanarBenchmark.gpuTimes.size() >= gpuFrames)
        {
            mSavedVizMode = mVizMode;
            mVizMode = VizMode::HelperLanes;
            mReadBackHelperLaneCount = true;
            mpProgram->addDefine("VIZ_MODE", "0");
            mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "1");
            mPlanarBenchmark.phase = BenchmarkPhase::Diagnostics;
        }
        break;
    }
    case BenchmarkPhase::Diagnostics: // Diagnostic draw completed on the preceding frame
    {
        // Restore original VizMode
        mVizMode = mSavedVizMode;

        mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));

        const auto statistics = BenchmarkSupport::summarize(mPlanarBenchmark.gpuTimes);
        const double median = statistics.median, mean = statistics.mean, stddev = statistics.standardDeviation;

        // Read helper lane counter
        uint32_t helperCount = mHelperLaneCount;
        mReadBackHelperLaneCount = false;
        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");

        size_t totalEdgeLength;
        double uniqueEdgeLength;
        Triangulation::ComputeEdgeMetrics(mVertices, mIndices, totalEdgeLength, uniqueEdgeLength);

        std::ostringstream oss;
        if (mFolderBenchmark.active)
        {
            std::string fileName;
            if (mFolderBenchmark.syntheticShapePhase == 1)
            {
                fileName = "circle_512";
            }
            else if (mFolderBenchmark.syntheticShapePhase == 2)
            {
                fileName = "ellipse_512";
            }
            else
            {
                fileName = std::filesystem::path(mFolderBenchmark.folderFiles[mFolderBenchmark.currentFolderFile]).filename().string();
            }
            oss << BenchmarkSupport::csvField(fileName) << "," << BenchmarkSupport::csvField(methodName) << ","
                << mPlanarBenchmark.cpuTimeMs << "," << median << "," << mean << "," << stddev << "," << helperCount << ","
                << uniqueEdgeLength;
        }
        else
        {
            oss << BenchmarkSupport::csvField(methodName) << "," << mPlanarBenchmark.cpuTimeMs << "," << median << "," << mean << ","
                << stddev << "," << helperCount << "," << uniqueEdgeLength;
        }
        oss << "," << cntMSAA << "," << mGridCols << "," << mGridRows << ","
            << (mSavedVizMode == VizMode::MaterialStress ? "SyntheticWave8" : "Texture1");
        std::string logLine = oss.str();

        mBenchmarkLog.push_back(logLine);

        if (mBenchmarkFile.is_open())
        {
            mBenchmarkFile << logLine << "\n";
            mBenchmarkFile.flush();
        }

        // Move to next method
        mPlanarBenchmark.currentMethod++;
        if (mPlanarBenchmark.currentMethod >= (int)mPlanarBenchmark.methods.size())
        {
            if (mFolderBenchmark.active)
            {
                mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
            }
            else
            {
                mPlanarBenchmark.active = false;
                mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
                if (mBenchmarkFile.is_open())
                    mBenchmarkFile.close();
                finishBenchmarkSession("Benchmark complete!");
            }
        }
        else
        {
            mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
        }
        break;
    }
    }
}

void HelperLaneViz::processBenchmarkFolderStep(RenderContext* pRenderContext)
{
    // Process SVG files or synthetic shapes
    if (mFolderBenchmark.currentFolderFile < (int)mFolderBenchmark.folderFiles.size() || mFolderBenchmark.syntheticShapePhase > 0)
    {
        processBenchmarkStep(pRenderContext);

        if (mPlanarBenchmark.currentMethod >= (int)mPlanarBenchmark.methods.size())
        {
            if (mFolderBenchmark.currentFolderFile < (int)mFolderBenchmark.folderFiles.size())
            {
                // Move to next SVG file
                mFolderBenchmark.currentFolderFile++;
                if (mFolderBenchmark.currentFolderFile < (int)mFolderBenchmark.folderFiles.size())
                {
                    mSvgPath = mFolderBenchmark.folderFiles[mFolderBenchmark.currentFolderFile];
                    mPlanarBenchmark.currentMethod = 0;
                    mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
                    mPlanarBenchmark.waitFrameCounter = 0;
                }
                else
                {
                    // All SVG files done, move to synthetic shapes
                    mFolderBenchmark.syntheticShapePhase = 1;
                    mUseCircle = true;
                    mCircleVertexCount = 512;
                    // Use default radius (will be set to same for circle)
                    mEllipseRadiusX = 0.4f;
                    mEllipseRadiusY = 0.4f;
                    mPlanarBenchmark.currentMethod = 0;
                    mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
                    mPlanarBenchmark.waitFrameCounter = 0;
                }
            }
            else if (mFolderBenchmark.syntheticShapePhase == 1)
            {
                // Circle done, move to ellipse
                mFolderBenchmark.syntheticShapePhase = 2;
                mUseCircle = true;
                mCircleVertexCount = 512;
                mEllipseRadiusX = 0.4f;
                mEllipseRadiusY = 0.2f; // 0.5x the x axis
                mPlanarBenchmark.currentMethod = 0;
                mPlanarBenchmark.phase = BenchmarkPhase::Prepare;
                mPlanarBenchmark.waitFrameCounter = 0;
            }
            else if (mFolderBenchmark.syntheticShapePhase == 2)
            {
                // Ellipse done for current config, move to next config or finish
                if (mFolderBenchmark.configPhase < 3)
                {
                    // Move to next configuration
                    startBenchmarkConfig(mFolderBenchmark.configPhase + 1);
                }
                else
                {
                    // All configurations done
                    mFolderBenchmark.active = false;
                    if (mBenchmarkFile.is_open())
                    {
                        mBenchmarkFile.flush();
                        mBenchmarkFile.close();
                    }
                    mFolderBenchmark.syntheticShapePhase = 0;
                    mFolderBenchmark.configPhase = 0;
                    finishBenchmarkSession("Folder benchmark complete!");
                }
            }
        }
    }
}

void HelperLaneViz::writeVolumeBenchmarkHeader()
{
    if (!mBenchmarkFile.is_open())
    {
        return;
    }

    mBenchmarkFile << "Geometry,SourceVertices,Radius,Instances,GridMode,GridCols,GridRows,GridLayers,Method,"
                   << "AverageGPU_DrawMedian_ms,MinGPU_DrawMedian_ms,MaxGPU_DrawMedian_ms,"
                   << "TotalEdgeLength3D,AverageTotalEdgeLengthScreenSpace,AverageHelperLaneCount,ConstructionAndAuditCPU_ms,FailedPatches,"
                      "MeshValid,SampledReferenceError,MSAA,Width,Height,Shader,SamplesPerView,VisibleOrientations,ValidForRanking,"
                      "SurfaceLocalFlips\n";
}

void HelperLaneViz::writeVolumeBenchmarkRow(
    const std::string& methodName,
    double averageMedianGpuMs,
    double minMedianGpuMs,
    double maxMedianGpuMs,
    double averageProjectedEdgeLength,
    double averageHelperLaneCount
)
{
    if (!mBenchmarkFile.is_open())
    {
        return;
    }

    uint32_t uniqueEdgeCount = 0;
    const uint32_t instanceCount = std::max(mVolumeInstanceCount, 1u);
    const double totalEdgeLength3D =
        computeUniqueEdgeLength3D(mVolumeVertices, mVolumeIndices, uniqueEdgeCount) * static_cast<double>(instanceCount);
    (void)uniqueEdgeCount;

    mBenchmarkFile << BenchmarkSupport::csvField(mVolumeGeometryName) << "," << mVolumeSourceVertexCount << "," << mVolumeRadius << ","
                   << instanceCount << "," << (mVolumeUse3DInstancing ? "XYZ" : "XY") << "," << mVolumeGridCols << "," << mVolumeGridRows
                   << "," << mVolumeGridLayers << "," << BenchmarkSupport::csvField(methodName) << "," << averageMedianGpuMs << ","
                   << minMedianGpuMs << "," << maxMedianGpuMs << "," << totalEdgeLength3D << "," << averageProjectedEdgeLength << ","
                   << averageHelperLaneCount << "," << mVolumeBenchmark.cpuTimeMs << "," << mVolumeFailedPatchCount << ","
                   << mVolumeResultValid << "," << mVolumeReferenceError << "," << cntMSAA << "," << getTargetFbo()->getWidth() << ","
                   << getTargetFbo()->getHeight() << "," << (mSavedVizMode == VizMode::MaterialStress ? "SyntheticWave8" : "Texture1")
                   << "," << mBenchmarkConfigSnapshot.volumeSamples << "," << mVolumeBenchmark.visibleOrientations << ","
                   << (mVolumeResultValid && mVolumeBenchmark.visibleOrientations == mBenchmarkConfigSnapshot.orientations) << ","
                   << mVolumeSurfaceLocalFlips << "\n";
    mBenchmarkFile.flush();
}

void HelperLaneViz::writeVolumeBenchmarkDetails()
{
    auto path = std::filesystem::path(mVolumeBenchmark.outputPath);
    path.replace_extension(".orientations.csv");
    std::ofstream details;
    details.exceptions(std::ios::failbit | std::ios::badbit);
    details.open(path);
    details << std::setprecision(10);
    details << "Orientation,YRotationDegrees,XRotationDegrees,Method,GPU_DrawMedian_ms,ProjectedEdgeLength,HelperLaneCount,"
               "ConstructionAndAuditCPU_ms,TotalEdgeLength3D,FailedPatches,MeshValid,SampledReferenceError,Samples,MSAA,Width,Height,"
               "Shader,ActiveLanes,ValidForRanking,SurfaceLocalFlips\n";
    for (const auto& row : mVolumeBenchmark.detailRows)
        details << row.orientation << "," << (row.orientation / kVolumeBenchmarkXSteps) * 45 << ","
                << (row.orientation % kVolumeBenchmarkXSteps) * 90 << ","
                << BenchmarkSupport::csvField(getVolumeTriangulationMethodName(row.method)) << "," << row.gpuMedianMs << ","
                << row.projectedEdgeLength << "," << row.helperLaneCount << "," << row.cpuMs << "," << row.edgeLength3D << ","
                << row.failedPatches << "," << row.valid << "," << row.referenceError << "," << row.samples << "," << cntMSAA << ","
                << getTargetFbo()->getWidth() << "," << getTargetFbo()->getHeight() << ","
                << (mSavedVizMode == VizMode::MaterialStress ? "SyntheticWave8" : "Texture1") << "," << row.activeLanes << ","
                << (row.valid && row.activeLanes > 0) << "," << mVolumeSurfaceLocalFlips << "\n";
    details.flush();
}

void HelperLaneViz::startVolumeBenchmark()
{
    beginBenchmarkSession();
    if (mBenchmarkFile.is_open())
    {
        mBenchmarkFile.close();
    }

    mPlanarBenchmark.active = false;
    mFolderBenchmark.active = false;
    mVolumeBenchmark.active = true;
    mVolumeBenchmark.phase = BenchmarkPhase::Prepare;
    mVolumeBenchmark.method = 0;
    mVolumeBenchmark.waitFrameCounter = 0;

    mVolumeBenchmark.medianGpuMs = 0.0f;
    mVolumeBenchmark.orientation = 0;
    mVolumeBenchmark.visibleOrientations = 0;

    mVolumeBenchmark.gpuMedianSum = 0.0;
    mVolumeBenchmark.gpuMedianMin = std::numeric_limits<double>::max();
    mVolumeBenchmark.gpuMedianMax = 0.0;
    mVolumeBenchmark.projectedEdgeLengthSum = 0.0;
    mVolumeBenchmark.helperLaneSum = 0.0;
    mVolumeBenchmark.gpuTimes.clear();
    mVolumeBenchmark.detailRows.clear();
    mVolumeBenchmark.methods = {3, 0, 1, 2, 4, 5};
    mReadBackHelperLaneCount = false;

    mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
    mVolumeModelTransform = makeVolumeBenchmarkModelTransform(0);
    mGeometryDimensionality = GeometryDimensionality::Volumetric;
    updateVolumeGridParams();
    updateVolumeModelParams();

    mVolumeBenchmark.outputPath = makeTimestampedCsvPath("helper_lane_volume_benchmark");
    std::filesystem::create_directories(std::filesystem::path(mVolumeBenchmark.outputPath).parent_path());
    mBenchmarkFile.open(mVolumeBenchmark.outputPath, std::ios::out | std::ios::trunc);
    writeVolumeBenchmarkHeader();
    writeBenchmarkMetadata(mVolumeBenchmark.outputPath);
}

void HelperLaneViz::processVolumeBenchmarkStep(RenderContext* pRenderContext)
{
    if (mVolumeBenchmark.method >= static_cast<int>(mVolumeBenchmark.methods.size()))
    {
        writeVolumeBenchmarkDetails();
        finishBenchmarkSession("Volume benchmark complete.");
        return;
    }

    Profiler* pProfiler = getDevice()->getProfiler();
    if (pProfiler)
    {
        pProfiler->setEnabled(true);
    }

    switch (mVolumeBenchmark.phase)
    {
    case BenchmarkPhase::Prepare:
    {
        mVolumeTriangulationType = mVolumeBenchmark.methods[mVolumeBenchmark.method];
        const auto start = std::chrono::high_resolution_clock::now();
        generateVolumeGeometry();
        const auto end = std::chrono::high_resolution_clock::now();
        mVolumeBenchmark.cpuTimeMs = std::chrono::duration<double, std::milli>(end - start).count();

        mVolumeBenchmark.waitFrameCounter = 0;

        mVolumeBenchmark.medianGpuMs = 0.0f;
        mVolumeBenchmark.orientation = 0;
        mVolumeBenchmark.visibleOrientations = 0;

        mVolumeBenchmark.gpuMedianSum = 0.0;
        mVolumeBenchmark.gpuMedianMin = std::numeric_limits<double>::max();
        mVolumeBenchmark.gpuMedianMax = 0.0;
        mVolumeBenchmark.projectedEdgeLengthSum = 0.0;
        mVolumeBenchmark.helperLaneSum = 0.0;
        mVolumeBenchmark.gpuTimes.clear();
        mReadBackHelperLaneCount = false;

        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
        mVolumeModelTransform = makeVolumeBenchmarkModelTransform(mVolumeBenchmark.orientation);
        updateVolumeModelParams();
        mVolumeBenchmark.phase = BenchmarkPhase::Warmup;
        break;
    }
    case BenchmarkPhase::Warmup:
    {
        const auto waitFrames = mBenchmarkConfigSnapshot.warmup;
        ++mVolumeBenchmark.waitFrameCounter;
        if (mVolumeBenchmark.waitFrameCounter >= waitFrames)
        {
            mVolumeBenchmark.phase = BenchmarkPhase::Timing;
        }
        break;
    }
    case BenchmarkPhase::Timing:
    {
        const auto gpuFrames = mBenchmarkConfigSnapshot.volumeSamples;
        if (!(mLastDrawMs > 0 && std::isfinite(mLastDrawMs)))
        {
            finishBenchmarkSession("Missing GPU draw sample; benchmark stopped.");
            return;
        }
        mVolumeBenchmark.gpuTimes.push_back(float(mLastDrawMs));

        if (mVolumeBenchmark.gpuTimes.size() >= gpuFrames)
        {
            mVolumeBenchmark.medianGpuMs = float(BenchmarkSupport::summarize(mVolumeBenchmark.gpuTimes).median);

            mSavedVizMode = mVizMode;
            mVizMode = VizMode::HelperLanes;
            mReadBackHelperLaneCount = true;

            mpProgram->addDefine("VIZ_MODE", "0");

            mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "1");

            mVolumeBenchmark.phase = BenchmarkPhase::Diagnostics;
        }
        break;
    }
    case BenchmarkPhase::Diagnostics:
    {
        const uint32_t helperLaneCount = mHelperLaneCount;
        mVizMode = mSavedVizMode;
        mReadBackHelperLaneCount = false;

        mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));

        mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");

        const uint32_t instanceCount = std::max(mVolumeInstanceCount, 1u);
        const uint32_t width = getTargetFbo() ? getTargetFbo()->getWidth() : 1;
        const uint32_t height = getTargetFbo() ? getTargetFbo()->getHeight() : 1;
        const double projectedEdgeLength = computeProjectedEdgeLengthScreenSpace(
            mVolumeVertices,
            mVolumeIndices,
            mVolumeModelTransform,
            instanceCount,
            width,
            height,
            mVolumeGridCols,
            mVolumeGridRows,
            mVolumeGridLayers,
            mVolumeUse3DInstancing,
            mVolumeGridSpacing,
            mVolumeBoundsRadius
        );

        mVolumeBenchmark.gpuMedianSum += static_cast<double>(mVolumeBenchmark.medianGpuMs);
        mVolumeBenchmark.gpuMedianMin = std::min(mVolumeBenchmark.gpuMedianMin, static_cast<double>(mVolumeBenchmark.medianGpuMs));
        mVolumeBenchmark.gpuMedianMax = std::max(mVolumeBenchmark.gpuMedianMax, static_cast<double>(mVolumeBenchmark.medianGpuMs));
        mVolumeBenchmark.projectedEdgeLengthSum += projectedEdgeLength;
        mVolumeBenchmark.helperLaneSum += static_cast<double>(helperLaneCount);
        uint32_t edgeCount = 0;
        mVolumeBenchmark.detailRows.push_back(VolumeBenchmarkDetailRow{
            mVolumeBenchmark.orientation,
            mVolumeTriangulationType,
            static_cast<double>(mVolumeBenchmark.medianGpuMs),
            projectedEdgeLength,
            static_cast<double>(helperLaneCount),
            mVolumeBenchmark.cpuTimeMs,
            computeUniqueEdgeLength3D(mVolumeVertices, mVolumeIndices, edgeCount),
            mVolumeReferenceError,
            mVolumeFailedPatchCount,
            uint32_t(mVolumeBenchmark.gpuTimes.size()),
            mVolumeResultValid,
            mActiveLaneCount
        });
        if (mActiveLaneCount > 0)
            ++mVolumeBenchmark.visibleOrientations;
        ++mVolumeBenchmark.orientation;

        if (mVolumeBenchmark.orientation < mBenchmarkConfigSnapshot.orientations)
        {
            mVolumeModelTransform = makeVolumeBenchmarkModelTransform(mVolumeBenchmark.orientation);
            updateVolumeModelParams();
            mVolumeBenchmark.waitFrameCounter = 0;

            mVolumeBenchmark.medianGpuMs = 0.0f;
            mVolumeBenchmark.gpuTimes.clear();
            mVolumeBenchmark.phase = BenchmarkPhase::Warmup;
        }
        else
        {
            const double sampleCount = static_cast<double>(std::max(mVolumeBenchmark.orientation, 1u));
            writeVolumeBenchmarkRow(
                getVolumeTriangulationMethodName(mVolumeTriangulationType),
                mVolumeBenchmark.gpuMedianSum / sampleCount,
                mVolumeBenchmark.gpuMedianMin == std::numeric_limits<double>::max() ? 0.0 : mVolumeBenchmark.gpuMedianMin,
                mVolumeBenchmark.gpuMedianMax,
                mVolumeBenchmark.projectedEdgeLengthSum / sampleCount,
                mVolumeBenchmark.helperLaneSum / sampleCount
            );

            ++mVolumeBenchmark.method;
            mVolumeBenchmark.phase = BenchmarkPhase::Prepare;
        }
        break;
    }
    default:
        mVolumeBenchmark.phase = BenchmarkPhase::Prepare;
        break;
    }
}

void HelperLaneViz::beginBenchmarkSession()
{
    if (mBenchmarkSavedState)
        return;
    mBenchmarkConfigSnapshot = mBenchmarkConfig;
    mBenchmarkSavedState = SavedInteractiveState{
        mGeometryDimensionality,
        mVizMode,
        mReadBackHelperLaneCount,
        mUseCircle,
        cntMSAA,
        mGridCols,
        mGridRows,
        mTriangulationType,
        mVolumeTriangulationType,
        mCircleVertexCount,
        mEllipseRadiusX,
        mEllipseRadiusY,
        mSvgPath,
        mVolumeModelTransform
    };
    mReadBackHelperLaneCount = false;
    if (mVizMode == VizMode::HelperLanes || mVizMode == VizMode::Wireframe)
        mVizMode = VizMode::Texture;
    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));
    mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", "0");
    mLastDrawMs = std::numeric_limits<double>::quiet_NaN();
    mBenchmarkFile.exceptions(std::ios::failbit | std::ios::badbit);
    mBenchmarkFile << std::setprecision(10);
}

void HelperLaneViz::finishBenchmarkSession(const std::string& message)
{
    mPlanarBenchmark.active = mVolumeBenchmark.active = mFolderBenchmark.active = false;
    mBenchmarkFile.exceptions(std::ios::goodbit);
    if (mBenchmarkFile.is_open())
        mBenchmarkFile.close();
    mBenchmarkFile.clear();
    mBenchmarkLog.push_back(message);
    if (!mBenchmarkSavedState)
        return;
    auto saved = std::move(*mBenchmarkSavedState);
    mBenchmarkSavedState.reset();
    mGeometryDimensionality = saved.dimensionality;
    mVizMode = saved.viz;
    mReadBackHelperLaneCount = saved.readback;
    mUseCircle = saved.circle;
    cntMSAA = saved.msaa;
    mGridCols = saved.cols;
    mGridRows = saved.rows;
    mTriangulationType = saved.planarMethod;
    mVolumeTriangulationType = saved.volumeMethod;
    mCircleVertexCount = saved.circleVertices;
    mEllipseRadiusX = saved.ellipseX;
    mEllipseRadiusY = saved.ellipseY;
    mSvgPath = std::move(saved.svgPath);
    mVolumeModelTransform = saved.model;
    mpProgram->addDefine("VIZ_MODE", std::to_string(uint32_t(mVizMode)));
    mpProgram->addDefine("READ_BACK_HELPER_LANE_COUNT", mReadBackHelperLaneCount ? "1" : "0");
    if (mGeometryDimensionality == GeometryDimensionality::Volumetric)
        generateVolumeGeometry();
    else if (mUseCircle)
        generateCircle();
    else
        loadSvg(mSvgPath);
    CreateMSAATargets();
    updateGridParams();
    updateVolumeGridParams();
    updateVolumeModelParams();
    updateCameraParams();
}

void HelperLaneViz::writeBenchmarkMetadata(const std::string& output)
{
    auto path = std::filesystem::path(output);
    path.replace_extension(".metadata.txt");
    std::ofstream file;
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file.open(path);
    file << std::setprecision(9) << "Protocol: 2; isolated draw query, helper diagnostics separate\nBuild: " << HELPERLANE_BUILD_CONFIG << "\nCompiler: " << HELPERLANE_COMPILER_DESC
         << "\nSource revision: " << HELPERLANE_GIT_REVISION << "\nCompiled source digest: " << HELPERLANE_SOURCE_DIGEST
         << "\nGPU: " << getDevice()->getInfo().adapterName << "\nAPI: " << getDevice()->getInfo().apiName << "\nMSAA: " << cntMSAA
         << "\nResolution: " << getTargetFbo()->getWidth() << 'x' << getTargetFbo()->getHeight()
         << "\nTimed shader: " << (mVizMode == VizMode::MaterialStress ? "SyntheticWave8" : "Texture1")
         << "\nDepth: Less, writes enabled, matching D32Float MSAA\nGeometry: " << mVolumeGeometryName
         << "\nSegments: " << mVolumeLatitudeSegments << ',' << mVolumeLongitudeSegments << "\nRadius: " << mVolumeRadius
         << "\nOBJ: " << mVolumeObjPath << "\nNormalize OBJ: " << mVolumeNormalizeObj
         << "\nPatch normal degrees: " << mVolumeMaxNormalDeviationDegrees
         << "\nPatch relative plane tolerance: " << mVolumeMaxRelativePlaneDeviation
         << "\nLocal flip metric: " << (mVolumeSurfaceLocalFlips ? "3D length" : "historical projected squared length")
         << "\nMaximum patch faces: " << mVolumeMaxFacesPerPatch << "\nCustom GP: " << mVolumePclMaximumNearestNeighbors << ','
         << mVolumePclMu << ',' << mVolumePclSearchRadius << ',' << mVolumePclMinimumAngleDegrees << ',' << mVolumePclMaximumAngleDegrees
         << "\nInstances: " << mVolumeInstanceCount << "\nXYZ grid: " << mVolumeUse3DInstancing << "\nPlanar grid: " << mGridCols << ','
         << mGridRows << "\nCircle: " << mUseCircle << ',' << mCircleVertexCount << ',' << mEllipseRadiusX << ',' << mEllipseRadiusY
         << "\nSVG: " << mSvgPath << "\nBezier tolerance: " << mMaxBezierDeviation << "\nPlanar meshopt: " << mUseMeshOptimizer
         << "\nWarmup: " << mBenchmarkConfigSnapshot.warmup << "\nPlanar samples: " << mBenchmarkConfigSnapshot.planarSamples
         << "\nVolume samples: " << mBenchmarkConfigSnapshot.volumeSamples
         << "\nVolume orientations: " << mBenchmarkConfigSnapshot.orientations
         << "\nCPU scope: geometry cache lookup, method, audit, upload; shared preparation reused when source is unchanged.\n"
         << "Screen-space edge length: unclipped geometric proxy; helper counts: separate shader proxy.\n";
    file.flush();
}
