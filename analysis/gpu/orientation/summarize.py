"""Compact descriptive summaries, restricted to valid meshes and stable controls."""
import csv
import statistics as st
from pathlib import Path

root = Path(__file__).parent
with (root/'unique_poses.csv').open(newline='') as f:
    all_rows = list(csv.DictReader(f))
rows = [r for r in all_rows if r['MeshValid']=='True' and r['TimingControlStable']=='True']
def mean(rs, field):
    return st.mean(float(r[field]) for r in rs)

lines = ['Eligible unique poses; changes are averages of matched pose/block ratios, not ratios of pooled means.',
         'World/screen edges count unique edges; helper and active lanes are separate diagnostic shader proxies.', '']
for run in ['steady_lit_1080_q8','lit_1080_q8','lit_360_q8','lit_2160_q8']:
    for model in ['ellipse2d','ellipsoid','suzanne','torus_skew']:
        for method in ['CDTFlip','Heap3D']:
            for msaa in ['1','4']:
                rs = [r for r in rows if (r['Run'],r['Geometry'],r['Method'],r['MSAA'])==(run,model,method,msaa)]
                if not rs: continue
                lines.append(f'{run} {model} {method} {msaa}x: {len(rs)}/22 poses; '
                             f'{rs[0]["Triangles"]} triangles; {rs[0]["Flips"]} flips; '
                             f'draw {mean(rs,"Draw_ms"):.6f} ms ({mean(rs,"DrawChange_pct"):+.2f}%); '
                             f'pass {mean(rs,"Pass_ms"):.6f} ms ({mean(rs,"PassChange_pct"):+.2f}%); '
                             + '; '.join(f'{n} {100*mean(rs,n+"RelativeChange"):+.2f}%'
                               for n in ['TotalEdgeLength3D','ProjectedEdgeLength','ProjectedBBoxArea_px2',
                                         'MeanTriangleArea_px2','MeanMinAltitude_px','MeanShapeQuality',
                                         'DiagnosticHelpers','DiagnosticActiveLanes','DiagnosticQuads','CacheSimACMR']))

lines += ['', 'Queue depth control (Source, common eligible poses only; separate runs, clocks not controlled):']
def pose(r):
    return tuple(r[n] for n in ['XRotationDegrees','YRotationDegrees','ZRotationDegrees'])
for model in ['ellipse2d','ellipsoid','suzanne','torus_skew']:
    for msaa in ['1','4']:
        rs = [[r for r in rows if (r['Run'],r['Geometry'],r['Method'],r['MSAA'])==(run,model,'Source',msaa)]
              for run in ['lit_1080_q1','lit_1080_q8']]
        common = set(map(pose,rs[0])) & set(map(pose,rs[1]))
        rs = [[r for r in group if pose(r) in common] for group in rs]
        lines.append(f'{model} {msaa}x {len(common)} common poses: ' + '; '.join(
            f'q{q}: median within-case CV {st.median(float(r["RawDrawCV_pct"]) for r in group):.2f}%, '
            f'mean draw {mean(group,"Draw_ms"):.6f} ms' for q,group in zip([1,8],rs)))
(root/'summary.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
