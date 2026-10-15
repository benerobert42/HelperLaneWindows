"""Reuse timestamp validation and matched comparison analysis; report five new meshes."""
from pathlib import Path
import csv,subprocess,sys,json,hashlib

root=Path(__file__).parent
subprocess.run([sys.executable,str(root/'../topology/analyze.py'),str(root),'--keep-identities'],
               check=True,stdout=(root/'comparison_log.txt').open('w'))
def read(name):
 with (root/name).open(newline='') as f: return list(csv.DictReader(f))
rows=read('summary.csv'); models=json.loads((root/'models.json').read_text())
def name(r): return Path(r['Geometry'].removeprefix('obj:')).stem
def number(r,key): return f"{float(r[key]):+.2f}" if r[key]!='' else 'NA'
table=[]
for m in models:
 for msaa in ['1','4']:
  tested=[r for r in rows if name(r)==m['Name'] and r['MSAA']==msaa and r['Stage']=='confirm'
          and r['Method'] not in ['Source','SourceControl']]
  eligible=[r for r in tested if int(r['StrictEligibleConditions'])>=12 and float(r['ChangedTriangles'])>0]
  winning=[r for r in eligible if float(r['DrawVsSourceStrict_pct']) < -3]
  best=min(winning,key=lambda r:float(r['DrawVsSourceStrict_pct'])) if winning else None
  out=dict(Model=m['Name'],MSAA=int(msaa),Method=best['Method'] if best else 'No confirmed >=3% gain',
           EligibleConditions=best['StrictEligibleConditions'] if best else 0)
  for k in ['DrawVsSourceStrict_pct','PassVsSourceStrict_pct','WorldLengthVsSource_pct','ScreenLengthVsSource_pct',
            'HelpersVsSource_pct','PSVsSource_pct','VSVsSource_pct','SetupCPU_ms','SampledError_pctBounds']:
   out[k]=best[k] if best else ''
  table.append(out)
with (root/'five_model_results.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(table[0]));w.writeheader();w.writerows(table)
lines=['Five new 3D connectivity candidates','',
       '1920x1080, reviewed raster lit shader, 24 separate-axis conditions (22 unique static poses), 1x/4x MSAA.',
       'Screen/confirm/control: 64 instances. Large: one object, a separate projected-footprint workload with fixed counts within every method comparison.',
       'Screen: 2 randomized blocks, 64 warmups, 32 raw samples. Confirmation and controls: 3 blocks, 256 warmups, 128 samples.',
       'All methods keep source vertex positions/storage/order, used vertices and triangle count. Uniform meshoptimizer cache/overdraw policy.',
       'Strict comparisons require structural validity and <=0.1% source bounds diagonal sampled error in BOTH directions.',
       'Not a continuous Hausdorff, analytic-surface-error or self-intersection guarantee. Surface-changing and structurally invalid results are excluded.',
       'Per-pose SourceControl gate: <=2% mean and <=3% worst-block draw drift. At least 12 eligible conditions; 3% practical gain threshold.',
       'No raw timing samples removed. Draw excludes clear/resolve/UI/present; pass includes clear/draw/resolve, not the entire application frame.',
       'Laptop clocks are unlocked and recorded. SourceControl gates mitigate temporal drift, but do not isolate workload-dependent DVFS.',
       'Counter queries and diagnostic helper shader run separately after timing. Helpers are a proxy, not a direct count of wasted lit-shader ALU.',
       'Lowest observed candidate mean is reported; small differences do not establish superiority between algorithms.', '',
       'Models and intentionally introduced source defects']
for m in models: lines.append(f"{m['Name']}: {m['Vertices']} vertices, {m['Triangles']} triangles. {m['Description']}")
lines += ['', 'These were chosen to contain improvable connectivity; their gains do not estimate average production-asset gains.',
          'Panel source/control pairs preserve the exact planar surface and the same vertex list. Balanced controls use short-ear triangulation with the same interior vertex reinserted.',
          'Knot source/control share vertices and topology, but their piecewise-planar sampled surfaces differ. Ramp control only swaps cell diagonals; it is not a globally optimized control.',
          'Each control run fixes normals from its own source. Do not compare absolute source/control GPU times as a pure connectivity effect without accounting for that attribute difference.',
          'The numerical area-guard fix is recorded in the current source revision. Earlier 27-model results retain their own older measured-source snapshots.', '',
          'Confirmed best observed strict results',
          'model | MSAA | method | eligible/24 | draw % | enclosing pass % | world edge % | projected edge % | diagnostic helpers % | PS % | VS %']
for r in table:
 lines.append(f"{r['Model']} | {r['MSAA']} | {r['Method']} | {r['EligibleConditions']}/24 | "+
              ' | '.join(number(r,k) for k in ['DrawVsSourceStrict_pct','PassVsSourceStrict_pct','WorldLengthVsSource_pct',
                                              'ScreenLengthVsSource_pct','HelpersVsSource_pct','PSVsSource_pct','VSVsSource_pct']))
lines += ['', 'Interpretation',
          'All five deliberately poor source triangulations admit >20% world-edge reduction with a valid static method. At 64 instances/4x, the static heap improves draw time by ~70% wing, ~41% wheel, ~40% arch, ~11% knot and ~27% ramp.',
          'The wing requires the plane-penalized heap: plain Heap3D is rejected by the reverse sampled-distance audit. Its ~82% edge reduction is retained by HeapPlane.',
          'On balanced/short-diagonal controls, static 4x gains shrink to ~4% wing, ~4% wheel, ~8% arch, <1% knot and ~4% ramp. These are comparisons against each control\'s own source, with its own fixed normals.',
          'Thus the large gains are principally repairs to deliberately inefficient connectivity. The shapes themselves do not guarantee similar gains on well-triangulated assets.',
          'The 64-instance knot/ramp world heaps regress at 1x (~4%/~1%). With one larger object they improve 1x draw time by ~13%/~16%. Instance count also changes primitive load and lighting directions; this is a workload sensitivity check, not an isolated causal scale experiment.',
          'The one-object wing has only 11/24 stable conditions at 1x and 6/24 at 4x, below the predefined 12-condition threshold. Its numerical means remain recorded but are exploratory, not accepted performance conclusions.',
          'Wing/arch/wheel gains accompany large bbox/boundary reductions, and substantial 4x PS invocation savings. Some 1x gains occur despite more PS/VS invocations. The exact limiting raster/scheduling unit remains unproven without vendor counters.', '',
          'Static method summary: plane-penalized heap for wing, world heap for the other meshes',
          'model | objects | MSAA | strict eligible/24 | interpretation | draw % | enclosing pass % | projected edges % | helpers % | PS % | VS %']
for m in models:
 method='HeapPlane' if m['Name']=='SweptWing' else 'Heap3D'
 for stage,instances in [('confirm',64),('large',1)]:
  for msaa in ['1','4']:
   r=next((r for r in rows if name(r)==m['Name'] and r['Stage']==stage and r['Method']==method and r['MSAA']==msaa),None)
   if not r: continue
   status='accepted' if int(r['StrictEligibleConditions'])>=12 else 'exploratory; insufficient stable conditions'
   lines.append(f"{m['Name']} | {instances} | {msaa} | {r['StrictEligibleConditions']}/24 | {status} | "+
                ' | '.join(number(r,k) for k in ['DrawVsSourceStrict_pct','PassVsSourceStrict_pct','ScreenLengthVsSource_pct',
                                                'HelpersVsSource_pct','PSVsSource_pct','VSVsSource_pct']))
lines += ['', 'Every longer comparison (including balanced/short-diagonal controls)',
          'stage/model | method | MSAA | strict eligible/24 | draw % | pass % | error % of bounds | construction CPU ms | setup CPU ms | changed triangles | status']
for r in rows:
 if r['Stage']=='screen' or r['Method'] in ['Source','SourceControl']: continue
 lines.append(f"{r['Stage']}/{name(r)} | {r['Method']} | {r['MSAA']} | {r['StrictEligibleConditions']}/24 | "+
              ' | '.join(number(r,k) for k in ['DrawVsSourceStrict_pct','PassVsSourceStrict_pct','SampledError_pctBounds',
                                              'ConstructionCPU_ms','SetupCPU_ms','ChangedTriangles','TriangulationStatus']))
lines += ['', 'Native counts measure submitted/processed work, not hardware stage stalls or occupancy. The limiting stage is not identified by correlation alone.',
          'Summed projected bounding-box area is a proxy; it is not a count of hardware coverage tests. Projected metrics include occluded faces.',
          'summary.csv has all screened and confirmed methods; view_comparisons.csv retains every orientation and control exclusion.',
          'raw_validation.txt records reconstructed timestamp summaries and count checks; preflight/ is exploratory and excluded from the study.']
(root/'report.txt').write_text('\n'.join(lines)+'\n')
runs={p.stem:p.read_text() for p in root.glob('*.metadata.txt') if 'Status: Complete' in p.read_text()}
manifest={'AnalysisSampleGitCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root.parents[2],text=True).strip(),
          'MeasuredSampleGitCommits':sorted({line.removeprefix('Source revision: ') for text in runs.values()
                                           for line in text.splitlines() if line.startswith('Source revision: ')}),
          'Provenance':'Measured revisions/digests come from each run, independently of the current analysis checkout.',
          'Runs':runs,
          'Files':{}}
for p in list((root.parents[2]/'Data/Models/ResearchCandidates').glob('*.obj'))+list(root.glob('*.py'))+list(root.glob('*.ps1'))+list(root.glob('*.cjs'))+list((root/'measured_source').glob('*')):
 manifest['Files'][str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
(root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('\n'.join(lines[:37]))
