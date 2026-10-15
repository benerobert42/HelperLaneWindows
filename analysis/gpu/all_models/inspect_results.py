import csv
from pathlib import Path
root=Path(__file__).parent
with (root/'summary.csv').open(newline='') as f: rows=list(csv.DictReader(f))
columns=['Stage','Geometry','Method','MSAA','StrictEligibleConditions','DrawVsSourceStrict_pct','PassVsSourceStrict_pct',
 'WorldLengthVsSource_pct','ScreenLengthVsSource_pct','HelpersVsSource_pct','BBoxVsSource_pct','VSVsSource_pct','PSVsSource_pct',
 'DrawVsPeer_pct','DrawVsCDTStrict_pct','ConstructionCPU_ms','SetupCPU_ms','StarReplacements','SampledError_pctBounds']
selected=[r for r in rows if r['Stage']!='screen' and r['Method'] not in ['Source','SourceControl']]
with (root/'confirmation_details.txt').open('w') as f:
 f.write(' | '.join(columns)+'\n')
 for r in selected:
  f.write(' | '.join(f'{float(r[c]):+.3f}' if c not in ['Stage','Geometry','Method'] and r[c]!='' else r[c] for c in columns)+'\n')
for g in ['ellipse2d','torus_skew','cylinder_skew','skew2','skew3','skew_saddle','ellipsoid','suzanne','torus']:
 print('\n'+g)
 for r in selected:
  if r['Geometry']==g and (r['Stage']=='confirm' or r['Stage']=='special'):
   print(' | '.join(f'{float(r[c]):+.2f}' if c not in ['Stage','Geometry','Method'] and r[c]!='' else r[c] for c in columns))
