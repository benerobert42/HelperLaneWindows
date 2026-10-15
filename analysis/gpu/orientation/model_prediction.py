"""Exploratory feature combinations with entire geometry families held out.

Uses only the 1080p, 128-sample, 256-warmup study. Fits within each MSAA regime;
no random frame split, hyperparameter search, or claim of causal attribution.
"""
import csv
import math
from collections import defaultdict
from pathlib import Path
import statistics as st
import sys
import numpy as np

root=Path(__file__).parent
def read(name):
    with (root/name).open(newline='') as f: return list(csv.DictReader(f))
def save(name,rs):
    with (root/name).open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rs[0])); w.writeheader(); w.writerows(rs)
def family(model):
    if model in ['sphere_dense','jittered','ellipsoid','blob']: return 'RoundBodies'
    if model.startswith('cylinder'): return 'Cylinders'
    if model.startswith('torus'): return 'Tori'
    if model in ['saddle_branch','clustered','planar','folded','skew2','skew_fold']: return 'Sheets'
    if model.startswith('obj:'): return 'GraphExImplicitExports'
    return model
def key(r):
    return tuple(r[n] for n in ['Run','Geometry','MSAA','XRotationDegrees','YRotationDegrees','ZRotationDegrees'])
runs=sys.argv[1:] or ['steady_lit_1080_q8','completed_models_lit_1080_q8','graphex_lit_1080_q8']
all_rows=[r for r in read('unique_poses.csv') if r['Run'] in runs]
assert {r['Run'] for r in all_rows}==set(runs), 'A requested run is absent or unfinished; validate completed runs first.'
model_runs=defaultdict(set)
for r in all_rows: model_runs[r['Geometry']].add(r['Run'])
assert all(len(v)==1 for v in model_runs.values()), 'Select one run per model; do not count repeated poses as new observations.'
base={key(r):r for r in all_rows if r['Method']=='Source'}
rows=[r for r in all_rows if r['Method']!='SourceControl' and r['MeshValid']=='True'
      and r['TimingControlStable']=='True' and r['Visible']=='True']
assert all(r['Height']=='1080' and r['QueueDepth']=='8' and r['Samples']=='128' and r['Warmup']=='256' for r in rows)

# A model gets equal total training weight regardless of surviving pose count.
def weights(rs):
    count=defaultdict(int)
    for r in rs: count[r['Geometry']]+=1
    w=np.array([1/count[r['Geometry']] for r in rs]); return w/w.sum()
def fit_predict(x,y,w,test,delta):
    # Scaling is learned only from training data. Delta fits have no intercept,
    # so unchanged geometry necessarily predicts unchanged time.
    center=np.zeros(x.shape[1]) if delta else np.sum(w[:,None]*x,axis=0)
    scale=np.sqrt(np.sum(w[:,None]*(x-center)**2,axis=0))
    scale=np.where(scale>1e-12,scale,1)
    a=(x-center)/scale; t=(test-center)/scale
    if not delta:
        a=np.column_stack([np.ones(len(a)),a]); t=np.column_stack([np.ones(len(t)),t])
    penalty=np.eye(a.shape[1])*0.01  # fixed before seeing held-out results
    if not delta: penalty[0,0]=0
    beta=np.linalg.solve(a.T@(w[:,None]*a)+penalty,a.T@(w*y))
    return t@beta

def log(v): return math.log(max(v,1e-12))
def features(r,delta):
    b=base[key(r)]
    def ratio(n): return log(float(r[n])/float(b[n]))
    if delta:
        area=float(b['MeanTriangleArea_px2'])
        return dict(World=ratio('TotalEdgeLength3D'),SurfaceArea=ratio('SurfaceArea'),
                    Screen=ratio('ProjectedEdgeLength'),ScreenL1=ratio('ProjectedEdgeL1_px'),
                    BBox=ratio('ProjectedBBoxArea_px2'),Area=ratio('MeanTriangleArea_px2'),
                    Shape=ratio('MeanShapeQuality'),Altitude=ratio('MeanMinAltitude_px'),
                    Thin=float(r['ThinFraction'])-float(b['ThinFraction']),
                    Subpixel=float(r['SubpixelFraction'])-float(b['SubpixelFraction']),
                    Cache=ratio('CacheSimACMR'),Quads=ratio('DiagnosticQuads'),
                    Active=ratio('DiagnosticActiveLanes'),
                    ScreenFootprint=ratio('ProjectedEdgeLength')*area/(area+1))
    return dict(Triangles=log(int(r['Triangles'])*int(r['Instances'])),
                VertexWork=log(int(r['Triangles'])*int(r['Instances'])*float(r['CacheSimACMR'])),
                Vertices=log(int(r['Vertices'])*int(r['Instances'])),
                World=log(float(r['NormalizedEdgeLength3D'])*int(r['Instances'])),
                SurfaceArea=log(float(r['NormalizedSurfaceArea'])*int(r['Instances'])),
                Screen=log(float(r['ProjectedEdgeLength'])),ScreenL1=log(float(r['ProjectedEdgeL1_px'])),
                BBox=log(float(r['ProjectedBBoxArea_px2'])),Area=log(float(r['MeanTriangleArea_px2'])),
                Shape=log(float(r['MeanShapeQuality'])),Altitude=log(float(r['MeanMinAltitude_px'])),
                Thin=float(r['ThinFraction']),Subpixel=float(r['SubpixelFraction']),
                Cache=log(float(r['CacheSimACMR'])),Quads=log(float(r['DiagnosticQuads'])),
                Active=log(float(r['DiagnosticActiveLanes'])))

absolute=[['Triangles'],['Triangles','Vertices','World','SurfaceArea'],
          ['Triangles','Area'],['Triangles','ScreenL1'],['Triangles','BBox'],
          ['Triangles','ScreenL1','Thin','Shape'],
          ['Triangles','Area','ScreenL1','BBox','Thin','Shape','Cache'],
          ['Triangles','Quads'],['Triangles','Active'],['VertexWork','Active'],['VertexWork','ScreenL1'],
          ['Triangles','ScreenL1','Cache'],['Triangles','Quads','Active','Cache']]
changes=[['World'],['Screen'],['ScreenL1'],['ScreenFootprint'],['BBox'],['Quads'],
         ['ScreenL1','Thin','Shape'],['BBox','Thin','Shape'],
         ['ScreenL1','Area','BBox','Thin','Shape','Cache'],
         ['ScreenL1','Area','BBox','Thin','Shape','Cache','Quads','Active'],
         ['Cache'],['ScreenL1','Cache'],['World','Cache'],['Quads','Cache'],['Active','Cache'],['ScreenFootprint','Cache']]
scores, predictions=[],[]
for msaa in ['1','4']:
    for delta in [False,True]:
        rs=[r for r in rows if r['MSAA']==msaa and (not delta or r['Method']!='Source')]
        fs=[features(r,delta) for r in rs]
        y=np.array([math.log1p(float(r['DrawChange_pct'])/100) if delta else log(float(r['Draw_ms'])) for r in rs])
        families=sorted({family(r['Geometry']) for r in rs})
        assert len(families)>=3
        for names in changes if delta else absolute:
            x=np.array([[f[n] for n in names] for f in fs])
            errors, naive_errors=defaultdict(list),defaultdict(list)
            for held in families:
                train=np.array([i for i,r in enumerate(rs) if family(r['Geometry'])!=held])
                test=np.array([i for i,r in enumerate(rs) if family(r['Geometry'])==held])
                w=weights([rs[i] for i in train])
                p=fit_predict(x[train],y[train],w,x[test],delta)
                naive=0 if delta else np.sum(w*y[train])
                for i,v in zip(test,p):
                    errors[rs[i]['Geometry']].append(float((v-y[i])**2))
                    naive_errors[rs[i]['Geometry']].append(float((naive-y[i])**2))
                    predictions.append(dict(Target='ConnectivityChange' if delta else 'AbsoluteDraw',
                        MSAA=msaa,Features='+'.join(names),HeldOutFamily=held,Geometry=rs[i]['Geometry'],
                        Run=rs[i]['Run'],Method=rs[i]['Method'],X=rs[i]['XRotationDegrees'],Y=rs[i]['YRotationDegrees'],Z=rs[i]['ZRotationDegrees'],
                        Actual=math.exp(y[i]),Predicted=math.exp(v)))
            # Equal families, then equal models within a family, then equal poses.
            def macro(es):
                return st.mean(st.mean(st.mean(v) for g,v in es.items() if family(g)==f) for f in families)
            mse,naive_mse=macro(errors),macro(naive_errors)
            scores.append(dict(Target='ConnectivityChange' if delta else 'AbsoluteDraw',MSAA=msaa,
                Features='+'.join(names),InstrumentedProxy=any(n in ['Quads','Active'] for n in names),
                Models=len(errors),HeldOutFamilies=len(families),UniqueObservations=len(rs),
                CV_RMSE_LogRatio=math.sqrt(mse),CV_ImprovementOverNaive=1-mse/naive_mse,
                MedianModelRMSE=st.median(math.sqrt(st.mean(v)) for v in errors.values())))
save('combination_scores.csv',scores); save('held_out_predictions.csv',predictions)

groups=defaultdict(list)
for r in rows: groups[(r['Geometry'],r['Method'],r['MSAA'])].append(r)
summary=[]
for (model,method,msaa),rs in sorted(groups.items()):
    record=dict(Model=model,Family=family(model),Method=method,MSAA=msaa,EligibleUniquePoses=len(rs),
                Triangles=rs[0]['Triangles'],Vertices=rs[0]['Vertices'],
                DrawMean_ms=st.mean(float(r['Draw_ms']) for r in rs),
                DrawChange_pct=st.mean(float(r['DrawChange_pct']) for r in rs),
                DrawChangeMin_pct=min(float(r['DrawChange_pct']) for r in rs),
                DrawChangeMax_pct=max(float(r['DrawChange_pct']) for r in rs),
                PassMean_ms=st.mean(float(r['Pass_ms']) for r in rs),
                PassChange_pct=st.mean(float(r['PassChange_pct']) for r in rs))
    for n in ['TotalEdgeLength3D','SurfaceArea','MeanTriangleArea3D','ProjectedEdgeLength','ProjectedEdgeL1_px',
              'ProjectedBBoxArea_px2','MeanTriangleArea_px2','MeanMinAltitude_px','MeanShapeQuality',
              'DiagnosticHelpers','DiagnosticActiveLanes','DiagnosticQuads','CacheSimACMR']:
        record[n]=st.mean(float(r[n]) for r in rs)
        record[n+'Change_pct']=100*st.mean(float(r[n+'RelativeChange']) for r in rs)
    summary.append(record)
save('model_summary.csv',summary)
lines=['1080p/64 instances/24 axis rotations/3 blocks/128 samples/256 warmup; Source/CDTFlip/Heap3D.',
       'Only stable-control, visible, valid-mesh unique poses. Identity repeats combined.',
       'Two different questions: absolute GPU draw time and matched connectivity time ratio.',
       'CV holds out entire geometry families, with related variants kept in the same fold.',
       'Ridge=0.01 fixed; train-only scaling; equal family/model weights in the reported error.',
       'Naive: training mean log time for absolute prediction; no change for connectivity prediction.',
       'Proxy predictors require a diagnostic raster pass and are not direct geometric flip scores.',
       'Feature comparisons are exploratory; correlated predictors cannot establish causal importance.',
       'One GPU/lighting shader; no significance tests, frame-level train/test split or discarded raw timing outliers.', '']
for target in ['AbsoluteDraw','ConnectivityChange']:
    for msaa in ['1','4']:
        selected=sorted([s for s in scores if s['Target']==target and s['MSAA']==msaa],key=lambda s:s['CV_RMSE_LogRatio'])
        lines.append(f'{target}, {msaa}x: {selected[0]["Models"]} models, {selected[0]["HeldOutFamilies"]} families, {selected[0]["UniqueObservations"]} observations')
        lines += [f'  {s["Features"]}: RMSE log={s["CV_RMSE_LogRatio"]:.4f}; improvement={s["CV_ImprovementOverNaive"]:+.3f}' for s in selected]
(root/'prediction_results.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
