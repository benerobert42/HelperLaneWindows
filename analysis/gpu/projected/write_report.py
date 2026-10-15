"""Self-contained explanation with measured marginal gains and update costs."""
import csv
import hashlib
import json
import subprocess
from pathlib import Path

root=Path(__file__).parent
def read(name):
    with (root/name).open(newline='') as f: return list(csv.DictReader(f))
rows=read('summary.csv')
main='projected_full_lit_1080_q8'
selected={(r['Geometry'],r['Method'],r['MSAA']):r for r in rows if r['Run']==main}
lines=[
    'Projected edge length, view dependence and whether rebuilding pays off', '',
    'The earlier correlation described absolute draw cost across meshes. It did not establish that minimizing projected length always speeds up a fixed mesh.',
    'We now directly compare the objectives against the existing Heap3D result, with unchanged vertices/counts, original source normals, lighting and meshoptimizer policy.',
    '10,080 new cases / 1,290,240 raw samples: central-instance ablation 6 models, then full-instance comparison 4 genuinely volumetric models.',
    '1920x1080, 64 instances, 24 separate-axis conditions, 1x/4x MSAA, 3 randomized blocks, 256 warmup draws and 128 samples per case.',
    'The requested 24 axis conditions are not uniform samples of all 3D orientations. Identity poses are retained by axis because stale candidates have different histories.',
    'All 3,360 model/method/view/MSAA groups passed mesh validity. Controls were stable in 2,989 groups. All raw data remain; no timing outliers were trimmed.', '',
    'Methods',
    'Heap3D: existing world-length max heap, baseline.',
    'HeapQuad: existing 25% world + 75% central-instance projected L1 blend, tested in the first ablation.',
    'HeapQuadSet: pure expected projected L1 length over the fixed view set. The full run sums all actual instance projections.',
    'HeapQuadView: pure current-view projected L1, starting from source.',
    'HeapQuadWarm: same current-view objective, starting from cached prepared Heap3D indices. The original reference and geometry guards remain active.',
    'HeapQuadLag / HeapQuadWarmLag: independently recreated previous-axis-pose candidate measured 45 degrees away. Edge length, area, flip count and cache summaries must match its training candidate.',
    'Every full-run warm candidate is checked to never increase total scene projected L1 relative to Heap3D at its training view, including comparisons excluded for timing drift.',
    'Variants are all prepared before GPU timing; there is no per-frame optimizer in the app. Projected objectives include occluded edges and ignore depth visibility.', '',
    'Full-run marginal changes versus Heap3D; negative means less/faster',
    'model | L1 reduction at eligible 4x poses | draw 1x | draw 4x | 4x poses | warm setup CPU | 45-degree stale draw 4x'
]
for geometry in ['ellipsoid','cylinder_skew','torus_skew','suzanne']:
    a=selected[(geometry,'HeapQuadWarm','1')]; b=selected[(geometry,'HeapQuadWarm','4')]
    lag=selected[(geometry,'HeapQuadWarmLag','4')]
    lines.append(f"{geometry:14} | {float(b['ScreenL1VsWorld_pct']):+7.2f}% | {float(a['DrawVsWorld_pct']):+6.2f}% | "
                 f"{float(b['DrawVsWorld_pct']):+6.2f}% | {b['EligibleViewConditions']}/24 | {float(b['SetupCPU_ms']):7.2f}ms | "
                 f"{float(lag['DrawVsWorld_pct']):+6.2f}%")
lines.extend(['',
    'The source-initialized current-view objective produced similarly small marginal gains. Averaging the whole view set added under 1% average draw benefit in these full-run contexts.',
    'Warm current-view reductions averaged 2-7% beyond Heap3D; none reached 20% in total projected L1 at an individual full-run pose. Large source-to-optimized reductions already come primarily from Heap3D.',
    'For example, skewed-torus 4x improves 35.4% from source with Heap3D and 37.4% with the warm projected objective. Suzanne improves about 2.2% from source, rather than a large gain.',
    'At 1x, warm changes for cylinder/torus/Suzanne are small regressions; ellipsoid has a small gain. These few-percent effects are exploratory on one laptop GPU with changing clocks, not universal guarantees.',
    'Helper changes do not consistently explain the extra gains. Ellipsoid warm 4x helpers are almost unchanged and diagnostic active lanes increase 3.0%, while draw improves 2.7%.',
    'Diagnostic helper/active counts come from an instrumented shader. The timed reviewed lit shader contains no derivatives/quad operations. Counts are coverage proxies, not its literal helper ALU workload.', '',
    'Update cost and amortization',
    'ConstructionCPU covers projection/score preparation, heap work and per-flip reference checks. SetupCPU additionally includes full final audits, index ordering and VAO/buffer creation.',
    'SetupCPU still excludes shared canonical-source/reference setup, cached World seed setup, lighting initialization and GPU upload completion. It is a lower bound on this current rebuild path, not on every possible optimized implementation.',
    'The final sampled reference audit repeats closest-surface queries for vertices, edge midpoints and triangle centroids. This is a substantial part of current CPU cost.',
    'Warm starts and edge-score memoization reduce repeated work, but preserve all audits. No optimization algorithm or safeguard was removed.',
    'A serial cost accounting requires N > update_ms / GPU_ms_saved. It measures cumulative time saved, not actual application FPS; CPU/GPU overlap and other frame work matter.',
    'The following use the 64-instance draw and a constant current-view benefit; moving views generally weaken this assumption.',
    'model | extra GPU draw saving 4x | lower-bound frames per rebuild | at illustrative 60fps | 24 warm index buffers | bank CPU preprocessing'
])
for geometry in ['ellipsoid','cylinder_skew','torus_skew','suzanne']:
    r=selected[(geometry,'HeapQuadWarm','4')]
    frames=int(r['LowerBoundBreakEvenVsWorldDraw_frames'])
    lines.append(f"{geometry:14} | {1000*float(r['DrawSavedVsWorld_ms']):6.2f}us | {frames:7} | {frames/3600:5.1f}min | "
                 f"{float(r['Bank24IndexBuffers_MiB']):5.2f}MiB | {float(r['Bank24SetupCPU_ms'])/1000:5.2f}s")
lines.extend(['',
    'A cached resident bank avoids runtime rebuilding and streaming uploads. These raw storage estimates assume shared position/normal buffers and 24 index buffers; repeated identity poses can share an entry.',
    'This study has not implemented a production selector. A 45-degree lag is not nearest-bank selection: a bank sampled every 45 degrees on one axis has a nearest-pose error up to 22.5 degrees. Finer offsets and compound rotations still need validation.',
    'The 24 axis conditions do not cover all combined 3D rotations. A production selector needs object-to-camera direction and roll, appropriate distance regimes, conservative fallback to World and hysteresis.', '',
    'Why length alone is insufficient',
    'Changing the internal diagonal of a planar quad leaves its area/silhouette and submitted vertex/triangle counts unchanged, but changes which primitive owns pixels near that diagonal.',
    'At 1x, total covered pixel centers of an unchanged opaque surface are approximately fixed. Packet occupancy, non-empty primitive count, vertex reuse, overdraw order and raster setup can still change.',
    'At MSAA boundaries, different triangles can cover disjoint samples of the same pixel. Pixel-frequency shading can therefore run separately for the different primitives. Reducing internal boundaries can reduce that duplication.',
    'Length is a boundary proxy, not a count of actual visible shader work. Hidden faces, lattice alignment, area distribution and very small triangles weaken it.',
    'GPU stages overlap: reducing boundary work may not shorten the draw if vertex/primitive or memory work determines throughput. Meshoptimizer uses the same policy, but connectivity changes its resulting index order and simulated cache rate.',
    'The exact limiting stage is not proven by the current proxy counts. Actual VS/PS invocation queries and raster hardware counters are the next useful measurements.', '',
    'Two mathematical limits of a pure length objective',
    'If resolution changes uniformly by s, all projected edge lengths multiply by s. A pure objective chooses the same connectivity; normalization used here cancels s entirely.',
    'MSAA does not change vertex projections either. Thus a pure length score cannot represent the observed resolution/MSAA-dependent sign changes in GPU benefit.',
    'For a world edge of length L under uniformly random orthographic rotations with fixed pixel scale k:',
    'E[projected Euclidean length] = k*pi*L/4; E[projected L1 length] = k*L.',
    'For L1, each screen-axis component has E[absolute component]=L/2. For Euclidean length, integrate L*sqrt(1-u^2) for uniform u in [-1,1]. Linearity extends both identities to total edge length.',
    'Therefore optimizing broadly averaged linear projected length is equivalent to world length in that idealized limit. Our perspective camera and nonuniform 24-view set are exceptions; the identity is an explanation, not an exact description of the measured camera.',
    'View-restricted optimization, visibility and nonlinear pixel occupancy can break this equivalence.', '',
    'Concrete illustration',
    'quad_example.svg/png shows one exactly planar quad where choosing the screen-shorter diagonal cuts total projected Euclidean length 19.4% while increasing total world length 21.2%. Both choices have the same projected area.',
    'The preferred diagonal reverses as the camera becomes face-on. This illustration has no measured or simulated GPU timings.', '',
    'Most promising next direction',
    'Keep the World result as a robust baseline. Test small offline view-specific banks only for model/view/MSAA regimes with demonstrated extra gain; do not rebuild on ordinary camera motion.',
    'Prioritize actual raster occupancy: shader invocations per primitive/pixel, non-empty triangles, pixel-scale shape distributions and actual vertex invocations. D3D12 standard pipeline statistics can expose VS/PS counts without modifying the reviewed lighting shader; availability and counter interpretation should be verified on this machine.',
    'Only then test a nonlinear triangle/quad cost tied to pixel area and MSAA. A length-only edge callback cannot express two-triangle occupancy; the existing heap can be retained with a local quad/triangle-cost callback.',
    'Retaining adjacency and exact fixed-source error certificates could reduce repeated audits for future incremental updates. That is more involved than a bank; present gains do not justify introducing a complex per-frame optimizer.', '',
    'Code and reproduction',
    '0c58447: fixed/pure/current/stale projected objectives and CPU setup scopes, retaining the existing heap and reviewed renderer.',
    '4aa97ac: all-instance objectives, edge-score cache, cached-world warm starts and explicit projection/seed provenance. Only CPP/header files were committed.',
    'Release builds, geometry regression checks and both smoke matrices passed. The full-run warm L1 monotonicity and stale/training summary consistency were verified for every case.',
    'The first study uses 0c58447 and the full study uses 4aa97ac. Compiled source digests and original settings are preserved in each metadata sidecar.',
    'An early aborted run launched during compilation is preserved separately and excluded; both final matrices ran after their builds had finished.',
    'No UI mode, lighting shader, new core triangulation backend or runtime model importer was added.', '',
    'From the sample folder:',
    r'& .\analysis\gpu\projected\run_study.ps1',
    'python analysis/gpu/projected/analyze.py',
    'python analysis/gpu/projected/plot.py',
    'Default wrapper: four volumetric models, 1080p, actual full-instance scores, cold/warm current candidates and warm 45-degree stale candidates.', '',
    'References:',
    'https://learn.microsoft.com/en-gb/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules',
    'https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-system-architecture.html',
    'https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_query_data_pipeline_statistics'
])
(root/'interpretation.txt').write_text('\n'.join(lines)+'\n')
sample_root=root.parents[2]
paths=[sample_root/'Experiments'/name for name in ['Benchmark.cpp','Benchmark.h','BenchmarkOptions.cpp','BenchmarkOptions.h','PerformanceBenchmark.cpp']]
paths+=list(root.glob('*.py'))+[root/'run_study.ps1',root.parent/'orientation/run_sweep.ps1']
manifest=dict(SampleCommit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=sample_root,text=True).strip(),
              Files={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
              RunMetadata={p.name:p.read_text() for p in root.glob('*.metadata.txt')})
telemetry={}
for p in root.glob('telemetry*.csv'):
    rs=read(p.name)
    def numbers(prefix):
        name=next(n for n in rs[0] if n.strip().startswith(prefix))
        return [float(r[name].strip().split()[0]) for r in rs if r.get(name) and r[name].strip().split()[0] not in ['[N/A]','N/A']]
    telemetry[p.name]=dict(Rows=len(rs),TemperatureRange_C=[min(numbers('temperature')),max(numbers('temperature'))],
                           CoreRange_MHz=[min(numbers('clocks.current.graphics')),max(numbers('clocks.current.graphics'))],
                           MemoryRange_MHz=[min(numbers('clocks.current.memory')),max(numbers('clocks.current.memory'))],
                           PowerReadingsAbove150W=sum(v>150 for v in numbers('power.draw')),
                           Note='One-second samples are not aligned with draw queries. Implausible idle power readings remain preserved and are not used for performance attribution.')
manifest['Telemetry']=telemetry
(root/'study_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Wrote interpretation.txt and study_manifest.json.')
