> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# Cleanup and correctness fixes

**Later timing audit:** Positive/finite/nested timer checks missed a shared Falcor timestamp buffer-state defect. Earlier GPU timing verification does not establish reliable sample ownership. See [TIMING_AUDIT.md](TIMING_AUDIT.md) for the reproduced defect and required revalidation; geometry checks remain applicable.

This follows the 10 October review in `CODE_REVIEW.md`. That review describes the pre-fix snapshot; its line numbers are historical. Existing 3DScenes methods remain, with stricter preparation and validation. The global `Heap3D`/`HeapPlane` experiment remains a separate method.

## Where the code lives

| Module | Responsibility |
|---|---|
| `HelperLaneViz.cpp/.h` | Falcor lifecycle, rendering, controls, source/patch cache |
| `SampleSupport.cpp/.h` | Shader constant layouts, camera fit, instance offsets, geometric metrics |
| `Import/ObjMeshLoader.cpp/.h` | Position/polygon OBJ parser, checked indices and normalization |
| `Triangulation/3D/Triangulation3D.cpp/.h` | Source surface definition, eligible patch growth, lifting, complete assembly/fallback |
| `Triangulation/2D/ProjectedGeometry.cpp/.h` | Predicates, visibility, ear clipping |
| `Triangulation/2D/ProjectedGraph.cpp` | Existing Greedy, LMT heuristic, custom greedy projection searches |
| `Triangulation/2D/ProjectedPatchTriangulation.cpp/.h` | CDT adapter, backend selection, projected disk validation |
| `Triangulation/MeshValidation.cpp/.h` | Shared topology, boundary, winding, area and edge-length audit |
| `Triangulation/3D/ReferenceSurface.cpp/.h` | Immutable reference BVH, distance queries, deduplicated error samples |
| `Triangulation/3D/SurfaceFlipExperiment.cpp/.h` | Existing guarded global length/plane heap |
| `Experiments/InteractiveBenchmark.cpp` | GUI benchmark phases, session restoration, CSV/metadata |
| `Experiments/BenchmarkOptions.cpp/.h` | Checked command-line settings |
| `Experiments/BenchmarkSupport.h` | CSV quoting and sample statistics |
| `Experiments/PerformanceBenchmark.cpp` | GPU workload orchestration and isolated draw/pass measurement |
| `Experiments/RegressionChecks.cpp`, `InteractiveSmoke.cpp` | Native correctness and render-path checks |

The extraction does not delete algorithms or pretend to reduce the total algorithmic work by moving files. It separates responsibilities, removes repeated uploads/calculations, bounds expensive searches, and adds validation. Safety checks account for some new code.

## Review issue resolutions

| Finding | Change |
|---|---|
| F01 partial output accepted | Ear clipping is transactional. Projected output checks triangle count, used vertices, duplicates, positive area, oriented interior edges, exact boundary coverage and area. Surface assembly tracks source triangle coverage; any failure retains the full validated source with `SourceFallback` status. Fallbacks cannot rank as successful method output. |
| F02 invalid seeds/projections | Finite inputs checked before normals or projection. Normal comparison respects orientation. Seed eligibility is checked. Curved seeds outside the plane tolerance split along their source diagonals before growing. Patches require oriented manifold disk topology, a simple projected boundary and enclosed points. |
| F03 concave source fans | Simple planar polygons use a checked fan when valid, otherwise checked ear clipping. A curved quad explicitly retains its historical source diagonal. Ambiguous nonplanar polygons with more than four vertices are rejected and request explicit input triangulation. |
| F04 shifted OBJ numbering | Malformed positions abort the import rather than deleting a numbered vertex. Incomplete files/invalid faces fail instead of silently delivering a partial model. |
| F05 index token prefixes | `from_chars` must consume the complete position index. Negative and positive indices are bounds checked. Position components are also fully parsed and finite; zero homogeneous weights fail. |
| F06 invalid/ambiguous GUI measurements | GUI methods receive the shared topology/reference audit, typed failure counts, visibility and ranking eligibility. GPU samples use a synchronized draw query, separate from helper diagnostics. Missing samples stop the run. |
| F07 missing MSAA depth | Matching D32Float depth buffers and explicit Less/write state at 1x/multisample rendering. |
| F08 mutable benchmark state | Configuration controls lock during a run. A saved session restores dimensionality, methods, visualization, readback, MSAA, grid, input and model on completion/cancellation/resize/error. Source OBJ data stays frozen across methods. Benchmark phases and method-specific state are grouped and named. |
| F09 logging | Source informational messages no longer count as failed patches. Import counts describe polygons, not supposed fans. CSV text is quoted; summary/detail files have one schema each, include CPU/quality/config fields, and have metadata sidecars. Output stream failures are reported. GUI output stays under the sample's `analysis/gui`. |
| F10 repeated preparation/copies | GUI caches the source, validated baseline, patches and reference BVH; method changes reuse them. Face normals are computed once per growth invocation. Candidate queue duplicates are avoided. Vertex/index outputs move into the UI. |
| F11 custom search/metric | Backends are identified as heuristics, not PCL or exact MWT. LMT is limited to 64 projected vertices; Greedy/GP to 256, with explicit fallback. Visibility and LMT lengths are cached; GP edge proposals are deduplicated. Historical projected local flip scoring stays the default, with an explicit 3D length option. |
| F12 repeated utility/render work | One upload function, one initial-data upload per buffer, shared edge/camera/instance functions, cached per-vertex screen projection. Grid/model/camera updates happen on changes. Defines update through Falcor's no-op-aware `addDefine`. Unused FullScreenPass removed. |
| F13 analyzer pairing | Pair identity includes run ID, dimensions, instances, depth and mesh ordering. Duplicate baselines fail; absent Source/empty eligible groups produce empty comparison tables. Undefined percentages stay null. Mesh audit validity is distinct from per-view visibility. |
| F14 benchmark organization/cost | Options, audits, statistics and reference queries have dedicated modules. FBOs are reused per MSAA setting. Raw sample IO happens after the case's sampling loop. Metadata records real build configuration, revision, source digest and workload settings. Synchronous isolated timing is preserved and labeled. |
| F15 GUI shader workload | Optional helper texture sample has a real output dependency. Texture input is nonconstant and its sampler is explicit. GUI material work is labeled SyntheticWave8; it remains distinct from the headless Material16 workload. Timed headless shaders are unchanged. |

## Behavior and limits

Validation and guarded fallback intentionally change cases previously accepted with incomplete or invalid output. Re-run measurements with the current build; do not merge them with the historical matrices as the same implementation. The historical matrices remain under `analysis/gpu`.

Patch and reference checks are finite, numerical tests, not a full self-intersection or Hausdorff certificate. The custom methods still have high-order search, now bounded; no production remeshing library or exact MWT solver was substituted. OBJ import preserves position indices and polygons, not UV/material/normal seams. The global heap's hard guards and gain ordering were preserved. Reference accelerator preparation is now shared and excluded from per-method CPU timing in the headless runner; final auditing is also excluded, as before. GPU timings measure an isolated draw or clear-plus-draw, not application FPS.

## Build and checks

From `C:\Users\RobertBene\Documents\Falcor`:

```powershell
& .\tools\.packman\cmake\bin\cmake.exe -S . -B build/windows-vs2022-helperlanes
& .\tools\.packman\cmake\bin\cmake.exe --build build/windows-vs2022-helperlanes --config Release --target HelperLaneWindows FalcorPython --parallel 6
```

Open `build/windows-vs2022-helperlanes/Falcor.sln` in VS2022. It contains the updated project sources and folder filters. From the sample folder:

```powershell
& ..\..\..\build\windows-vs2022-helperlanes\bin\Release\HelperLaneWindows.exe --self-test
& ..\..\..\build\windows-vs2022-helperlanes\bin\Release\HelperLaneWindows.exe --ui-smoke
& ..\..\..\tools\.packman\python\python.exe analysis/test_summarize_gpu.py
```

## Verification and measured tradeoffs

- Release build of `HelperLaneWindows` and `FalcorPython` passed. The generated VS2022 project and filters contain the new module paths. [Build log](build/fixes_build.log).
- `--self-test` passed: malformed OBJ numbering/index/float handling, concave source polygons, finite inputs, complete fallback, opt-in 3D local flip scoring, and source/heap topology on sphere/jittered/torus. [Regression log](build/fixes_regressions.log).
- `--ui-smoke` passed: planar measurement, volume 1x/4x MSAA, Texture1/SyntheticWave8 paths, separate draw queries/helper-active diagnostics and session restoration. [GUI log](build/fixes_ui_smoke.log).
- Four analyzer regression tests passed. CSV headers/row widths, positive finite raw samples, enclosing pass queries, medians, sample counts, ranking eligibility, source digest and VS source entries passed [verification](build/fixes_verification.json).
- Fresh native GPU data: 480 torus cases/28,800 timed draws plus 2,304 original-method comparison cases/69,120 draws. All torus cases are eligible; 1,584 method cases are eligible and 720 correctly excluded. Files are under [gpu/fixes](gpu/fixes).

`HelperLaneViz.cpp` is now 1,331 lines, versus 2,337 before cleanup; the surface core is 775 versus 1,707. This reflects extraction plus removal of repeated work; total project size still includes the extracted modules and new validation. The OBJ loader remains about the same size, with stronger parsing contracts. Line endings match the repository to avoid whole-file diff noise.

The guarded global heap reproduces 50.714% torus shortening, 25.659% fewer helper proxies and 33.106% less 4x Material16 draw time (16 instances, depth and meshoptimizer). The plane-score variant is 32.901% faster in that repeat; it does not establish a benefit over length scoring. Stricter seed/projection validation changes current CDT torus shortening from the historical 32.3% to 16.3%, with about 8.9% draw improvement. These are measured behavior changes, not a claim that every triangulation is unchanged.

See [METHOD_ASSESSMENT.md](METHOD_ASSESSMENT.md) for keep/retire decisions. Source, CDT and its optional flip pass earn their place; Greedy is a research comparator; the current LMT/custom GP do not justify a normal application role. Their implementations were retained with truthful labels, bounds and rejection behavior for comparison. All code changes remain uncommitted.
