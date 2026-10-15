> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# UI and live GPU timing review

**Later timing audit:** The GPU values below were collected through a shared Falcor timer now shown to have a buffer-state defect and previous-sample readback. They require revalidation. The source-level scope/readback analysis and UI/capture checks remain applicable. See [TIMING_AUDIT.md](TIMING_AUDIT.md).

Implemented on 2026-10-10. Source changes follow commit `760a72a`; the tested compiled source digest is `3a92cbad12f3f7877fba5d3ae6e4741a02c06c31e249c62c6175f86d1b9364d6`. Native logs and machine-readable verification are in `build/ui-cleanup-*`. Documentation, CMake, assets and generated measurements remain uncommitted as requested.

## Why the GPU time fluctuates

Falcor `SampleApp.cpp` wraps the entire render callback in `/onFrameRender`. Our callback includes target clears, draw, MSAA resolve/blit and any requested synchronous helper-counter readback or PNG capture. During benchmarks it also performs preparation and synchronous query submission. A timestamp interval across a submission/readback can include a queue gap while the CPU waits or processes data. UI is measured separately and presentation is outside the callback. Calling this event "GPU Frame Time" was inaccurate.

Enabling helper counters changes the shader by adding per-pixel atomics; fetching them blocks every frame. We kept that small existing diagnostic path, default off, with an explicit synchronous label. Normal live rendering has separate `MeshDraw`, `HelperReadback` and `ResolveMSAA` scopes. Timed GUI benchmark draws retain their separate GpuTimer with no additional nested profiler scope inside the timed draw.

The sample previously attempted printf formatting through `Gui::text(string, bool sameLine)`. This did not substitute numeric values. Labels now use `fmt::format`. Live timings display Falcor's existing EMA and the last raw sample. The profiler's Average checkbox already defaults on; smoothing is not a performance fix. Its EMA uses sigma 0.98 and retains earlier settings after a mode switch, so let it settle before comparing views. Shader compilation/pipeline creation and first normal-buffer upload are additional transient costs.

Laptop GPU clock/power changes and driver/OS scheduling can also vary timings; NVIDIA describes this behavior in [Advanced API Performance: SetStablePowerState](https://developer.nvidia.com/blog/advanced-api-performance-setstablepowerstate/). Our measurements below isolate the readback effect, not those hardware contributors. No power or driver settings were changed.

## Fixed-view evidence

Release / D3D12 / RTX 4050 Laptop GPU. Suzanne Source Faces, one instance, 1920x1080, 4x MSAA, GUI and profiler enabled. Each phase renders 96 frames; discard 16 and collect 80 previous-frame profiler samples. Raw values, not EMA. One short run; not a broad hardware performance claim.

| View | Callback mean ± population stddev, ms | Callback range, ms | Mesh draw mean, ms |
|---|---:|---:|---:|
| GraphEx lit | 0.257434 ± 0.001239 | 0.256000–0.262144 | 0.064230 |
| Helpers, counters/readback off | 0.197235 ± 0.000499 | 0.196608–0.197632 | 0.064922 |
| Helpers, counters/readback on | 0.877658 ± 0.118802 | 0.722944–1.398780 | 0.344333 |

The readback scope alone has mean 0.382758 ms and stddev 0.118709 ms, nearly all of the callback's variability in the instrumented phase. Its mesh draw stddev is only 0.000555 ms. Thus the larger diagnostic draw cost and variable synchronous readback must be distinguished from normal raster performance. Use counters off and MeshDraw for live comparisons; use warmup/repeated benchmark samples for published performance claims.

## Controls reviewed

| Controls | Decision and reason |
|---|---|
| Geometry, OBJ presets/path/normalization, scale, methods, instance count | Keep: directly affect the experiment or preview. Default Volumetric/Sphere/Source Faces. Source Faces skips unused patch growth. |
| Source resolution | Keep target vertex count; collapse explicit segment counts. Both are useful for controlled source meshes. |
| Patch tolerances | Collapse and show only projected methods. They do nothing for Source Faces. |
| 3D local flip scoring | Show only CDT+Flip/LMT, where the backend uses it. |
| Greedy projection parameters | Collapse; show only GP. LMT/GP explicitly labeled experimental. Existing methods remain unchanged. |
| Mesh statistics and fallback/audit status | Collapse counts; keep failures visible so invalid outputs are not mistaken for wins. |
| Planar/SVG/circle methods and meshoptimizer | Keep: still support existing 2D comparisons. No algorithm removal in this UI task. |
| MSAA and instance grids | Keep 1/2/4/8x; remove unchecked 16x offer. Hide XYZ grid toggle for a single instance. |
| Helper/Wireframe/Lit views | Keep: diagnostic, topology inspection and GraphEx raster appearance respectively. |
| Texture/Synthetic Wave views and screenshot recipes | Remove entirely from the interactive enum, shader branches, setup and selector. GUI timed workload is now internal FlatRaster, recorded explicitly. Old GUI Texture1/SyntheticWave8 measurements are different workloads. Independent CLI PerfShader Texture1/Material16 research workloads are retained. |
| Dummy texture / force helpers toggle | Remove: misleading name and unused texture allocation after removing views. |
| CPU timing checkbox | Remove: always record the last existing construction measurement. Label it geometry rebuild, not isolated algorithm time. |
| Lighting, PNG capture | Keep collapsed. Lighting equations unchanged; capture still one synchronous Falcor call before UI. |
| Custom Win32 Set Window Size | Remove: title/class/foreground lookup could target the wrong window. Normal resizing and exact CLI recipe dimensions suffice. |
| Benchmarks and SVG folder sweep | Keep; controls remain locked during measurement and prior state restored. CSV labels reflect FlatRaster. |

At 1920x1080 the controls start at (16,16), size 680x648, and profiler at (16,676), size 680x388. First-frame placement overrides old saved defaults, then allows moving/resizing. The image in `screenshots/ui-layout.png` verifies actual GUI rendering. Its EMA still reflects the preceding instrumented phase by design.

## Verification

- Release build and `git diff --check` pass. Falcor's preexisting TextRenderer conversion warning remains.
- Native `--self-test` passes, including all three OBJ presets and torus heap invariants.
- `--ui-smoke` passes: 1x/4x volumetric benchmarks, planar benchmark, restoration, lit preview, real GUI rendering and the fixed-view timing phases above.
- Two programmatic lit captures have identical decoded 1920x1080 RGB pixels, also identical to the earlier Suzanne preview. RGB SHA256: `1807be75b0f01c0219afd43079d0fca8f2f41c45674bf40a8d6c4cf0ed336c9d`.
- Removed texture/stress recipes return nonzero and write no PNG.
- CLI perf smoke completes 8 valid Source/Heap3D cases with unchanged Texture1/Material16 workloads. Both outputs have 16384 triangles; heap has the same 20415 flips and 50.7137% edge reduction. This short smoke run validates wiring/geometry; its GPU times are not a new performance conclusion.

See `build/ui-cleanup-verification.json`, `build/ui-cleanup-smoke.log`, and `gpu/smoke/ui-cleanup.metadata.txt` for provenance.
