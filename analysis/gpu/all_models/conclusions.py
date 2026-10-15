"""Describe measured mechanisms; do not label a correlation a proven bottleneck."""
import csv,statistics as st
from collections import defaultdict
from pathlib import Path

root=Path(__file__).parent
def read(name):
    with (root/name).open(newline='') as f: return list(csv.DictReader(f))
def save(name,rows):
    with (root/name).open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
def yes(v): return v in ['True','1']
def num(v): return float(v) if v!='' else None
def family(g):
    if g=='ellipse2d': return 'Planar boundary fan'
    if g in ['planar','skew1','skew2','skew3']: return 'Planar grid'
    if g in ['folded','skew_fold']: return 'Folded sheet'
    if g in ['saddle_branch','skew_saddle','jittered_planar','clustered']: return 'Curved/irregular sheet'
    if g in ['sphere','sphere_dense','ellipsoid','ellipsoid_small','torus']: return 'Regular smooth closed surface'
    if g in ['jittered','blob','blob_small']: return 'Perturbed closed surface'
    if g in ['torus_skew','cylinder_skew']: return 'Deliberately skewed grid'
    if g=='cylinder': return 'Corrugated open tube'
    if g.startswith('obj:'): return 'Implicit surface exported through OBJ'
    return 'Imported triangulated model'

poses=read('view_comparisons.csv'); groups=defaultdict(list)
for p in poses:
    groups[(p['Stage'],p['Geometry'],p['MSAA'],p['Method'])].append(p)
mechanisms=[]
for (stage,g,msaa,method),ps in sorted(groups.items()):
    eligible=[p for p in ps if yes(p['MeshValid']) and yes(p['ControlStable']) and yes(p['Visible'])]
    strict=[p for p in eligible if yes(p['SurfaceEquivalent'])]
    cohort=strict if strict else eligible
    out=dict(Stage=stage,Geometry=g,Family=family(g),MSAA=int(msaa),Method=method,
             Eligible=len(eligible),StrictEligible=len(strict),Total=len(ps),
             Fallback=all(float(p['TriangulationStatus'])==2 for p in ps),
             ChangedTrianglesMean=st.mean(float(p['ChangedTriangles']) for p in ps),
             SampledError_pctBounds=max(float(p['SampledError_pctBounds']) for p in ps),
             SetupCPU_ms=st.mean(float(p['SetupCPU_ms']) for p in ps))
    for key in ['Draw','Pass','WorldLength','ScreenLength','ScreenL1','BBox','Area','Helpers','Active','VS','PS']:
        values=[num(p[f'{key}VsSource_pct']) for p in cohort]
        out[f'{key}Change_pct']=st.mean(values) if values and all(v is not None for v in values) else ''
    out['FasterConditions']=sum(float(p['DrawVsSource_pct']) < -3 for p in strict)
    out['SlowerConditions']=sum(float(p['DrawVsSource_pct']) > 3 for p in strict)
    out['BestCondition_pct']=min(float(p['DrawVsSource_pct']) for p in strict) if strict else ''
    out['WorstCondition_pct']=max(float(p['DrawVsSource_pct']) for p in strict) if strict else ''
    if out['Fallback']: why='Source fallback: no new connectivity was produced.'
    elif not cohort: why='No eligible comparison after geometry/coverage/timing controls.'
    elif not strict: why='Surface changed beyond 0.1%: a geometry/performance tradeoff, not equivalent remeshing.'
    elif out['ChangedTrianglesMean']==0: why='No connectivity change: any difference is index arrangement or measurement variation.'
    elif out['DrawChange_pct'] >= -3: why='No material mean draw gain confirmed by the 3% practical threshold.'
    elif out['PSChange_pct']!='' and out['PSChange_pct'] < -3:
        why='Gain accompanies fewer pixel-shader invocations (boundary coverage and/or depth-order work).'
        if out['VSChange_pct']!='' and out['VSChange_pct'] < -3: why+=' Vertex invocation work also falls.'
    elif out['VSChange_pct']!='' and out['VSChange_pct'] < -3:
        why='Gain accompanies fewer vertex-shader invocations: cache/index-order contribution.'
    elif out['BBoxChange_pct'] < -10 or out['HelpersChange_pct'] < -10:
        why='Active shader counts do not explain the gain; lower bbox/quad pressure supports a raster/packing explanation.'
    else: why='Logged counters do not identify the cause; hardware stage/occupancy profiling is required.'
    out['MechanismEvidence']=why
    mechanisms.append(out)
save('mechanisms.csv',mechanisms)

# Screen winners are hypotheses for separate longer confirmation, not final recommendations.
recommend=[]
for g in sorted({p['Geometry'] for p in poses}):
    for msaa in [1,4]:
        tested=[r for r in mechanisms if r['Stage'] in ['confirm','selected','special'] and r['Geometry']==g and r['MSAA']==msaa
                and r['Method'] not in ['Source','SourceControl']]
        # Use the primary confirmation for core methods, not the best repeat.
        tested=[r for r in tested if (r['Stage']=='confirm' if r['Method'] in ['CDT','Heap3D','Star3D']
                                    else r['Stage']=='special' if r['Method'] in ['GridAligned','GridOpposite','TopologySplit']
                                    else r['Stage']=='selected')]
        confirmed=[r for r in tested if r['StrictEligible']>=12 and r['ChangedTrianglesMean']>0]
        winning=[r for r in confirmed if r['DrawChange_pct']!='' and r['DrawChange_pct'] < -3]
        winner=min(winning,key=lambda r:r['DrawChange_pct']) if winning else None
        r=dict(Geometry=g,Family=family(g),MSAA=msaa,
               Recommendation=winner['Method'] if winner else 'Source + meshoptimizer',
               Conclusion=('View-dependent benefit: test intended camera range' if winner and winner['SlowerConditions']>2 else
                           'Offline connectivity preprocessing is worth testing' if winner else
                           'No material strict mean gain confirmed in the tested methods'),
               DrawChange_pct=winner['DrawChange_pct'] if winner else '',PassChange_pct=winner['PassChange_pct'] if winner else '',
               EligibleConditions=winner['StrictEligible'] if winner else 0,
               FasterConditions=winner['FasterConditions'] if winner else 0,SlowerConditions=winner['SlowerConditions'] if winner else 0,
               Mechanism=winner['MechanismEvidence'] if winner else '',
               TestedConfirmationMethods=','.join(sorted({r['Method'] for r in tested})),
               EligibleStrictMethods=','.join(sorted({r['Method'] for r in confirmed})))
        r['SelectionNote']='Lowest observed mean among eligible candidates; small differences are not evidence of algorithm superiority.'
        r['BestTestedStrictCoverage']=max((r['StrictEligible'] for r in tested),default=0)
        recommend.append(r)
save('geometry_recommendations.csv',recommend)

lines=['Triangulation choice by geometry', '',
       '27 named fixtures/models; every general backend/variant screened, plus applicable grid/surgery methods.',
       'Longer core confirmation compares Source/SourceControl/CDT/Heap3D/Star3D.',
       'Counts and source vertex attributes remain fixed. A method must preserve the sampled surface within 0.1% to enter strict recommendations.',
       'Controls: <=2% absolute SourceControl mean and <=3% worst block drift per pose; at least 12 eligible conditions for a model-level recommendation.',
       'Material mean draw gain threshold: 3%; practical calibration rule, not a significance test.',
       'Percentages average matched per-pose ratios. Angle ranges and excluded conditions remain in the CSVs.',
       'No vendor occupancy/stall counters: PS/VS workload effects are measured; the critical GPU stage is not always identified.',
       'Identical meshoptimizer policy does not make every output index sequence identical. Connectivity and resulting cache/depth order can interact.', '',
       'model | MSAA | recommendation | draw change % | eligible | explanation']
for r in recommend:
    change=f"{r['DrawChange_pct']:+.2f}" if r['DrawChange_pct']!='' else 'no confirmed >=3% mean gain'
    coverage=f"{r['EligibleConditions']}/24" if r['DrawChange_pct']!='' else 'NA (no selected gain)'
    lines.append(f"{r['Geometry']} | {r['MSAA']} | {r['Recommendation']} | {change} | {coverage} | {r['Conclusion']}")
(root/'conclusions.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
