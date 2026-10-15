> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# Review against master: correctness, size, and performance

This is the historical pre-fix review. Implemented changes and verification are in [FIXES.md](FIXES.md); original-method recommendations are in [METHOD_ASSESSMENT.md](METHOD_ASSESSMENT.md).

Reviewed 10 October 2026. Scope: the current working tree on `codex/gpu-triangulation-study`, including the reorganized source files and the experimental measurement code. This is a review and proposal; production algorithms, rendering, and UI were not changed during this review.

## Size and provenance

The committed `3DScenes` base differs from `master` by 3,960 added and 72 removed lines across ten files. `HelperLaneViz.cpp` grows from 1,166 to 2,334 lines, with 1,236 additions and 68 deletions. Its current size is 2,337 lines: the experimental CLI dispatch adds three lines. Moving files into folders does not reduce their implementation size.

Approximate ownership of additions in the original branch, computed from a line comparison:

| Area in HelperLaneViz.cpp | Added lines |
|---|---:|
| Method/geometry mapping, fitting, metrics, output-path utilities | 363 |
| Volume benchmark and CSV machinery | 301 |
| GUI | 230 |
| Geometry construction/upload | 189 |
| Volume grid/camera setup | 85 |
| Other | 70 |

`Triangulation3D.cpp` is 1,707 lines; `ObjMeshLoader.cpp` is 242. The new experimental runner is 588 lines and the new heap implementation is 373. The loader is not especially large. The main problems are mixed responsibilities, repeated work, and incomplete correctness contracts, rather than formatting or brace count.

The current ordinary `git diff master` omits untracked moved/new files. Its deletion totals therefore do not describe the complete working tree. The size comparison above uses committed `master` and the committed 3DScenes base, with current files inspected separately.

## Findings requiring correctness or measurement fixes

### F01 — P1: incomplete reconstruction can be reported as success

Locations: [Triangulation3D.cpp](../Triangulation/3D/Triangulation3D.cpp), lines 875–877, 981, 1397, 1628–1631, 1657.

`appendEarClippedFace()` returns `emittedTriangle` when it gets stuck: emitting one ear makes an unfinished face look successful. Its caller ignores the return value anyway. Patch success then means only “nonempty and divisible by three.” At mesh assembly, failed patches are skipped and any surviving indices make the mesh successful. Geometry disappears without a transactional failure contract. Simply moving these functions into smaller files leaves this defect intact.

Existing native data confirms the consequences: jittered-sphere Greedy emits 3,924 instead of 4,092 triangles with 84 failed patches; dense-sphere Greedy retains the count but contains 363 duplicate triangles. See [the baseline audit](gpu/baseline/gpu_single.audit.json).

**Simplify/fix:** return a typed completion/failure status; build a patch's triangles in temporary storage; commit only after complete coverage, orientation, boundary, and incidence checks. Otherwise fail the operation or preserve a validated source triangulation and mark the output degraded. Do not substitute an invalid n-gon fan as the fallback. Validate whole triangles before assembly rather than individually dropping indices.

**Performance:** cheap local checks and transactional rollback are inexpensive compared with the current candidate searches. More complete validation has a cost and belongs in preparation/diagnostics, outside timed draws. It must not be removed to make an invalid output look faster.

### F02 — P1: patch eligibility does not establish a valid projection

Locations: [Triangulation3D.cpp](../Triangulation/3D/Triangulation3D.cpp), lines 166–169, 442–456, 459–472, 1530–1531, 1574.

A seed face is accepted before testing `PatchStats::valid`; `makePatch()` computes those stats but discards their validity. A nonplanar single n-gon can therefore bypass the plane/normal thresholds. `isFiniteFace()` checks face size and index range, not finite coordinates. The normal comparison uses `abs(dot)`, and fitting flips normals toward the seed. This treats antiparallel normals as similar without establishing consistent orientation or an injective projection. A single degree-two boundary loop also does not certify a manifold disk or a non-overlapping planar domain.

**Simplify/fix:** one validated patch-preparation function should establish the input/index/finite-data, orientation, plane-error, topology, and projected-domain contract. Reject or split inadmissible seeds. Retain original surface orientation when lifting projected triangles. Distinguish “one boundary loop” from “validated disk.”

**Performance:** cache input validation and face geometry once. Validation and projection checks can add preparation cost; treating every normal threshold as proof of valid coverage is not an acceptable shortcut. Changing eligibility changes the resulting geometry, so this is an algorithm change, not a mechanical refactor.

### F03 — P1: Source is not a valid baseline for arbitrary OBJ polygons

Location: [Triangulation3D.cpp](../Triangulation/3D/Triangulation3D.cpp), lines 1690–1699.

Every face is fanned from vertex zero. A concave n-gon whose first vertex cannot see the whole polygon produces overlapping/outside triangles. A C-shaped polygon of area 7 produces fan triangles with total absolute area 11; the [software illustration](review/concave_fan.json) records the vertices and calculation. This is not a GPU test. Nonplanar n-gons also need an explicit surface interpretation. A baseline called Source cannot serve as a quality reference merely because it has `n-2` triangles.

**Simplify/fix:** establish valid source triangles once during preprocessing. Preserve original polygon boundaries separately when comparing n-gon triangulators. Use existing triangles directly; use a robust, checked planar polygon triangulation for supported concave n-gons; explicitly reject or define handling for nonplanar polygons.

**Performance:** preprocessing once avoids repeated fanning/logging. Current controlled GPU fixtures are not arbitrary OBJ polygons; this finding does not establish that their recorded Source baselines are invalid.

### F04 — P1: skipping a malformed OBJ vertex changes later face references

Location: [ObjMeshLoader.cpp](../Import/ObjMeshLoader.cpp), lines 170–172.

On a malformed `v` record, the parser warns and omits the position. Subsequent OBJ references use original position numbering, while the loader resolves them against the shortened vector. A later face can remain in range and silently select different positions. The native loader probe demonstrates a successful load with a warning and an incorrectly shifted face. [Probe source](review/obj_loader_probe.cpp), [output](review/obj_loader_probe.txt), [fixture](review/bad_vertex.obj).

**Simplify/fix:** fail the import on malformed position records, or preserve original position slots and reject references to invalid slots. A compact strict parser is easier to reason about than permissive recovery that changes numbering.

**Performance:** this is import-time work and does not affect GPU draw performance. Strict failure usually reduces recovery complexity.

### F05 — P2: OBJ integer parsing accepts trailing garbage

Location: [ObjMeshLoader.cpp](../Import/ObjMeshLoader.cpp), lines 45–62.

`std::stoi(indexText)` is used without checking the consumed length. The loader accepts `f 1junk 2 3` with no warnings. This was reproduced with the same native probe and [fixture](review/bad_index.obj).

**Simplify/fix:** parse a complete integer using `from_chars` or check the consumed position, range, and OBJ negative-index rules. Use signed arithmetic with checked conversion. Validate face degeneracy and supported coordinate data separately rather than treating syntactic parsing as valid geometry.

**Performance:** avoiding substring allocation and exception-based parsing can improve import CPU cost, but no speedup was measured here.

### F06 — P1: the GUI benchmark does not establish equivalent-quality comparisons

Locations: [HelperLaneViz.cpp](../HelperLaneViz.cpp), lines 850–858, 1510–1685, particularly 1583–1596 and 1638–1673.

The GUI displays/uploads reconstructed output without the topology/reference audits used by the experimental runner. A method that drops or duplicates geometry can enter the same summary as Source. Missing profiler samples become zero in the volume path; the inherited planar path indexes an empty vector at line 1232. `/onFrameRender` timing includes the rendering callback's other GPU work, not just the indexed draw. Timing scope and sample availability must be explicit.

**Simplify/fix:** use one benchmark case/result contract shared by GUI and CLI: immutable settings, validation eligibility, actual sample count, named timing scope, raw samples, and separate diagnostics. Missing samples must be an error/incomplete case, not zero. Keep GUI progress/reporting as a thin adapter to a benchmark session.

**Performance:** shared contracts do not require additional draw-time work. Keep audits and counter readback outside the timed workload. Preserve draw/pass/frame scopes as distinct measurements instead of forcing them into one ambiguous “GPU time.”

### F07 — P1: changing MSAA also changes the GUI depth configuration

Locations: [HelperLaneViz.cpp](../HelperLaneViz.cpp), lines 466–480, 1719–1727.

The custom multisampled FBO has only a color attachment. The ordinary target FBO has a depth attachment. Switching the 3D GUI from 1x to MSAA therefore changes available depth testing along with sample count. No explicit matching depth state is established in this sample's GUI setup. This confounds visibility, overdraw, helper work, and timings.

**Simplify/fix:** one render-target factory should create matching color/depth formats and explicit depth/cull state for all compared sample counts. The headless runner already has matching D32 attachments and explicit depth state; reuse the design without replacing the visualization shader with the timing shader.

**Performance:** restoring intended depth behavior may substantially change rendering cost. That is a correctness correction and requires new GUI measurements, not a promised performance-neutral cleanup.

### F08 — P2: GUI benchmark configuration and restoration are scattered

Locations: [HelperLaneViz.h](../HelperLaneViz.h), lines 163–209; [HelperLaneViz.cpp](../HelperLaneViz.cpp), lines 1469–1526 and 1929–2263.

Three benchmark modes share an output stream and parts of their saved state, but have separate integer steps, counters, accumulators, flags, and duplicated transitions. GUI controls remain editable during a run, including geometry, method, dimensions, sample count, and instance count. Cases can therefore be collected under changing settings. Only selected fields are restored; failures/file-open problems lack one cleanup path.

**Simplify/fix:** one session owns a copied `BenchmarkConfig`, explicit phase enum, current case, saved interactive state, and result writer. Disable conflicting edits or apply them only after the run. Use one finish/cancel/error restoration path. This is a small state machine, not a generic scheduling framework.

**Performance:** no additional per-pixel cost; it can remove per-frame maps/strings and repeated state changes. GPU timing behavior still needs regression checks when changing the execution schedule.

### F09 — P2: informational strings are used as failure counts and structured data

Locations: [Triangulation3D.cpp](../Triangulation/3D/Triangulation3D.cpp), lines 1697–1699; [HelperLaneViz.cpp](../HelperLaneViz.cpp), lines 854, 1392–1420, 1431–1466, 1541–1544.

Source emits one “fan-triangulated from an OBJ polygon” message for every n-gon, including procedural quads. The GUI counts `messages.size()` as failed patches. A successful Source sphere can consequently display failures and allocate hundreds of strings. The loader's `fanTriangulatedFaceCount` also counts polygons before triangulation actually occurs. GUI CPU construction time is measured into `mVolumeBenchmarkCpuTimeMs` but is absent from the volume CSV. That CSV mixes summary and per-transform tables with different headers and free text; a comma in an OBJ filename is not escaped. Output-open/write failures are largely silent.

**Simplify/fix:** typed diagnostics with separate error/warning/count fields; aggregate routine fan information once. Return import statistics under accurate names. Write one schema per file, quote string fields, check IO, and include declared CPU stages. Keep human-readable status as a view of typed results, not the source of failure counts.

**Performance:** removes routine string allocation. Typed counters/writers run outside timed draws. This is a more useful reduction than compressing C++ formatting.

## Findings about repeated work and experimental code

### F10 — P2: source import, patch preparation, and copies are repeated on method changes

Locations: [HelperLaneViz.cpp](../HelperLaneViz.cpp), lines 717–869, particularly 811–812 and 850–851; [Triangulation3D.cpp](../Triangulation/3D/Triangulation3D.cpp), lines 249–321, 1553–1555, 1623; [PerformanceBenchmark.cpp](../Experiments/PerformanceBenchmark.cpp), lines 232–292.

Changing a triangulation method reparses/regenerates the source and rebuilds its patches. Each candidate patch-growth attempt copies the face list, reconstructs a tree-backed unique-vertex set, recalculates face normals twice, scans bounds, and finally recalculates the accepted patch statistics. Duplicate candidate queue entries repeat work. GUI upload also copies result vectors that could be moved. An OBJ load result is const and copied into the source mesh.

**Simplify/improve:** immutable source mesh + prepared surface keyed by source revision and patch settings + method-specific index generation. Cache face normals/weights, adjacency, projection, source-index mapping, and bounds. Track queued candidates without permanently forbidding retries after a patch changes. Move final result buffers into their owner. Incremental fitting must preserve unique-vertex weighting and existing acceptance semantics; caching face geometry is the safer first step.

**Performance:** likely reduces CPU cost and allocations; actual improvement requires CPU profiling. Record both cold preparation+construction and cached method-only time so caching does not silently change the benchmark's meaning. Keep fixed camera bounds from the source rather than a potentially degraded output.

### F11 — P2: expensive custom backends repeat searches and switch objectives

Locations: [Triangulation3D.cpp](../Triangulation/3D/Triangulation3D.cpp), lines 1010–1163 and 1251–1359.

Visible edges are enumerated over all point pairs. LMT loops over candidate edges and possible witness pairs, with additional all-point/boundary checks; its loose worst-case cost can reach O(n^5) for n projected points. GP constructs/scans a complete neighbor list per point, sorts it twice, creates repeated edge candidates, and then repeats visibility/intersection scans. Raising the GUI patch limit to 2,048 faces can make these approaches impractical. The previous native jittered-grid LMT observation took about 2.7 seconds, so this is not merely stylistic complexity.

LMT initially uses actual 3D edge weights when requested, then invokes the 2D `optimizeByMinLengthFlips()` at line 1162. CDT+Flips also uses that projected metric. The final score is therefore not uniformly a 3D length objective. GP is custom code, not PCL GP3; “LMT MWT” is a heuristic label, not proof of an exact minimum.

**Simplify/improve:** share a weighted candidate record and cached visibility/edge-length data; deduplicate GP edges before expensive work; use spatial neighbor queries when they preserve the intended neighborhood definition. Keep expensive methods explicitly experimental and bounded. Provide an explicit metric to a local flip engine if 3D scoring is desired. Do not replace point-set triangulation with a boundary-only polygon triangulator: that would discard interior points. Spatial restrictions, objective changes, and replacing a backend require new quality/performance comparisons; they are not guaranteed equivalent.

### F12 — P2: common CPU/render utilities are duplicated and refreshed unnecessarily

Locations: [HelperLaneViz.cpp](../HelperLaneViz.cpp), lines 271–300, 303–337, 438–465, 665–715, 975–1017, 1744–1748, 1974–1979.

2D and 3D upload functions differ mainly in vertex stride/layout. Both initialize buffers with source data and then upload it again. Parent Falcor's `Buffer` constructor calls `setBlob()` for initial data, and its DeviceLocal path already calls `updateBuffer()`. The double upload existed on master and was copied into the new 3D path. There are repeated edge-set/metric calculations, duplicated camera fitting, and matching CPU/shader instance-grid math. Grid and camera state are recomputed every rendered 3D frame despite also updating on changes.

Removing/readding shader defines every GUI frame forces Falcor's program dirty path, even when the final values are unchanged. Cached program versions prevent assuming a full shader recompile every frame, but the avoidable invalidation/lookups remain. This pattern existed on master and the new 3D define extends it.

**Simplify/improve:** one upload function taking vertex bytes/stride/layout and indices; one initialization/upload; shared indexed-mesh metrics/topology; one camera/grid configuration object; dirty flags for changes; call `addDefine()` with the desired value without first removing it, or update cached shader variants only when needed. Keep layouts and workload-specific state explicit rather than erasing everything behind runtime polymorphism.

**Performance:** removing duplicate uploads and CPU recalculation should help setup/CPU overhead while keeping GPU data/draws identical. Camera/projection and numeric-helper refactors still need exact geometry/parameter checks. Existing double-precision helpers in the experimental optimizer should not be replaced wholesale with float math merely to remove wrappers.

### F13 — P2: the new summarizer has implicit prerequisites and an incomplete case key

Location: [summarize_gpu.py](summarize_gpu.py), lines 12–27, 52–60, 71–72.

The supported CLI allows runs without Source, but the summarizer immediately indexes a Source map. A heap-only subset of the current smoke run reproduces a `KeyError`; see [probe output](review/summarizer_probe.txt). A file with no eligible groups also fails at `result[0]`. Source matching omits instance count, resolution, depth, and meshoptimizer settings; concatenated/mixed runs can be silently paired incorrectly. Current saved matrices have uniform settings per file, so that last defect does not invalidate their existing pairings.

The geometry audit JSON takes the last row's `ValidForRanking`, which combines mesh eligibility and that view's visible pixel work. It is not a stable mesh-only validation status. A zero helper baseline produces a reported 0% change instead of an undefined percentage.

**Simplify/fix:** validate required columns and duplicate keys, use a full run/case key, distinguish mesh validity from view visibility, handle missing baselines/empty results explicitly, and use null/N/A for undefined ratios. A comparison-only command can require Source up front; a measurement/export command should still support standalone diagnostic runs.

**Performance:** offline analysis only; no rendering penalty.

### F14 — P2: the new runner needs typed results, truthful provenance, and a declared sampling regime

Locations: [PerformanceBenchmark.cpp](../Experiments/PerformanceBenchmark.cpp), lines 343–371, 392–462, 540–546; [SurfaceFlipExperiment.cpp](../Triangulation/3D/SurfaceFlipExperiment.cpp), lines 218 and 362.

`PerformanceApp` combines command-line configuration, construction, mesh validation, upload, camera setup, workloads, sample statistics, logging, and run orchestration. Draw/pass statistics duplicate the same reduction code. A long manually paired CSV header/row makes schema changes brittle. Metadata hardcodes Release instead of identifying the actual build and omits a self-contained source revision/dirty-state/configuration manifest. A reusable FBO is allocated again for each view/block. The reference BVH is rebuilt for each heap method and again for each final audit; samples repeat shared vertices and edge midpoints.

Every draw sample synchronizes the GPU and writes a raw CSV row before the next draw. Timing fences exclude IO from the GPU interval, but CPU gaps and queue isolation can still affect clock/cache state. These are isolated draw/pass measurements, not a continuously queued full-frame benchmark. Faster query batching is a separate protocol and must be reported separately.

**Simplify/improve:** small `BenchmarkConfig`, `PreparedGeometry`, `CaseResult`, `SampleStats`, and `ResultWriter` components; one statistics helper; reusable FBOs; a reusable fixed-reference surface accelerator; deduplicated audit samples; typed/versioned CSV/JSON schemas and actual build/source/configuration provenance. Buffer raw rows until after a case. If using query rings/batched draws, keep the original synchronized protocol as a control and rerun paired comparisons.

**Performance:** structural extraction has no inherent per-pixel cost. Reference caching reduces preparation/audit work; preserve all error guards. FBO/query scheduling changes can change measured workload conditions and need new benchmarks. The runner's separate instrumentation, explicit render state, source-based camera, raw samples, paired blocks, and rejection of invalid geometry are valuable and should be retained.

### F15 — P2: GUI and headless workload names describe different experiments

Locations: [MainShader.slang](../Shaders/MainShader.slang), lines 105–112 and 140–152; [HelperLaneViz.cpp](../HelperLaneViz.cpp), lines 532–536; [PerfShader.slang](../Shaders/PerfShader.slang).

The GUI's “Material Stress” uses lane-dependent UVs, eight texture samples, and wave sums. Its texture is constant. It therefore measures a synthetic wave/texture workload, not the headless runner's 16 dependent texture samples on nonconstant data. In the helper visualization, the supposed dead-code-elimination prevention expression selects zero on both sides; it does not require the texture sample to survive optimization. This expression and the constant texture predate 3DScenes, while the stress mode is a branch addition.

**Simplify/fix:** define explicit workload descriptors and stable names, share only common shader utilities, and use a real output dependency when texture work is intended. Keep visualization and diagnostic wave operations separate from timed material shading. Label the existing GUI stress mode as synthetic if retaining it. Do not silently consolidate the shaders into one different workload and compare its new times with old CSVs.

**Performance:** removing artificial wave work changes the benchmark, even if it makes the shader faster. This requires a new workload/version and paired measurements; it is not a performance-neutral source cleanup.

## A smaller structure without a generic framework

| Area | Proposed responsibility split | Real simplification |
|---|---|---|
| `HelperLaneViz.cpp` | Keep Falcor lifecycle, interactive rendering, and widgets; delegate source construction and benchmark sessions | Remove duplicate uploads/camera fitting/method tables; move non-UI metrics and benchmark machinery out |
| `Triangulation/3D/Triangulation3D.cpp` | Prepare valid surface patches, project/lift, and assemble results | Keep one prepared patch representation and one failure contract; avoid repeated preparation |
| `Triangulation/2D/ProjectedPatchTriangulation` | Planar point-set/PSLG predicates and backend adapters, with optional original 3D edge metric | Share projected algorithms and Triangle conversion without confusing polygons with interior-point sets |
| `Triangulation/3D/SurfaceFlipExperiment` | Constrained local flip engine | Reuse reference acceleration/topology utilities; keep metric and admissibility separate |
| `Triangulation/` common utilities | Canonical edge keys, indexed-mesh metrics/topology, typed diagnostics | Share mechanisms that are actually common; retain dimension-specific algorithms in their folders |
| `Import/ObjMeshLoader` | Strict position/polygon importer | Keep n-gons, index semantics, diagnostics, and normalization explicit; no triangulation side effects |
| `Experiments/` | Immutable cases, workloads, sample collection, export | Share the GUI/CLI contracts and statistics while keeping visualization distinct from timed shaders |

Roughly 700–900 lines can plausibly be moved or reused out of `HelperLaneViz.cpp`; this is a file-size estimate, not a claim that all those lines disappear from the project. Its UI additions remain legitimate features. Most of lines 508–1489 in `Triangulation3D.cpp` are projected planar machinery/backend adapters and can leave the 3D coordinator. Extraction makes review easier; actual total reduction comes from eliminating duplicate mechanisms and redundant copying/logging.

The small OBJ importer is not automatically redundant with Falcor's existing importer. Falcor's Assimp scene import path rejects non-triangle meshes (`Source/plugins/importers/AssimpImporter/AssimpImporter.cpp:580`), whereas this study wants original polygon faces. Reusing an already-triangulated scene would change the experiment. A strict lightweight polygon adapter remains reasonable; replacing its parser should be evaluated against preservation of original n-gons, import cost, and added dependencies.

The repeated scalar vector wrappers can be shortened where Falcor provides equivalent inline operations, but preserve precision, zero-length behavior, tolerances, and decision order. Consolidate predicates with explicit boundary/tolerance policies; similarly named point/segment predicates currently have different semantics. Do not create one overly generic geometry framework or add virtual dispatch to candidate/flip loops.

## Order of work and performance safeguards

1. Establish valid source triangles and typed complete/degraded/failed results. Fix the parser, partial-output handling, patch eligibility, and GUI depth/sample correctness before ranking more outputs.
2. Extract common upload, metrics, camera configuration, statistics, result schemas, and benchmark sessions. Keep algorithm decisions and shader work unchanged in this stage.
3. Separate source/preparation/method stages and cache immutable data. Preserve cold construction timings and report cached retriangulation separately. Move expensive work out of continuous GUI edits where appropriate.
4. Optimize candidate searches/reference audits using CPU profiling. Keep global heap constraints; label custom GP/LMT honestly and preserve them as experimental comparisons unless intentionally changing scope.
5. Evaluate metric/search/backend/query-schedule changes as new algorithms/protocols, using the substantial-shortening matrices as controls.

Mechanical extraction should reproduce triangle/index outputs, mesh audits, edge lengths, camera/instance parameters, shader variants, and helper diagnostics. CPU optimizations need repeated cold/cached timings. GPU changes need paired randomized blocks on representative single/many-instance inputs, 1x/4x MSAA, both workloads, and consistent meshoptimizer state. Matching total edge length alone is not a regression test: the measured small-triangle counterexample already shows that large shortening can increase helpers.

Do not delete validation, timing isolation, warmup, raw evidence, or surface constraints to reduce line count. Do not claim a runtime speedup from moving functions. Preserve the existing [large-reduction results](GPU_RESULTS.md), including their finite reference-error and hardware/protocol limitations.

## Review evidence and generated artifacts

This review inspected the current files and committed master/base diff. It reused the existing native audit evidence and ran a small MSVC Release probe against the current OBJ loader with the earlier scalar Falcor math shim. It reproduced the no-Source summarizer failure using saved rows. It did not rerun the complete GPU matrices or change production code.

Saved GPU artifacts occupy approximately 31 MiB across large-reduction, baseline, scan, and smoke folders. Their raw rows are useful evidence, not algorithm bloat. Keep one indexed run manifest and curated comparisons for review; treat logs, images, raw rows, and generated harness source/binaries as generated artifacts, with an explicit Git inclusion policy. The pinned CPU harness already regenerates its source from Git; avoid committing duplicate derived snapshots just to preserve them. Existing measurement files were retained.

The targeted probe sources, fixtures, and outputs are under `analysis/review`; generated executables/objects are ignored. Build verification from the preceding organization step remains in [CHANGES.md](CHANGES.md). No production refactor, commit, or push was made during this review.
