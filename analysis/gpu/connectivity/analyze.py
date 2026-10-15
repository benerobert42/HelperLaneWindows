"""Validate raw observations, form matched comparisons, test simple predictors.

No sample filtering. Blocks are repetitions; individual frames are not independent
training observations. Prediction tests leave out entire geometries.
"""
import csv
import math
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent

def read(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream))

def save(name, rows):
    with (root / name).open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

metrics = ['TotalEdgeLength3D', 'ProjectedEdgeLength', 'ProjectedEdgeL1_px',
           'ProjectedArea_px2', 'ProjectedBBoxArea_px2', 'DiagnosticHelpers',
           'DiagnosticActiveLanes', 'DiagnosticQuads', 'CacheSimACMR', 'CacheSimATVR']
fractions = dict(SubpixelFraction='SubpixelTriangles', BelowFourPxFraction='BelowFourPxTriangles',
                 ThinFraction='ThinTriangles', SliverFraction='SliverTriangles')
paired, summary, predictions = [], [], []
raw_count = case_count = 0
protocols, geometries_seen = set(), set()
for path in sorted(root.glob('*.csv')):
    metadata = path.with_suffix('.metadata.txt')
    if not metadata.exists() or 'Status: Complete' not in metadata.read_text():
        continue
    rows = read(path)
    protocols.update((r['Instances'], r['Warmup'], r['Samples'], r['QueueDepth']) for r in rows)
    geometries_seen.update(r['Geometry'] for r in rows)
    diagnostic = {r['Case']: r for r in read(path.with_suffix('.raster.csv'))}
    raw = defaultdict(list)
    for r in read(path.with_suffix('.raw.csv')):
        raw[r['Case']].append(r)
    assert len(rows) == len(diagnostic) == len(raw)
    for r in rows:
        samples = raw[r['Case']]
        assert r['CaseComplete'] == '1' and len(samples) == int(r['Samples']) == int(r['ExpectedSamples'])
        assert [int(s['Sample']) for s in samples] == list(range(len(samples)))
        assert all(s['RunId'] == r['RunId'] for s in samples)
        for kind, prefix in [('Draw', 'GPU_'), ('Pass', 'GPU_Pass')]:
            values = [float(s[f'GPU_{kind}_ms']) for s in samples]
            assert all(math.isfinite(v) and v > 0 for v in values)
            for n, v in [('Mean', st.mean(values)), ('Median', st.median(values)),
                         ('StdDev', st.pstdev(values)), ('Min', min(values)), ('Max', max(values))]:
                assert math.isclose(v, float(r[f'{prefix}{n}_ms']), rel_tol=2e-8, abs_tol=2e-9)
        assert all(float(s['GPU_Pass_ms']) >= float(s['GPU_Draw_ms']) for s in samples)
        r.update(diagnostic[r['Case']])
        assert int(r['ProjectedTriangles']) == int(r['OutputTriangles']) * int(r['Instances'])
        assert int(r['OutsideViewportTriangles']) == 0, 'Unclipped statistics cannot describe clipped cases.'
        assert math.isclose(float(r['TotalSceneEdgeLength3D']), float(r['TotalEdgeLength3D']) * int(r['Instances']), rel_tol=2e-9)
        r['DiagnosticQuads'] = (float(r['DiagnosticHelpers']) + float(r['DiagnosticActiveLanes'])) / 4
        for name, count in fractions.items():
            r[name] = float(r[count]) / float(r['ProjectedTriangles'])
    raw_count += sum(map(len, raw.values()))
    case_count += len(rows)
    def match(r):
        return tuple(r[n] for n in ['Geometry', 'Block', 'View', 'MSAA', 'Shader'])
    baseline = {match(r): r for r in rows if r['Method'] == 'Source'}
    control_blocks = defaultdict(lambda: defaultdict(list))
    for r in rows:
        if r['Method'] == 'SourceControl':
            b = baseline[match(r)]
            control_blocks[(r['Geometry'], r['MSAA'], r['Shader'])][r['Block']].append(
                100 * (float(r['GPU_Mean_ms']) / float(b['GPU_Mean_ms']) - 1))
    # Practical eligibility guard, not a statistical confidence test. Preserve
    # every observation; fit only contexts with <=2% mean and <=3% block control drift.
    stable = {k: abs(st.mean(v for vs in blocks.values() for v in vs)) <= 2 and
                 max(abs(st.mean(vs)) for vs in blocks.values()) <= 3
              for k, blocks in control_blocks.items()}
    groups = defaultdict(list)
    for r in rows:
        b = baseline[match(r)]
        assert r['OutputVertices'] == b['OutputVertices'] and r['OutputTriangles'] == b['OutputTriangles']
        if r['Method'] == 'SourceControl':
            assert all(float(r[n]) == float(b[n]) for n in metrics)
        p = {n: r[n] for n in ['Geometry', 'Method', 'Block', 'View', 'MSAA', 'Shader', 'Ordering', 'Depth', 'Instances', 'Width', 'Height']}
        p.update(Run=path.stem, Valid=str(int(r['ValidForRanking'] == b['ValidForRanking'] == '1')), TimingControlStable=stable[(r['Geometry'], r['MSAA'], r['Shader'])],
                 SourceDraw_ms=float(b['GPU_Mean_ms']), Draw_ms=float(r['GPU_Mean_ms']),
                 SourcePass_ms=float(b['GPU_PassMean_ms']), Pass_ms=float(r['GPU_PassMean_ms']))
        p['DrawChangePct'] = 100 * (p['Draw_ms'] / p['SourceDraw_ms'] - 1)
        p['PassChangePct'] = 100 * (p['Pass_ms'] / p['SourcePass_ms'] - 1)
        p['SourceMeanTriangleArea_px2'] = float(b['ProjectedArea_px2']) / float(b['ProjectedTriangles'])
        p['SourceQuadsPerTriangle'] = b['DiagnosticQuads'] / float(b['ProjectedTriangles'])
        for n in metrics:
            p[n] = float(r[n])
            p[n + 'ChangePct'] = 100 * (float(r[n]) / float(b[n]) - 1) if float(b[n]) else 0
        for n in fractions:
            p[n] = r[n]
            p[n + 'Delta'] = r[n] - b[n]
        paired.append(p)
        groups[(r['Geometry'], r['Method'], r['MSAA'], r['Shader'])].append(p)
    for (geometry, method, msaa, shader), ps in sorted(groups.items()):
        s = dict(Run=path.stem, Geometry=geometry, Method=method, MSAA=msaa, Shader=shader,
                 Ordering=ps[0]['Ordering'], Depth=ps[0]['Depth'], Height=ps[0]['Height'],
                 Valid=all(p['Valid'] == '1' for p in ps), TimingControlStable=ps[0]['TimingControlStable'],
                 SourceDraw_ms=st.mean(p['SourceDraw_ms'] for p in ps),
                 Draw_ms=st.mean(p['Draw_ms'] for p in ps), SourcePass_ms=st.mean(p['SourcePass_ms'] for p in ps),
                 Pass_ms=st.mean(p['Pass_ms'] for p in ps))
        for n in ['DrawChangePct', 'PassChangePct'] + metrics + [n + 'ChangePct' for n in metrics] + list(fractions):
            s[n] = st.mean(p[n] for p in ps)
        blocks = defaultdict(list)
        for p in ps:
            blocks[p['Block']].append(p['DrawChangePct'])
        means = [st.mean(v) for v in blocks.values()]
        s['DrawBlockMinPct'], s['DrawBlockMaxPct'] = min(means), max(means)
        summary.append(s)
    print(f'Validated {path.name}: {len(rows)} cases, {sum(map(len, raw.values()))} raw samples')

assert summary, 'No completed runs found.'
save('paired.csv', paired)
save('comparison.csv', summary)

# Each observation averages repeated blocks for one camera/geometry/method/run.
# Fit one slope through zero in log-relative cost/time; no complex fitted model.
observations = defaultdict(list)
for p in paired:
    if p['Valid'] == '1' and p['TimingControlStable'] and p['Method'] not in ['Source', 'SourceControl']:
        observations[(p['Run'], p['Geometry'], p['Method'], p['View'], p['MSAA'], p['Shader'])].append(p)
regimes = defaultdict(list)
for key, ps in observations.items():
    p = dict(ps[0])
    for n in ['DrawChangePct'] + [n + 'ChangePct' for n in metrics] + [n + 'Delta' for n in fractions]:
        p[n] = st.mean(v[n] for v in ps)
    regimes[(p['Shader'], p['MSAA'], p['Ordering'], p['Depth'])].append(p)
for (shader, msaa, ordering, depth), ps in regimes.items():
    geometries = sorted({p['Geometry'] for p in ps})
    if len(geometries) < 3:
        continue
    y = [math.log1p(p['DrawChangePct'] / 100) for p in ps]
    for feature in metrics + list(fractions) + ['FootprintWeightedScreen', 'CoverageWeightedScreen']:
        def predictor(p):
            if feature in metrics:
                return math.log1p(p[feature + 'ChangePct'] / 100)
            if feature in fractions:
                return p[feature + 'Delta']
            # Fixed one-pixel/one-quad gates, not thresholds fitted on the test set.
            size = p['SourceMeanTriangleArea_px2'] if feature == 'FootprintWeightedScreen' else p['SourceQuadsPerTriangle']
            return math.log1p(p['ProjectedEdgeLengthChangePct'] / 100) * size / (size + 1)
        x = [predictor(p) for p in ps]
        errors = []
        for geometry in geometries:
            training = [i for i, p in enumerate(ps) if p['Geometry'] != geometry]
            denom = sum(x[i] ** 2 for i in training)
            slope = sum(x[i] * y[i] for i in training) / denom if denom > 1e-15 else 0
            errors += [(y[i] - slope * x[i]) ** 2 for i, p in enumerate(ps) if p['Geometry'] == geometry]
        naive = sum(v * v for v in y)
        predictions.append(dict(Shader=shader, MSAA=msaa, Ordering=ordering, Depth=depth, Feature=feature,
                                Observations=len(ps), HeldOutGeometries=len(geometries),
                                CV_RMSE_LogRatio=math.sqrt(st.mean(errors)),
                                CV_ImprovementOverNoChange=1 - sum(errors) / naive if naive else 0))
save('predictors.csv', predictions)

lines = [f'Connectivity mechanism study: {case_count} cases, {raw_count} raw samples.',
         'RTX 4050 Laptop, D3D12 Release; randomized blocks; (instances, warmup, samples, queue depth): ' + str(sorted(protocols)),
         'Geometry: ' + ', '.join(sorted(geometries_seen)) + '; views and blocks are recorded per case.',
         'Triangle/vertex counts fixed within each model; skew2 keeps the exact surface and affine interpolated attributes.',
         'Main baseline: identical meshoptimizer cache + overdraw optimization for every candidate. Native/cache-only are ablations.',
         'Vertex storage fixed. GPU draw excludes clear/resolve/UI; pass includes clear and resolve (lit 4x only).',
         'Projected metrics are pre-depth and include occluded triangles. Cache simulation is not a hardware counter.',
         'Diagnostic helpers/quads are instrumented coverage proxies; neither is a timed-shader execution counter.',
         'Explicit LOD uses SampleLevel(0), implicit LOD uses Sample, on the same single-mip texture.',
         'LOD comparison changes derivative/LOD instructions and scheduling; it does not isolate helper execution alone.',
         'No observations removed. Invalid triangulations retained and excluded from prediction fitting.',
         'Control eligibility: <=2% mean and <=3% block-mean draw drift; unstable contexts retained but excluded from prediction fitting.',
         'Ranges below are mean connectivity effects by repetition block, not confidence intervals.',
         'Predictor test averages blocks and leaves out whole geometries; small model set, one GPU. No significance claims.', '']
lines += ['Gated predictors multiply log screen-edge ratio by source mean area/(area+1 pixel) or source quads/(quads+triangles).',
          'These fixed gates test footprint dependence; the fitted slope still assumes a simple cost relation.', '']
for run in sorted({s['Run'] for s in summary}):
    selected = [s for s in summary if s['Run'] == run]
    controls = [s for s in selected if s['Method'] == 'SourceControl']
    lines += [f'{run}: maximum absolute control mean draw change {max(abs(s["DrawChangePct"]) for s in controls):.2f}%; '
              f'maximum control block change {max(max(abs(s["DrawBlockMinPct"]), abs(s["DrawBlockMaxPct"])) for s in controls):.2f}%.',
              'model method shader MSAA valid stable world% screen% bbox% helper% quad% ACMR% subpixel% source_draw_ms draw_ms draw% block_range']
    for s in selected:
        if s['Method'] in ['Source', 'SourceControl']:
            continue
        lines.append(f'{s["Geometry"]:13} {s["Method"]:8} {s["Shader"]:20} {s["MSAA"]} '
                     f'{str(s["Valid"]):5} {str(s["TimingControlStable"]):5} '
                     f'{s["TotalEdgeLength3DChangePct"]:+6.1f} {s["ProjectedEdgeLengthChangePct"]:+6.1f} '
                     f'{s["ProjectedBBoxArea_px2ChangePct"]:+6.1f} {s["DiagnosticHelpersChangePct"]:+6.1f} '
                     f'{s["DiagnosticQuadsChangePct"]:+6.1f} {s["CacheSimACMRChangePct"]:+6.1f} '
                     f'{100*s["SubpixelFraction"]:5.1f} {s["SourceDraw_ms"]:.4f} {s["Draw_ms"]:.4f} '
                     f'{s["DrawChangePct"]:+6.1f} [{s["DrawBlockMinPct"]:+.1f},{s["DrawBlockMaxPct"]:+.1f}]')
    lines.append('')
lines += ['Single-predictor cross-validation (positive improvement beats predicting no time change):']
for regime in sorted(regimes):
    chosen = [p for p in predictions if (p['Shader'], p['MSAA'], p['Ordering'], p['Depth']) == regime]
    if chosen:
        best = sorted(chosen, key=lambda p: p['CV_RMSE_LogRatio'])[:4]
        lines.append(str(regime) + ': ' + ', '.join(f'{p["Feature"]}={p["CV_ImprovementOverNoChange"]:+.2f}' for p in best))
(root / 'results.txt').write_text('\n'.join(lines) + '\n')
print(f'All {case_count} cases and {raw_count} raw samples verified. Results and geometry-held-out predictor scores written.')
