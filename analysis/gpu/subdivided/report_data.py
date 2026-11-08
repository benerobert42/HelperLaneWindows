"""Condense analysis outputs (effects/baselines/points) into the JSON the report page embeds."""
import csv, json, math, statistics as st
from collections import defaultdict
from pathlib import Path

root = Path(__file__).parent
read = lambda n: list(csv.DictReader((root / n).open(newline='')))
effects, baselines = read('effects.csv'), read('baselines.csv')
points = json.loads((root / 'points.json').read_text())['points']
num = lambda v: float(v) if v not in ('', None) else None
cad = lambda m: m.startswith('nist')

def rank(v):
    o = sorted(range(len(v)), key=lambda i: v[i]); r = [0.0] * len(v); i = 0
    while i < len(v):
        j = i
        while j + 1 < len(v) and v[o[j + 1]] == v[o[i]]: j += 1
        for k in range(i, j + 1): r[o[k]] = (i + j) / 2
        i = j + 1
    return r
def pearson(x, y):
    mx, my = st.mean(x), st.mean(y)
    sx = math.sqrt(sum((a - mx) ** 2 for a in x)); sy = math.sqrt(sum((b - my) ** 2 for b in y))
    return sum((a - mx) * (b - my) for a, b in zip(x, y)) / (sx * sy)
def fit(x, y):
    mx, my = st.mean(x), st.mean(y); slope = sum((a - mx) * (b - my) for a, b in zip(x, y)) / sum((a - mx) ** 2 for a in x)
    return slope, my - slope * mx

# Mesh/method means over eligible poses (one dot per mesh x method x footprint x shader x MSAA).
groups = defaultdict(list)
for p in points:
    groups[(p['Model'], p['Instances'], p['QuadWork'], p['MSAA'], p['Method'])].append(p)
dots = []
for (model, inst, work, msaa, method), ps in groups.items():
    if len(ps) < 12: continue  # practical threshold: >=12 stable eligible poses
    dots.append(dict(model=model, inst=inst, work=work, msaa=msaa, method=method, n=len(ps), family='CAD (OCCT)' if cad(model) else 'Subdivided',
                     helpers=st.mean(p['Helpers'] for p in ps), draw=st.mean(p['Draw'] for p in ps),
                     pass_=st.mean(p['Pass'] for p in ps), l1=st.mean(p['ScreenL1'] for p in ps)))

# Association strength by family / shader / MSAA (mesh x method means, CAD at 16 instances).
assoc = []
for work in ['none', 'tex2k16']:
    for msaa in [1, 2, 4, 8]:
        sel = [d for d in dots if d['family'] == 'CAD (OCCT)' and d['inst'] == 16 and d['work'] == work and d['msaa'] == msaa]
        pose = [p for p in points if cad(p['Model']) and p['Instances'] == 16 and p['QuadWork'] == work and p['MSAA'] == msaa]
        if len(sel) < 5: continue
        h, d = [s['helpers'] for s in sel], [s['draw'] for s in sel]
        slope, icpt = fit(h, d)
        assoc.append(dict(work=work, msaa=msaa, meshes=len(sel), r2=pearson(h, d) ** 2, rho=pearson(rank(h), rank(d)),
                          rho_l1=pearson(rank([s['l1'] for s in sel]), rank(d)), slope=slope, intercept=icpt,
                          pose_rho=pearson(rank([p['Helpers'] for p in pose]), rank([p['Draw'] for p in pose])), poses=len(pose)))

# Per-mesh source baselines and the best eligible strict result (by draw) for each config.
base = {(b['Model'], int(b['Instances']), b['QuadWork'], int(b['MSAA'])): b for b in baselines}
rows = []
for e in effects:
    if not e['EligiblePoses'] or int(e['EligiblePoses']) < 12: continue
    b = base[(e['Model'], int(e['Instances']), e['QuadWork'], int(e['MSAA']))]
    rows.append(dict(model=e['Model'], inst=int(e['Instances']), work=e['QuadWork'], msaa=int(e['MSAA']), method=e['Method'],
                     eligible=int(e['EligiblePoses']), total=int(e['TotalPoses']), helpers=num(e['HelpersVsSource_pct']),
                     ps=num(e['PSVsSource_pct']), draw=num(e['DrawVsSource_pct']), pass_=num(e['PassVsSource_pct']),
                     l1=num(e['ScreenL1VsSource_pct']), world=num(e['WorldLengthVsSource_pct']), err=num(e['SampledError_pctBounds']),
                     changed=num(e['ChangedTriangles']), cpu=num(e['ConstructionCPU_ms']),
                     tris=int(b['Triangles']), area=float(b['MeanTriangleArea_px2']), helperFrac=float(b['HelperFraction']),
                     srcDraw=float(b['SourceDraw_ms']), stable=int(b['StablePoses']), poses=int(b['Poses'])))
out = dict(dots=dots, assoc=assoc, rows=rows)
(root / 'report_data.json').write_text(json.dumps(out, separators=(',', ':')))
print(len(dots), 'dots;', len(assoc), 'assoc rows;', len(rows), 'effect rows')
for a in assoc: print(a)
