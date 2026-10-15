"""Freeze hypotheses from the independent full-method screen for longer confirmation."""
import csv
from pathlib import Path
from collections import defaultdict
root=Path(__file__).parent
with (root/'summary.csv').open(newline='') as f: rows=list(csv.DictReader(f))
groups=defaultdict(list)
core=['Source','SourceControl','CDT','Heap3D','Star3D']
for r in rows:
    if r['Stage']=='screen' and r['Method'] not in core and int(r['StrictEligibleConditions'])>=12 and r['DrawVsSourceStrict_pct']!='':
        groups[(r['Geometry'],r['MSAA'])].append(r)
chosen=defaultdict(set)
for (g,msaa),rs in groups.items():
    best=min(rs,key=lambda r:float(r['DrawVsSourceStrict_pct']))
    if float(best['DrawVsSourceStrict_pct']) < -3: chosen[g].add(best['Method'])
with (root/'confirmation_plan.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=['Geometry','Methods']);w.writeheader()
    for g,methods in sorted(chosen.items()): w.writerow({'Geometry':g,'Methods':','.join(core+sorted(methods))})
print('Selected extra methods for',len(chosen),'geometries.')
