> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# HelperLaneWindows / 3DScenes investigation

Reviewed 10 October 2026. Current checkout: `master`, commit `89d314b`.
Reviewed 3D branch: `origin/3DScenes`, commit `0a485c78ce29377ad958ce7c9340b9ac8c93fac2` (`3D iteration 1`).
The remote-tracking revision available locally was reviewed; no remote fetch was performed.

**Follow-up:** The user subsequently authorized changes to parent Falcor and GPU execution. The sample is now registered, built, and measured on `codex/gpu-triangulation-study`, based on the reviewed `3DScenes` revision. See [GPU_RESULTS.md](GPU_RESULTS.md) for native GPU measurements, the guarded 3D heap prototype, and updated build instructions. The build limitations below describe the earlier folder-confined phase only.

## Findings and recommendation

The proposed max-heap edge-flipping approach is a good next experiment. Start from a valid surface triangulation, preserve topology and features, reject excessive surface error, and prioritize reductions in expected raster work. Use a plane-deviation test primarily as a geometric constraint. A raw length-minus-planarity score alone does not establish faster rendering or preserve the original surface.

The branch already has the max-heap mechanism. Its flip score uses squared diagonal lengths in each patch's 2D projection. The missing parts are 3D geometric validation, an objective tied to the intended views, and trustworthy performance measurements.

Correctness needs attention before GPU comparisons. The isolated original algorithms produced missing patches, duplicate triangles, and edges used by more than two triangles. Those outputs can change the workload enough to invalidate an apparent speedup.

## What this project is

This is a Falcor rasterization research sample, not volumetric tetrahedral meshing or camera-based 3D point reconstruction. It studies how a polygon or surface's triangulation affects GPU helper lanes, construction cost, and render time.

On `master`, it loads SVG outlines or generates circles/ellipses, applies polygon triangulators, uploads an indexed mesh, instances it, and displays helper-lane colors, wireframe, or a texture. Optional meshoptimizer passes reorder vertices/triangles for cache, overdraw, and fetch efficiency. Folder benchmarks vary MSAA and instance grids.

Pixel shaders operate with 2x2 quads when derivatives or quad operations require them. A triangle covering only some pixels of a scheduled quad leaves helper lanes. Neighboring triangles are separate primitives: an internal diagonal can cause extra partially covered quads on both sides. Short projected edges and fewer skinny or tiny projected triangles can reduce this waste. Microsoft documents both quad organization and the conditions under which helper lanes exist in its [wave intrinsics documentation](https://github.com/Microsoft/DirectXShaderCompiler/wiki/Wave-Intrinsics) and [helper-lane specification](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_7_Wave_Ops_Include_Helper_Lanes.html).

## Build setup and limits of this investigation

The sample is not standalone: its CMake file calls Falcor's `add_falcor_executable()` and shader-copy helpers. The actual target and shader directory are `HelperLaneWindows`. The README's suggested `HelperLaneViz` directory name is inconsistent with the shader path used by the source.

The normal Windows build procedure is:

1. Initialize the six pinned sample submodules with `git submodule update --init --recursive` from this folder. These provide Eigen, libigl, Triangle, Earcut, meshoptimizer, and NanoSVG. All six folders were empty in this checkout. Git's submodule helper also failed in the current shell because its Unix helper tools were absent from PATH; add Git's `usr/bin` to that process's PATH if needed.
2. Register `add_subdirectory(HelperLaneWindows)` in Falcor's `Source/Samples/CMakeLists.txt`. The existing parent file does not register this sample.
3. From the Falcor root, configure `cmake --preset windows-vs2022`, then build `cmake --build build/windows-vs2022 --config Release --target HelperLaneWindows` using Falcor's bundled CMake if it is absent from PATH. The executable should be under `build/windows-vs2022/bin/Release`.
4. Configure runtime input/output paths. `master` has placeholders for SVG, screenshots, and the single benchmark file. The 3D benchmark instead defaults to a timestamped CSV in Downloads; change that destination to this project's `analysis` directory for folder-confined runs. The source initially starts in planar mode; select 3D geometry to use procedural surfaces or OBJ input.

The parent has an existing VS2022 build, but no HelperLaneWindows executable was found. A full Falcor configure unconditionally runs Packman against the parent dependency manifest and can write outside this sample. Parent registration and that dependency workflow were not run under the user's instruction to work only in this folder. Consequently **no GPU frame-time or hardware helper-lane measurements were obtained**. CDT and CDT+flips were also not CPU-benchmarked because their dependencies are absent.

Instead, `cpu_harness` compiles the branch's original non-CDT functions using a minimal scalar `float2`/`float3` shim. Only the unavailable CDT function is disabled; it is never replaced with another triangulator. The original heap optimizer is extracted unchanged, with an equivalent scalar cross-product helper. Production files and the checked-out branch were not modified.

Reproduce the measurements here with:

```powershell
& .\analysis\cpu_harness\run.ps1
```

The script uses installed MSVC and Falcor's bundled Python as read-only tools. All generated files are inside this project. The MSVC/SDK paths are machine-specific. Preparation is pinned to the reviewed commit.

## What 3DScenes adds

The branch adds approximately 3,960 lines across ten files. Its pipeline is:

`polygonal surface → face adjacency → approximately planar patches → 2D projection → point-set triangulation → original 3D vertices → indexed draw`

Supported procedural inputs include spheres, ellipsoids, jittered spheres, lumpy ellipsoids, saddle patches, folded sheets, and clustered wave patches. The OBJ loader reads positions and polygon indices, with optional normalization; it does not preserve OBJ materials, UV seams, or normal attributes. The source baseline fans each polygon from its first vertex, which needs a robust alternative for concave OBJ polygons.

Patch defaults are 15° normal deviation, plane distance at most 0.5% of patch bounds diagonal, and at most 64 source faces. Boundaries must form one loop; holes or other boundary topology are rejected. Original vertex positions are retained, but changed connectivity changes the piecewise planar surface on noncoplanar points.

| Branch method | What it actually implements |
|---|---|
| Source | Existing triangle faces or a fan of each polygon |
| CDT | Triangle/libigl constrained Delaunay on patch vertices and boundary segments |
| CDT + Flips | CDT followed by the existing projected 2D length heap |
| Greedy Point Set | All-pairs candidate edges, sorted by 3D length, checked for projected intersections |
| LMT MWT | Custom witness filtering, greedy graph completion, then projected 2D flips |
| PCL Greedy Projection | Custom neighborhood/angle heuristic on the patch projection |

`LMT MWT` is not an exact minimum-weight solver. `PCL Greedy Projection` does not call PCL; official [PCL GP3](https://pointclouds.org/documentation/tutorials/greedy_projection.html) grows a surface using point normals and local neighborhoods. Treat the branch names as heuristic labels, not equivalent implementations of those algorithms. The LMT final flip objective is 2D even when its earlier edge weights are 3D.

The volume benchmark tries six methods over 32 transforms, with 50 warmup frames and 200 timed frames per transform. It records mean/min/max of the orientation medians, total 3D edge length, average projected edge length, and helper counts, followed by per-transform rows. It measures roughly 38,400 timed frames per complete run. The exported `ZRotationDegrees` is misleading: the implementation rotates around world Y for that sweep.

## New CPU measurements

MSVC x64, `/O2 /DNDEBUG`, one warmup and five timed runs per case; median wall time shown. These are local isolated CPU measurements, not end-to-end app or GPU measurements. Times include patch growth, triangulation, and result assembly, but exclude geometry generation, file IO, buffer uploads, and meshoptimizer. Timing noise and scalar math differences limit precise transfer to the Falcor build.

There are 52 cases in [cpu_benchmark.csv](cpu_benchmark.csv), with timing ranges and topology checks. [benchmark_metadata.json](benchmark_metadata.json) describes the experiment.

| Geometry | Vertices | Source fan ms | Greedy ms | LMT ms | Custom GP ms | Original heap + source ms |
|---|---:|---:|---:|---:|---:|---:|
| Sphere | 512 | 0.061 | 4.998 | 5.376 | 5.352 | — |
| Sphere | 2,048 | 0.262 | 18.097 | 23.260 | 18.752 | — |
| Flat grid | 576 | 0.061 | 29.720 | 368.654 | 15.969 | 0.237 |
| Flat grid | 2,176 | 0.223 | 122.897 | 1,514.180 | 62.572 | 0.818 |
| Flat grid with mild position jitter | 2,176 | 0.266 | 129.472 | 2,828.260 | 58.714 | 1.309 |

The heap comparison is restricted to genuinely planar inputs. It does not validate an implemented 3D planarity-aware heuristic.

On the 512-vertex sphere, 495 of 503 patches are single faces; the largest has only four faces. Greedy reduces total unique edge length by approximately 0.12%, LMT by 0.13%, and custom GP by 0%. The cost of growth alone is about 3.44 ms in this run. The patch machinery consumes milliseconds while giving the triangulator very little freedom.

On the 2,176-vertex planar grid, all tested methods have the same total edge length. The heap avoids the exhaustive search cost. On the mildly jittered planar grid, the original heap reduces edge length from 254.211 to 251.047, approximately **1.24%**, in **1.309 ms including source triangulation**. It retains all 4,158 triangles and passes the implemented boundary, duplicate, edge-incidence, and degeneracy checks. This establishes useful CPU feasibility for local flips; it does not establish a GPU speedup or complete geometric validity.

### Reproduced correctness failures

| Case | Observed output |
|---|---|
| Jittered sphere, 512 vertices | All three patch methods: 14 failed patches, 992 instead of 1,020 triangles, 54 unexpected boundary edges |
| Jittered sphere, 2,048 vertices | All three: 84 failed patches, 3,924 instead of 4,092 triangles, 316 unexpected boundary edges |
| Sphere, 2,048 vertices | Greedy: 363 duplicate triangles and 363 edges with incidence above two; LMT: 24 of each; custom GP: 15 of each |
| Jittered planar grid, 2,176 vertices | All three patch methods: 4,160 instead of 4,158 triangles, three edges with incidence above two |

The sphere duplicate case has exactly the expected triangle count, showing why triangle counts alone are insufficient. These are harness observations from original algorithm bodies. Revalidate them in the full Falcor build before treating exact failure counts as platform-independent.

## Why the costs occur

**CPU:** patch growth repeatedly copies candidate face lists and recomputes vertex sets, normals, centroids, bounds, and deviations. Nonplanar or noisy surfaces fragment into tiny patches. On flatter surfaces, larger patches expose the expensive candidate searches: greedy scans vertex pairs and repeatedly checks intersections, LMT additionally searches opposite-vertex witnesses and competing candidates, and custom GP scans all points rather than using a spatial neighbor index. Its final graph processing is also nontrivial. The measurements show both overhead-dominated small patches and expensive large patches.

**GPU:** changing triangulation can change projected triangle shape, partial-quad frequency, depth coverage, cache reuse, and overdraw. On a nonplanar quad, swapping the diagonal changes the actual surface and may change visible coverage. Total object-space edge length therefore cannot explain all render-time differences. Small triangles can also become geometry/setup-bound, in which case reducing helpers may produce little improvement; NVIDIA discusses the importance of subpixel triangle workloads in its [micro-mesh rasterization material](https://developer.download.nvidia.com/ProGraphics/nvpro-samples/slides/Micro-Mesh_Rasterization.pdf).

The amount of GPU benefit is currently unknown. A useful diagnostic approximation, holding coverage and shader work fixed, is:

`T ≈ Tfixed + c × (P + H)`

where `P` is active pixel-lane work, counted with overdraw, and `H` is helper-lane work. Estimated whole-pass savings are `f × (Hbefore − Hafter)/(P + Hbefore)`, where `f` is the fraction of time attributable to that lane work. For example, P=1,000,000 and H falling from 500,000 to 250,000 imply 16.7% less modeled lane work; at f=0.6, that is about 10% less total time. This is an illustration, not a prediction for this project. Hardware helper costs are not uniformly equal to active-lane costs.

## Measurement problems to fix first

1. **Missing/invalid geometry is accepted.** [triangulateMesh](cpu_harness/Triangulation3D.cpp#L1615) skips failed patches and declares success if any indices remain. Patch success only checks that a nonempty index count is divisible by three. Preserve source triangles as fallback, mark the result degraded, and reject invalid results from performance rankings. Audit duplicate triangles, manifold edge/vertex incidence, original boundaries, coverage, winding, and reference surface error.
2. **Ear clipping can report partial completion.** `appendEarClippedFace()` returns success after emitting any triangles even when it cannot finish the polygon; its caller ignores the return value. Make this operation transactional and validate full domain coverage.
3. **A seed patch bypasses tolerance acceptance.** `growPatches()` validates proposed additions, but a lone seed goes through `makePatch()` without rejecting `stats.valid == false`. The configured tolerances do not certify every resulting patch. Normal comparisons also use the absolute dot product, masking reversed orientation. Validate seeds, projection injectivity, and winding separately.
4. **The MSAA switch also changes depth availability.** Culling is disabled and the sample creates no depth attachment for its MSAA FBO, used by default at 4x. At 1x it instead draws to Falcor's target FBO, whose default configuration includes D32 depth. Thus the MSAA comparison can change occlusion behavior as well as sample count. Without depth, closed surfaces and XYZ instance layers can shade hidden faces. Give the MSAA FBO a matching depth attachment, set depth/culling states explicitly, and separate diagnostic/opaque benchmark modes. Verify winding before enabling culling.
5. **Timing is the whole callback.** `/onFrameRender` includes clears, resource updates, drawing, resolve/blit, and benchmark transitions. CPU work inside timestamp bounds can also leave the GPU idle. Put timestamps directly around the draw or intended render pass, move setup outside, and handle the profiler's delayed results with frame IDs.
6. **CPU time and important metadata are discarded.** `mVolumeBenchmarkCpuTimeMs` is computed but not exported. Its measured function includes generation/OBJ loading and uploads. Export separate generation, patching, triangulation, flips, optimization, and upload costs; record GPU/driver, framebuffer size, MSAA, shader, sample count, culling/depth state, output vertex/triangle counts, failures, and reference error. Missing timing samples must be errors, not zero milliseconds.
7. **Helpers are counted in a different shader.** GPU time is measured in the selected mode, then the shader switches to helper visualization with quad intrinsics and a global atomic counter. Those intrinsics can change whether helpers are retained. Keep diagnostic counts separate, inspect optimized shader code, and correlate with representative texture/material passes. The purported dummy dependency `dummy > -1 ? 0 : 0` is identically zero; it does not guarantee the sample survives optimization. The constant red texture can also make the stress pass unrepresentative, and its lane-dependent UVs/wave reduction add artificial work.
8. **Length is not visibility-aware.** The projected metric sums all unique edges in all instances without clipping or depth visibility tests. It is a proxy. Camera fitting also changes screen coverage as instance count increases. Treat instance sweeps as different scene scales, or hold camera and projected size fixed for causal comparisons. The fixed 32-transform grid is not uniform orientation sampling.
9. **Meshoptimizer does not run on the volume path.** The 3D generation/upload functions bypass the existing planar optimizer. Compare cache/fetch optimization consistently across methods. For opaque meshes, also compare overdraw reordering, following the [meshoptimizer pipeline order](https://github.com/zeux/meshoptimizer#core-pipeline).

## Assessing the proposed flip score

Let `ab` be the current shared diagonal and `cd` the alternative. The two surrounding triangles form the candidate quad.

**Length reduction is a reasonable benefit term.** Replacing only the internal diagonal changes total unique edge length by `|ab| − |cd|`; surrounding edges are unchanged. Prefer actual length reduction for priority if that is the objective. The existing code ranks `|ab|² − |cd|²`, which accepts the same shortening moves but orders them differently. It already has versioned stale-entry rejection and local neighbor updates.

There is a useful theoretical reason for 3D length: under uniformly distributed orthographic viewing directions, expected projected segment length is `(π/4)|e|`. Minimizing 3D length then minimizes that particular average projected-length proxy. This is a mathematical inference from isotropic directions, not a measured result. Perspective, occlusion, finite view distributions, anisotropic pixels, and quad alignment break that simple equivalence.

**Plane deviation needs careful interpretation.** A plane fitted to the same four fixed vertices has the same residual before and after a flip. It ranks or vetoes risky quads, but does not itself say which diagonal approximates the reference surface better. Likewise, comparing two endpoint distances to the patch plane is insufficient to prevent shortcuts over folds or changed interpolation.

For a convex quad that is a graph over a local plane, find the intersection of the two projected diagonals. Lift that intersection once by interpolation on `ab` and once on `cd`. The separation is a cheap measure of the local surface change. In this restricted graph setting, the maximum height difference between the two piecewise linear triangulations occurs at that crossing. It is a useful rejection test. It is symmetric and per-flip: repeated small changes can accumulate, so validate against the fixed original surface rather than just the previous mesh.

Use hard admissibility constraints first:

- Exactly two adjacent triangles, four distinct vertices, consistent orientation, and a convex/nondegenerate projected quad.
- The new diagonal is absent elsewhere in the mesh; the halfedge/link conditions preserve manifold topology.
- Boundaries, creases, material boundaries, UV seams, and designated feature edges remain protected.
- No inverted faces, unacceptable minimum angles, or triangle/reference normal deviations.
- Reference surface and silhouette errors stay within absolute object-space and/or pixel-space budgets.

Then score admissible candidates using an actual decrease of a fixed objective:

`J = Σview wview × rasterCost(mesh, view) + λ × referenceError(mesh) + μ × poorShapeCost(mesh)`

`heapPriority = Jbefore − Jafter`

For a cheap first version, `rasterCost` is expected projected perimeter over representative cameras; for a better version, it is touched quad count or estimated helper work for the two candidate triangles. Normalize soft costs or express them in compatible units. A true decreasing energy plus an epsilon prevents flip oscillation; a context-dependent score without such an energy may cycle. Recompute affected edges and stale heap entries, and impose flip/pop/time limits. Changes in camera distribution require rescoring.

The fixed triangle count matters: flips cannot cure excessive tessellation or eliminate all subpixel triangles. They optimize connectivity locally and can become trapped in local minima.

### Software counterexample to length alone

The reproducible [quad experiment](quad_experiment.py) uses a planar convex quad with identical coverage under both diagonals. In its one-sample software raster model:

| Diagonal | Length in pixels | Covered pixels | Modeled helper lanes | Total scheduled lanes |
|---|---:|---:|---:|---:|
| AC | 15.202 | 104 | 36 | 140 |
| BD | 14.681 | 104 | 44 | 148 |

The diagonal is 3.43% shorter but generates 22.2% more modeled helpers. Across 64 quarter-pixel translations spanning a 2x2 phase period, the shorter diagonal is better in 26 cases, worse in 12, and tied in 26. This supports projected length as a useful surrogate while showing why direct quad cost can choose better flips. It is not a hardware rasterization benchmark; it omits MSAA, depth, clipping, quantization, and GPU scheduling differences.

## Better approaches, in priority order

1. **Valid source mesh + constrained global local flips.** This is the first implementation to try. It avoids growing and retriangulating many patches, naturally retains interior vertices, and can preserve topology with halfedge validation. Protect features and use reference-error rejection. The planar CPU heap experiment supports its low construction cost. Benchmark length-only, length-with-error-gates, and projected/quad-cost scoring separately.
2. **Fix the benchmark and rendering state.** Preserve complete geometry, add topology/reference checks, measure the draw separately, and compare diagnostic versus representative opaque texture/material passes. Otherwise the optimization objective is being inferred from confounded results.
3. **Apply cache/fetch/overdraw optimization consistently.** This targets independent costs and may outperform connectivity changes when helper overhead is a small fraction of the pass. Reordering should be measured separately so its benefits are not credited to triangulation.
4. **Use LOD or error-bounded simplification for tiny triangles.** If geometry/setup or microtriangle processing dominates, reduce triangle count under projected error bounds. Keep a fixed-count experiment for the paper's triangulation question, and a separate best-rendering-performance experiment where simplification is allowed.
5. **Use robust remeshing when mesh quality or sampling is the problem.** Edge splits/collapses, feature-aware flips, and projection to the reference surface can address problems that connectivity-only flips cannot. [CGAL's remeshing documentation](https://doc.cgal.org/latest/Polygon_mesh_processing/index.html) provides a reference for this workflow. This changes vertex positions/counts, so evaluate quality and cost separately.
6. **Retain patch retriangulation where it is justified.** Use it for large, well-validated near-planar polygonal domains rather than as the default for every curved mesh. Cache adjacency/patches across methods, update fitting statistics incrementally, split invalid domains, preserve holes/constraints, and replace all-pairs neighborhood scans with spatial indexing. A larger patch tolerance trades optimization freedom against surface error; it is not automatically a performance improvement.

## GPU experiment that would answer “how much?”

After the renderer is buildable, compare the same valid source against CDT, original flips, 3D length flips, length flips with reference-error constraints, and expected projected/quad-cost flips. Include regular/jittered flat grids, curved smooth surfaces, sharp folds, and a triangulated OBJ with meaningful materials. Keep valid coverage and geometric error comparable.

Sweep fixed framebuffer sizes, projected object sizes, MSAA 1x/4x, and representative cameras. Separate fixed-projected-size instance replication from camera-fit experiments. Test a cheap pass, a texture-derivative pass, and a representative expensive material. Use an opaque depth/culling configuration and a separately labeled no-depth diagnostic configuration.

Collect paired per-view draw medians across several randomized method-order blocks, raw sample counts, variability/confidence intervals, active/sample work, diagnostic helpers, projected area/perimeter/aspect ratios, topology/error checks, cache metrics, and CPU stage costs. Report paired percentage changes with unchanged quality; do not rank incomplete meshes. Use pass/geometry counters or a GPU profiler to identify the bottleneck before attributing improvement to helper reduction.

For dynamic optimization, check the break-even point: `framesToRecover ≈ extraOptimizationCPUms / savedGPUmsPerFrame`, subject to actual CPU/GPU overlap and update frequency. For example, 5 ms extra preprocessing needs about 100 frames to recover a measured 0.05 ms per-frame saving. Bake stable static-mesh results offline where possible.

The present evidence establishes CPU construction costs, reproduces validity failures, and supports testing constrained local flips. It does not establish any percentage reduction in GPU render time for the proposed 3D heuristic.
