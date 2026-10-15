"""Readable interpretation alongside all raw observations and validation outputs."""
import csv
from pathlib import Path

root=Path(__file__).parent
with (root/'model_summary.csv').open(newline='') as f: rows=list(csv.DictReader(f))
heap={(r['Model'],r['MSAA']):r for r in rows if r['Method']=='Heap3D'}
lines=['20-model connectivity study, final default 1920 x 1080',
       'RTX 4050 Laptop / D3D12 / Release / common GraphEx raster lighting.',
       'Main study: 11,520 cases and 1,474,560 raw timing samples. Including prior resolution/queue controls: 19,584 cases and 1,990,656 samples.',
       '24 requested rotations: 0,45,...315 degrees independently around X, Y and Z; three identity repetitions combine into 22 unique rigid poses.',
       '64 instances; 3 randomized blocks; 256 warmup draws and 128 measured samples per case; query ring depth 8; 1x/4x MSAA.',
       'Every candidate uses the same meshoptimizer cache + overdraw policy. Vertex storage, counts, source normals and lighting stay fixed within each model.',
       'SourceControl is identical geometry independently uploaded. Eligibility: <=2% mean and <=3% maximum block-mean control drift, evaluated per unique pose.',
       'All raw samples remain saved. Invalid/fallback candidates and unstable comparisons remain inspectable but do not enter fits.', '',
       'Mesh-draw GPU timestamps exclude clear, resolve, UI, presentation and diagnostics. Render-pass timestamps include clear and, at 4x, resolve.',
       'These are GPU intervals, not whole interactive application frame times. Means below average matched pose/block ratios; they are not ratios of pooled means.',
       'Projected lengths sum unique geometric edges over all instances. Triangle area, bbox and shape include occluded faces; these runs had no viewport clipping.',
       'Minimum altitude = 2*projected area / longest edge, in pixels. Shape quality = 2*area / longest-edge-squared (equilateral maximum sqrt(3)/2).',
       'Thin means minimum altitude <1px; subpixel means area <1px^2; sliver means shape quality <0.1. These are descriptive thresholds, not GPU hardware rules.',
       'World-area/edge fields and radius-normalized variants are retained; the camera fits each source model, so raw world units alone are not comparable screen footprints.',
       'Helpers, active lanes and quads are counted in a separate instrumented shader. The timed lit shader contains no quad/derivative operations. Counts are coverage proxies, not its actual helper instruction count.',
       'CacheSimACMR is a meshoptimizer simulation, not a measured hardware vertex-invocation counter.', '',
       'model | world edges change | GPU draw change 1x (eligible poses) | GPU draw change 4x (eligible poses)']
for model in sorted({r['Model'] for r in rows}):
    a,b=heap.get((model,'1')),heap.get((model,'4'))
    if not a or not b: continue
    label=Path(model[4:]).stem if model.startswith('obj:') else model
    lines.append(f'{label:18} | {float(a["TotalEdgeLength3DChange_pct"]):+7.2f}% | '
                 f'{float(a["DrawChange_pct"]):+7.2f}% ({a["EligibleUniquePoses"]}/22) | '
                 f'{float(b["DrawChange_pct"]):+7.2f}% ({b["EligibleUniquePoses"]}/22)')
lines += ['',
    'What the observations support',
    'The ellipse baseline has many long, narrow interior triangles. Heap optimization removes 93.1% of world edge length and about 92.8% of projected edge length, with the same vertices, triangles and filled area.',
    'Its mean projected triangle area is unchanged. Diagnostic helpers fall about 88.5%/93.8% (1x/4x); active lanes are unchanged at 1x but fall 65.4% at 4x. GPU draw improves 69.1%/79.3%; whole GPU pass improves 63.3%/51.8%.',
    'Ellipse is a planar polygon rendered through the common lit pipeline; these numbers are not timings for every original 2D shader/backend.',
    'Suzanne offers very little edge-length headroom under the existing fixed-vertex/surface constraints: 578 accepted heap flips shorten total edges only 0.34%. Projected footprint changes are also tiny; no repeatable frame-time benefit is demonstrated.',
    'Bunny, teapot, regular torus, smooth/bumpy/jittered bodies and both GraphEx exports generally show much smaller connectivity effects than the intentionally skewed inputs.',
    'The skewed cylinder loses 28.8% world edge length but is 6.9% slower at 1x and 15.8% faster at 4x. Skew2 loses 50.5%, yet is 7.0% slower at 1x and 14.1% faster at 4x. Edge shortening is not a universal GPU optimization.',
    'For skewed torus, the strict warmed-run average is -8.2% at 1x and -31.7% at 4x, but only 6/22 poses pass the warmed-run 4x control gate. The earlier independent 64-sample run gave about -39.9% over 21/22 poses; do not silently pool those protocols.',
    'Some 4x speedups occur while the diagnostic helper/active counts increase (Skew2 is an example). Raster setup/coverage, attribute/vertex work and shader scheduling are distinct costs; total helpers alone cannot identify the limiting stage.',
    'Small compact triangles can be cheaper than long slivers even if the fraction of subpixel triangles grows. At fixed coverage/count, mean area alone cannot describe the connectivity change.', '',
    'Predictor evaluation',
    'See combination_scores.csv, held_out_predictions.csv and prediction_results.txt. Absolute draw time and matched connectivity gain are fitted separately for 1x and 4x.',
    'Entire geometry families are held out: related round bodies, cylinders, tori, sheets and the two offline GraphEx exports stay together. No individual timing samples are used as independent training observations.',
    'The strongest tested absolute predictor uses estimated vertex work (scene triangle count * CacheSimACMR) together with cumulative projected L1 edge length. Log-ratio RMSE is 0.2206 at 1x and 0.1486 at 4x; triangle count alone gives 0.3248 and 0.4067.',
    'At 4x, triangle count plus projected L1 length alone is nearly as good (0.1517). More terms for area, bbox, thinness and shape often make held-out prediction worse.',
    'Predicting a connectivity gain is harder. At 1x, the tested area-gated projected-edge change plus cache change is best (RMSE 0.3154), but remains a weak general predictor. Ungated length-only models are worse than predicting no change.',
    'At 4x, world-edge change is the best tested simple predictor (0.2116), with projected Euclidean/L1 changes close behind (~0.223). This does not imply world length predicts which orientation is fastest: rigid rotation leaves it constant.',
    'The gate is source mean area/(area+1 pixel^2), not a known rasterizer threshold. Ridge strength is fixed at 0.01 and scaling uses training data only. Feature suites and their rankings are exploratory; validate a chosen score on new models/runs before treating it as a general rule.',
    'CV improvement percentages describe prediction-error reduction, not GPU speedups. Correlated features, one GPU and adaptive clocks prevent causal feature-importance claims.', '',
    'Timing fluctuations',
    'The live profiler shows single-frame values and a 0.98 EMA, rather than the arithmetic mean of a fixed benchmark batch. The headless benchmark disables UI/presentation/profiler work, primes pipelines, warms cases and collects diagnostics after timing.',
    'Falcor Profiler::endFrame waits for the previous frame fence. In helper visualization the shader itself uses quad operations; enabling helper readback adds a synchronous readTextureSubresource call inside the render callback.',
    'That readback can split submission and introduce idle time inside the outer callback timestamp interval. MeshDraw, render callback, profiler total and wall-clock frame time therefore measure different things.',
    'Readback is disabled by default. For comparable interactive values use LitPreview, matching resolution/MSAA/instance count, and the MeshDraw scope.',
    'Longer warmup did not consistently fix drift. Telemetry observed changing core/memory clocks and P0/P3/P4 states; temperature reached 88C during the long model run. The one-second readings are not aligned with individual draw queries, so they cannot identify a per-case causal clock change.',
    'The warmed ellipse 4x Source identity repeats span 33.1% in case means even though median within-case CV is only 0.55%. A smooth-looking batch average can hide between-case drift; retain controls, raw samples and repeated poses.',
    'For publication-quality causal attribution, controlled clock/power conditions and actual raster/vertex/pixel counters would improve the evidence. Prior Nsight timestamp validation was successful, but vendor GPU counters were restricted by ERR_NVGPUCTRPERM. No driver/clock/permission settings were changed.', '',
    'Next research step',
    'Prefer a small view/resolution/MSAA-aware cost, combining a projected boundary/footprint term with a vertex-work safeguard. Keep the existing fixed-source geometric guards and final audit.',
    'Use the current world-length heap as a baseline. Evaluate existing projected objectives on held-out rotations/models; measure before accepting an improvement. Do not assume 20% world-edge reduction guarantees a frame-time win.',
    'Measuring actual non-empty primitives, vertex invocations, pixel invocations and raster-unit load would help explain the 1x regressions and cases where coverage proxies disagree with timing.', '',
    'Implementation and reproducibility',
    '11df218: axis sweep, one shared rotation definition, XYZ case fields, altitude/shape diagnostics, planar ellipse fixture, generic obj:path benchmark input, and 1080p CLI default. Existing Source/CDTFlip/Heap3D implementations are used.',
    '7656537: explicit 3D triangles preserve their corner order directly; the 3D area audit threshold rejects degenerates. This fixes small valid OBJ triangles rejected by polygon projection. Focused small-triangle/winding/collinear regression checks pass.',
    'No new triangulation optimizer, lighting model or runtime implicit importer was introduced. GraphExHeart/GraphExTangleCube are offline 40^3 marching-tetrahedra approximations loaded through the existing OBJ pipeline.',
    'The interrupted original models_lit_1080_q8 files remain saved. completed_models_lit_1080_q8 is an exact copy of its 14 fully completed schedules, with explicit derived-subset provenance; all copied raw samples were revalidated.',
    'Study input hashes, driver version, compiled source digests and original run metadata are in study_manifest.json. Historical revision labels are configure-time bases; source digests identify the actual compiled files. The final VS2022 solution/build was regenerated at HEAD 7656537.',
    'Only CPP/header changes were committed. Scripts, assets, results, README and CMake remain local as requested.', '',
    'From the sample folder:',
    r'& ..\..\..\tools\.packman\cmake\bin\cmake.exe --build ..\..\..\build\windows-vs2022-helperlanes --config Release --target HelperLaneWindows FalcorPython --parallel 6',
    r'& .\analysis\gpu\orientation\run_sweep.ps1',
    'The wrapper defaults to all 20 models and 1080p; output is study_lit_1080_q8.csv plus sidecars. Explicit paths overwrite their previous outputs.',
    'With a Python environment containing numpy/reportlab (the bundled Codex runtime was used here):',
    'python analysis/gpu/orientation/export_graphex.py',
    'python analysis/gpu/orientation/analyze_sweep.py',
    'python analysis/gpu/orientation/model_prediction.py study_lit_1080_q8',
    'python analysis/gpu/orientation/plot_models.py',
    'For the current saved study, model_prediction.py without run arguments selects the three primary completed runs.', '',
    'Primary references for interpretation:',
    'https://learn.microsoft.com/en-us/windows/win32/direct3d11/pixel-shader-stage',
    'https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_7_Wave_Ops_Include_Helper_Lanes.html',
    'https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-system-architecture.html',
    'https://developer.nvidia.com/ERR_NVGPUCTRPERM']
(root/'interpretation.txt').write_text('\n'.join(lines)+'\n')
print('Wrote interpretation.txt with the 20-model table, scope, predictor results and reproduction commands.')
