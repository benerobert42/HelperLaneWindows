# HelperLaneViz

A Falcor sample application for visualizing and benchmarking GPU helper lane usage with various polygon triangulation methods.

## Setup

This project must be added as a sample within the **Falcor repository**. Place this directory in:
```
Falcor/Source/Samples/HelperLaneViz/
```

Ensure you have the Falcor repository cloned and properly configured before building.

## Build

Build using CMake as part of the Falcor project. The sample will be available as `HelperLaneWindows` executable.

## Configuration

**⚠️ IMPORTANT: You must configure file paths before running:**

1. **SVG File Path** (`HelperLaneViz.cpp`, line 113):
   ```cpp
   mSvgPath = "Path to your SVG file";
   ```
   Replace with the full path to your SVG file (e.g., `"C:/path/to/your/file.svg"`).

2. **Screenshot Output Path** (`HelperLaneViz.cpp`, line 896):
   ```cpp
   std::string downloadsPath = "Folder to save your image";
   ```
   Replace with the directory where screenshots should be saved (e.g., `"C:/Users/YourName/Downloads"`).

3. **Benchmark Output Path** (`HelperLaneViz.cpp`, line 1067):
   ```cpp
   mBenchmarkFile.open("Folder to output your benchmark", ...);
   ```
   Replace with the full path to the benchmark output file (e.g., `"C:/path/to/benchmark_results.txt"`).

## ImGUI Controls

The application provides the following controls:

- **MSAA**: Enable/disable multi-sample anti-aliasing (1x, 2x, 4x, 8x, 16x)
- **Use Circle**: Switch between SVG file or synthetic circle/ellipse generation
- **Visualization**: Choose between Helper Lanes, Wireframe, or Texture modes
- **Triangulation**: Select from 11 different triangulation algorithms (only CDT, CDT + flip, Earcut, Earcut + flip, Greedy Max Area and Minimum-Weight Triangulation is maintained and used)
- **Use Mesh Optimizer**: Enable mesh optimization (vertex cache, overdraw, vertex fetch)
- **Grid**: Configure grid instancing (columns/rows) for repeated rendering
- **Read back helper lane count**: Display the number of helper lanes executed
- **Measure triangulation time**: Enable CPU triangulation timing
- **Run Benchmark**: Execute automated benchmark across multiple triangulation methods
- **Benchmark Folder**: Benchmark all SVG files in a selected folder with MSAA off/4x and instance count 1/100x100
- **Save Screenshot**: Capture the current render to PNG

## Measurements

### GPU Frame Time
Displayed automatically in the ImGUI window when the profiler is enabled.

### Helper Lane Count
Enable "Read back helper lane count" checkbox to display the number of helper lanes executed during rendering.

### Triangulation Time
Enable "Measure triangulation time" checkbox to measure CPU-side triangulation performance. The result is displayed as "Last triangulation: X.XXX ms".

### Benchmarking
Click "Run Benchmark" to automatically test multiple triangulation methods. Results are saved to the configured benchmark output file with columns
"Benchmark Folder" allows batch processing of all SVG files in a directory, testing each with multiple triangulation methods and MSAA/grid configurations.

