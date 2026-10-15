"""Validate with the shared analyzer, then correlate unique poses within meshes.

Raw samples are never filtered. Correlations use pose/block averages, not frames
as independent observations. Identity repeats are combined; no causal/p-value
claims. Absolute-view correlations and matched connectivity effects stay apart.
"""
import csv
import math
from pathlib import Path
import statistics as st
import subprocess
import sys
from collections import defaultdict

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
subprocess.run([sys.executable, str(Path(__file__).parent.parent/'connectivity/analyze.py'), str(root)], check=True)

def read(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream))

def save(name, rows):
    assert rows, name
    with (root/name).open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

def correlation(x, y):
    if len(x) < 3:
        return ''
    a, b = st.mean(x), st.mean(y)
    xx, yy = sum((v-a)**2 for v in x), sum((v-b)**2 for v in y)
    if xx <= 1e-20 or yy <= 1e-20:
        return ''
    return sum((u-a)*(v-b) for u,v in zip(x,y))/math.sqrt(xx*yy)

def ranks(values):
    positions = defaultdict(list)
    for rank, index in enumerate(sorted(range(len(values)), key=values.__getitem__)):
        positions[values[index]].append(rank)
    return [st.mean(positions[value]) for value in values]

features = ['TotalEdgeLength3D', 'SurfaceArea', 'MeanTriangleArea3D', 'NormalizedEdgeLength3D',
            'NormalizedSurfaceArea', 'ProjectedEdgeLength', 'ProjectedEdgeL1_px', 'MeanTriangleArea_px2',
            'ProjectedBBoxArea_px2', 'BBoxEfficiency', 'MeanMinAltitude_px', 'MeanShapeQuality',
            'SubpixelFraction', 'ThinFraction', 'SliverFraction', 'DiagnosticHelpers',
            'DiagnosticActiveLanes', 'DiagnosticQuads', 'CacheSimACMR']
cases, per_view = [], []
for path in sorted(root.glob('*.csv')):
    if not path.with_suffix('.metadata.txt').exists() or 'Status: Complete' not in path.with_suffix('.metadata.txt').read_text():
        continue
    rows = read(path)
    raster = {r['Case']: r for r in read(path.with_suffix('.raster.csv'))}
    baseline = {(r['Geometry'],r['Block'],r['View'],r['MSAA'],r['Shader']):r for r in rows if r['Method']=='Source'}
    for r in rows:
        r.update(raster[r['Case']])
        r['MeanTriangleArea3D'] = float(r['SurfaceArea'])/int(r['OutputTriangles'])
        r['NormalizedEdgeLength3D'] = float(r['TotalEdgeLength3D'])/float(r['Radius'])
        r['NormalizedSurfaceArea'] = float(r['SurfaceArea'])/float(r['Radius'])**2
        r['MeanTriangleArea_px2'] = float(r['ProjectedArea_px2'])/int(r['ProjectedTriangles'])
        r['BBoxEfficiency'] = float(r['ProjectedArea_px2'])/float(r['ProjectedBBoxArea_px2']) if float(r['ProjectedBBoxArea_px2']) else 0
        for field, count in [('SubpixelFraction','SubpixelTriangles'),('ThinFraction','ThinTriangles'),('SliverFraction','SliverTriangles')]:
            r[field] = float(r[count])/int(r['ProjectedTriangles'])
        r['DiagnosticQuads'] = (float(r['DiagnosticHelpers'])+float(r['DiagnosticActiveLanes']))/4
    for r in rows:
        b = baseline[(r['Geometry'],r['Block'],r['View'],r['MSAA'],r['Shader'])]
        p = {n:r[n] for n in ['Geometry','Method','View','Block','MSAA','Shader','XRotationDegrees','YRotationDegrees','ZRotationDegrees']}
        p.update(Run=path.stem, Height=int(r['Height']), QueueDepth=int(r['QueueDepth']),
                 Samples=int(r['Samples']), Warmup=int(r['Warmup']),
                 Triangles=int(r['OutputTriangles']), Vertices=int(r['OutputVertices']), Instances=int(r['Instances']),
                 Radius=float(r['Radius']), Flips=int(r['Flips']),
                 TotalSceneEdgeLength3D=float(r['TotalSceneEdgeLength3D']),
                 MeshValid=r['MeshValid']==b['MeshValid']=='1', Visible=int(r['DiagnosticActiveLanes'])>0,
                 Draw_ms=float(r['GPU_Mean_ms']), Pass_ms=float(r['GPU_PassMean_ms']),
                 RawDrawCV_pct=100*float(r['GPU_StdDev_ms'])/float(r['GPU_Mean_ms']),
                 DrawChange_pct=100*(float(r['GPU_Mean_ms'])/float(b['GPU_Mean_ms'])-1),
                 PassChange_pct=100*(float(r['GPU_PassMean_ms'])/float(b['GPU_PassMean_ms'])-1))
        for n in features:
            p[n] = float(r[n])
            source = float(b[n])
            p[n+'RelativeChange'] = p[n]/source-1 if source else ''
        cases.append(p)
    # Export every requested view separately. Repeated blocks become one mean.
    groups = defaultdict(list)
    for p in cases:
        if p['Run']==path.stem:
            groups[(p['Geometry'],p['Method'],p['View'],p['MSAA'],p['Shader'])].append(p)
    for ps in groups.values():
        p = dict(ps[0])
        p.pop('Block')
        p['Blocks'] = len(ps)
        p['MeshValid'] = all(q['MeshValid'] for q in ps)
        p['Visible'] = all(q['Visible'] for q in ps)
        for n in ['Draw_ms','Pass_ms','RawDrawCV_pct','DrawChange_pct','PassChange_pct']+features+[n+'RelativeChange' for n in features]:
            values = [q[n] for q in ps if q[n]!='']
            p[n] = st.mean(values) if len(values)==len(ps) else ''
        p['DrawBlockMin_pct'] = min(q['DrawChange_pct'] for q in ps)
        p['DrawBlockMax_pct'] = max(q['DrawChange_pct'] for q in ps)
        per_view.append(p)

# A pose is its actual rotation, not the axis label: three 0-degree views coincide.
def angles(p):
    return tuple(float(p[n])%360 for n in ['XRotationDegrees','YRotationDegrees','ZRotationDegrees'])

control_groups = defaultdict(list)
identity = defaultdict(list)
for p in cases:
    if angles(p)==(0,0,0):
        identity[(p['Run'],p['Geometry'],p['Method'],p['MSAA'])].append(p)
    if p['Method']=='SourceControl':
        control_groups[(p['Run'],p['Geometry'],p['MSAA'],p['Shader'],angles(p))].append(p)
identity_rows=[]
for key,ps in identity.items():
    times=[p['Draw_ms'] for p in ps]
    identity_rows.append(dict(Run=key[0],Geometry=key[1],Method=key[2],MSAA=key[3],RepeatedCases=len(ps),
        DrawMean_ms=st.mean(times),BetweenCaseCV_pct=100*st.pstdev(times)/st.mean(times),
        Range_pct=100*(max(times)-min(times))/st.mean(times),
        MedianWithinCaseCV_pct=st.median(p['RawDrawCV_pct'] for p in ps)))
save('identity_repeats.csv',identity_rows)
poses = defaultdict(list)
for p in per_view:
    poses[(p['Run'],p['Geometry'],p['Method'],p['MSAA'],p['Shader'],angles(p))].append(p)
unique = {}
for key, ps in poses.items():
    p = dict(ps[0])
    p['CombinedViews'] = len(ps)
    for n in ['Draw_ms','Pass_ms','RawDrawCV_pct','DrawChange_pct','PassChange_pct']+features+[n+'RelativeChange' for n in features]:
        p[n] = st.mean(q[n] for q in ps) if all(q[n]!='' for q in ps) else ''
    p['MeshValid'] = all(q['MeshValid'] for q in ps)
    # Guard each pose with its repeated SourceControl results, rather than letting
    # stable other views hide drift in this one. Zero-coverage views remain saved.
    controls = control_groups[(key[0],key[1],key[3],key[4],key[5])]
    blocks = defaultdict(list)
    for q in controls:
        blocks[q['Block']].append(q['DrawChange_pct'])
    p['TimingControlStable'] = bool(controls) and abs(st.mean(q['DrawChange_pct'] for q in controls))<=2 and max(abs(st.mean(v)) for v in blocks.values())<=3
    unique[key] = p
save('unique_poses.csv', list(unique.values()))
for p in per_view:
    p['TimingControlStable'] = unique[(p['Run'],p['Geometry'],p['Method'],p['MSAA'],p['Shader'],angles(p))]['TimingControlStable']
save('orientations.csv', per_view)

groups = defaultdict(list)
for key,p in unique.items():
    if p['MeshValid'] and p['TimingControlStable']:
        groups[key[:5]].append(p)
correlations = []
for key, ps in groups.items():
    for kind in ['AbsoluteView','MatchedConnectivityChange']:
        if kind=='MatchedConnectivityChange' and key[2] in ['Source','SourceControl']:
            continue
        for time in ['Draw','Pass']:
            target = time+'_ms' if kind=='AbsoluteView' else time+'Change_pct'
            for feature in features:
                column = feature if kind=='AbsoluteView' else feature+'RelativeChange'
                selected = [p for p in ps if p[column]!='']
                x,y = [p[column] for p in selected],[p[target] for p in selected]
                correlations.append(dict(Run=key[0],Geometry=key[1],Method=key[2],MSAA=key[3],Shader=key[4],
                                         Kind=kind,Timing=time,Feature=feature,UniquePoses=len(selected),
                                         Pearson=correlation(x,y),Spearman=correlation(ranks(x),ranks(y))))
save('correlations.csv', correlations)
lines = [f'{len(cases)} cases validated; {len(per_view)} view means; {len(unique)} unique pose/method/MSAA/run means.',
         'Correlations average blocks and combine repeated identity rotations. No sample trimming or p-value claims.',
         'Fit within each model/method/MSAA/resolution/queue-depth. Never pool triangle counts or shader regimes.',
         'Per-pose controls: <=2% mean, <=3% maximum block-mean drift. All flagged observations remain saved.',
         'Projected metrics include occluded faces. Diagnostic active/helper/quad counts are instrumented proxies.',
         'AbsoluteView correlations describe changing orientation, not connectivity causation.',
         'MatchedConnectivityChange compares each candidate with Source under the same pose/block/settings.',
         'Undefined correlations (constant metrics or fewer than 3 poses) are blank.', '']
for key, ps in sorted(groups.items()):
    lines.append(f'{key}: {len(ps)} eligible unique poses; mean draw {st.mean(p["Draw_ms"] for p in ps):.6f} ms; '
                 f'mean matched change {st.mean(p["DrawChange_pct"] for p in ps):+.2f}%; '
                 f'pose range [{min(p["DrawChange_pct"] for p in ps):+.2f},{max(p["DrawChange_pct"] for p in ps):+.2f}]%; '
                 f'median raw draw CV {st.median(p["RawDrawCV_pct"] for p in ps):.2f}%.')
    for kind in ['AbsoluteView','MatchedConnectivityChange']:
        chosen = [r for r in correlations if (r['Run'],r['Geometry'],r['Method'],r['MSAA'],r['Shader'])==key and r['Kind']==kind and r['Timing']=='Draw' and r['Spearman']!='']
        best = sorted(chosen,key=lambda r:abs(r['Spearman']),reverse=True)[:4]
        if best:
            lines.append('  '+kind+': '+', '.join(f'{r["Feature"]}: rho={r["Spearman"]:+.2f}, r={r["Pearson"]:+.2f}' for r in best))
(root/'orientation_results.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines[:8]))
