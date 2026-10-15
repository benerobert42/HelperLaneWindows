"""Matched projected-objective ablation and update-cost accounting.

All 24 axis conditions remain distinct: the three identity poses have different
previous-pose histories for Lag. No frame-level significance or causal claims.
"""
import csv
import math
import statistics as st
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

root = Path(__file__).parent
subprocess.run([sys.executable, str(root.parent/'connectivity/analyze.py'), str(root)], check=True)

def read(path):
    with path.open(newline='') as f:
        return list(csv.DictReader(f))

def save(name, rows):
    assert rows
    with (root/name).open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)

views, source_rows, setup = [], {}, {}
for path in sorted(root.glob('*.csv')):
    metadata = path.with_suffix('.metadata.txt')
    if not metadata.exists() or 'Status: Complete' not in metadata.read_text():
        continue
    rows = read(path)
    raster = {r['Case']: r for r in read(path.with_suffix('.raster.csv'))}
    by_case = {}
    for r in rows:
        r.update(raster[r['Case']])
        by_case[(r['Geometry'], r['Block'], r['View'], r['MSAA'], r['Method'])] = r
        setup[(path.stem,r['Geometry'],r['Method'],r['View'])] = float(r['SetupCPU_ms'])
    groups = defaultdict(list)
    for r in rows:
        groups[(r['Geometry'], r['View'], r['MSAA'], r['Method'])].append(r)
    for (geometry, view, msaa, method), rs in groups.items():
        def lookup(r, name): return by_case[(geometry,r['Block'],view,msaa,name)]
        controls = [100*(float(lookup(r,'SourceControl')['GPU_Mean_ms'])/float(lookup(r,'Source')['GPU_Mean_ms'])-1) for r in rs]
        stable = abs(st.mean(controls)) <= 2 and max(map(abs,controls)) <= 3
        valid = all(r['MeshValid']==lookup(r,'Source')['MeshValid']=='1' for r in rs)
        world_valid = all(lookup(r,'Heap3D')['MeshValid']=='1' for r in rs)
        visible = all(float(r['DiagnosticActiveLanes'])>0 and float(lookup(r,'Source')['DiagnosticActiveLanes'])>0 and
                      float(lookup(r,'Heap3D')['DiagnosticActiveLanes'])>0 for r in rs)
        if method in ['HeapQuadView','HeapQuadLag','HeapQuadWarm','HeapQuadWarmLag']:
            for r in rs:
                lag = method in ['HeapQuadLag','HeapQuadWarmLag']
                expected = int(view)//8*8+(int(view)+7)%8 if lag else int(view)
                assert int(r['OptimizationView']) == expected
                if lag:
                    training_method = 'HeapQuadWarm' if method=='HeapQuadWarmLag' else 'HeapQuadView'
                    trained = by_case[(geometry,r['Block'],str(expected),msaa,training_method)]
                    for n in ['TotalEdgeLength3D','SurfaceArea','Flips','CacheSimACMR']:
                        assert r[n]==trained[n], (geometry,view,n)
                if method=='HeapQuadWarm':
                    world=lookup(r,'Heap3D')
                    assert float(r['ProjectedEdgeL1_px']) <= float(world['ProjectedEdgeL1_px'])*(1+1e-7), (geometry,view)
                    assert r['ConnectivitySeed']=='Heap3D' and r['ObjectiveInstances']==r['Instances']
        v = dict(Run=path.stem, Geometry=geometry, Method=method, View=int(view), MSAA=int(msaa),
                 XRotationDegrees=rs[0]['XRotationDegrees'],YRotationDegrees=rs[0]['YRotationDegrees'],
                 ZRotationDegrees=rs[0]['ZRotationDegrees'],OptimizationView=rs[0]['OptimizationView'],
                 ObjectiveInstances=int(rs[0].get('ObjectiveInstances',1 if method.startswith('HeapQuad') else 0)),
                 ConnectivitySeed=rs[0].get('ConnectivitySeed','Source'),
                 ControlStable=stable,MeshValid=valid,WorldMeshValid=world_valid,Visible=visible,Blocks=len(rs),
                 ControlMean_pct=st.mean(controls),ControlMaxAbs_pct=max(map(abs,controls)))
        for n in ['ConstructionCPU_ms','SetupCPU_ms','ReferenceSetupCPU_ms','GPU_Mean_ms','GPU_PassMean_ms',
                  'TotalEdgeLength3D','ProjectedEdgeLength','ProjectedEdgeL1_px','ProjectedBBoxArea_px2',
                  'DiagnosticHelpers','DiagnosticActiveLanes','CacheSimACMR','Flips','ProjectedArea_px2',
                  'MeanMinAltitude_px','MeanShapeQuality','SubpixelTriangles','ThinTriangles','SliverTriangles']:
            v[n] = st.mean(float(r[n]) for r in rs)
        for label, name in [('Source','Source'),('World','Heap3D')]:
            for field, column in [('Draw','GPU_Mean_ms'),('Pass','GPU_PassMean_ms'),('WorldLength','TotalEdgeLength3D'),
                                  ('ScreenL1','ProjectedEdgeL1_px'),('ScreenLength','ProjectedEdgeLength'),
                                  ('BBox','ProjectedBBoxArea_px2'),('Helpers','DiagnosticHelpers'),('Active','DiagnosticActiveLanes'),
                                  ('Cache','CacheSimACMR'),('Area','ProjectedArea_px2'),('Altitude','MeanMinAltitude_px'),
                                  ('Shape','MeanShapeQuality')]:
                changes = [100*(float(r[column])/float(lookup(r,name)[column])-1) for r in rs if float(lookup(r,name)[column])>0]
                v[f'{field}Vs{label}_pct'] = st.mean(changes) if len(changes)==len(rs) else ''
                if field in ['Draw','Pass']:
                    v[f'{field}SavedVs{label}_ms'] = st.mean(float(lookup(r,name)[column])-float(r[column]) for r in rs)
            if label=='Source':
                source_rows[(path.stem,geometry,int(view),int(msaa))] = lookup(rs[0],name)
        views.append(v)

save('view_comparisons.csv',views)
groups = defaultdict(list)
for v in views:
    if v['ControlStable'] and v['MeshValid'] and v['WorldMeshValid'] and v['Visible']:
        groups[(v['Run'],v['Geometry'],v['Method'],v['MSAA'])].append(v)
summaries=[]
for (run,geometry,method,msaa), vs in sorted(groups.items()):
    s=dict(Run=run,Geometry=geometry,Method=method,MSAA=msaa,ObjectiveInstances=vs[0]['ObjectiveInstances'],
           ConnectivitySeed=vs[0]['ConnectivitySeed'],
           EligibleViewConditions=len(vs),TotalViewConditions=24)
    fields=[n for n in vs[0] if n.endswith('_pct') or n.endswith('_ms')]
    for n in fields:
        values=[v[n] for v in vs if v[n]!='']
        s[n]=st.mean(values) if len(values)==len(vs) else ''
    s['DrawVsWorldMin_pct']=min(v['DrawVsWorld_pct'] for v in vs)
    s['DrawVsWorldMax_pct']=max(v['DrawVsWorld_pct'] for v in vs)
    s['FasterThanWorldConditions']=sum(v['DrawVsWorld_pct'] < -2 for v in vs)
    s['ShorterScreenL1ThanWorldConditions']=sum(v['ScreenL1VsWorld_pct'] < -1 for v in vs)
    for baseline in ['Source','World']:
        for scope in ['Draw','Pass']:
            gain=s[f'{scope}SavedVs{baseline}_ms']
            s[f'LowerBoundBreakEvenVs{baseline}{scope}_frames']=math.ceil(s['SetupCPU_ms']/gain) if gain>0 else ''
    source=source_rows[(run,geometry,vs[0]['View'],msaa)]
    s['Bank24IndexBuffers_MiB']=24*int(source['OutputTriangles'])*3*4/1024**2
    bank_method='HeapQuadWarm' if method.startswith('HeapQuadWarm') else 'HeapQuadView'
    s['Bank24SetupCPU_ms']=sum(value for (r,g,m,v),value in setup.items() if (r,g,m)==(run,geometry,bank_method))
    # Lag is an independently recreated copy of a bank entry for measurement;
    # the checks above require matching geometry/cache summaries. A production
    # bank would reuse that resident entry instead of independently recreating it.
    if method in ['HeapQuadLag','HeapQuadWarmLag']:
        s['LowerBoundBreakEvenVsSourceDraw_frames']=''
        s['LowerBoundBreakEvenVsSourcePass_frames']=''
        s['LowerBoundBreakEvenVsWorldDraw_frames']=''
        s['LowerBoundBreakEvenVsWorldPass_frames']=''
    summaries.append(s)
save('summary.csv',summaries)

lines=['Projected edge objective study: 1080p, 64 instances, 24 axis conditions, 3 blocks, 128 samples, 256 warmup.',
       'Source/SourceControl/Heap3D/HeapQuad(blend)/HeapQuadSet(pure set)/HeapQuadView(pure current)/HeapQuadLag(pure previous).',
       'GPU timestamp intervals; all candidates prepared before timing. Single-view optimization is not inserted into frames.',
       'SourceControl gates: <=2% absolute mean and <=3% maximum block drift per view. All raw observations remain.',
       'Identity rotations are retained per axis, because Lag has different trajectory history; these are 24 conditions, not independent directions.',
       'Initial run projected objectives use the central instance. Full run pure methods average all 64 actual projections; the blended control stays central.',
       'ObjectiveInstances identifies the projection count. All edges, including occluded edges; metrics sum all 64 instances.',
       'Warm methods start at cached Heap3D indices, others at source. All retain the original fixed reference, guards and meshoptimizer policy.',
       'Warm CPU costs exclude cached world seed construction. Warm current-view total L1 is checked to never exceed its world seed.',
       'SetupCPU includes audit/order/buffer creation but excludes shared canonical source/reference, lazy normal/PSO creation and GPU upload completion.',
       'ReferenceSetupCPU is measured once/model. Refreshes can reuse it. CPU times are preparation observations, not repeated microbenchmarks.',
       'Break-even is a lower bound: setup CPU ms / matched GPU ms saved, assuming serial costs. CPU/GPU overlap means this is not an FPS forecast.',
       'All GPU gains refer to a draw of 64 shared-connectivity instances; do not apply these amortization counts to one object without measuring.',
       'Bank storage estimates share positions/normals and count 24 raw 32-bit index buffers; three identities could share an entry.',
       'Lag is a 45-degree stale candidate, not a full camera trajectory or a tested production nearest-direction selector.', '',
       'run | model | method | MSAA | objective instances | eligible/24 | L1 vs world % | draw vs world % | draw vs source % | setup CPU ms | lower-bound frames vs world']
for s in summaries:
    if s['Method'] not in ['Source','SourceControl']:
        lines.append(f"{s['Run']} | {s['Geometry']:15} | {s['Method']:12} | {s['MSAA']} | {s['ObjectiveInstances']} | {s['EligibleViewConditions']:2}/24 | "
                     f"{s['ScreenL1VsWorld_pct']:+7.2f} | {s['DrawVsWorld_pct']:+7.2f} | {s['DrawVsSource_pct']:+7.2f} | "
                     f"{s['SetupCPU_ms']:8.2f} | {s['LowerBoundBreakEvenVsWorldDraw_frames']}")
lines.extend(['',f'Validated {len(views)} model/method/view/MSAA groups.',
              f'Valid groups: {sum(v["MeshValid"] for v in views)}; stable controls: {sum(v["ControlStable"] for v in views)}.',
              'No vendor hardware counters; length/coverage relationships do not establish the limiting GPU stage.'])
(root/'results.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
