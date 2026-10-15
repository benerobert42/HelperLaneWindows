> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# Which original 3D methods earn their place?

Assessment of the original `3DScenes` designs and the cleaned implementation, 10 October 2026. Recommendation: retain Source, CDT and an optional constrained flip pass; retain Greedy only as an offline research comparator; retire the current LMT and custom GP backends from a normal application workflow. Keep their code/CLI names for reproducible historical comparisons until a deliberate removal. No backend was deleted during cleanup.

## They share a projected-patch architecture

The original five methods first grow approximately planar disk patches from the connected source mesh. They project the patch boundary and all its vertices into one plane, triangulate there, and lift the existing vertex IDs back onto their original 3D positions. They differ in choosing the planar connectivity. These are not five independent general 3D reconstruction algorithms, and their fixed patch boundaries limit shortening across the surface.

Source defines the reference connectivity. CDT calls the existing Triangle backend with constrained patch boundaries and interior points. CDTFlip adds local diagonal changes to the CDT output. Greedy searches visible point pairs and greedily adds the shortest noncrossing edges. LMT first filters edges through local witnesses, assembles a graph and adds flips. Custom GP proposes edges from angularly consecutive local neighbors, then builds a noncrossing graph. The last three share graph extraction/ear clipping in `Triangulation/2D/ProjectedGraph.cpp`.

The cleanup checks complete projected disks, oriented topology and source coverage. Backend failure rolls back to the complete Source mesh. Such fallbacks are excluded from performance ranking. A separate reference-error audit can also reject a completed mesh; a rejected output is not necessarily a topology failure.

## Keep/remove decisions

| Method | Value | Recommendation |
|---|---|---|
| Source | Trusted input connectivity, fixed reference and baseline. Needed to interpret every speedup and preserve failed cases. | Keep. |
| CDT | Handles boundary constraints and interior vertices using a mature existing backend. Most reliable projected baseline in this sample. Does not minimize total 3D edge length. | Keep as the normal projected method, with validation/fallback. |
| CDTFlip | Cheap additional local search. Can improve an asymmetric surface where projection loses the relevant length ordering. Sometimes identical to CDT, sometimes violates the reference budget. | Keep as an optional postpass, rather than a separate large backend. Add per-flip reference/feature guards before production use. |
| Greedy point set | Direct shortest-edge comparator, with actual 3D edge ordering by default. Sometimes changes raster behavior even at almost identical total length. Exhaustive candidates/intersection tests are expensive and completeness is fragile. | Keep only for offline comparisons for now. First retirement candidate after LMT/GP if reducing research scope further. |
| LMT heuristic | Local empty-triangle witnesses, greedy graph completion and local flips. No proof/certificate of minimum weight; high-order witness scans add considerable cost. | Retire from the normal workflow. Preserve as explicitly named research code if studying LMT itself. |
| Custom greedy projection / GP | Additional radius, neighborhood and angle parameters; sparse angular proposals can leave vertices unconnected. Uses the same patch projection/graph machinery and has no convincing unique benefit here. | Retire from the normal workflow. Do not present it as PCL GP3. |

### PCL greedy projection: useful algorithm, wrong role here

Actual PCL GP3 reconstructs surfaces from unordered point clouds with normals. It grows a fringe, projects local neighborhoods along each point's normal, uses a spatial search structure and tests normal/surface angles. That is a useful role when connectivity does not exist. See the [official PCL description](https://pointclouds.org/documentation/tutorials/greedy_projection.html).

This repository neither links PCL nor invokes GP3. Its custom implementation works in the single fitted plane of an already-connected patch, gathers neighbors by all-pairs search, applies distance/Mu/angle settings, scores triangle-perimeter-derived edge proposals, and extracts planar graph faces. It lacks GP3's per-point tangent frames, normal consistency filtering and advancing fringe. Similar option names do not make it equivalent to PCL.

For this sample's connected OBJ/procedural meshes, GP3 would discard useful input connectivity and add normal estimation/search/reconstruction work. It also would not directly optimize the stated total-length/helper-lane objective. Adding the real library is justified only for a separate unordered-point-cloud import/reconstruction feature, with its own coverage/error criteria. It is not the recommended optimization of the current pipeline.

### LMT is not exact MWT

`edgeHasLmtWitness()` checks empty triangles on opposite sides of a candidate edge and compares the two diagonals. `triangulateLmtMinimumWeight()` then filters conflicting candidates, adds compatible edges greedily, extracts faces, and runs a local flip optimizer. It does not globally solve minimum-weight triangulation, nor establish that its output is minimum weight. Graph completion/ear clipping and fallback further separate its output from an exact solver.

With n vertices, the explicit edge/c/d/empty-triangle scans can reach roughly quintic work before early exits; this is an inference from the nested loops, not a measured scaling law. Visibility/length caching helps constants, but does not remove that structure. The cleaned backend caps patches at 64 vertices to bound latency, which makes it unsuitable as a general backend for larger patches.

The separate original 2D minimum-weight polygon routine uses dynamic programming over boundary chains. It addresses a different input class from a 3D projected patch containing interior vertices. Its presence does not confer an exact MWT guarantee on the 3D LMT method.

### Greedy is the least redundant research method

Greedy tries all visible vertex pairs, sorts by length and inserts edges that do not cross previously chosen edges. Its shortest-first objective differs from CDT's projected Delaunay connectivity and GP's neighborhood/angle scoring. It does not need the LMT witness phase or GP's many parameters. However, local choices need not minimize the final graph's total length; the exhaustive tests and graph-to-triangle conversion cost more than a triangulation followed by guarded flips.

If one of these three research backends stays, choose Greedy. Improve it only if it demonstrates a useful valid result unavailable from CDT plus a guarded flip pass. Avoid building three different incomplete graph producers with the same downstream machinery.

## Original snapshot evidence

The original native baseline is retained in `gpu/baseline/gpu_many.csv` (64 instances). CPU construction values below are single observations and include the original setup scope. They are useful for identifying large costs, not precise CPU speed ratios across builds.

| Regular planar sheet, original method | CPU construction, ms | Unique 3D edge length |
|---|---:|---:|
| CDT | 15.89 | 253.6794 |
| CDTFlip | 16.45 | 253.6794 |
| Greedy | 124.02 | 253.6794 |
| Custom GP | 61.08 | 253.6794 |
| LMT | 1478.00 | 253.6794 |

On the original dense sphere, Greedy produced 363 duplicate triangles, LMT 24, and GP 15; their apparent shortening cannot rank as a valid win. On jittered_planar, original LMT cost about 2.84 seconds and failed the implemented audit. Curved jittered/folded outputs also contained winding/coverage problems. These defects affected several original methods, including some CDT variants; CDT was not universally correct merely because it used Triangle. Preparation and assembly had to be fixed too.

## Fresh comparison of the cleaned implementation

[methods.csv](gpu/fixes/methods.csv), [paired comparisons](gpu/fixes/methods.paired.csv), [metadata](gpu/fixes/methods.metadata.txt), and [audit](gpu/fixes/methods.audit.json) record six shapes: sphere, asymmetric ellipsoid, blob, jittered planar sheet, folded sheet and skew-connected torus. Each uses 16 instances, meshoptimizer, depth, 1280x720, four views, three randomized blocks, 1x/4x MSAA, Texture1/Material16, 30 warmup and 30 timed draws per case. Counts are independent source meshes, not the number of repeated GPU observations.

| Method | Audited eligible shapes / 6 | Eligible shapes |
|---|---:|---|
| Source | 6 | All |
| CDT | 5 | All except folded |
| CDTFlip | 4 | Sphere, ellipsoid, jittered_planar, torus_skew |
| Greedy | 2 | Sphere, torus_skew |
| LMT | 2 | Sphere, torus_skew |
| Custom GP | 2 | Sphere, torus_skew |
| Heap3D / HeapPlane | 6 | All |

LMT and CDTFlip complete the blob but exceed the finite reference-error budget; they are excluded despite zero failed patches. Other excluded projected outputs retain Source after a backend/patch failure. The current complete rollback is conservative: one failed patch excludes the method for the whole mesh. A future mixed per-patch fallback can preserve valid improvements, but must report its fallback fraction and keep the coverage/quality audit.

Selected fresh 4x Material16 comparisons follow. Negative values mean reduction from the matching Source; construction costs are single observations. Small timing differences should be treated cautiously on an unlocked laptop GPU.

| Shape | Method | Edge change | Helper proxy change | Draw change | Construction CPU, ms |
|---|---|---:|---:|---:|---:|
| Ellipsoid | CDT | -3.74% | -0.96% | +0.42% | 26.4 |
| Ellipsoid | CDTFlip | -10.14% | -14.28% | -11.14% | 27.7 |
| Torus skew | CDT | -16.29% | -4.14% | -9.46% | 137.6 |
| Torus skew | Greedy | -16.29% | -6.48% | -9.83% | 161.8 |
| Torus skew | LMT | -16.29% | -5.16% | -9.20% | 214.9 |
| Torus skew | Custom GP | -15.25% | -4.83% | -8.50% | 154.6 |
| Torus skew | Heap3D | -50.71% | -26.12% | -33.51% | 310.9 |

Greedy has a modest torus raster improvement over CDT despite the same aggregate length; connectivity matters. It is still dominated by the global heap's shortening and draw benefit on that fixture. LMT and GP show no strong advantage that offsets their lower reliability and added complexity. On the sphere, GP is about 1.23% faster than Source with essentially unchanged length; this small fixture-specific result does not justify making it a general backend. CDTFlip's ellipsoid result does justify retaining the flip option.

The eight-view, 60-sample repeat in [torus_final.paired.csv](gpu/fixes/torus_final.paired.csv) supplies the stronger torus claim: Heap3D shortens edges by 50.714%, reduces helper proxies by 25.659%, and reduces 4x Material16 draw time by about 33.1%. The plane score does not consistently improve GPU time over length scoring. Stricter seed/projection rules reduce current CDT torus shortening to 16.3%, versus 32.3% in the historical snapshot; this is a real behavior change, not a performance-preserving claim.

## Better implementation direction

1. Keep validated Source and CDT as the two starting connectivities; compare both as seeds for a common guarded optimizer. Existing quad splitting may discard useful geometry-level structure, so preserve source triangulation IDs and all boundary/feature constraints.
2. Make local/global flips share actual 3D length gain, topology/crease/seam constraints and fixed-reference error checks. Default projected local scoring is preserved for comparison, with an explicit 3D option. Benchmark that option separately before changing defaults.
3. Prefer the global heap when long connectivity spans patch boundaries. Keep surface deviation as a hard admissibility constraint. A quad plane residual measures how nonplanar the quad is; it does not directly compare old versus new surface approximation, so tune it as an optional preference rather than a replacement for the reference guard.
4. For helper-lane optimization, add a camera-aware projected perimeter or estimated touched-quad objective over representative views and pixel phases. Preserve a monotone objective or cycle prevention if allowing length-increasing raster improvements. Total 3D length is a useful experiment variable, not a guarantee of reduced helpers or frame time.
5. Add a precisely reported mixed-patch fallback only after verifying source triangle ownership and identical shared boundaries. This can recover useful CDT improvements on folded/jittered inputs without reviving partial-mesh bugs.
6. Continue applying cache/fetch/overdraw optimization consistently. Consider error-bounded LOD if triangle setup or microtriangle shading dominates; connectivity-only flips preserve triangle count and cannot remove that cost.

The heap is still an offline prototype. Current timing measures isolated indexed draws and clear-plus-draw, not total application FPS. Helper/active counters are separately instrumented proxies, not hardware profiler counters. All reference checks are finite tests rather than certified Hausdorff/self-intersection guarantees.
