> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# Native Falcor triangulation performance study

**Timing audit update:** [TIMING_AUDIT.md](TIMING_AUDIT.md) confirms a shared Falcor timestamp buffer-state defect and previous-sample results. GPU timing numbers below, including post-cleanup numbers, require revalidation after the timer fix before use as paper results. Geometry/edge reductions are unaffected; helper values remain separate shader proxies. Original measurements are retained for traceability.

10 October 2026. This follows the earlier [source/CPU investigation](README.md) after permission to register the sample in parent Falcor and run it. The earlier report's lack of GPU measurements is superseded here.

The tables below describe the earlier implementation snapshot. The subsequent cleanup changes patch eligibility, validation and CPU timing scope. New results are separated in the post-cleanup section; see [FIXES.md](FIXES.md) and [METHOD_ASSESSMENT.md](METHOD_ASSESSMENT.md).

## Historical results with substantial edge shortening

The requested 20%+ regime was reproduced. The original CDT and CDT+Flips implementations reduce total unique 3D edge length by **32.3%** on the skew-connected torus. The new guarded global heap reduces it by **50.7%**. Existing methods were reused without modification; see the [file-by-file change guide](CHANGES.md).

This comparison uses the same 8,192 vertices and 16,384 triangles, 16 instances, 1280x720, depth enabled, 4x MSAA, the heavier `Material16` shader, and meshoptimizer enabled for every method. Values average case medians over eight views and three randomized blocks. Negative percentages indicate reductions relative to Source.

| Method | 3D edge length change | Helper proxy change | Draw, ms | Draw time change | Clear + draw, ms | Construction CPU, ms |
|---|---:|---:|---:|---:|---:|---:|
| Source | — | — | 1.180 | — | 1.186 | 1.12 |
| Existing CDT | -32.3% | -14.8% | 0.962 | -18.4% | 0.968 | 66.7 |
| Existing CDT+Flips | -32.3% | -14.8% | 0.964 | -18.3% | 0.970 | 68.1 |
| New Heap3D | -50.7% | -25.7% | 0.812 | -31.2% | 0.818 | 326.0 |
| New HeapPlane | -50.7% | -25.4% | 0.808 | -31.5% | 0.815 | 265.8 |

Heap3D's per-block draw improvement ranges from 30.9% to 31.4%; CDT's ranges from 18.2% to 19.0%. Clear-plus-draw improves by 31.0% with Heap3D. These are draw/pass timings, not total application frame times or FPS improvements. Active-lane counts also decrease by 18.7% with Heap3D, so the speedup cannot be attributed exclusively to helpers. MSAA coverage per primitive, visibility, and order affect the workload too. A hardware profiler would be needed to isolate the bottleneck.

All five torus outputs pass the implemented topology/error checks. The largest sampled reference error is approximately 0.001447 against a 0.001818 budget. Nonplanar flips change the piecewise-linear surface: Heap3D's torus area decreases by about 1.14%, despite retaining vertices and triangle count. The audit is finite and one-sided, not a certified Hausdorff bound.

Other substantial reductions from the single-instance matrix, without meshoptimizer, using the same heavier 4x workload:

| Fixture | Heap3D length change | Helper proxy change | Draw time change | Clear + draw time change |
|---|---:|---:|---:|---:|
| Skewed planar grid, shear 1 | -29.2% | -28.9% | -15.2% | -14.0% |
| Skewed planar grid, shear 2 | -50.5% | -37.1% | -20.5% | -18.4% |
| Skewed planar grid, shear 3 | -61.6% | -42.2% | -25.2% | -21.8% |
| Skewed folded grid | -56.9% | -41.8% | -25.6% | -22.6% |
| Skewed saddle | -59.4% | -44.5% | -22.7% | -20.4% |
| Skew-connected torus | -50.7% | -40.8% | -20.8% | -20.1% |
| Skew-connected deformed cylinder | -28.8% | -19.1% | -10.9% | -10.7% |

These fixtures deliberately have long starting connectivity. The torus/cylinder aligned and skewed variants sample the same vertex positions but connect rows differently. The cylinder uses 192 columns and 96 height intervals; the torus uses 128 columns and 64 rows. Original ellipsoid/blob generators were also tested, but the strict reference guard limits their valid heap shortening to a few percent. Larger apparent reductions from some branch methods come with omitted/duplicate triangles or reference-error failures and cannot be ranked as equivalent-quality improvements.

**Length alone does not predict raster performance.** Fitting 16 skewed planar grids into the framebuffer makes their triangles smaller. Shear-3 Heap3D still shortens edges by 61.6%, but helpers increase by 3.2% and the heavier 4x draw becomes 2.3% slower without meshoptimizer. With meshoptimizer applied to both methods, helpers still increase by 3.2%, while draw time improves by 16.2%. On the deformed cylinder with 16 instances and meshoptimizer, shortening is 28.8%, helpers fall only 3.8%, and draw time improves by 12.7%. Cache/order and triangle-size effects therefore need to be measured alongside helper utilization.

The plane penalty reduces accepted torus flips from 20,415 to 16,836 and CPU construction from about 326 to 266 ms in this observation, while final length and draw times are very similar. It is useful as a risk/cost preference, but these measurements do not establish a distinct rendering advantage over guarded length scoring. This prototype should be baked or amortized; its CPU construction cost is unsuitable for per-frame use here.

The four primary large-reduction matrices contain **5,088 cases and 305,280 timed draws**, using 60 warmup draws and 60 samples per case, eight views, three randomized blocks, both shaders, and 1x/4x MSAA. They use 200 ms GPU priming after preparing each geometry and nested timestamps for draw and clear-plus-draw. Every primary output passes the implemented ranking checks. Clocks were not locked.

| Dataset | Scope | Cases |
|---|---|---:|
| [gpu_large_single.csv](gpu/large_reduction/gpu_large_single.csv), [paired results](gpu/large_reduction/gpu_large_single.paired.csv) | Seven fixtures, Source/Heap3D/HeapPlane, one instance | 2,016 |
| [gpu_large_many.csv](gpu/large_reduction/gpu_large_many.csv), [paired results](gpu/large_reduction/gpu_large_many.paired.csv) | Same fixtures/methods, 16 instances | 2,016 |
| [gpu_large_meshopt.csv](gpu/large_reduction/gpu_large_meshopt.csv), [paired results](gpu/large_reduction/gpu_large_meshopt.paired.csv) | Shear-3 sheet, torus, cylinder; Source/Heap3D, 16 instances, meshoptimizer | 576 |
| [gpu_torus_methods.csv](gpu/large_reduction/gpu_torus_methods.csv), [paired results](gpu/large_reduction/gpu_torus_methods.paired.csv) | Torus, five methods, 16 instances, meshoptimizer | 480 |

Each stem also has `.raw.csv`, `.audit.json`, `.metadata.txt`, and a Source image in the same folder. Original-method discovery scans are under `gpu/scans`; short scans are not used for precise timing claims.

## Build and source state

Sample branch: `codex/gpu-triangulation-study`, based on `origin/3DScenes` at `0a485c78ce29377ad958ce7c9340b9ac8c93fac2`. Parent Falcor reports 8.0, commit `eb540f67`, with preexisting local changes. The original branch triangulators are unchanged. The new performance entry point is isolated behind `--perf`; the original GUI remains available.

The sample is registered with `add_subdirectory(HelperLaneWindows)` in parent `Source/Samples/CMakeLists.txt`. All six pinned sample dependencies and Earcut's nested screenshot-header dependency were initialized. A separate `build/windows-vs2022-helperlanes` directory avoids the existing cache's unavailable MSVC version. Both `HelperLaneWindows` and `FalcorPython` built successfully in Release. Falcor's SampleApp initializes Python even headlessly.

Environment: NVIDIA GeForce RTX 4050 Laptop GPU, Windows driver `32.0.16.1062`, Direct3D 12; Intel Core i7-13620H; MSVC 14.44.35207; Windows SDK 10.0.26100.0. The Intel integrated GPU was not used. Power/clock state was not locked. These are measurements of this laptop and these synthetic workloads, not a general speedup guarantee.

## What was implemented

[PerformanceBenchmark.cpp](../Experiments/PerformanceBenchmark.cpp) adds headless, deterministic GPU draw measurements. [PerfShader.slang](../Shaders/PerfShader.slang) provides a one-texture-sample workload and a heavier workload with 16 dependent texture samples. Both use a nonconstant texture, implicit derivatives, and position-derived UVs. The timed shaders contain no helper visualization, wave reduction, counter atomics, or lane-dependent UVs. Helper counts come from separate instrumented draws and are diagnostic proxies, not hardware performance counters.

[SurfaceFlipExperiment.cpp](../Triangulation/3D/SurfaceFlipExperiment.cpp) implements a global, fixed-vertex max heap starting from source triangles, avoiding patch reconstruction. Candidate edges must have two adjacent consistently oriented triangles, four distinct vertices, no existing alternative edge, a convex projected quad, nondegenerate new faces, and acceptable local normals. A 30-degree current dihedral/normal guard rejects sharp or inverted configurations. A fixed original-surface BVH checks the new diagonal midpoint and triangle centroids before each accepted flip, within 0.1% of the source bounds diagonal. The final mesh is independently audited.

`Heap3D` score: `old diagonal length - new diagonal length`.

`HeapPlane` score: `old diagonal length - new diagonal length - 4 * quad plane residual`.

The residual is the largest vertex distance to the plane through the four-vertex mean with the average facet normal. The coefficient 4 is one experimental setting, not a tuned optimum. Both variants have the same reference-error guards; this isolates the penalty's additional effect. Positive scores imply strictly decreasing total 3D edge length. Versioned heap entries and affected-edge updates avoid global rescans; flip and pop limits bound work.

This prototype does not carry arbitrary OBJ UVs/normals/materials, store persistent feature-edge tags, prove absence of self-intersection or vertex nonmanifoldness, or certify a Hausdorff error. Its finite, one-sided error sampling is a screening test. `ValidForRanking` means eligible under the implemented checks, not fully certified for production assets.

## Earlier baseline measurement protocol

Six meshes: regular spheres with 512 and 2,048 vertices; a 2,048-vertex jittered sphere; regular and mildly jittered flat grids with 2,176 vertices; a folded sheet with 2,176 vertices. Eight methods: Source, CDT, CDTFlip, Greedy, LMT, GP, Heap3D, HeapPlane.

Each matrix uses 1280x720, 1x/4x MSAA, matching D32 depth attachments, depth testing/writing enabled, no culling, two shaders, eight model orientations, three randomized method-order blocks, 20 warmup frames and 40 timed draws per case. Camera fitting and source bounds are fixed across methods. Y angles are 11.25, 101.25, 191.25, and 281.25 degrees; each is paired with X angles of 22.5 and 67.5 degrees. This avoids exactly edge-on flat sheets but is not uniform orientation sampling.

The single-instance and 64-instance matrices each contain 4,608 cases and 184,320 timed draws. The 64-instance grid is fitted into the same framebuffer, so its objects are smaller on screen. It deliberately tests a different triangle-size regime; it is not fixed-projected-size replication.

GPU timestamps enclose the indexed instanced draw; setup, mesh construction, upload, framebuffer clear, resolve, readback, screenshot, and instrumentation are excluded. Each query is resolved and synchronized before reading. Raw samples are retained. CPU construction values are single observations, with final topology/reference audits excluded. Per-flip guards and BVH construction remain included for the heap variants. Meshoptimizer cost, when enabled, is outside this CPU construction field.

[summarize_gpu.py](summarize_gpu.py) pairs methods with Source at the same view, block, MSAA, and shader. It averages per-case medians, then reports `(method time / source time - 1) * 100`; negative means faster. Block ranges expose variation. It also reports aggregate helper and active-lane changes. Invalid meshes are excluded from rankings.

The measured timestamp values are quantized at approximately 1.024 microseconds. A one-tick change is substantial for a 10-microsecond draw. Identical no-flip meshes expose residual clock/cache/order effects. Small percentages must be interpreted with those controls; three blocks do not justify precise population confidence intervals.

Parent `GpuTimer.cpp` now subtracts integer timestamps before converting the interval to double, avoiding cancellation of large absolute values. This defensive precision correction did not remove the observed timestamp quantization; it persists in the primary large-reduction measurements. Their nested clear-plus-draw scope includes framebuffer clear and draw, and excludes upload, resolve, readback, presentation, and GUI work.

## Correctness findings reproduced natively

| Mesh/method | Observed problem |
|---|---|
| Dense sphere, Greedy | 363 duplicate triangles and 363 edges used by more than two faces |
| Dense sphere, LMT / GP / CDTFlip | 24 / 15 / 26 duplicate triangles and corresponding nonmanifold edges |
| Jittered sphere, Greedy / LMT / GP | 84 failed patches, 3,924 instead of 4,092 triangles; winding and boundary errors |
| Jittered sphere, CDT / CDTFlip | Triangle count retained, but 316 winding errors, 2,198 unexpected boundary edges, sampled reference error about 0.0427 versus a 0.00208 budget |
| Jittered flat grid, Greedy / LMT / GP | 4,160 instead of 4,158 triangles, three nonmanifold edges |
| Folded sheet, all branch reconstruction methods | Winding/boundary and reference-error failures; several also omit patches |

Source and both guarded heap variants pass the implemented checks on all six inputs. Native CDT is also eligible on both regular spheres and both flat grids; native CDTFlip is eligible on the small sphere and flat grids. Lower timings from the rejected outputs are not improvements at equivalent quality. [gpu_single.audit.json](gpu/baseline/gpu_single.audit.json) contains the exact counts and errors.

## Construction cost

Native Release CPU wall time, milliseconds, single observation from the single-instance matrix. The original isolated harness remains useful for repeated CPU medians; these native values add CDT and the guarded prototypes. Invalid branch outputs still have measurable construction costs but are not usable replacements.

| Input | Source | CDT | Greedy | LMT | GP | Heap3D | HeapPlane |
|---|---:|---:|---:|---:|---:|---:|---:|
| Small sphere | 0.053 | 4.22 | 4.66 | 5.46 | 4.94 | 2.61 | 1.04 |
| Dense sphere | 0.210 | 13.14 | 17.10 | 22.74 | 18.07 | 13.13 | 4.45 |
| Jittered sphere | 0.163 | 17.23 | 18.71 | 24.13 | 21.80 | 6.76 | 6.30 |
| Flat grid | 0.187 | 15.89 | 129.87 | 1465.72 | 59.65 | 3.02 | 2.81 |
| Jittered flat grid | 0.202 | 15.69 | 147.86 | 2719.24 | 56.32 | 14.74 | 14.26 |
| Folded sheet | 0.180 | 17.17 | 24.40 | 22.69 | 21.98 | 7.77 | 6.53 |

The guarded heap is much cheaper than exhaustive LMT/greedy on the flat domains, but its reference BVH and per-flip queries are considerably more expensive than the earlier unguarded 2D heap. On the flat grid it spends roughly 3 ms despite accepting no flips. Do not run it every frame for a sub-percent GPU benefit. Bake static connectivity offline; for dynamic meshes, amortize it and measure actual CPU/GPU overlap.

Construction improvements worth implementing include reusing a persistent halfedge adjacency, caching the reference BVH, querying it only after cheap topology/normal/length rejection, deduplicating final validation samples, and using a certified planar-domain fast path. For the original patch pipeline, cache adjacency/patches across algorithms, maintain fitting statistics incrementally, validate seeds, use spatial neighbor indexing, and make failed reconstruction transactional with source fallback. The latter is required for correctness, not just performance.

## Interpreting the proposed heuristic

Length reduction is a useful surrogate for projected edge/quad work, especially averaged across views. It cannot model pixel-grid alignment, clipping, occlusion, shading locality, triangle setup, or vertex-cache behavior. A shorter diagonal can produce more partial quads at a particular view and subpixel phase; the earlier [software counterexample](README.md#software-counterexample-to-length-alone) demonstrates this with identical planar coverage.

The four fixed vertices have the same fitted-plane residual whichever diagonal is chosen. The plane penalty therefore discourages risky quads; it does not directly measure whether the new surface better matches the reference. For correctness, the fixed-reference error gate matters more. On the mildly jittered planar grid its penalty is zero, so both variants choose the same 1,244 flips. On the dense sphere it reduces flips from 1,006 to 134, and reduces construction work, but also sacrifices some length reduction. It does not establish an additional rendering benefit.

The best implementation direction is a valid source triangulation with constrained local flips. Keep topology and reference-surface error as hard constraints. For known cameras, score the change in weighted projected perimeter or touched 2x2 quads, including subpixel phases and clipping, rather than relying solely on object-space length. Use an actual decreasing global objective or explicit cycle protection when accepting length-increasing raster improvements. Protect original feature edges, seams, materials, boundaries, and reference normal/silhouette error. Benchmark cache/fetch/overdraw optimization separately, and use error-bounded LOD when microtriangle/setup costs dominate.

## Reproduction and artifacts

From the sample folder, [run_gpu.ps1](run_gpu.ps1) launches the tested build and writes summary, raw timing, metadata, audit, and paired-result files. `-Quick` is a five-case smoke test. Other switches include `-Instances`, `-Geometries`, `-Methods`, `-Samples`, `-Warmup`, `-Views`, `-Blocks`, `-MSAA`, `-NoDepth`, and `-Meshopt`. The executable additionally accepts `--width` and `--height`.

```powershell
# Choose a new output stem to retain saved runs.
& .\analysis\run_gpu.ps1 -Geometries 'skew1,skew2,skew3,skew_fold,skew_saddle,torus_skew,cylinder_skew' -Methods 'Source,Heap3D,HeapPlane' -Warmup 60 -Samples 60 -Output analysis/gpu/repeat_single.csv
& .\analysis\run_gpu.ps1 -Instances 16 -Geometries 'torus_skew' -Methods 'Source,CDT,CDTFlip,Heap3D,HeapPlane' -Meshopt -Warmup 60 -Samples 60 -Output analysis/gpu/repeat_torus.csv
```

Primary artifacts: [single-instance CSV](gpu/baseline/gpu_single.csv), [raw samples](gpu/baseline/gpu_single.raw.csv), [paired comparisons](gpu/baseline/gpu_single.paired.csv), [metadata](gpu/baseline/gpu_single.metadata.txt); [64-instance CSV](gpu/baseline/gpu_many.csv), [raw samples](gpu/baseline/gpu_many.raw.csv), [paired comparisons](gpu/baseline/gpu_many.paired.csv), [metadata](gpu/baseline/gpu_many.metadata.txt). Each run also captures one Source frame. `gpu/smoke/gpu_smoke` and `gpu/smoke/gpu_single_exploratory` are preliminary validation runs; the latter stopped at an exactly edge-on sheet and is excluded from the results.

The final runner adds 200 ms of both-shader GPU priming after preparing each geometry. The two earlier baseline matrices preceded that refinement and used the stated 20-frame warmup. The primary large-reduction matrices and follow-up controls use priming and longer per-case warmup; original metadata is retained to distinguish the protocols.


## Post-cleanup measurements

The final cleaned build has compiled source digest `48aefeb43b4920e92e382a4a95c9367b73120975f36186cc8467f5538cce884b` (MSVC 19.44, Release, protocol 2). Its eight-view torus repeat is saved in [torus_final.csv](gpu/fixes/torus_final.csv), [paired results](gpu/fixes/torus_final.paired.csv), [raw draws](gpu/fixes/torus_final.raw.csv), and [metadata](gpu/fixes/torus_final.metadata.txt). This uses the same 16 instances, meshoptimizer, depth, 1280x720, 4x Material16 comparison as the historical torus table, with three randomized blocks, 60 warmup and 60 timed samples per case.

| Method | Edge change | Helper proxy change | Draw, ms | Draw change | Construction CPU, ms |
|---|---:|---:|---:|---:|---:|
| Source | +0.00% | +0.00% | 1.605 | +0.00% | 0.7 |
| CDT | -16.29% | -3.75% | 1.463 | -8.86% | 140.8 |
| CDTFlip | -16.29% | -3.69% | 1.465 | -8.71% | 148.4 |
| Heap3D | -50.71% | -25.66% | 1.073 | -33.11% | 315.4 |
| HeapPlane | -50.71% | -25.39% | 1.077 | -32.90% | 267.5 |

Heap3D's per-block draw reduction is 33.08–33.16%, and clear-plus-draw improves by 32.98%. The length, flip count, surface area and helper/active diagnostics reproduce the original heap experiment; the core optimization was preserved. The surface remains an approximation: area drops about 1.14% and active-lane proxies drop 18.69%. Draw timing is not total application frame time.

Stricter source-seed/projection admission changes CDT's output: its torus shortening is now 16.3%, compared with the historical 32.3%. It now yields about 8.9% draw improvement in this repeat. That tradeoff must be acknowledged; cleanup did not preserve every original projected triangulation. Reference BVH setup is now shared and excluded from method CPU construction timing, so CPU values should not be compared directly to the earlier timing scope.

The [six-shape comparison](gpu/fixes/methods.csv) contains 2,304 cases and 69,120 timed draws, with four views, three randomized blocks and 30 warmup/timed samples. Its [method assessment](METHOD_ASSESSMENT.md) explains eligibility, costs and retirement recommendations. The torus repeat contains 480 cases and 28,800 draws; all 480 rank as valid. The six-shape matrix has 1,584 eligible cases; 720 are explicitly excluded because the method failed or exceeded its reference budget. Invalid rows are retained for diagnosis, rather than being reported as speedups.

Release compilation, native regressions, 1x/4x GUI benchmark/state restoration checks, CSV/raw-sample consistency and generated VS project entries passed. [fixes_verification.json](build/fixes_verification.json) records the checked cases and source digest. The historical datasets remain separate under their original folders.
