"""Subdivided-mesh study: does reducing helper lanes by re-triangulation reduce GPU time?

Reuses the shared raw-sample validator and per-pose SourceControl gate (topology/analyze.py), then
relates per-pose helper-lane changes to draw/pass changes by model, camera distance and MSAA.
No samples are removed. Eligibility = valid mesh, visible coverage, stable SourceControl, strict surface.
Correlations are descriptive (Spearman over eligible poses); they are not significance tests.
"""
import csv, json, math, re, statistics as st, subprocess, sys
from collections import defaultdict
from pathlib import Path

root = Path(__file__).parent
here = root.parent
subprocess.run([sys.executable, str(here / 'topology/analyze.py'), str(root)], check=True,
               stdout=(root / 'comparison_log.txt').open('w'))

def read(p):
    with p.open(newline='') as f: return list(csv.DictReader(f))
def save(name, rows):
    with (root / name).open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
run_re = re.compile(r'^(?P<stage>[a-z]+)_(?P<model>.+)_d(?P<dist>[0-9.]+)_i(?P<inst>\d+)_(?P<work>none|ddx|tex2k16|tex2k)_lit_(?P<h>\d+)_q\d+$')
def parse(run):
    m = run_re.match(run); return m['model'], float(m['dist']), int(m['inst']), m['work']

def rank(v):
    order = sorted(range(len(v)), key=lambda i: v[i]); r = [0.0] * len(v); i = 0
    while i < len(v):
        j = i
        while j + 1 < len(v) and v[order[j + 1]] == v[order[i]]: j += 1
        for k in range(i, j + 1): r[order[k]] = (i + j) / 2
        i = j + 1
    return r
def pearson(x, y):
    mx, my = st.mean(x), st.mean(y)
    sx = math.sqrt(sum((a - mx) ** 2 for a in x)); sy = math.sqrt(sum((b - my) ** 2 for b in y))
    return sum((a - mx) * (b - my) for a, b in zip(x, y)) / (sx * sy) if sx and sy else float('nan')
def spearman(x, y): return pearson(rank(x), rank(y)) if len(x) >= 5 else float('nan')
def slope(x, y):
    mx, my = st.mean(x), st.mean(y); d = sum((a - mx) ** 2 for a in x)
    return sum((a - mx) * (b - my) for a, b in zip(x, y)) / d if d else float('nan')

# Per-case absolute values (Source baselines, quads) from the run files.
cases = {}
for path in sorted(root.glob('*_lit_*.csv')):
    if path.name.endswith(('.raw.csv', '.raster.csv')): continue
    meta = path.with_suffix('.metadata.txt')
    if not meta.exists() or 'Status: Complete' not in meta.read_text(): continue
    raster = {r['Case']: r for r in read(path.with_suffix('.raster.csv'))}
    for r in read(path):
        r.update(raster[r['Case']]); r['Run'] = path.stem
        cases[(path.stem, r['Method'], r['View'], r['MSAA'], r['Block'])] = r

blocks = defaultdict(list)  # (run, method, view, msaa) -> cases over blocks
for (run, m, v, a, b), r in cases.items(): blocks[(run, m, v, a)].append(r)
poses = read(root / 'view_comparisons.csv')
for p in poses:
    p['Model'], p['Distance'], p['Instances'], p['QuadWork'] = parse(p['Run'])
    p['Footprint'] = f"{p['Instances']} inst" + ('' if p['Distance'] == 1 else f", {p['Distance']:g}x dist")
    rs = blocks[(p['Run'], p['Method'], p['View'], p['MSAA'])]
    srcs = [cases[(p['Run'], 'Source', p['View'], p['MSAA'], r['Block'])] for r in rs]
    quads = lambda r: (float(r['DiagnosticHelpers']) + float(r['DiagnosticActiveLanes'])) / 4
    p['QuadsVsSource_pct'] = st.mean(100 * (quads(r) / quads(s) - 1) for r, s in zip(rs, srcs))
    p['SourceHelpers'] = st.mean(float(s['DiagnosticHelpers']) for s in srcs)
    p['SourceActive'] = st.mean(float(s['DiagnosticActiveLanes']) for s in srcs)
    p['SourceHelperFraction'] = p['SourceHelpers'] / (p['SourceHelpers'] + p['SourceActive'])
    p['SourceTriangleArea_px2'] = st.mean(float(s['ProjectedArea_px2']) / float(s['ProjectedTriangles']) for s in srcs)
    p['SourceSubpixelFraction'] = st.mean(float(s['SubpixelTriangles']) / float(s['ProjectedTriangles']) for s in srcs)
    p['Eligible'] = all(p[k] == 'True' for k in ['MeshValid', 'ControlStable', 'Visible', 'SurfaceEquivalent'])
candidates = [p for p in poses if p['Method'] not in ('Source', 'SourceControl')]
num = lambda p, k: float(p[k])

# 1. Source baselines: how footprint/MSAA/subdivision set the helper-lane regime.
baseline = []
for key in sorted({(p['Model'], p['Instances'], p['Distance'], p['QuadWork'], int(p['MSAA'])) for p in poses}):
    ps = [p for p in poses if (p['Model'], p['Instances'], p['Distance'], p['QuadWork'], int(p['MSAA'])) == key and p['Method'] == 'SourceControl']
    tri = next(r for r in cases.values() if parse(r['Run'])[0] == key[0])['OutputTriangles']
    baseline.append(dict(Model=key[0], Instances=key[1], Distance=key[2], QuadWork=key[3], MSAA=key[4], Triangles=int(tri),
                         DrawnTriangles=int(tri) * key[1], Poses=len(ps),
                         StablePoses=sum(p['ControlStable'] == 'True' for p in ps),
                         SourceDraw_ms=st.mean(num(p, 'SourceDraw_ms') for p in ps),
                         SourcePass_ms=st.mean(num(p, 'SourcePass_ms') for p in ps),
                         HelperFraction=st.mean(p['SourceHelperFraction'] for p in ps),
                         SourceHelpers=st.mean(p['SourceHelpers'] for p in ps),
                         SourceActiveLanes=st.mean(p['SourceActive'] for p in ps),
                         MeanTriangleArea_px2=st.mean(p['SourceTriangleArea_px2'] for p in ps),
                         SubpixelTriangleFraction=st.mean(p['SourceSubpixelFraction'] for p in ps)))
save('baselines.csv', baseline)

# 2. Method effects per model/distance/MSAA over strict eligible poses.
effects = []
keys = ['HelpersVsSource_pct', 'QuadsVsSource_pct', 'PSVsSource_pct', 'DrawVsSource_pct', 'PassVsSource_pct',
        'WorldLengthVsSource_pct', 'ScreenLengthVsSource_pct', 'ScreenL1VsSource_pct', 'BBoxVsSource_pct']
for key in sorted({(p['Model'], p['Instances'], p['Distance'], p['QuadWork'], int(p['MSAA']), p['Method']) for p in candidates}):
    ps = [p for p in candidates if (p['Model'], p['Instances'], p['Distance'], p['QuadWork'], int(p['MSAA']), p['Method']) == key]
    ok = [p for p in ps if p['Eligible']]
    e = dict(Model=key[0], Instances=key[1], Distance=key[2], QuadWork=key[3], MSAA=key[4], Method=key[5], EligiblePoses=len(ok), TotalPoses=len(ps),
             ChangedTriangles=st.mean(num(p, 'ChangedTriangles') for p in ps),
             ConstructionCPU_ms=st.mean(num(p, 'ConstructionCPU_ms') for p in ps),
             SampledError_pctBounds=max(num(p, 'SampledError_pctBounds') for p in ps))
    for k in keys:
        vals = [num(p, k) if k != 'QuadsVsSource_pct' else p[k] for p in ok if p[k] != '']
        e[k] = st.mean(vals) if vals else ''
    e['Draw_rho_helpers'] = spearman([num(p, 'HelpersVsSource_pct') for p in ok], [num(p, 'DrawVsSource_pct') for p in ok]) if ok else ''
    effects.append(e)
save('effects.csv', effects)

# 3. Correlation of helper change with time change across eligible poses (pooled methods).
def corr_rows(group):
    rows = []
    for key in sorted({group(p) for p in candidates}):
        ok = [p for p in candidates if p['Eligible'] and group(p) == key]
        if len(ok) < 5: continue
        h = [num(p, 'HelpersVsSource_pct') for p in ok]; q = [p['QuadsVsSource_pct'] for p in ok]
        d = [num(p, 'DrawVsSource_pct') for p in ok]; s = [num(p, 'PassVsSource_pct') for p in ok]
        fewer = [p for p in ok if num(p, 'HelpersVsSource_pct') < 0]; more = [p for p in ok if num(p, 'HelpersVsSource_pct') >= 0]
        rows.append(dict(Group=key, Observations=len(ok),
                         Spearman_Helpers_Draw=spearman(h, d), Spearman_Quads_Draw=spearman(q, d),
                         Spearman_Helpers_Pass=spearman(h, s), Slope_DrawPct_per_HelperPct=slope(h, d),
                         MeanDraw_whenFewerHelpers=st.mean(num(p, 'DrawVsSource_pct') for p in fewer) if fewer else '',
                         MeanDraw_whenMoreHelpers=st.mean(num(p, 'DrawVsSource_pct') for p in more) if more else '',
                         FewerHelpersAndFaster_pct=100 * sum(num(p, 'DrawVsSource_pct') < 0 for p in fewer) / len(fewer) if fewer else ''))
    return rows
correlations = (corr_rows(lambda p: 'all') + corr_rows(lambda p: f"quad work {p['QuadWork']}") +
                corr_rows(lambda p: f"quad work {p['QuadWork']}, MSAA {p['MSAA']}x") + corr_rows(lambda p: f"MSAA {p['MSAA']}x") +
                corr_rows(lambda p: p['Footprint']) + corr_rows(lambda p: f"MSAA {p['MSAA']}x, {p['Footprint']}") +
                corr_rows(lambda p: p['Model']) + corr_rows(lambda p: f"{p['Model']}, MSAA {p['MSAA']}x") +
                corr_rows(lambda p: f"{p['Model']}, {p['Footprint']}, MSAA {p['MSAA']}x") +
                corr_rows(lambda p: f"{p['Model']}, {p['Footprint']}, {p['QuadWork']}, MSAA {p['MSAA']}x"))
save('correlations.csv', correlations)

# Pose-level points for plots.
points = [dict(Model=p['Model'], Instances=p['Instances'], Distance=p['Distance'], QuadWork=p['QuadWork'], MSAA=int(p['MSAA']), Method=p['Method'], View=int(p['View']),
               Helpers=num(p, 'HelpersVsSource_pct'), Quads=p['QuadsVsSource_pct'], PS=num(p, 'PSVsSource_pct') if p['PSVsSource_pct'] else None,
               Draw=num(p, 'DrawVsSource_pct'), Pass=num(p, 'PassVsSource_pct'), ScreenL1=num(p, 'ScreenL1VsSource_pct'))
          for p in candidates if p['Eligible']]
(root / 'points.json').write_text(json.dumps(dict(points=points, baselines=baseline, effects=effects, correlations=correlations)))
f = lambda v: f'{v:+.2f}' if isinstance(v, float) and not math.isnan(v) else 'NA'
print('model | inst | dist | work | MSAA | method | eligible | helpers% | quads% | PS% | draw% | pass% | screenL1%')
for e in effects:
    print(f"{e['Model']} | {e['Instances']} | {e['Distance']:g} | {e['QuadWork']} | {e['MSAA']} | {e['Method']} | {e['EligiblePoses']}/{e['TotalPoses']} | " +
          ' | '.join(f(e[k]) if e[k] != '' else 'NA' for k in ['HelpersVsSource_pct', 'QuadsVsSource_pct', 'PSVsSource_pct',
                                                               'DrawVsSource_pct', 'PassVsSource_pct', 'ScreenL1VsSource_pct']))
print('\ncorrelations (eligible poses):')
for c in correlations:
    print(f"{c['Group']}: n={c['Observations']} rho(helpers,draw)={f(c['Spearman_Helpers_Draw'])} rho(quads,draw)={f(c['Spearman_Quads_Draw'])} "
          f"slope={f(c['Slope_DrawPct_per_HelperPct'])} draw|fewer={c['MeanDraw_whenFewerHelpers'] if c['MeanDraw_whenFewerHelpers']=='' else f(c['MeanDraw_whenFewerHelpers'])}")
