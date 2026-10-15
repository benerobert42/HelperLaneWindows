> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# Change guide

The working tree on `codex/gpu-triangulation-study` starts from `origin/3DScenes` commit `0a485c78ce29377ad958ce7c9340b9ac8c93fac2`. Source changes are grouped in logical commits; CMake, documentation, assets and generated measurements remain local as requested. The historical review is [CODE_REVIEW.md](CODE_REVIEW.md); the implemented resolutions are [FIXES.md](FIXES.md). The original-method assessment is [METHOD_ASSESSMENT.md](METHOD_ASSESSMENT.md). The implemented appearance/model scope is described in [GRAPHEX_INTEGRATION_PLAN.md](GRAPHEX_INTEGRATION_PLAN.md). The later UI/timing cleanup is documented in [UI_REVIEW.md](UI_REVIEW.md).

## Organization and existing methods

SVG and OBJ loaders live in `Import`. Planar algorithms and projected patch backends live in `Triangulation/2D`. Surface preparation, patch growth, generators, reference queries and global flips live in `Triangulation/3D`. Shared types, math and validation live at the `Triangulation` root. Falcor lifecycle/rendering/UI stay in `HelperLaneViz`; benchmark orchestration is extracted to `Experiments`.

The initial folder-only move preserved implementation bodies, recorded in [organization_audit.json](build/organization_audit.json). The subsequent cleanup deliberately changes validation, failure handling, expensive searches and measurement behavior. The current implementation should not be described as unchanged.

| Code | Current responsibility and change |
|---|---|
| [Triangulation3D.cpp](../Triangulation/3D/Triangulation3D.cpp) | Source surface definition, eligible patch growth, lifting and assembly. Seed validation, cached normals, source-triangle coverage and complete rollback on failure. |
| [ProjectedPatchTriangulation.cpp](../Triangulation/2D/ProjectedPatchTriangulation.cpp) | Original Triangle CDT/CDTFlip adapter and backend dispatch, with strict projected disk validation. |
| [ProjectedGraph.cpp](../Triangulation/2D/ProjectedGraph.cpp) | Original Greedy, LMT heuristic and custom greedy projection searches; cached visibility/lengths, bounded input size and deduplicated proposals. No PCL dependency or exact MWT solver was added. |
| [TriangulationHelpers.cpp](../Triangulation/2D/TriangulationHelpers.cpp) | Original 2D algorithms. The local flip helper now accepts an optional actual 3D length metric; default projected scoring stays compatible. |
| [ProceduralGeometry3D.cpp](../Triangulation/3D/ProceduralGeometry3D.cpp) | Original generators; bodies preserved. |
| [ObjMeshLoader.cpp](../Import/ObjMeshLoader.cpp) | Strict position/index parsing, correct numbering, finite normalization, complete failure on malformed input. |
| [MeshValidation.cpp](../Triangulation/MeshValidation.cpp), [ReferenceSurface.cpp](../Triangulation/3D/ReferenceSurface.cpp) | Shared topology/edge/area audit and immutable reference BVH, reused by GUI and CLI. |
| [HelperLaneViz.cpp](../HelperLaneViz.cpp), [SampleSupport.cpp](../SampleSupport.cpp) | UI/rendering plus extracted camera/instance/edge utilities; cached source preparation, one buffer upload, state-safe benchmark controls. |
| [InteractiveBenchmark.cpp](../Experiments/InteractiveBenchmark.cpp) | GUI benchmark state machine, restoration, timed draw/diagnostic separation, CSV and metadata. |
| [Shaders/MainShader.slang](../Shaders/MainShader.slang) | Helper visualization with separate active/helper counters, wireframe and internal flat raster benchmark. Interactive texture/wave modes and dummy texture setup are removed. Independent CLI Texture1/Material16 workloads remain in PerfShader. |

No existing 3D backend was deleted. Source, CDT, CDTFlip, Greedy, LMT and GP remain available for comparison. Failed methods retain the entire validated source mesh with explicit `SourceFallback` status and cannot rank as successful results. This prevents missing or invalid triangles from being rewarded as speedups. See the method assessment for what should remain in a normal workflow versus research-only code.

## New global 3D experiment

[SurfaceFlipExperiment.cpp](../Triangulation/3D/SurfaceFlipExperiment.cpp) is the added algorithm, separate from the original projected methods. It starts from Source triangles, builds global edge adjacency and a max heap, and preserves vertex positions and triangle count. Local topology, convexity, orientation, current crease checks and sampled distance to the fixed source surface guard every accepted flip.

- `Heap3D`: prioritize old diagonal length minus new diagonal length.
- `HeapPlane`: prioritize that decrease minus four times the quad plane residual.

Cleanup extracted shared math and the BVH while preserving the heap's gain ordering and guards. Both variants are accessible through the benchmark CLI, rather than the GUI selector. Finite error sampling is not a Hausdorff or self-intersection certificate. OBJ attributes, seams and persistent feature constraints still require additional work.

## Measurement code

[PerformanceBenchmark.cpp](../Experiments/PerformanceBenchmark.cpp) orchestrates native GPU cases. Options, statistics, geometry factories and audits have dedicated modules. [PerfShader.slang](../Shaders/PerfShader.slang) is unchanged by the cleanup. Draw timing and clear-plus-draw timing exclude upload, instrumentation, resolve, readback, presentation and UI. CPU construction includes method preparation/per-flip guards; shared BVH setup, final audit and upload are excluded. CPU values are single observations, not a repeated CPU benchmark.

[run_gpu.ps1](run_gpu.ps1) invokes the tested build; [summarize_gpu.py](summarize_gpu.py) pairs matching configurations and rejects duplicate baselines. Summary/detail CSVs have separate schemas, quoted names, explicit audit/visibility eligibility and metadata with build configuration, revision and compiled source digest. Native regression and GUI checks live in `Experiments/RegressionChecks.cpp` and `InteractiveSmoke.cpp`.

The skew-connected torus/cylinder and skewed sheets deliberately begin with long edges to probe substantial shortening. They do not represent every imported mesh. Ellipsoid/blob inputs reuse the branch generators. [GPU_RESULTS.md](GPU_RESULTS.md) distinguishes historical measurements from the cleaned implementation; new data lives in `gpu/fixes`.

## Parent Falcor and build

Parent `Source/Samples/CMakeLists.txt` registers the sample. Parent `Source/Falcor/Core/API/GpuTimer.cpp` subtracts integer timestamps before converting the interval to double, avoiding cancellation of large absolute timestamps. Preexisting parent edits were preserved. Source-only commits were made in the sample repository; no parent commit, PR or push was made.

Use the commands in [FIXES.md](FIXES.md) or the main README to regenerate and rebuild `build/windows-vs2022-helperlanes/Falcor.sln`. CMake entries and Visual Studio filters follow the current folders.

## OBJ presets, raster preview and paper screenshots

`Data/Models` contains unmodified Suzanne/Bunny/Teapot assets with provenance and hashes. `Import/ModelPresets.h` is a small path mapping; all three go through the existing OBJ loader and 3D methods. CMake deploys the assets and groups the new files in VS2022. Native regression checks cover deployed asset counts and Source topology.

`Rendering/LitPreview.cpp/.h` and `Shaders/LitPreview.slang` isolate GraphEx's existing single-light raster material equations and source-normal upload. `Rendering/PreviewCapture.cpp` adapts the existing camera/grid to that renderer and provides `--screenshot recipe.json output.png`. HelperLaneViz adds only preset/mode selection, GUI controls and dispatch. The old custom half-float/gamma/flip PNG writer is removed; GUI and performance smoke captures use Falcor's synchronous capture API. One-instance volume grids now stay centered in wide viewports.

The existing OBJ parser, triangulation algorithms and `PerfShader.slang` are unchanged by this feature. Comparison capture and heap GUI integration remain deferred. [Implemented scope](GRAPHEX_INTEGRATION_PLAN.md) and the main README explain settings and commands.
