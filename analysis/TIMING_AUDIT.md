> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# Timing and measurement audit

Reviewed 10 October 2026 at source commit `fa2ae1b`. Production sample and parent sources were not modified during this audit. Standalone probes, logs, a proposed one-line parent fix and verification are under `build/timer-probe`. This audit supersedes earlier claims that positive finite timestamps alone establish reliable measurement.

## Measurements needed for this study

| Metric | Scope | Purpose |
|---|---|---|
| Elapsed frame, ms | CPU wall clock from one frame start to the next, including CPU work, synchronization, UI and presentation waits | Application throughput and hitching. Existing Falcor FrameRate already supplies average and last values; display them explicitly as elapsed time, not GPU time. This is not an OS display-present cadence measurement. |
| Scene render GPU, ms | Clear + geometry draw + MSAA resolve/display blit, for a fixed steady scene, with counters/capture off | Primary application-relevant GPU comparison. Includes fixed costs that dilute a draw-only improvement. |
| Mesh draw GPU, ms | Indexed draw with fixed camera, shader, render state, resolution and instances | Explains whether triangulation changes geometry/raster/shading cost. Keep as a subordinate measurement. |
| Full frame GPU command span | Scene + UI + final swapchain copy, excluding presentation waits; one enclosing span on the relevant queue | Optional completeness metric. Requires a hook around SampleApp's actual GPU work; the sample callback alone cannot provide it. Do not sum nested profiler rows. |
| CPU construction | Explicit cold or cached construction scope, separate from steady frames | One-time/remeshing cost, not steady rendering performance. |
| Helper/active proxy counts | Separate instrumented pass | Explain coverage changes; not hardware occupancy or measured scheduled lanes. |

For the user's triangulation question, report **scene GPU time and elapsed frame time**, with draw time and helper proxies explaining the change. Scene GPU time is useful even when wall time is limited by CPU/UI/profiling or VSync. CPU and GPU overlap, so their times cannot simply be added. Resolve/UI overhead can make a substantial draw reduction a much smaller frame reduction.

`SampleApp.cpp:476` ticks FrameRate before the callback; its previous frame interval includes the rest of that render loop. `/onFrameRender` wraps only the callback at line 488. UI is separate at line 505; profiler readback at 511 and swapchain copy/present at 520–526 are outside it. The current CLI runs all cases in `onLoad`, without UI/presentation; its Draw and Pass columns are **not frame times**. Its Pass is only clear + draw, not MSAA resolve.

## P1: Confirmed timestamp buffer-state defect

`Source/Falcor/Core/API/GpuTimer.cpp:131` resolves query data into `mpResolveBuffer` without a transition to CopyDest. `copyResource` subsequently transitions that buffer to CopySource. On the next resolve the buffer is still tracked as CopySource, so the copy lacks the necessary write-to-read transition. The first resolve also leaves Falcor's tracked Common state inconsistent with the API's assumed CopyDest state.

Microsoft requires ResolveQueryData's destination buffer to be in [D3D12_RESOURCE_STATE_COPY_DEST](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvequerydata). The pinned [Slang 2024.1.34 implementation](https://github.com/shader-slang/slang/blob/v2024.1.34/tools/gfx/d3d12/d3d12-command-encoder.cpp#L487) calls native ResolveQueryData directly; it does not supply this barrier for Falcor.

Standalone test: time the same clear workload using the existing Falcor timer and an explicitly transitioned resolve buffer. Alternate one and 128 clears, with 20 untimed warmups and eight samples per case. No shader compilation, geometry change, GUI or parent source modification is involved.

| Transition/sample | Existing Falcor timer, ms | Explicit barrier timer, ms |
|---|---:|---:|
| First sample after 1 → 128 clears | 0.007168 | 1.073152 |
| First sample after 128 → 1 clear | 1.073152 | 0.007168 |

All **31/31** existing-timer results after the first exactly match the explicitly transitioned timer's preceding result, including small within-case changes. This is measurement lag, not GPU frequency fluctuation. The original path produces 32 D3D12 debug messages (IDs 527/538: inconsistent state and invalid copy source). The explicitly transitioned path alone produces **zero** debug messages and responds immediately to workload changes.

Minimal fix: transition `mpResolveBuffer` to CopyDest **before obtaining the encoder** in `GpuTimer::resolve()`. Existing copyResource then handles the CopySource transition. See `build/timer-probe/GpuTimer_copy_dest.patch`. A different sample timer implementation would duplicate framework code and leave profiler timing broken, so fixing the shared timer is preferable.

Warmup currently does not refresh the CLI timer/readback data: it issues untimed draws. Consequently the first collected result can belong to the previous case even after many warmups. GUI and profiler use the same defective timer, with different buffering/collection delays; do not blindly shift their CSV samples by one to "correct" them.

## Existing result implications

Saved raw logs show the same pattern: in the scanned 21 runs, 5506 of 5628 transitions with substantially different durations have previous-case-like first values. This is a heuristic scan, not a count of all erroneous samples. Details: `build/timing-audit-transitions.json`.

Long steady cases can retain approximately correct medians despite losing the last sample and gaining a foreign first sample. In `gpu/fixes/torus_final.csv`, dropping the first raw sample changes any case median by at most 0.3674%, but changes a case mean by up to 29.0742%. That sensitivity calculation is **not a correction** and does not establish driver-independent validity of an invalid resource-state path. Min/max/stddev, short cases, transitions and frame/sample ownership are especially compromised. Re-run key paired GPU comparisons after correcting and validating the timer. Do not present earlier GPU speedup numbers as validated paper results in the meantime.

Mesh topology audits, flip counts and total edge lengths do not depend on timestamps. The 50.7% torus edge reduction remains geometry evidence. Helper counters use a separate texture readback path; the timestamp defect does not by itself invalidate their numerical counts. They remain instrumented shader proxies with the usual coverage/surface/early-depth caveats, not hardware counters.

The previous UI readback experiment demonstrates a real synchronous diagnostic path in source, but its absolute GPU numbers were collected through the defective timer. They also require revalidation. Positive/finite samples and an enclosing Pass duration were insufficient checks: both timers lagged together, allowing those checks to pass.

## Other correctness and robustness findings

| Priority | Finding and evidence | Required improvement |
|---|---|---|
| P1 | CLI submits and waits after every draw (`PerformanceBenchmark.cpp:231`). This empties the queue and changes normal frame scheduling/cache/power conditions. Excluding the wait from a draw timestamp does not make it a frame benchmark. | Measure continuously queued scene frames using a small fixed query pool/ring or batched queries, with explicit case/frame ownership. Collect completed results after fences; keep synchronous diagnostics/capture out of measured frames. Validate the changed protocol against the isolated control. |
| P1 | Analyzer rejects duplicate Source rows but accepts duplicate method rows and invalid times. Injected duplicate Heap3D rows give Pairs=2 for one actual case; -1 ms is accepted as a -4345.9% change; NaN propagates into output. | Require necessary columns and valid finite positive times/counts. Reject duplicate keys for every method, including raw samples. Check raw count/IDs/statistics against summary. Report missing baselines/coverage explicitly. |
| P1 | GUI planar results have no mesh/visibility eligibility. Nonempty invalid output is not audited before timing. Failed/empty SVG input is stopped later as a missing GPU sample rather than reported directly as an input failure. | Reject failed/empty input and invalid planar output before timing; do not rank it. Every collected numeric sample must carry a matching case/frame ID. |
| P1 | GUI stores completion/failure in mBenchmarkLog, but the new UI never displays that log. A failed/cancelled run can leave partial files with no visible status. | Keep one visible status string, return a structured success/failure result, and mark run completion in metadata. This can replace the unused per-row string log vector. |
| P2 | GUI discards raw samples, retaining only median per orientation for volume and median/mean/stddev for planar. Volume min/max are across orientation medians, not frame variability. | Store one raw sample table plus a summary using the same definitions as CLI. Keep view aggregation distinct from temporal statistics. |
| P2 | GUI methods run in fixed order, no repeated randomized blocks. It uses different camera sweep, render-target format and flat shader from CLI. | Use paired, repeated cases with a fixed workload description. Preserve distinct workload names; do not directly compare GUI FlatRaster with CLI Texture1/Material16. |
| P2 | GUI and CLI CPU columns use different scopes; GUI includes cache/audit/upload, CLI excludes shared reference setup/final audit/upload. Both are single observations. | Name scopes consistently; report cold/cached setup separately if CPU performance is a research claim. Use steady_clock. |
| P2 | CSV identity/provenance is incomplete. GUI has no explicit RunId/CaseId in rows; raw CLI rows link only by Case within a file. No completion status or input mesh hash. Driver/timestamp resolution are absent. Protocol 2 stayed unchanged across interactive shader changes. | One schema/protocol identifier, explicit run/case/frame IDs and a small run manifest containing exact workload/input/build/device settings. Include actual input hash and timer protocol. |
| P2 | 32-bit helper/active counters can overflow with large workload settings. CLI Active is inferred from quad proxy counts; GUI counts active invocations. Analyzer calls their sum ScheduledLaneChange. | Bound workload/count range or flag overflow risk, document different counter definitions, rename to a quad-lane proxy rather than hardware scheduled usage. |
| P2 | Analyzer silently compares only eligible matching subsets. Pair count is recorded, but missing views/blocks can disappear from a headline comparison. | Require/report expected coverage and repeated-block uncertainty. Preserve outliers; flag noisy cases rather than silently trimming them. |
| P2 | Output names are unconstrained: a summary named *.raw.csv or *.metadata.txt can collide with its derived sidecar. Header/row order is maintained manually in several writers. | Validate one output basename; derive distinct paths. Use one typed row/writer with one column definition. |
| P3 | Planar rows are preformatted through a default-precision ostringstream, bypassing the output stream's precision setting. Folder traversal order is unspecified. | Apply precision once in the shared writer; sort case inputs and record the seed. |

## What is already worth keeping

Explicit render state and source-based camera fitting; fixed material settings; integer timestamp subtraction before conversion; warmup and repeated CLI blocks; separate uninstrumented timings and helper diagnostics; raw CLI samples; topology/reference/visibility eligibility; quoted CSV text, source digest and checked output streams. These safeguards are useful. Removing them to reduce lines would make the study worse. The finite geometry audit is still not a certified Hausdorff/self-intersection check.

## Simplification that improves the measurement

There are 721 lines of interactive orchestration and 474 lines of CLI orchestration, plus benchmark state in HelperLaneViz.h. The main duplication is phases, sample collection, per-view aggregation, CSV schemas and metadata. Moving these into still more files would not remove it.

Use a small concrete design:

1. **Case description:** prepared mesh, camera, workload, MSAA and instance settings. Expand folder/configuration/view loops into a simple case list instead of overlapping folder/planar/volume active flags and counters.
2. **One measurement runner:** prepare/warmup/sample/diagnose, with explicit ownership and delayed timestamp collection. GUI drives it once per frame; CLI drives the same measurement core. Geometry preparation can remain separate for existing planar/volume methods.
3. **One result writer:** one raw table, one case summary, one run manifest. Shared statistics computed from retained rows; remove hand-maintained running sums/min/max and the unused per-row UI log vector. Keep one status string for the UI.

Do not add a general telemetry framework, virtual renderer hierarchy, plugin logger or a new configurable profiler UI. Reuse existing Falcor FrameRate for elapsed frame display and existing profiler for detailed investigation after fixing its timer. Prefer one paper benchmark path with the GUI acting as a thin control surface; preserve older isolated measurement as a diagnostic control while validating the new protocol.

## Order of work

First apply/validate the one-line shared timer fix, including debug-layer and alternating-workload tests. Then add clearly labeled elapsed frame display and an enclosing steady scene GPU scope. Replace per-sample waits with deferred collection, share result definitions, validate logs/analyzer and make failure status visible. Finally re-run Source versus Heap3D on the torus and an asymmetric blob with matched shading, 1x/4x MSAA and single/multiple instances. Only after those controls pass should broader matrices be regenerated. The delayed comparison exporter/heap GUI work remains deferred.
