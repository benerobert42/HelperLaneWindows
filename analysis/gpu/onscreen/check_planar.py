"""Audit completed paper-planar reruns; keep original observations unchanged."""
import itertools
import json
import math
import sys
from pathlib import Path
import numpy as np
import pandas as pd

stem = Path(sys.argv[1])
summary = pd.read_csv(stem.with_suffix('.csv'))
raw = pd.read_csv(stem.with_suffix('.raw.csv'))
raster = pd.read_csv(stem.with_suffix('.raster.csv'))
metadata = stem.with_suffix('.metadata.txt').read_text()
audit_directions = 1 if 'GeometryValidationPolicy: ForwardOnlyAdmissionV1' in metadata else 2
errors = []
def check(ok, message):
    if not bool(ok): errors.append(message)

methods = {'Earcut', 'SourceControl', 'EarcutFlip', 'CDT', 'CDTFlip', 'GMA', 'MWT'}
check(len(summary) == 140 and len(raw) == 14000 and len(raster) == 140, 'Expected 140 cases, 14000 samples and 140 raster rows')
check(summary.Case.tolist() == list(range(140)), 'Case ownership/order')
check(summary.Geometry.nunique() == 5 and set(summary.Method) == methods, 'Scene/method set')
expected = set(itertools.product(summary.Geometry.unique(), methods, [1, 4], [1, 10000]))
actual = set(summary[['Geometry', 'Method', 'MSAA', 'Instances']].itertuples(index=False, name=None))
check(actual == expected and len(actual) == len(summary), 'Full factorial coverage without duplicates')
for name, value in {'Width':1920, 'Height':1080, 'Warmup':30, 'Samples':100, 'ExpectedSamples':100,
                    'QueueDepth':8, 'Meshopt':1, 'Block':0, 'View':0, 'XRotationDegrees':0,
                    'YRotationDegrees':0, 'ZRotationDegrees':0, 'MeshValid':1, 'SurfaceEquivalent':1,
                    'StructuralValid':1, 'CaseComplete':1, 'ReferenceAuditDirections':audit_directions}.items():
    check(summary[name].eq(value).all(), f'Invalid {name}')
check(summary.Shader.eq('PaperPlanarTexture2K').all(), 'Wrong timed shader')
check(summary.ConnectivitySeed.eq('Earcut').all(), 'Wrong reference')
check(summary.OutputTriangles.eq(summary.SourceTriangles).all(), 'Triangle count changed')
check(summary.groupby('Geometry').OutputVertices.nunique().eq(1).all(), 'Vertex count changed')
check(summary.HardwareInputPrimitives.eq(summary.OutputTriangles * summary.Instances).all(), 'Hardware input primitive count')
check(summary.PipelineStatisticsAvailable.eq(1).all(), 'Missing hardware counters')
check(summary.Depth.eq(summary.MSAA.eq(1).astype(int)).all(), 'Original per-MSAA depth settings')
for name in ['FailedPatches', 'DuplicateTriangles', 'NonManifoldEdges', 'WindingErrors', 'MissingBoundaryEdges',
             'UnexpectedBoundaryEdges', 'DegenerateTriangles']:
    check(summary[name].eq(0).all(), f'Geometry audit {name}')
check(np.isfinite(raw[['GPU_Draw_ms', 'GPU_Pass_ms']]).all().all(), 'Nonfinite timestamp')
check(raw.GPU_Draw_ms.gt(0).all() and raw.GPU_Pass_ms.ge(raw.GPU_Draw_ms).all(), 'Invalid draw/pass interval')
check(raw.Frame.eq(raw.Sample + 30).all(), 'Raw frame indices')
for case, block in raw.groupby('Case', sort=False):
    check(block.Sample.tolist() == list(range(100)), f'Raw samples for case {case}')
    row = summary.loc[summary.Case.eq(case)].iloc[0]
    for source, prefix in [('GPU_Draw_ms', 'GPU_'), ('GPU_Pass_ms', 'GPU_Pass')]:
        values = block[source].to_numpy()
        stats = {'Mean_ms': values.mean(), 'Median_ms': np.median(values), 'StdDev_ms': values.std(),
                 'Min_ms': values.min(), 'Max_ms': values.max()}
        for name, value in stats.items():
            check(math.isclose(row[prefix+name], value, rel_tol=2e-8, abs_tol=2e-9), f'Summary {case} {prefix+name}')
joined = summary.merge(raster, on='Case', validate='one_to_one')
check(joined.OutsideViewportTriangles.eq(0).all(), 'Clipped geometry')
check(joined.ProjectedTriangles.eq(joined.OutputTriangles * joined.Instances).all(), 'Raster triangle count')
controls = []
for key, group in summary.groupby(['Geometry', 'MSAA', 'Instances']):
    a = group.loc[group.Method.eq('Earcut')].iloc[0]
    b = group.loc[group.Method.eq('SourceControl')].iloc[0]
    for name in ['DiagnosticHelpers', 'DiagnosticActiveLanes', 'HardwareVSInvocations', 'HardwarePSInvocations', 'TotalEdgeLength3D']:
        check(a[name] == b[name], f'Identical control differs: {key} {name}')
    delta = 100 * (b.GPU_Mean_ms / a.GPU_Mean_ms - 1)
    controls.append(dict(zip(['Geometry','MSAA','Instances'], key), ControlDifference_pct=delta, Within20Pct=bool(abs(delta)<=20)))
check('Status: Complete' in metadata, 'Incomplete run')
report = {'complete': not errors, 'errors': errors, 'cases': len(summary), 'raw_samples': len(raw),
          'controls_within_20pct': sum(x['Within20Pct'] for x in controls), 'controls_total':len(controls), 'controls':controls}
stem.with_suffix('.quality.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
raise SystemExit(bool(errors))
