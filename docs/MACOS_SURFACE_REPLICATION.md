# Reproducing the surface-connectivity measurements on macOS

This is the workload specification for the corrected, protocol-4 Windows measurements behind [the study report](TRIANGULATION_PERFORMANCE.md) and the drafted 3D results section. It covers **90 completed runs, 109,536 case rows, and 7,790,592 measured draw/pass sample pairs**. These are the 27-input broad study and the five additional candidates, including controls and the one-object experiment. The planar ellipse and planar grids remain in the matrix as reference cases.

The aim is to repeat the **same geometry, connectivity methods, projected workload, shader, sampling protocol, and analysis** on an Apple GPU. Equal GPU times, helper counts, rasterization decisions, or pixels across architectures are not expected.

**Implementation status:** this repository's performance entry point explicitly selects D3D12. It is not a working macOS/Metal executable. The specifications below can be implemented in the Mac renderer; the documented Windows arguments do not establish that a Mac executable already accepts them. In particular, a Metal timing implementation must establish which GPU regions it can actually measure. No Mac measurements were made while preparing this guide.

## 1. Files to take to the Mac

Take the repository, including vendored dependencies and `Data/Models`, and these companions:

| File | Purpose |
|---|---|
| [surface_replication_manifest.json](reproduction/surface_replication_manifest.json) | Every run's actual settings, ordered method list, geometry identifier, original metadata, expected source counts, source revision/digest, and complete argument vector. Also contains asset SHA-256 values. |
| [surface_case_order.csv.gz](reproduction/surface_case_order.csv.gz) | The actual execution order of all 109,536 recorded cases, without GPU times. Columns: `RunIndex,Case,Block,View,MSAA,Method`. |
| [surface_measured_sources.zip](reproduction/surface_measured_sources.zip) | The measured broad-screen and broad-confirmation source snapshots, plus the sample sources at the five-candidate measured commit. No executables or raw GPU samples. |
| [confirmed_measurements.csv](confirmed_measurements.csv) | Windows reference means, changes, and accepted-condition counts. |
| [export_surface_replication.py](reproduction/export_surface_replication.py) | Rebuilds the companions from the retained local completed Windows summaries, metadata, and measured source snapshots. It does not run or alter measurements. |

The ZIP has three directories: `broad_screen/`, `broad_confirmation/`, and `five_candidates_6d3a922/`. These are references for porting, not three already configured Mac projects. Third-party code and the model files remain in the repository and are not duplicated in the ZIP.

The manifest deliberately records both the **historical geometry identifier** and a **portable identifier**. Old GraphEx paths such as `obj:analysis/gpu/orientation/models/GraphExHeart.obj` map to the byte-identical deployed `obj:Data/Models/GraphExHeart.obj`. Do not substitute newly generated GraphEx meshes or different Suzanne/Bunny/Teapot downloads.

### Exact input identity versus rebuilding the algorithms

The companions contain original asset hashes and source code, but **not frozen uploaded position, normal, or per-method index buffers**. Rebuilding the C++ preprocessing on a different compiler can change floating-point predicates, tied choices, or procedural trigonometric results. Equal counts alone do not prove equal connectivity.

For byte-identical GPU input comparisons, export the final Windows position buffer, fixed source-normal buffer, and each post-meshoptimizer index buffer from the corresponding archived implementation, together with model/view/projection constants. Upload those exact bytes on the Mac. Such a buffer exporter is not implemented by this documentation change. If regenerating instead, compare the source and candidate audits, edge lengths, errors, and projected metrics; record any mismatch and describe the experiment as an algorithm replication rather than identical-buffer replay.

## 2. Measured implementation identities

The checkout used to prepare this guide is `802bffa051a6da10ae2edb5ac96458787966173a`. It is not retroactively the identity of the measured executable.

| Study/stage | Recorded source revision | Recorded compiled-source digest |
|---|---|---|
| Broad screen | `4aa97acafbed21a759e1fca01d942587257e32c9`, with the archived working-tree sources | `720a982519a4126788fd23c7dce5320029b3ea8607be330fda22f850df35719e` |
| Broad confirm, special, selected | Same recorded base revision, with the archived confirmation sources | `25f2fe2244aa731141e8ba78a8b89cbb784688fdc7564f010b300e46d60f0b2f` |
| Five candidates, every stage | `6d3a9225bee88fed37c08daf685c973212a30a3e` | `9824602c15e528e58625177f7633ad659526ce32d5c6ec4271d99df538977ddd` |

The broad measured implementation was subsequently committed in `012f2f9`/`deff9f1`. The five-candidate revision includes the improved rejection of roundoff-degenerate surface triangles. Use the corresponding archive when reproducing an old result; do not silently replace all runs with the latest optimizer.

The broad confirmation shares canonical source triangulation and patch growth across methods. The broad screen originally included those operations in individual preparation paths. This matters for **CPU-cost comparisons**, even where GPU input connectivity agrees. The archive preserves that difference.

Windows reference environment: RTX 4050 Laptop GPU, NVIDIA driver 610.62, D3D12, Falcor 8, Release, MSVC 19.44.35229.0. The lit program explicitly requests shader model **6.6** in `Rendering/LitPreview.cpp`; do not confuse supported device shader models with the program's requested target. Clocks were unlocked. The D3D12 debug layer, UI, and Falcor profiler were disabled. The performance app was headless and set its shader-cache path to an empty string.

For local provenance, the parent Falcor checkout at documentation time is `eb540f6748774680ce0039aaf3ac9279266ec521`, with local changes including the timer correction. This parent revision was not independently stamped into each old run and is not a substitute for the archived timer/source bytes. The vendored meshoptimizer header identifies version **1.0**. Use the vendored Triangle, Earcut, Eigen, libigl, and meshoptimizer files from the same sample checkout, not newer package-manager versions. The current Windows Release cache specifies `/O2 /Ob2 /DNDEBUG`; the sample also enables AVX2 on MSVC x64. Record the new Mac compiler/optimization/FP settings. Its ARM build must not inherit the sample CMake's x86 `-mavx2 -mfma` options for non-Windows Clang.

All listed runs use the corrected timestamp implementation. [falcor-gpu-timer.patch](falcor-gpu-timer.patch) documents the Windows resolve-buffer state correction and integer timestamp subtraction. Older pre-fix timing studies are not replication targets for the chapter.

## 3. Common rendering and measurement settings

These apply to every listed run unless its stage explicitly changes the instance count or sample schedule.

| Setting | Exact value/behavior |
|---|---|
| Actual render target | **1920 x 1080 pixels**, aspect 16:9; independent of window size or Retina backing scale |
| Render mode | Existing GraphEx-derived raster **LitPreview** shader |
| Draw | One indexed instanced triangle-list draw per sample; uint32 indices; base index, base vertex, and base instance all zero |
| Main instance count | **64**, arranged as 8 columns x 8 rows x 1 layer |
| `large` stage only | **1**, arranged as 1 x 1 x 1 |
| 3D stacking | Disabled; every instance translation has z=0 |
| Instance scale | Identity; no random per-instance scale, rotation, or material |
| Model rotation | The same selected rotation for all instances; rotate local vertices before adding instance translation |
| Camera | Fixed perspective camera per geometry/workload; exact fit in section 4 |
| MSAA configurations | `1,4`, in that order within an orientation; 1 means no MSAA |
| Color attachment | `RGBA8Unorm`, not sRGB and not a floating-point HDR target |
| Depth attachment | `D32Float`, same sample count as color |
| Color clear | `(0.15, 0.15, 0.15, 1)` |
| Depth clear / test / write | Clear 1; strict `Less`; writes enabled; conventional depth, not reversed-Z |
| Culling / clipping | No face culling; ordinary clipping, no depth clamp or custom clip planes |
| Blending | Default opaque replacement; no alpha blending |
| Viewport / scissor | Full attachment; no cropped measurement viewport |
| Other raster state | No wireframe, conservative rasterization, polygon offset, alpha-to-coverage, or explicit sample-frequency shading requested |
| MSAA resolve | At 4x, resolve color into a separate 1920 x 1080 `RGBA8Unorm` texture after each draw; no resolve at 1x |
| Shading normals | Area-weighted normals of the canonical **source triangulation**, reused for every method within a run |
| Vertex data | Position and normal in separate float3 streams; keep original position storage and vertex order |
| Meshoptimizer | `meshopt_optimizeVertexCache`, then `meshopt_optimizeOverdraw(..., 1.05f)` on each candidate, including Source and SourceControl |
| Vertex-fetch optimization | **Disabled**; do not reorder or compact vertices |
| Geometry metrics | Enabled; CPU projection/audits prepared before GPU priming |
| Diagnostics | Separate helper-proxy draw and separate uninstrumented hardware-statistics draw, after timed samples |
| Queue | Eight outstanding draw/pass query slots, fence-retired in issue order |
| Random generator | `std::mt19937(20261010)` per process; actual case order is supplied to avoid library-dependent shuffle differences |
| Compilation/initialization | Prepare all candidates and all timed/diagnostic 1x/4x pipelines and normal buffers before measurement |
| Initial priming | Repeated uninstrumented 1x draws over prepared candidates for at least **1 second of CPU wall time**, then submit and wait |
| Per-case warmup | 64 draws in screening; 256 draws in longer stages; clear/draw/resolve each warmup |
| Measured samples | 32 per case in screening; 128 per case in longer stages |
| Blocks | 2 in screening; 3 in longer stages |
| Sample filtering | None; retain outliers and raw draw/pass pairs |
| Timing statistics | Arithmetic mean, median, population standard deviation, minimum, maximum |
| Presentation | Outside measurement; no display pacing, swapchain present, or UI draw in the repeated workload |

The lit shader does not sample the benchmark's synthetic texture. Do not reproduce the old texture/stress modes instead of LitPreview. Likewise, the old planar paper's 10x10/100x100 grids, 2800x1800 Apple target, and texture shader are **not** settings for this surface replication.

The original runner did not set explicit programmable MSAA positions. Record the Metal sample pattern and any difference from the D3D12 default; do not claim cross-API sample-position identity without checking it. The Windows metadata did not fully record power limits, fan mode, or per-draw GPU frequency. Those cannot be reconstructed exactly from this guide.

## 4. Geometry scale, grid placement, and exact camera

Let `p[i]` be the **loaded/generated source positions**, after OBJ normalization if applicable. Compute the source bounds and radius once:

```text
lo = componentwise_min(p)
hi = componentwise_max(p)
B  = length(hi - lo)
r  = max_i length(p[i])
spacing = 2.5 * r
cols = ceil(sqrt(instances))
rows = ceil(instances / cols)
```

Do not fit the camera to a candidate, a particular orientation, visible triangles, or a per-view bounding box. Do not renormalize procedural fixtures: their different native extents affect light directions and the camera fit.

For instance `j`, with zero-based indexing:

```text
col = j % cols
row = j / cols                 # integer division
offset = ((col - 0.5*(cols-1))*spacing,
          (row - 0.5*(rows-1))*spacing,
          0)
worldPosition = (M * float4(p,1)).xyz + offset
```

At 64 instances the centers span `[-3.5*spacing, +3.5*spacing]` on both X and Y. Instance zero is the negative-X/negative-Y corner. All instances share one mesh and one material.

Camera fit, using float arithmetic as in `SampleSupport.cpp`:

```text
aspect = 1920.0 / 1080.0
fovY   = radians(45.0)
halfX  = 0.5*(cols-1)*spacing + r
halfY  = 0.5*(rows-1)*spacing + r
halfZ  = r
fitY   = halfY / max(tan(fovY/2), 0.001)
fitX   = halfX / max(tan(fovY/2)*aspect, 0.001)
d      = max(2.0, max(fitX,fitY) + halfZ + 0.5)
near   = 0.01
far    = max(10.0, d + halfZ + 2.0)
eye    = (0,0,d)
target = (0,0,0)
up     = (0,1,0)
```

Use a right-handed look-at matrix and a right-handed perspective projection with depth in **[0,1]**. With column vectors, the mathematical row representation of the projection is:

```text
f = 1/tan(fovY/2)
P = [ f/aspect  0      0                   0               ]
    [ 0         f      0                   0               ]
    [ 0         0      far/(near-far)      -far*near/(far-near) ]
    [ 0         0     -1                   0               ]
clip = P * V * float4(worldPosition,1)
```

The camera position supplied to lighting is this same `eye`. Matrix storage layout may differ in the port; preserve the multiplication semantics, not a mistaken byte transpose. Match screen orientation when porting D3D to Metal. The CPU metric convention is:

```text
ndc = clip.xy / clip.w
pixel = (ndc*0.5 + 0.5) * (1920,1080)
```

CPU metrics require `clip.w > 1e-6`. They include occluded triangles and do not clip triangles to the viewport. All original validated cases had zero projected triangle bounding boxes outside the viewport. A vertical coordinate reflection preserves length/area magnitudes but can change the correspondence to raster sample positions; verify the rendered orientation too.

## 5. All 24 orientation conditions

These are separate axis rotations, **not** 8x8x8 Euler combinations and not the interactive GUI's older orientation preset. `M = Rx * Ry * Rz`; angles are degrees. Standard right-handed positive rotations are used, with column vectors. For example, `Rx(a)` maps `(x,y,z)` to `(x, cos(a)*y-sin(a)*z, sin(a)*y+cos(a)*z)`.

| View | X | Y | Z | View | X | Y | Z | View | X | Y | Z |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 0 | 0 | 8 | 0 | 0 | 0 | 16 | 0 | 0 | 0 |
| 1 | 45 | 0 | 0 | 9 | 0 | 45 | 0 | 17 | 0 | 0 | 45 |
| 2 | 90 | 0 | 0 | 10 | 0 | 90 | 0 | 18 | 0 | 0 | 90 |
| 3 | 135 | 0 | 0 | 11 | 0 | 135 | 0 | 19 | 0 | 0 | 135 |
| 4 | 180 | 0 | 0 | 12 | 0 | 180 | 0 | 20 | 0 | 0 | 180 |
| 5 | 225 | 0 | 0 | 13 | 0 | 225 | 0 | 21 | 0 | 0 | 225 |
| 6 | 270 | 0 | 0 | 14 | 0 | 270 | 0 | 22 | 0 | 0 | 270 |
| 7 | 315 | 0 | 0 | 15 | 0 | 315 | 0 | 23 | 0 | 0 | 315 |

Retain all 24 condition IDs in the final broad and five-candidate analyses. There are 22 unique static poses, but the three identity conditions have distinct execution histories. The lag methods also train against distinct preceding poses. Do not deduplicate them when reproducing these reported means.

For `HeapQuadLag` and `HeapQuadWarmLag`, optimize for the previous angle **within the same axis**:

```text
optimizationView = (view/8)*8 + ((view+7) % 8)
```

Thus view 0 trains on view 7, view 8 on view 15, and view 16 on view 23; it does not train on the previously executed randomized case.

## 6. Lit shader and fixed attributes

Use the equations in the archived `Shaders/LitPreview.slang`, also present in the current tree. This is GraphEx raster lighting adapted to indexed instancing, not a PBR substitute.

| Parameter | Value |
|---|---|
| Background RGB | `(0.15, 0.15, 0.15)` |
| Material ambient | `(0.5, 0.5, 0.5)` |
| Material diffuse | `(0.55, 0.65, 0.8)` |
| Material specular | `(0.4, 0.4, 0.4)` |
| Shininess exponent | `32` |
| Light type | `0`: point light |
| Light position | `(0,1,1)`, fixed in world space |
| Stored directional-light vector | `(0,1,1)`, unused for the point-light configuration |
| Light ambient | `(0.125,0.125,0.125)` |
| Light diffuse/specular | Both `(1,1,1)` |
| Attenuation coefficients | `(1,0,0)`; constant attenuation |
| Final shader multiplier | All four components multiplied by `0.75`, including alpha |
| Interpolation | Ordinary perspective-correct interpolation of world position and normal |
| Normal transform | `transpose(inverse(M))`; normalize interpolated normal in the pixel shader |
| Early depth | Requested by `[earlydepthstencil]`; preserve the opaque early-depth opportunity in Metal |

For each source triangle `(a,b,c)`, accumulate `cross(p[b]-p[a],p[c]-p[a])` into all three incident vertices in canonical source-index order. Normalize each sum; for length <= `1e-20`, use `(0,1,0)`. Compute these normals **before candidate connectivity or meshoptimizer index reordering**, and reuse them for all methods. Do not load OBJ `vn` values or recalculate normals per candidate. Control models use their own control source to define their fixed normals.

The pixel shader computes normalized light direction `L`, normalized normal `N`, normalized camera direction `V`, reflection `R=reflect(-L,N)`, then:

```text
attenuation = 1 / (kc + kl*lightDistance + kq*lightDistance^2)
ambient  = lightAmbient * materialAmbient
diffuse  = max(dot(L,N),0) * attenuation * lightDiffuse * materialDiffuse
specular = pow(max(dot(V,R),0),shininess) * attenuation
           * lightSpecular * materialSpecular
output   = float4(ambient+diffuse+specular,1) * 0.75
```

There is no extra `dot(L,N)>0` gate on specular, normal mapping, texture sampling, gamma correction, tone mapping, shadows, reflections, environment map, or ray tracing. Do not add them for this comparison. The RGBA8Unorm target performs its normal format conversion/clamping.

## 7. Geometry construction and import

The CLI fixture definitions are authoritative: [BenchmarkGeometry.cpp](../Experiments/BenchmarkGeometry.cpp) and [ProceduralGeometry3D.cpp](../Triangulation/3D/ProceduralGeometry3D.cpp), or their matching measured snapshots. The adjustable GUI generators are not the measurement settings.

All generator centers below are `(0,0,0)`. Parameters are in function argument order; sphere/ellipsoid arguments are **latitude, longitude**, while open-patch arguments are **width, height** segments.

| CLI identifier | Exact construction |
|---|---|
| `sphere` | `makeSphere(31,17,0.5,center)` |
| `sphere_dense` | `makeSphere(63,33,0.5,center)` |
| `jittered` | `makeJitteredSphere(63,33,0.5,0.22,0.18,center)` |
| `ellipsoid` | `makeEllipsoid(63,33,(0.825,0.275,0.475),center)` |
| `ellipsoid_small` | Same radii, segments `(31,17)` |
| `blob` | `makeLumpyEllipsoid(63,33,(0.725,0.325,0.525),0.35,center)` |
| `blob_small` | Same radii/amplitude, segments `(31,17)` |
| `saddle_branch` | `makeSaddlePatch(33,63,1.5,0.425,1.0,center)` |
| `clustered` | `makeClusteredWavePatch(33,63,1.5,0.4,0.8,center)` |
| `planar` | `makeSaddlePatch(33,63,1.5,0,0,center)` |
| `jittered_planar` | `makeSaddlePatch(33,63,1.5,0,0.05,center)` |
| `folded` | `makeFoldedSheet(33,63,1.6,0.45,0.06,1.0,center)` |
| `skew1`, `skew2`, `skew3` | Start with `makeSaddlePatch(63,32,1.5,0,0,center)`; retain original `(u,v)=(x,y)` and set `x=u+s*v`, `y=v`, `z=0`, for `s=1,2,3` respectively |
| `skew_fold` | Same base, `x=u+3*v`, `y=v`, `z=0.7*abs(v)` |
| `skew_saddle` | Same base, `x=u+3*v`, `y=v`, `z=0.18*(u*u-v*v)` |
| `torus`, `torus_skew` | 128 columns x 64 rows, periodic in both directions; major radius `0.45`, tube radius `0.18`; skew shifts the angular sample index by `2*y` columns per row |
| `cylinder`, `cylinder_skew` | 192 columns x 96 cells along length, 97 vertex rows; open ends; exact deformed-cylinder equations below; skew shifts angular index by `2*y` |
| `ellipse2d` | One 512-vertex polygon, vertices `(0.4*cos(a),0.2*sin(a),0)`, `a=6.28318530718f*i/512`; canonical source triangulates that face |
| `suzanne`, `bunny`, `teapot` | Exact deployed `Data/Models/Suzanne.obj`, `Bunny.obj`, `Teapot.obj` |
| GraphEx inputs | Exact `obj:Data/Models/GraphExHeart.obj` and `obj:Data/Models/GraphExTangleCube.obj` |
| Five additional sources | Exact `obj:Data/Models/ResearchCandidates/{SweptWing,ToothedWheel,FacetedArch,TrefoilKnot,SpiralRamp}.obj` |
| Five controls | Same directory and names with `_control.obj` suffix |

For torus/cylinder row `y`, column `x`, use integer wrapping before converting to float:

```text
theta = 2*pi*((x + (skew ? 2*y : 0)) % columns)/columns
t = float(y)/rows
phi = 2*pi*t
torus:
    radial = 0.45 + 0.18*cos(phi)
    p = (radial*cos(theta), radial*sin(theta), 0.18*sin(phi))
cylinder:
    radial = 0.32*(1 + 0.22*sin(3*theta+2*t) + 0.16*sin(5*theta-3*t))
    p = (0.12*sin(phi) + radial*cos(theta),
         0.08*sin(1.5*phi) + radial*sin(theta),
         1.4*(t-0.5))
```

Faces are ordered quads `(y,x), (y,nextX), (nextY,nextX), (nextY,x)`, with torus row wrapping and no cylinder row wrapping. Preserve input vertex and face iteration order. Use the exact generators for the jitter hash, saddle noise, folded-sheet ripples, and clustered coordinate distribution; their amplitudes alone do not reproduce them. The hash uses uint32 arithmetic with wraparound, not a platform RNG.

OBJ import uses positions and face-position indices only. It ignores imported normals, UVs, colors, and materials; does not weld duplicate positions, simplify, smooth, or reorder vertices. Center using the input AABB midpoint, find the maximum distance to that midpoint, then uniformly scale to radius **0.5**. The center/radius calculation uses double intermediates; stored positions are float32. Loading and normalizing a mesh again after exporting normalized vertices can change their bytes.

Canonical source triangulation retains supplied triangles. Polygon faces use the exact source-face projection/fan/ear-clipping rules in `Triangulation3D.cpp`. Do not use a modeling application's arbitrary triangulation of these faces. In particular, the source diagonal of nonplanar quads is part of the reference piecewise-planar surface.

Measured source vertex/triangle counts and radii are listed in Appendix A. The machine manifest additionally records source unique edges, surface area, Euler characteristic, component count, world-edge sum, and reference budget for comparison.

## 8. Methods and optimizer settings

Names are case-sensitive. A method name denotes this project's implementation, not a generic library with default settings.

### Shared projected-patch preparation

| Parameter | Value |
|---|---|
| Maximum normal deviation | `15 degrees` |
| Relative plane deviation | `0.005 * patch bounding-box diagonal` |
| Absolute plane-deviation floor | `1e-5`; effective limit is max(absolute, relative) |
| Source-face limit per patch | `64` |
| Patch validity | Consistently oriented edge adjacency, disk-like topology, simple projected boundary, valid source-point placement |
| Interior vertices | Retained; no Steiner insertion or vertex deletion |
| CDT flags | Triangle `pQz`; constrained boundary, no requested quality refinement |
| Greedy edge metric | 3D Euclidean length |
| Default local-flip metric | Squared 2D chart length; this is not camera-space projection |
| `CDTFlip3D`, `LMT3D` | Opt into linear 3D Euclidean local-flip scoring |
| LMT patch vertex budget | `64` |
| Greedy/GP patch vertex budget | `256` |
| GP nearest-neighbor cap | `100` |
| GP neighborhood multiplier | `mu=2.5` |
| GP search radius | `0`: no fixed global radius cap |
| GP minimum/maximum angles | `10 degrees` soft minimum penalty; `120 degrees` hard maximum |

LMT is the custom local-witness heuristic, not exact MWT or a certified MWT skeleton. GP is custom PCL-inspired projected-graph code, not the PCL library's reconstruction implementation. Port their actual predicates, completion, and failure behavior rather than substituting similarly named algorithms. A failed patch/backend can return the whole source with `SourceFallback`; record that status and exclude it from connectivity-win claims.

### Global surface refinements

| Method | Objective / initialization |
|---|---|
| `Source` | Canonical source triangles, then common index optimization |
| `SourceControl` | Identical source mesh in an independent GPU allocation; randomized as another method |
| `Heap3D` | Max-heap of positive 3D diagonal-length reduction; starts from Source |
| `HeapPlane` | Same heap with gain = length reduction minus `4 * quad plane residual` |
| `HeapScreen` | `0.25*worldLength + 0.75*normalizedProjectedL2`, averaged over all 24 view conditions at one central representative instance |
| `HeapQuad` | Same mixture with projected L1 length |
| `HeapQuadSet` | Pure normalized projected L1, averaged over all 24 views and all actual instances |
| `HeapQuadView` | Pure normalized projected L1 for the current pose, averaged over all actual instances; starts from Source |
| `HeapQuadLag` | Same, trained on previous angle within its axis |
| `HeapQuadWarm`, `HeapQuadWarmLag` | Current/previous-pose projected L1 initialized from prepared Heap3D; fixed original reference retained |
| `HeapLoose`, `HeapWide` | Heap3D with error budget `0.005*B` or `0.02*B`, respectively |
| `Star3D`, `StarLoose`, `StarWide` | World heap, up to three convex interior vertex-star DP/reinsertion sweeps, then world heap; matching strict/loose/wide budget |
| `GridAligned` | Known torus/cylinder parameter-grid repair retaining the same position array |
| `GridOpposite` | Swap each source quad's selected diagonal |
| `TopologySplit` | Torus-only changed-surface control; reconnect two halves into two tori at fixed counts |

For projected objectives, normalize mean projected source-edge cost to world units with `scale = sum_source_world_edges / sum_source_projected_costs`. Reuse the actual render camera and offsets. Static Screen/Quad use one offset `(0,0,0)`, while Set/View/Lag/Warm variants use all instances. The two fixed-view averages include all 24 condition IDs, including repeated identity. Prepare view-dependent meshes before GPU measurement; CPU optimization is **not** included in `Draw` or `Pass`.

All global heaps keep the original reference surface fixed. Defaults are relative reference error `0.001` and minimum normal dot `0.8660254` (approximately a 30-degree guard). A flip requires two oriented adjacent faces, distinct opposite vertices, no existing replacement diagonal, local convexity, valid new triangle areas, and compatible normals. The pop-time reference check tests the new diagonal midpoint and new triangle centroids against the immutable source. It is one-way locally; the final audit is bidirectional.

The plane residual is the maximum absolute distance of the four vertices from a plane through their arithmetic mean, normal to the normalized sum of the old oriented triangle area vectors. It is not a fitted least-squares plane. Accept only `gain > 1e-8 * max(oldWorldLength,newWorldLength)`. Heap ties use ascending undirected edge key. Each optimize call allows at most `indexCount` flips and `10*indexCount` popped entries. Preserve stale-entry version checks and local updates.

Star search uses interior valence 4 through 12, preserves the boundary and the original center vertex, and performs at most three deterministic sweeps. The five-candidate implementation's stable-area predicate requires squared cross-product magnitude greater than both `4e-24` and `1e-12 * |ab|^2 * |ac|^2`. The broad archive predates this improved rejection; use its actual predicates when reproducing that study.

Warm-start CPU construction times exclude cached Heap3D seed preparation. Include that seed cost when reporting total preprocessing, rather than treating the historical incremental field as total cost.

### Final geometric eligibility

Let `B` be the fixed source AABB diagonal. Final CLI auditing samples used vertices, unique-edge midpoints, and triangle centroids in each direction between candidate and source. Strict equivalence requires the maximum sampled error <= `0.001*B*1.001`; the last factor is the original numerical comparison margin. The reference is the input polygonal surface, not the analytic shape.

Patch methods and grid variants have a separate permissive validity ceiling of `0.02*B`; loose/wide heaps use their declared budgets. **The strict-equivalence flag still tests 0.1%.** Being within a relaxed method budget is not sufficient for a strict-results table. Structural checks cover duplicate/degenerate triangles, nonmanifold edges, winding, preserved boundary, used vertices, and fixed final triangle count. Retain component and Euler statistics. Finite sampling does not prove continuous Hausdorff distance, absence of self-intersections, or arbitrary attribute preservation.

## 9. Full run matrix

One case is one `(geometry, method, block, view, MSAA, shader)` combination. One raw sample is a paired draw/pass measurement inside a case; the totals below count these pairs, not twice as many separate timing values. Per-case warmups and initial priming are additional work, not raw measured samples.

| Study/stage | Runs | Methods/run | Instances | Blocks | Warmup/case | Samples/case | Cases | Raw pairs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Broad screen | 27 | 23 | 64 | 2 | 64 | 32 | 59,616 | 1,907,712 |
| Broad confirm | 27 | 5 | 64 | 3 | 256 | 128 | 19,440 | 2,488,320 |
| Broad special | 4 | 5; torus has 6 | 64 | 3 | 256 | 128 | 3,024 | 387,072 |
| Broad selected | 12 | 6 or 7 | 64 | 3 | 256 | 128 | 10,656 | 1,363,968 |
| Five screen | 5 | 11 | 64 | 2 | 64 | 32 | 5,280 | 168,960 |
| Five confirm | 5 | 7 | 64 | 3 | 256 | 128 | 5,040 | 645,120 |
| Five control | 5 | 4 | 64 | 3 | 256 | 128 | 2,880 | 368,640 |
| Five large | 5 | 5 | **1** | 3 | 256 | 128 | 3,600 | 460,800 |
| **Total** | **90** | | | | | | **109,536** | **7,790,592** |

Every run has 24 view conditions and both MSAA settings. A long run with five methods therefore has `3*24*2*5 = 720` cases and `720*128 = 92,160` raw pairs.

### Broad study

The 27 model identifiers, in the original driver order, are:

```text
sphere,sphere_dense,jittered,ellipsoid,ellipsoid_small,blob,blob_small,
saddle_branch,clustered,planar,jittered_planar,folded,skew1,skew2,skew3,
skew_fold,skew_saddle,torus,torus_skew,cylinder,cylinder_skew,
suzanne,bunny,teapot,ellipse2d,
obj:Data/Models/GraphExHeart.obj,obj:Data/Models/GraphExTangleCube.obj
```

Each model is a **separate process/run**. This resets the RNG and repeats preparation/priming. Do not combine all 27 into one invocation when reproducing the order and thermal protocol.

Ordered broad screening methods:

```text
Source,SourceControl,CDT,CDTFlip,CDTFlip3D,Greedy,LMT,LMT3D,GP,
Heap3D,HeapPlane,HeapScreen,HeapQuad,HeapQuadSet,HeapQuadView,HeapQuadLag,
HeapQuadWarm,HeapQuadWarmLag,HeapLoose,HeapWide,Star3D,StarLoose,StarWide
```

Ordered broad confirmation methods:

```text
Source,SourceControl,CDT,Heap3D,Star3D
```

Special runs use `torus,torus_skew,cylinder,cylinder_skew`, each separately, with:

```text
Source,SourceControl,Heap3D,GridAligned,GridOpposite
```

Append `TopologySplit` only for `torus`. It intentionally changes the surface and components; its timings do not support a surface-preserving optimization claim.

The selected-method confirmation plan starts each row with `Source,SourceControl,CDT,Heap3D,Star3D`, followed by:

| Geometry | Appended method(s) |
|---|---|
| `cylinder_skew` | `HeapQuadWarm` |
| `ellipse2d` | `HeapLoose,HeapQuadWarm` |
| `ellipsoid` | `HeapQuadView` |
| `jittered_planar` | `HeapQuadView` |
| `obj:Data/Models/GraphExTangleCube.obj` | `HeapPlane` |
| `skew1` | `CDTFlip3D` |
| `skew2` | `CDTFlip` |
| `skew3` | `StarLoose` |
| `skew_fold` | `HeapQuadView` |
| `skew_saddle` | `StarLoose` |
| `torus` | `HeapQuadWarm` |
| `torus_skew` | `HeapQuadView,HeapWide` |

### Five additional candidates

For each stage, process `SweptWing,ToothedWheel,FacetedArch,TrefoilKnot,SpiralRamp` separately in that order. Use `obj:Data/Models/ResearchCandidates/<name>.obj`; only the `control` stage uses `<name>_control.obj`.

| Stage | Ordered methods |
|---|---|
| screen | `Source,SourceControl,CDT,CDTFlip,Greedy,LMT,GP,Heap3D,HeapPlane,HeapQuadView,Star3D` |
| confirm | `Source,SourceControl,CDT,CDTFlip,Heap3D,HeapPlane,HeapQuadView` |
| control | `Source,SourceControl,Heap3D,HeapPlane` |
| large | `Source,SourceControl,CDT,Heap3D,HeapPlane` |

The static headline comparison uses **HeapPlane for SweptWing and Heap3D for the other four**. Do not substitute the lowest observed method independently for each cell. The plain wing heap marginally fails the strict reverse-distance audit; the plane-penalized candidate passes.

### Mapping the drafted results to original runs

All names below have suffix `_lit_1080_q8`; the same CSV contains both MSAA settings. Counts are the original Windows accepted conditions, provided for provenance, not acceptance targets to force on the Mac.

| Results row | Study / run prefix | Method | Windows accepted 1x / 4x |
|---|---|---|---:|
| Swept Wing | `five_candidates/confirm_SweptWing` | `HeapPlane` | 23 / 23 |
| Toothed Wheel | `five_candidates/confirm_ToothedWheel` | `Heap3D` | 24 / 24 |
| Faceted Arch | `five_candidates/confirm_FacetedArch` | `Heap3D` | 24 / 24 |
| Trefoil Knot | `five_candidates/confirm_TrefoilKnot` | `Heap3D` | 23 / 23 |
| Spiral Ramp | `five_candidates/confirm_SpiralRamp` | `Heap3D` | 23 / 20 |
| Skew Torus | `all_models/special_torus_skew` | `Heap3D` | 21 / 18 |
| Skew Cylinder | `all_models/confirm_cylinder_skew` | `Heap3D` | 24 / 24 |
| Skew Grid 2 | `all_models/confirm_skew2` | `Heap3D` | 22 / 24 |
| Skew Grid 3 | `all_models/confirm_skew3` | `Heap3D` | 23 / 23 |

The skew-torus headline is from **special**, not the broad `confirm_torus_skew` run, whose 4x control coverage was insufficient. The matched CDT-versus-CDTFlip comparison uses `selected_skew2`, not differences between independent runs. Control-table rows use `control_<Name>` with HeapPlane for the wing and Heap3D elsewhere; the 4x accepted counts are respectively 24,22,20,22,16 in the five-model order above. The view-dependent ellipsoid result uses `selected_ellipsoid/HeapQuadView`. Keep run identity in joins; never pool cases merely because model/method names match.

The one-object stage is a distinct workload, not an isolated change of projected scale: it also changes total primitives and the distribution of light directions. The one-object wing has insufficient stable conditions for a model-level conclusion. Record all cases, then apply the same acceptance rules; do not drop a model before collecting its controls.

## 10. Commands and the portable execution plan

From the **sample directory**, the actual Windows drivers are:

```powershell
./analysis/gpu/all_models/run_matrix.ps1 -Stage screen
./analysis/gpu/all_models/run_matrix.ps1 -Stage confirm
./analysis/gpu/all_models/run_matrix.ps1 -Stage special
./analysis/gpu/all_models/run_selected.ps1
./analysis/gpu/five_candidates/run.ps1 -Stage screen
./analysis/gpu/five_candidates/run.ps1 -Stage confirm
./analysis/gpu/five_candidates/run.ps1 -Stage control
./analysis/gpu/five_candidates/run.ps1 -Stage large
```

The drivers skip files already marked complete. Use a fresh output directory when repeating measurements. Do not overwrite the reference run files or mix old and new cases in one run ID.

An expanded long-confirmation invocation, using actual existing CLI flags, is:

```text
HelperLaneWindows.exe --perf --lit --raster-metrics --rotation-axes 8
  --geometries obj:Data/Models/ResearchCandidates/SweptWing.obj
  --methods Source,SourceControl,CDT,CDTFlip,Heap3D,HeapPlane,HeapQuadView
  --samples 128 --warmup 256 --blocks 3 --instances 64
  --width 1920 --height 1080 --msaa 1,4 --queue-depth 8
  --ordering cache-overdraw
  --output analysis/gpu/five_candidates/confirm_SweptWing_lit_1080_q8.csv
```

Join those lines for a real invocation or use shell-appropriate continuations. The portable manifest has an `argv_contract` list for **every run**, so no quoting, method-list reconstruction, or inference from defaults is necessary. The output locations point into a new `analysis/gpu/mac_replication/` tree.

On the Mac, inspect that contract with standard Python before wiring it to the Metal renderer:

```bash
python3 - <<'PY'
import json, shlex
from pathlib import Path
m = json.loads(Path('docs/reproduction/surface_replication_manifest.json').read_text())
for r in m['runs']:
    print(r['index'], r['study'], r['run'])
    print(shlex.join(r['argv_contract']))
PY
```

The flags are a workload contract for the port, **not a claim that a Mac binary exists**. Do not invent a Mac build command for this Windows/Falcor target. A separate Metal project must load the same data and implement the equivalent settings and logging.

For exact case-order replay, load the compressed plan and group by `RunIndex`:

```python
import csv, gzip, json
from collections import defaultdict
from pathlib import Path

base = Path('docs/reproduction')
manifest = json.loads((base/'surface_replication_manifest.json').read_text())
plans = defaultdict(list)
with gzip.open(base/'surface_case_order.csv.gz', 'rt', newline='') as stream:
    for row in csv.DictReader(stream):
        plans[int(row['RunIndex'])].append(row)

for run in manifest['runs']:
    cases = plans[run['index']]
    assert len(cases) == run['cases']
    # Prepare this run's geometry, methods, view variants and pipelines.
    # Prime, then execute cases IN THIS ORDER with this run's warmup/samples.
    # Each row identifies Block, View, MSAA and Method; Case is its output ID.
```

The Mac port must add this scheduling integration; there is no existing `--case-plan` flag. `std::mt19937` has a specified generator sequence, but `std::shuffle`'s consumption/mapping is library-dependent. Using only the original seed with libc++ does not guarantee the measured Windows order. The manifest index is a portable key, not a claim that the 90 separate processes were executed in that global chronological order. Per-run order is the relevant frozen schedule; inter-run pauses and thermal history were not fully recorded.

## 11. GPU timing boundaries and Metal equivalence

The measured Windows sample executes this sequence:

```text
begin Pass timestamp
    clear color and depth; select state/parameters
    begin Draw timestamp
        uninstrumented lit indexed instanced draw
    end Draw timestamp
    resolve MSAA color, if 4x
end Pass timestamp
resolve timestamp queries
submit without draining; signal completion fence
```

The lit draw helper binds its own program/VAO/constants inside the Draw region. Shader compilation, source-normal creation, and PSO initialization have already occurred during preparation/priming. These are GPU command-region measurements; they do not measure CPU API-call duration. Clear at 1x and clear+resolve at 4x are excluded from Draw, included in Pass. Query resolve/readback is outside both.

Warmup ends with a GPU submit-and-wait. During measured samples, query slots are reused only after completion; wait when the eight-slot ring fills or while draining at the case end. Do not wait after every sample. Cases themselves complete and diagnose before the next case; the ring does not overlap arbitrary methods.

On Metal, preserve these distinctions:

1. Use offscreen 1920x1080 attachments and one indexed instanced draw. Keep depth enabled, opaque shading, and the same sample count. Do not replace the workload with UI-driven frame timing.
2. Establish supported GPU counter/timestamp sampling boundaries on the actual Mac. A command buffer's start/end time measures that command buffer, not automatically this Draw region. CPU submission/wait time is not GPU draw time.
3. Apple GPUs may defer attachment work to tile processing and encoder completion. Do not assume an in-encoder timestamp pair has the same stage semantics as D3D12 draw timestamps. Validate and record the scope supported by that device/API.
4. Define a complete render-workload scope that includes clear/load, rendering, depth processing and MSAA resolve/store. Preserve the D3D workload's observable attachment contents. Record Metal load/store actions, resolve placement, attachment storage modes, and timestamp sampling locations.
5. Avoid adding encoder breaks or forced stores merely to manufacture a Draw region: those can change tile behavior and performance. If an equivalent isolated Draw scope cannot be measured, report it as unavailable and use an explicitly named enclosing scope. Do not label whole-command-buffer duration `GPU_Draw_ms`.
6. Do not silently replace the persistent D3D attachment workload with memoryless/discarded attachments. A tile-native memoryless or fused-resolve variant is a useful separate experiment, but identify its changed attachment semantics. Record any unavoidable backend difference.
7. Retain asynchronous completion/readback with eight measurement slots, and retain each raw pair if both scopes are supported. If only Pass is available, use a documented schema/analysis adaptation rather than fabricating a Draw value to satisfy the Windows validator.

The two APIs cannot be made architecturally identical by matching flags. A faithful cross-platform result must state which scopes and attachment behaviors agree, and which differ.

## 12. Helper diagnostics and hardware counters

After every timed case, the Windows runner:

1. Clears the target, draws the **uninstrumented lit shader** under a separate D3D12 pipeline-statistics query, and resolves that query.
2. Clears the target and zeroes a two-element uint32 UAV.
3. Draws the separate `DIAGNOSTICS=1` lit variant and reads back its counters.

The diagnostic variant uses helper-lane and quad operations. A non-helper leader contributes the helper count and active count of its quad once; lanes contribute to counters `[helpers, active]`. Preserve the shader's actual quad grouping and early-depth behavior when translating. Do not assume the D3D wave-lane modulo mapping is valid for Metal SIMD groups. Validate the Metal election/counting independently on small triangles, fully covered quads, MSAA edges, and occluded triangles.

These counts are coverage/helper proxies for the diagnostic shader. The timed lit shader has no diagnostic atomics, helper tests, quad operations, or derivatives. Neither helper diagnostics nor API PS invocation counts directly measure wasted ALU instructions in the timed shader.

Windows native statistics are input primitives, VS invocations, PS invocations, and raster primitives. If a corresponding counter is unavailable or has different semantics on Metal, record availability and its definition. Never enter a missing count as zero or substitute fragment threads for the helper proxy. The exact D3D hardware-counter values are not cross-platform acceptance targets.

NVIDIA vendor counters were unavailable because of `ERR_NVGPUCTRPERM`; no archived claim depends on Nsight stall/occupancy attribution. Apple vendor counters can be collected separately, but do not introduce them into the timed workload or present them as the same counter by name alone.

## 13. Geometry and projected metrics

Compute metrics on the **rendered candidate connectivity after common index ordering**, using the fixed source positions and render camera. Build the undirected edge set from index pairs; equal positions with different indices are not welded.

| Metric / output field | Definition |
|---|---|
| `TotalEdgeLength3D` | Sum Euclidean lengths over unique undirected mesh edges, once per mesh |
| `TotalSceneEdgeLength3D` | `TotalEdgeLength3D * instances` |
| `ProjectedEdgeLength` | Sum Euclidean projected edge lengths in pixels over all instances |
| `ProjectedEdgeL1_px` | Sum `abs(dx)+abs(dy)` over the same projected edges |
| `ProjectedArea_px2` | Sum absolute 2D triangle areas over all instances, before depth/visibility |
| `ProjectedBBoxArea_px2` | Sum each projected triangle's axis-aligned bounding-box area |
| `ProjectedTriangles` | `triangleCount * instances` |
| `OutsideViewportTriangles` | Triangle bbox exceeds `[0,width] x [0,height]`; expected zero here |
| `SubpixelTriangles` | Projected area `< 1 pixel^2` |
| `BelowFourPxTriangles` | Projected area `< 4 pixels^2` |
| `ThinTriangles` | Minimum altitude `< 1 pixel`, including collapsed triangles |
| `SliverTriangles` | `2*area / longestEdgeSquared < 0.1`, including collapsed triangles |
| `MeanMinAltitude_px` | Mean of `2*area / longestEdgeLength` over all submitted projected triangles; collapsed cases contribute zero |
| `MeanShapeQuality` | Mean of `2*area / longestEdgeSquared`; collapsed cases contribute zero |
| `CacheSimACMR`, `CacheSimATVR` | `meshopt_analyzeVertexCache` with cache=16, warp=32, primitive-group=128; simulations, not hardware counters |

Area uses the absolute 2D cross product divided by two. Metrics include hidden/back-facing triangles and repeated instance geometry. They are not visible area, unique shaded pixels, actual hardware tile lists, or clipped coverage. Do not depth-filter the projected-edge sum in the Mac version and compare it directly to the Windows field.

The four triangle-classification fields are counts. Their fractions divide by `ProjectedTriangles`. Do not replace absolute projected area by the silhouette area or multiply edge length by MSAA sample count. For the same connectivity and pose, geometric projection itself is unchanged by 1x versus 4x; accepted-subset averages may differ.

## 14. Logging, validation, and aggregation

Each run produces `<run>.csv`, `<run>.raw.csv`, `<run>.raster.csv`, and `<run>.metadata.txt`. A single optional `<run>.png` is captured outside timing. It is the first encountered Source/no-MSAA case after randomized scheduling, **not necessarily view 0**. Use an explicit fixed-view capture recipe for publication images rather than inferring the pose from this automatic PNG.

Preserve the current [Benchmark writer schema](../Experiments/Benchmark.cpp) and [raster schema](../Experiments/RasterMetrics.cpp) where semantics agree. Appendix C lists the actual recorded headers. Metadata must identify backend, GPU, OS, compiler/shader compiler, revisions/hashes, timestamp units and locations, supported counters, sample patterns, load/store actions, power mode, and all deviations. Keep the Windows RunId separate from the new Mac RunId.

Validation before interpreting any gain:

- Every summary case has exactly the declared number of raw samples, matching RunId/Case IDs and consecutive Sample indices. Require complete-run status and all planned cases, not merely a CSV that exists.
- Recompute mean, median, population standard deviation, min, and max from raw values. Original checks use `rel_tol=2e-8`, `abs_tol=2e-9 ms` against formatted summary values.
- Require finite positive GPU durations. When scopes are equivalent and both exist, require `Pass >= Draw` for each pair.
- Check candidate/source vertex and triangle counts, used vertices, mesh validity, final surface error, and triangulation/fallback status.
- Check projected triangle count equals triangles*instances, and no triangle bbox exceeds the viewport. Verify source/source-control geometry metrics are identical.
- When semantically available, input primitive count must equal triangles*instances. A Metal counter with different semantics needs a different validation rule and label.
- Keep every observation. Mark invalid geometry, unstable controls, or unavailable counters; do not trim samples to make a candidate pass.

For each run, view, MSAA, and block, form the matched ratio:

```text
d[b,v] = 100*(candidate_case_GPU_Mean_ms / source_case_GPU_Mean_ms - 1)
c[b,v] = 100*(source_control_case_GPU_Mean_ms / source_case_GPU_Mean_ms - 1)
```

The SourceControl gate is **per view and MSAA**: accept timing stability iff `abs(mean_b(c[b,v])) <= 2` and `max_b(abs(c[b,v])) <= 3`. This is not a global model-average gate and not a p-value. Require candidate and source validity across blocks, active diagnostic coverage, and strict surface equivalence for the strict cohort.

Average matched percentage changes over blocks for each view, then over the eligible views. For absolute times, average candidate/source block means over exactly the same accepted cohort. Report accepted count out of 24. An average of ratios generally differs from a ratio of averaged times: retain both and label them. Treat blocks/views as repeated conditions, not millions of independent shape observations.

Recommendations use at least **12** strict stable conditions, changed connectivity, and a mean draw-time reduction exceeding **3%**. These are practical reporting thresholds. Mac control exclusions and eligible counts must be determined from the Mac data, not copied from Windows. For a direct cross-device effect comparison, additionally report the intersection of eligible orientations on both devices.

The final study analysis retains identity views 0,8,16. `all_models` does this by its directory-name condition; the five-candidate wrapper passes `--keep-identities`. For a new Mac output directory, explicitly pass `--keep-identities` to avoid silently producing a 22-condition analysis:

```bash
python3 analysis/gpu/topology/analyze.py analysis/gpu/mac_replication/all_models --keep-identities
python3 analysis/gpu/topology/analyze.py analysis/gpu/mac_replication/five_candidates --keep-identities
```

These commands require the original compatible four-file schema, available Draw/Pass scopes, and matching diagnostic semantics. They are not valid unchanged for a Pass-only Metal experiment. Run on a copy of new results: the analysis writes derived CSVs. `docs/export_measurements.py` currently names the original Windows study directories explicitly; it does not discover Mac folders automatically.

## 15. Practical execution sequence on the Mac

1. Verify repository/asset hashes and preserve the measured source variants. Record the Mac model, GPU core count, memory, macOS build, compiler/Metal language versions, power source/mode, external-display setup, and thermal conditions. Use Release and disable validation/capture/profiling during timed runs.
2. Establish geometry identity and source counts. Check camera placement, matrix conventions, normals, and color/depth formats on one source model before running the matrix. Use actual pixel dimensions, not Cocoa logical points.
3. Validate the Metal Pass/Draw scope definitions, resolve/store behavior, query ring, and helper diagnostics. Record unavailable scopes/counters rather than manufacturing equivalents.
4. Run the five candidate confirmations and controls first, with both MSAA settings and the complete original method sets. This covers strong, weak, and sign-reversing cases. A reduced-method pilot is useful for debugging, but its case order/thermal protocol is a different run.
5. Run all broad confirmation cases, then selected and special cases. Use the exact matching source version for historical replication. Execute the complete screen if reproducing method coverage/fallback rates.
6. Run the one-object stage separately. Never pool it with the 64-instance timings.
7. Reconstruct raw summaries, apply Mac-local drift/geometry eligibility, and retain exclusions. Compare per-view effects before reporting model means. Report draw and enclosing pass separately wherever both are valid.

No fixed inter-run cooldown or system-wide frequency lock was part of the archived protocol. Do not claim to reproduce unrecorded thermal history. Consistent power and thermal conditions plus randomized controls make the new dataset interpretable; any added cooldown or clock policy should be documented as a new protocol choice.

## Appendix A. Measured source catalogue

The following table is generated from completed Source cases, not estimated from GUI labels. Radius is `max length(position)` after import/generation. All rows appear at 64 instances; the five original candidates also have the separate one-instance `large` runs. Control meshes occur in the control stage only.

<!-- GENERATED_SOURCE_CATALOGUE -->

| Portable geometry identifier | Vertices | Triangles | Radius | Strict budget (world units) |
|---|---:|---:|---:|---:|
| `blob` | 2048 | 4092 | 0.9323059916 | 0.002295541482 |
| `blob_small` | 512 | 1020 | 0.8896394372 | 0.002278198636 |
| `bunny` | 2503 | 4968 | 0.5 | 0.001192722308 |
| `clustered` | 2176 | 4158 | 1.080170155 | 0.002182594126 |
| `cylinder` | 18624 | 36864 | 0.8231347799 | 0.00195591457 |
| `cylinder_skew` | 18624 | 36864 | 0.8231347799 | 0.00195591457 |
| `ellipse2d` | 512 | 510 | 0.400000006 | 0.0008944272043 |
| `ellipsoid` | 2048 | 4092 | 0.8247720599 | 0.001977599272 |
| `ellipsoid_small` | 512 | 1020 | 0.8240588307 | 0.001965832408 |
| `folded` | 2176 | 4158 | 1.209947467 | 0.002336756479 |
| `jittered` | 2048 | 4092 | 0.6176382303 | 0.002075567188 |
| `jittered_planar` | 2176 | 4158 | 1.060660124 | 0.002121320344 |
| `obj:Data/Models/GraphExHeart.obj` | 7656 | 15308 | 0.5 | 0.001300114393 |
| `obj:Data/Models/GraphExTangleCube.obj` | 17800 | 35616 | 0.5 | 0.00112710044 |
| `obj:Data/Models/ResearchCandidates/FacetedArch.obj` | 2114 | 4224 | 0.5 | 0.001031794144 |
| `obj:Data/Models/ResearchCandidates/FacetedArch_control.obj` | 2114 | 4224 | 0.5 | 0.001031794144 |
| `obj:Data/Models/ResearchCandidates/SpiralRamp.obj` | 7913 | 15360 | 0.5 | 0.001236921648 |
| `obj:Data/Models/ResearchCandidates/SpiralRamp_control.obj` | 7913 | 15360 | 0.5 | 0.001236921648 |
| `obj:Data/Models/ResearchCandidates/SweptWing.obj` | 1538 | 3072 | 0.5 | 0.001003029351 |
| `obj:Data/Models/ResearchCandidates/SweptWing_control.obj` | 1538 | 3072 | 0.5 | 0.001003029351 |
| `obj:Data/Models/ResearchCandidates/ToothedWheel.obj` | 3072 | 6144 | 0.5 | 0.001387975604 |
| `obj:Data/Models/ResearchCandidates/ToothedWheel_control.obj` | 3072 | 6144 | 0.5 | 0.001387975604 |
| `obj:Data/Models/ResearchCandidates/TrefoilKnot.obj` | 24576 | 49152 | 0.5 | 0.00130197638 |
| `obj:Data/Models/ResearchCandidates/TrefoilKnot_control.obj` | 24576 | 49152 | 0.5 | 0.00130197638 |
| `planar` | 2176 | 4158 | 1.060660124 | 0.002121320344 |
| `saddle_branch` | 2176 | 4158 | 1.071015835 | 0.002192366107 |
| `skew1` | 2112 | 4032 | 1.677050948 | 0.003354101966 |
| `skew2` | 2112 | 4032 | 2.371708155 | 0.00474341649 |
| `skew3` | 2112 | 4032 | 3.092329264 | 0.006184658438 |
| `skew_fold` | 2112 | 4032 | 3.13657856 | 0.006206901399 |
| `skew_saddle` | 2112 | 4032 | 3.092329264 | 0.006187971875 |
| `sphere` | 512 | 1020 | 0.5 | 0.001723214097 |
| `sphere_dense` | 2048 | 4092 | 0.5000000596 | 0.001729732385 |
| `suzanne` | 7958 | 15744 | 0.5 | 0.00125239712 |
| `teapot` | 1292 | 2464 | 0.5 | 0.001228632083 |
| `torus` | 8192 | 16384 | 0.6300000548 | 0.001817910879 |
| `torus_skew` | 8192 | 16384 | 0.6300000548 | 0.001817910879 |

<!-- END_GENERATED_SOURCE_CATALOGUE -->

## Appendix B. Asset identity and package verification

The manifest stores SHA-256 of every deployed OBJ, the measured-source ZIP, and the compressed case-order file. Check these locally on the Mac; no internet is required:

```bash
python3 - <<'PY'
import csv, gzip, hashlib, json, zipfile
from collections import Counter
from pathlib import Path
base = Path('docs/reproduction')
m = json.loads((base/'surface_replication_manifest.json').read_text())
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
for name, expected in m['asset_sha256'].items():
    assert sha(Path(name)) == expected, name
for file_key, hash_key in [('case_order_file','case_order_sha256'),
                           ('measured_sources_file','measured_sources_sha256')]:
    assert sha(base/m[file_key]) == m[hash_key]
with zipfile.ZipFile(base/m['measured_sources_file']) as z:
    assert z.testzip() is None
with gzip.open(base/m['case_order_file'], 'rt', newline='') as f:
    counts = Counter(int(r['RunIndex']) for r in csv.DictReader(f))
assert len(m['runs']) == 90
assert sum(counts.values()) == 109536
assert sum(r['raw_samples'] for r in m['runs']) == 7790592
for r in m['runs']:
    assert counts[r['index']] == r['cases']
print('Assets, source archive, and all 90 recorded run plans verified.')
PY
```

Asset hashes are also listed below so the Markdown retains readable input identity independently of parsing JSON.

<!-- GENERATED_ASSET_HASHES -->

```text
c93e4a6fc6423194ff8b24de372d0a0632264290302a11d391f832313a0d55ef  Data/Models/Bunny.obj
612b51eb6adfc564bf2a09dad1b48b0dda832aac11f2f1193739a79c58fdef2c  Data/Models/GraphExHeart.obj
eb3534ebb1ea6a9b439aa8abef19782aed18cd3a05c77bcbc105514f896824f5  Data/Models/GraphExTangleCube.obj
4ead602d01215aa990b3a91048224cfd8480c954ba209989e6b74fca8e86a43d  Data/Models/ResearchCandidates/FacetedArch.obj
32b6b50568ba00d16a2e458b76516556819c82d8c02db38dfb620590224aceac  Data/Models/ResearchCandidates/FacetedArch_control.obj
4455d468f7c4a5e0adbc4a85675826e9de68fb8f161c075fc320b758063ad582  Data/Models/ResearchCandidates/SpiralRamp.obj
117a6b8fcd774b18b5cd526e17373d932b9765277a92cbda393ca84264b9ecfc  Data/Models/ResearchCandidates/SpiralRamp_control.obj
0d0aa54add4b27c354fecb963351087e22acd9f33bd8512222295a88db3cd1e3  Data/Models/ResearchCandidates/SweptWing.obj
608267a45a7512a11fe92e3cb5ce4f351b8c41984485779f4c12a2ac7cb6f7d3  Data/Models/ResearchCandidates/SweptWing_control.obj
68a894a504dc25be26f6da0b4b331eabb9c9f4f9a2dde4e63c4a9fd72c0262a7  Data/Models/ResearchCandidates/ToothedWheel.obj
ee2f6b4f3f696d5fa3b8031ff3b1327fc172d4542cf3c030052b61c145478db9  Data/Models/ResearchCandidates/ToothedWheel_control.obj
186eee02bc17f21f1f143e5d0dd8ccfb35b1c865e39d356cfb89380e0f85be64  Data/Models/ResearchCandidates/TrefoilKnot.obj
8cedec758012c8995dbad376c85494801ae84a03627e97c6779ef9e3693546c8  Data/Models/ResearchCandidates/TrefoilKnot_control.obj
e41b8f753ec1a57a1cf7fd1017d2e58f26ee330fc29b2818b5a643be7c8b4e71  Data/Models/Suzanne.obj
153ba534a7f04c32f86a274432c4a2b290a248a385ed93e0d171bde9faf9c335  Data/Models/Teapot.obj
```

<!-- END_GENERATED_ASSET_HASHES -->

## Appendix C. Actual recorded CSV schemas

The following are the longer-confirmation headers. Broad-screen summaries predate `SourceSetupCPU_ms`, `PatchSetupCPU_ms`, and `ReferenceAuditDirections`; the original per-run metadata and source archive preserve that difference. Unavailable Metal-specific fields need explicit availability handling; zero is not a substitute for missing data.

<!-- GENERATED_SCHEMAS -->

**Case summary** (line wrapping below is for display only):

```text
Case,Geometry,Method,Block,View,MSAA,Shader,Instances,Width,Height,Depth,Meshopt,Samples,GPU_Median_ms,GPU_Mean_ms,GPU_StdDev_ms,GPU_Min_ms,GPU_Max_ms,ConstructionCPU_ms,SetupCPU_ms,ReferenceSetupCPU_ms,SourceSetupCPU_ms,PatchSetupCPU_ms,OutputVertices,OutputTriangles,SourceTriangles,FailedPatches,DuplicateTriangles,NonManifoldEdges,WindingErrors,MissingBoundaryEdges,UnexpectedBoundaryEdges,DegenerateTriangles,SampledReferenceError,ReverseSampledReferenceError,ReferenceErrorBudget,ReferenceAuditDirections,SurfaceEquivalent,ExperimentClass,TriangulationStatus,PatchCount,ChangedTriangles,StructuralValid,ConnectedComponents,UniqueEdges,EulerCharacteristic,MaxValence,ValenceVariance,ValidForRanking,Flips,StarReplacements,TotalEdgeLength3D,TotalSceneEdgeLength3D,SurfaceArea,DiagnosticHelpers,DiagnosticActiveLanes,PipelineStatisticsAvailable,HardwareInputPrimitives,HardwareVSInvocations,HardwarePSInvocations,HardwareRasterPrimitives,DiagnosticHelperFraction,GPU_PassMedian_ms,GPU_PassMean_ms,GPU_PassStdDev_ms,GPU_PassMin_ms,GPU_PassMax_ms,MeshValid,RunId,Protocol,QueueDepth,OptimizationView,ObjectiveInstances,ConnectivitySeed,PassScope,CPUScope,Warmup,XRotationDegrees,YRotationDegrees,ZRotationDegrees,GridCols,GridRows,GridLayers,Radius,SurfaceLocalFlips,ProjectedEdgeLength,ExpectedSamples,CaseComplete
```

**Raw samples** (line wrapping below is for display only):

```text
Case,Sample,Frame,GPU_Draw_ms,GPU_Pass_ms,RunId
```

**Raster metrics** (line wrapping below is for display only):

```text
Case,Ordering,ProjectedTriangles,OutsideViewportTriangles,ProjectedArea_px2,ProjectedBBoxArea_px2,ProjectedEdgeL1_px,SubpixelTriangles,BelowFourPxTriangles,ThinTriangles,SliverTriangles,CacheSimACMR,CacheSimATVR,MeanMinAltitude_px,MeanShapeQuality
```


<!-- END_GENERATED_SCHEMAS -->
