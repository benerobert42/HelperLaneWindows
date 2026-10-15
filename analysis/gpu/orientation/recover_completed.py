"""Preserve an interrupted run; export only wholly completed model schedules."""
import csv
import itertools
from pathlib import Path
import shutil

root=Path(__file__).parent
source=root/'models_lit_1080_q8.csv'
target=root/'completed_models_lit_1080_q8.csv'
with source.open(newline='') as f: rows=list(csv.DictReader(f))
models={r['Geometry'] for r in rows}
expected=set(itertools.product(['Source','SourceControl','CDTFlip','Heap3D'],range(3),range(24),[1,4]))
assert len(models)==14 and len(rows)==len(models)*len(expected)
assert [int(r['Case']) for r in rows]==list(range(len(rows)))
for model in models:
    rs=[r for r in rows if r['Geometry']==model]
    assert {(r['Method'],int(r['Block']),int(r['View']),int(r['MSAA'])) for r in rs}==expected
    assert all(r['CaseComplete']=='1' and r['Samples']==r['ExpectedSamples']=='128' for r in rs)
for suffix in ['.csv','.raw.csv','.raster.csv']:
    shutil.copyfile(source.with_suffix(suffix),target.with_suffix(suffix))
original=source.with_suffix('.metadata.txt').read_text()
assert 'Status: Incomplete' in original and 'Cases: 8064' in original
metadata=(f'Derived complete 14-model subset. Original file preserved: {source.name}\n'
          'Original status was Incomplete: next model failed source validation before a case was issued.\n'
          + original.replace('Status: Incomplete','Status: Complete').replace(
              'Message: Interrupted','Message: All scheduled cases completed for these 14 models; original interrupted run remains saved.'))
target.with_suffix('.metadata.txt').write_text(metadata)
print(f'Copied {len(rows)} completed cases; raw statistics must still pass the shared analyzer.')
