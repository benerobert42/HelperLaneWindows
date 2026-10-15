# HelperLaneWindows

A Falcor raster sample for investigating how triangle connectivity affects helper-lane diagnostics and GPU draw cost. It supports planar polygons, 3D surface triangulation, OBJ import, and the existing GraphEx raster Phong preview. It does not render with ray tracing.

## Build and open in VS2022

Place this repository in `Falcor/Source/Samples/HelperLaneWindows`, initialize its dependencies with `git submodule update --init --recursive`, and add `add_subdirectory(HelperLaneWindows)` to Falcor's `Source/Samples/CMakeLists.txt`.

The measurements require the shared Falcor timer correction in [docs/falcor-gpu-timer.patch](docs/falcor-gpu-timer.patch). It restores the resolve buffer's CopyDest state before resolving reused timestamp queries and subtracts integer timestamps before conversion. **It is already applied in this local Falcor checkout.** On another checkout, from the Falcor root:

```powershell
git apply --check Source/Samples/HelperLaneWindows/docs/falcor-gpu-timer.patch
git apply Source/Samples/HelperLaneWindows/docs/falcor-gpu-timer.patch
```

From the Falcor root, regenerate the existing solution and build:

```powershell
& .\tools\.packman\cmake\bin\cmake.exe -S . -B build/windows-vs2022-helperlanes -G 'Visual Studio 17 2022' -A x64 -T host=x64 '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'
& .\tools\.packman\cmake\bin\cmake.exe --build build/windows-vs2022-helperlanes --config Release --target HelperLaneWindows FalcorPython --parallel 6
```

Open `build/windows-vs2022-helperlanes/Falcor.sln` in VS2022. Select **Release / x64**, set **HelperLaneWindows** as startup project, and run it. The SDK setting above is the tested local SDK. FalcorPython is needed because SampleApp initializes Python, including headless runs. CMake supplies source filters and deploys every OBJ preset beside the executable.

## Models and controls

The default is Volumetric / Sphere / Source Faces at 1920x1080. Controls and profiler are docked within a narrow left column by default; the docking checkbox permits free placement.

The Volume Geometry dropdown contains:

- Seven adjustable generators: sphere, anisotropic ellipsoid, jittered sphere, lumpy ellipsoid, saddle, folded sheet and clustered wave.
- Fifteen OBJ presets: Suzanne, Bunny, Teapot, GraphEx Heart, GraphEx Tangle Cube, the five research candidates (Swept Wing, Toothed Wheel, Faceted Arch, Trefoil Knot, Spiral Ramp), and each candidate's control mesh.
- Twenty-two `Study:` entries: the exact procedural fixtures used in the measurements, including density variants, regular/skew torus, tweaked/skew cylinder, skew grids/fold/saddle, and the planar ellipse. These reuse the benchmark generators and keep their source resolution/scale fixed.
- A custom OBJ path through the same importer as the presets. See [asset provenance](Data/Models/README.md).

Choose Helper Lanes, Wireframe or Lit Preview. Lit Preview reuses GraphEx's ambient/diffuse/Phong specular equations, point/directional light, attenuation, and material settings. Source normals stay fixed across triangulations. Helper counters require an explicit synchronous readback; enable them for diagnostics, not normal timing.

The interactive 3D methods are Source Faces, CDT, CDT + Flip, Greedy Point Set, experimental LMT and experimental Greedy Projection. The latter is custom PCL-inspired code, not the PCL library. The global heap, projected-cost and star experiments remain CLI research methods. Model selection does not introduce another triangulation implementation.

## Reproducible screenshots

From this sample folder:

```powershell
& ..\..\..\build\windows-vs2022-helperlanes\bin\Release\HelperLaneWindows.exe --screenshot analysis/screenshots/suzanne.json analysis/screenshots/suzanne.png
```

The [example recipe](analysis/screenshots/suzanne.json) fixes resolution, MSAA, rotation, visualization, triangulation and lighting. `model` accepts any OBJ preset name above, any exact `Study: ...` dropdown name, or an OBJ path relative to the recipe. Names use spaces, e.g. `"Swept Wing"`, `"GraphEx Heart"`, `"Study: Skew Torus"`. Triangulation IDs are 0 CDT, 1 CDT+Flip, 2 Greedy, 3 Source (default), 4 LMT, 5 Greedy Projection. Rotation is XYZ degrees applied X then Y then Z.

Capture uses one synchronous Falcor `captureToFile` call after resolve/blit and before UI. It writes lossless 8-bit RGB at the target dimensions, without crop, scaling, timestamps or another gamma transform. Fixed GPU/driver/build/settings are needed for pixel reproducibility. Invalid/fallback triangulations fail headless capture.

## Measurements and findings

Read [Triangle connectivity: measurements, mechanisms and recommendations](docs/TRIANGULATION_PERFORMANCE.md) for the current conclusions, numerical tables, methodology, limits and proposed next experiments. That report supersedes historical performance claims in `analysis/*.md`, especially measurements made before the timer correction.

From the sample folder, a longer matched 1080p experiment:

```powershell
& .\analysis\gpu\orientation\run_sweep.ps1 -Models 'ellipse2d,ellipsoid,torus_skew,suzanne' -Methods 'Source,SourceControl,CDT,Heap3D,HeapPlane'
```

This uses 24 separate-axis rotation conditions, 1x/4x MSAA, randomized repeated blocks, a bounded query queue, fixed vertex storage, uniform meshoptimizer cache/overdraw ordering, separate diagnostic draws, raw samples, and run metadata. **Draw** means indexed-draw GPU time; **Pass** includes clear/draw/MSAA resolve. Neither includes UI/presentation or measures complete presented-frame latency. GUI benchmarks use the same case/runner/writer core, but their FlatRaster workload is distinct from the CLI LitPreview workload. Do not pair unlike workloads.

Saved summaries, manifests, figures and reusable scripts are tracked. Generated raw runs, logs, telemetry, temporary meshes, copied source snapshots and local harness/build files remain local and are ignored. The committed manifests preserve historical measured-source identities and raw-file hashes; they are not rewritten to claim a newer binary produced old results. Regenerating full analyses requires rerunning the benchmark or retaining those local raw files.

## Layout and validation

`Import/` contains SVG/OBJ loaders and model metadata. `Triangulation/2D` and `Triangulation/3D` contain their algorithms; common types/math/audits live at the triangulation root. `Rendering/` contains raster preview and capture; `Experiments/` contains benchmark cases, one measurement runner/result writer, metrics, fixtures and native checks. Falcor setup and controls stay in `HelperLaneViz.cpp/.h`.

From the sample folder after building:

```powershell
& ..\..\..\build\windows-vs2022-helperlanes\bin\Release\HelperLaneWindows.exe --self-test
& ..\..\..\build\windows-vs2022-helperlanes\bin\Release\HelperLaneWindows.exe --ui-smoke
python analysis/test_summarize_gpu.py
```

Native checks cover import failures, deployed OBJ counts/topology, every selectable study fixture, surface triangulation, and guarded flips. UI smoke exercises the actual render/benchmark path, 1x/4x, state restoration and output failure handling.
