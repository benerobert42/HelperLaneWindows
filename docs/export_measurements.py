"""Save compact absolute GPU times from the final studies' local pose tables.

Run from any directory with Python 3. Requires the generated view_comparisons.csv
files; these large intermediate tables are retained locally, not in Git.
No new eligibility rule or outlier filtering is introduced here.
"""
import csv
import statistics
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parent
fields = ['SourceDraw_ms', 'Draw_ms', 'SourcePass_ms', 'Pass_ms',
          'DrawVsSource_pct', 'PassVsSource_pct', 'WorldLengthVsSource_pct',
          'ScreenLengthVsSource_pct', 'HelpersVsSource_pct', 'PSVsSource_pct', 'VSVsSource_pct']
output = []
for study in ['all_models', 'five_candidates']:
    folder = root.parent / 'analysis/gpu' / study
    groups = defaultdict(list)
    with (folder / 'view_comparisons.csv').open(newline='') as stream:
        for pose in csv.DictReader(stream):
            if pose['Stage'] == 'screen' or pose['Method'] in ['Source', 'SourceControl']:
                continue
            key = tuple(pose[k] for k in ['Run', 'Geometry', 'Stage', 'Method', 'MSAA'])
            groups[key]  # Retain rows with no eligible conditions.
            if all(pose[k] == 'True' for k in ['MeshValid', 'ControlStable', 'Visible', 'SurfaceEquivalent']):
                groups[key].append({k: float(pose[k]) if pose[k] else None for k in fields})
    for key, poses in sorted(groups.items()):
        run, geometry, stage, method, msaa = key
        row = dict(Study=study, Run=run, Geometry=geometry, Stage=stage, Method=method,
                   MSAA=msaa, Instances=1 if stage == 'large' else 64,
                   StrictEligibleConditions=len(poses), TotalConditions=24,
                   MeetsReportingCoverage=len(poses) >= 12)
        for field in fields:
            values = [pose[field] for pose in poses if pose[field] is not None]
            row[field] = format(statistics.mean(values), '.10g') if values and len(values) == len(poses) else ''
        output.append(row)

with (root / 'confirmed_measurements.csv').open('w', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(output[0]))
    writer.writeheader()
    writer.writerows(output)
print(f'Saved {len(output)} method/model/MSAA rows; absolute times use matched strict eligible poses.')
