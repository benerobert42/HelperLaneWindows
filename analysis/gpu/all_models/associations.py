"""Descriptive associations and held-out geometry tests, not causal attribution."""
import csv
from pathlib import Path
import numpy as np
root=Path(__file__).parent
with (root/'summary.csv').open(newline='') as f: rows=list(csv.DictReader(f))
features=['WorldLengthVsSource_pct','ScreenLengthVsSource_pct','ScreenL1VsSource_pct','BBoxVsSource_pct',
          'HelpersVsSource_pct','PSVsSource_pct','VSVsSource_pct','AltitudeVsSource_pct','ShapeVsSource_pct']
def rank(x):
 order=np.argsort(x,kind='stable'); out=np.empty(len(x),dtype=float); i=0
 while i<len(x):
  j=i+1
  while j<len(x) and x[order[j]]==x[order[i]]: j+=1
  out[order[i:j]]=(i+j-1)/2; i=j
 return out
def corr(a,b):
 return float(np.corrcoef(rank(a),rank(b))[0,1]) if np.std(a)>0 and np.std(b)>0 else float('nan')
lines=['Associations in the primary CDT/Heap3D confirmation only.',
       'Model/method means, at least 12 strict stable view conditions. Each geometry receives total weight 1 in prediction.',
       'Repeated views and Star copies are not independent observations. Correlations below are descriptive, not causal.',
       'Prediction: ridge linear fit with intercept, standardized training features, penalty 1, leave one geometry out.',
       'No model/parameter selection on held-out results. Negative R2 means worse than the weighted global-mean baseline.',
       'One GPU, fixed workload and small geometry set; these are generalization checks, not a validated universal cost model.', '']
for msaa in ['1','4']:
 selected=[r for r in rows if r['Stage']=='confirm' and r['MSAA']==msaa and r['Method'] in ['CDT','Heap3D']
           and int(r['StrictEligibleConditions'])>=12 and float(r['ChangedTriangles'])>0
           and all(r[k]!='' for k in features)]
 lines.append(f'MSAA {msaa}: {len(selected)} geometry/method observations, {len({r["Geometry"] for r in selected})} geometries')
 for exclude in [False,True]:
  cohort=[r for r in selected if not exclude or r['Geometry']!='ellipse2d']
  y=np.array([float(r['DrawVsSourceStrict_pct']) for r in cohort])
  lines.append('Spearman correlations'+(' excluding ellipse2d:' if exclude else ':'))
  for k in features:
   x=np.array([float(r[k]) for r in cohort]); lines.append(f'  {k}: {corr(x,y):+.3f}')
 groups=np.array([r['Geometry'] for r in selected]); y=np.array([float(r['DrawVsSourceStrict_pct']) for r in selected])
 weights=np.array([1/sum(groups==g) for g in groups])
 for names in [['ScreenLengthVsSource_pct'],['PSVsSource_pct','VSVsSource_pct'],
               ['ScreenLengthVsSource_pct','BBoxVsSource_pct','HelpersVsSource_pct'],
               ['ScreenLengthVsSource_pct','BBoxVsSource_pct','HelpersVsSource_pct','PSVsSource_pct','VSVsSource_pct']]:
  x=np.array([[float(r[k]) for k in names] for r in selected]); pred=np.zeros(len(y))
  for g in set(groups):
   train=groups!=g; test=~train; w=weights[train]; mean=np.average(x[train],axis=0,weights=w)
   scale=np.sqrt(np.average((x[train]-mean)**2,axis=0,weights=w)); scale[scale<1e-8]=1
   a=np.column_stack([np.ones(sum(train)),(x[train]-mean)/scale]); penalty=np.eye(a.shape[1]); penalty[0,0]=0
   beta=np.linalg.solve(a.T@(w[:,None]*a)+penalty,a.T@(w*y[train]))
   pred[test]=np.column_stack([np.ones(sum(test)),(x[test]-mean)/scale])@beta
  sse=np.sum(weights*(y-pred)**2); sst=np.sum(weights*(y-np.average(y,weights=weights))**2)
  lines.append(f'Held-out {" + ".join(names)}: R2={1-sse/sst:+.3f}, RMSE={np.sqrt(sse/np.sum(weights)):.3f} percentage points')
 lines.append('')
(root/'associations.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
