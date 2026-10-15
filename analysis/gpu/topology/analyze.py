"""Fixed-count connectivity / geometry-tolerance tradeoff; preserve all observations.

Identity poses are deduplicated. Eligibility is a practical timing-control gate,
not a significance test. Altered geometry is never called equivalent geometry.
"""
import csv, math, statistics as st
from collections import defaultdict
from pathlib import Path
import subprocess, sys

root = Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).parent
validator = Path(__file__).parent/'../connectivity/analyze.py'
def read(p):
    with p.open(newline='') as f: return list(csv.DictReader(f))
def save(name, rows):
    with (root/name).open('w', newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)

# Shared validator reconstructs every timestamp summary from untrimmed raw data.
subprocess.run([sys.executable,str(validator),str(root)],check=True,
               stdout=(root/'raw_validation.txt').open('w'))
poses=[]; raw_cases=0; raw_samples=0; protocols=set()
fields={'Draw':'GPU_Mean_ms','Pass':'GPU_PassMean_ms','WorldLength':'TotalEdgeLength3D',
        'ScreenLength':'ProjectedEdgeLength','ScreenL1':'ProjectedEdgeL1_px',
        'BBox':'ProjectedBBoxArea_px2','Area':'ProjectedArea_px2','Helpers':'DiagnosticHelpers',
        'Active':'DiagnosticActiveLanes','Cache':'CacheSimACMR', 'Altitude':'MeanMinAltitude_px',
        'Shape':'MeanShapeQuality'}
for path in sorted(root.glob('*.csv')):
    meta=path.with_suffix('.metadata.txt')
    if not meta.exists() or 'Status: Complete' not in meta.read_text(): continue
    rows=read(path); raster={r['Case']:r for r in read(path.with_suffix('.raster.csv'))}
    protocols.add((path.stem.split('_')[0],len({r['Block'] for r in rows}),rows[0]['Warmup'],rows[0]['Samples']))
    raw_cases+=len(rows); raw_samples+=sum(int(r['Samples']) for r in rows)
    by={}; groups=defaultdict(list)
    for r in rows:
        r.update(raster[r['Case']])
        if r.get('PipelineStatisticsAvailable')=='1':
            assert int(r['HardwareInputPrimitives'])==int(r['OutputTriangles'])*int(r['Instances'])
            assert int(r['HardwareVSInvocations'])>0 and int(r['HardwarePSInvocations'])>=0
        by[(r['Geometry'],r['Block'],r['View'],r['MSAA'],r['Method'])]=r
        groups[(r['Geometry'],r['View'],r['MSAA'],r['Method'])].append(r)
    for (geometry,view,msaa,method),rs in groups.items():
        if int(view) in [8,16] and root.name!='all_models' and '--keep-identities' not in sys.argv: continue
        def lookup(r,m): return by[(geometry,r['Block'],view,msaa,m)]
        controls=[100*(float(lookup(r,'SourceControl')['GPU_Mean_ms'])/float(lookup(r,'Source')['GPU_Mean_ms'])-1) for r in rs]
        stable=abs(st.mean(controls))<=2 and max(map(abs,controls))<=3
        valid=all(r['MeshValid']==lookup(r,'Source')['MeshValid']=='1' for r in rs)
        visible=all(float(r['DiagnosticActiveLanes'])>0 and float(lookup(r,'Source')['DiagnosticActiveLanes'])>0 for r in rs)
        p=dict(Run=path.stem,Geometry=geometry,Method=method,View=int(view),MSAA=int(msaa),
               ExperimentClass=rs[0]['ExperimentClass'],SurfaceEquivalent=rs[0]['SurfaceEquivalent']=='1',
               MeshValid=valid,ControlStable=stable,Visible=visible,
               ControlMean_pct=st.mean(controls),ControlMaxAbs_pct=max(map(abs,controls)))
        p['Stage']=path.stem.split('_')[0]
        for n in ['TriangulationStatus','PatchCount','ChangedTriangles','StructuralValid','Samples','Warmup',
                  'HardwareInputPrimitives','HardwareVSInvocations','HardwarePSInvocations','HardwareRasterPrimitives',
                  'PipelineStatisticsAvailable','FailedPatches']:
            p[n]=st.mean(float(r.get(n,0)) for r in rs)
        for n in ['SourceSetupCPU_ms','PatchSetupCPU_ms','ReferenceSetupCPU_ms','ReferenceAuditDirections']:
            p[n]=st.mean(float(r.get(n,0)) for r in rs)
        for n in ['ConnectedComponents','EulerCharacteristic','UniqueEdges','MaxValence','ValenceVariance',
                  'Flips','StarReplacements','SetupCPU_ms','ConstructionCPU_ms','OutputVertices','OutputTriangles']:
            p[n]=st.mean(float(r[n]) for r in rs)
        p['SampledError_pctBounds']=.1*max(float(rs[0]['SampledReferenceError']),float(rs[0]['ReverseSampledReferenceError']))/float(lookup(rs[0],'Source')['ReferenceErrorBudget'])
        p['ForwardError_pctBounds']=.1*float(rs[0]['SampledReferenceError'])/float(lookup(rs[0],'Source')['ReferenceErrorBudget'])
        p['ReverseError_pctBounds']=.1*float(rs[0]['ReverseSampledReferenceError'])/float(lookup(rs[0],'Source')['ReferenceErrorBudget'])
        # Compare star edits with a heap using exactly the same geometric budget.
        peer={'Star3D':'Heap3D','StarLoose':'HeapLoose','StarWide':'HeapWide'}.get(method,'Heap3D')
        p['PeerMethod']=peer
        p['WorldMeshValid']=all(lookup(r,'Heap3D')['MeshValid']=='1' for r in rs)
        p['PeerMeshValid']=all((geometry,r['Block'],view,msaa,peer) in by and lookup(r,peer)['MeshValid']=='1' for r in rs)
        p['CDTMeshValid']=all(lookup(r,'CDT')['MeshValid']=='1' for r in rs) if (geometry,rs[0]['Block'],view,msaa,'CDT') in by else False
        p['CDTSurfaceEquivalent']=all(lookup(r,'CDT')['SurfaceEquivalent']=='1' for r in rs) if p['CDTMeshValid'] else False
        for label,name in [('Source','Source'),('World','Heap3D'),('Peer',peer),('CDT','CDT')]:
            if (geometry,rs[0]['Block'],view,msaa,name) not in by:
                for key in list(fields)+['VS','PS']: p[f'{key}Vs{label}_pct']=''
                for key in ['Draw','Pass']: p[f'{label}{key}_ms']=''
                continue
            for key,column in fields.items():
                changes=[100*(float(r[column])/float(lookup(r,name)[column])-1) for r in rs if float(lookup(r,name)[column])>0]
                p[f'{key}Vs{label}_pct']=st.mean(changes) if len(changes)==len(rs) else ''
                if key in ['Draw','Pass']:
                    p[f'{label}{key}_ms']=st.mean(float(lookup(r,name)[column]) for r in rs)
                    p[f'{key}_ms']=st.mean(float(r[column]) for r in rs)
            for key,column in [('VS','HardwareVSInvocations'),('PS','HardwarePSInvocations')]:
                available=all(r.get('PipelineStatisticsAvailable')=='1' and lookup(r,name).get('PipelineStatisticsAvailable')=='1' for r in rs)
                changes=[100*(float(r[column])/float(lookup(r,name)[column])-1) for r in rs] if available and all(float(lookup(r,name)[column])>0 for r in rs) else []
                p[f'{key}Vs{label}_pct']=st.mean(changes) if changes else ''
        poses.append(p)
save('view_comparisons.csv',poses)

groups=defaultdict(list)
for p in poses: groups[(p['Run'],p['Geometry'],p['Method'],p['MSAA'])].append(p)
summary=[]
for (run,geometry,method,msaa),ps in sorted(groups.items()):
    eligible=[p for p in ps if p['MeshValid'] and p['ControlStable'] and p['Visible']]
    s=dict(Run=run,Geometry=geometry,Method=method,MSAA=msaa,ExperimentClass=ps[0]['ExperimentClass'],
           SurfaceEquivalent=all(p['SurfaceEquivalent'] for p in ps),EligibleConditions=len(eligible),TotalConditions=len(ps),
           InvalidConditions=sum(not p['MeshValid'] for p in ps),UnstableConditions=sum(not p['ControlStable'] for p in ps))
    s['Stage']=ps[0]['Stage']
    strict=[p for p in eligible if p['SurfaceEquivalent']]
    s['StrictEligibleConditions']=len(strict)
    s['DrawVsSourceStrict_pct']=st.mean(p['DrawVsSource_pct'] for p in strict) if strict else ''
    s['PassVsSourceStrict_pct']=st.mean(p['PassVsSource_pct'] for p in strict) if strict else ''
    for n in ['ConnectedComponents','EulerCharacteristic','UniqueEdges','MaxValence','ValenceVariance','Flips','StarReplacements',
              'SetupCPU_ms','ConstructionCPU_ms','OutputVertices','OutputTriangles','SampledError_pctBounds',
              'ForwardError_pctBounds','ReverseError_pctBounds','TriangulationStatus','PatchCount','ChangedTriangles',
              'StructuralValid','Samples','Warmup','HardwareInputPrimitives','HardwareVSInvocations',
              'HardwarePSInvocations','HardwareRasterPrimitives','PipelineStatisticsAvailable','FailedPatches']:
        s[n]=max(p[n] for p in ps) if 'Error_pctBounds' in n or n=='SampledError_pctBounds' else st.mean(p[n] for p in ps)
    for n in ['SourceSetupCPU_ms','PatchSetupCPU_ms','ReferenceSetupCPU_ms','ReferenceAuditDirections']:
        s[n]=st.mean(p[n] for p in ps)
    for label in ['Source','World','Peer','CDT']:
        cohort=[p for p in eligible if label=='Source' or p[f'{label}MeshValid']]
        s[f'{label}EligibleConditions']=len(cohort)
        for n in [f'{key}Vs{label}_pct' for key in list(fields)+['VS','PS']]:
            values=[p[n] for p in cohort if p[n]!='']
            s[n]=st.mean(values) if values and len(values)==len(cohort) else ''
        for scope in ['Draw','Pass']:
            s[f'{scope}Vs{label}Aggregate_pct']=100*(sum(p[f'{scope}_ms'] for p in cohort)/sum(p[f'{label}{scope}_ms'] for p in cohort)-1) if cohort else ''
    strict_cdt=[p for p in strict if p['CDTMeshValid'] and p['CDTSurfaceEquivalent']]
    s['StrictCDTConditions']=len(strict_cdt)
    s['DrawVsCDTStrict_pct']=st.mean(p['DrawVsCDT_pct'] for p in strict_cdt) if strict_cdt else ''
    summary.append(s)
save('summary.csv',summary)
lines=[f'Validated {raw_cases} cases and {raw_samples} untrimmed raw timestamp samples.',
       '1080p; 64 instances; 24 separate-axis conditions (22 unique static poses); 1x/4x MSAA.',
       'Protocols (stage, randomized blocks, warmups, samples): '+str(sorted(protocols)),
       'Queue depth 8; identical meshoptimizer policy. All-model analysis retains all 24 conditions, including lag histories.',
       'Every candidate keeps source vertices, used vertices, triangle count and original lighting normals.',
       'Strict: 0.1% source bounds diagonal; Loose: 0.5%; Wide: 2%; all retain 30-degree normal guards.',
       'Final error is maximum of finite sampled distances in both directions, not a Hausdorff guarantee.',
       'No self-intersection guarantee. Non-equivalent surfaces excluded from generic ValidForRanking.',
       'TopologySplit intentionally changes components and surface: interpret as a changed-workload control.',
       'Eligibility: MeshValid, active coverage, SourceControl <=2% mean and <=3% worst block drift per pose.',
       'Gains vs Source are compared on candidate-eligible poses; World/Peer require their own validity too.',
       'Means of per-pose percentage changes; Aggregate columns additionally report ratio of summed draw/pass times.',
       'Star replacements are larger disk transactions; Flips counts heap operations; no hidden LOD or count change.', '',
       'model | method | MSAA | eligible/total | sampled error % | stars | world length vs source % | draw vs source % | draw vs peer %']
def f(v): return f'{v:+.2f}' if isinstance(v,(float,int)) else 'NA'
for s in summary:
    if s['Method'] not in ['Source','SourceControl']:
        lines.append(f"{s['Geometry']:17} | {s['Method']:13} | {s['MSAA']} | {s['EligibleConditions']:2}/{s['TotalConditions']} | {s['SampledError_pctBounds']:.4f} | {s['StarReplacements']:.0f} | {f(s['WorldLengthVsSource_pct'])} | {f(s['DrawVsSource_pct'])} | {f(s['DrawVsPeer_pct'])}")
(root/'results.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
