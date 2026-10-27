"""Freeze the completed protocol-4 run inventory and case order for a Metal port.

Run from any directory. Requires the retained local Windows summary/metadata
files; raw timestamp samples are deliberately not copied into this package.
No benchmark is executed and no measurement output is changed.
"""
import csv
import gzip
import hashlib
import io
import json
import re
import subprocess
import textwrap
import zipfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEST = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(name):
    return name.replace('obj:analysis/gpu/orientation/models/', 'obj:Data/Models/')


def methods_for(study, stage, geometry):
    if study == 'all_models':
        if stage == 'screen':
            return 'Source,SourceControl,CDT,CDTFlip,CDTFlip3D,Greedy,LMT,LMT3D,GP,Heap3D,HeapPlane,HeapScreen,HeapQuad,HeapQuadSet,HeapQuadView,HeapQuadLag,HeapQuadWarm,HeapQuadWarmLag,HeapLoose,HeapWide,Star3D,StarLoose,StarWide'.split(',')
        if stage == 'confirm':
            return 'Source,SourceControl,CDT,Heap3D,Star3D'.split(',')
        if stage == 'special':
            return ('Source,SourceControl,Heap3D,GridAligned,GridOpposite' + (',TopologySplit' if geometry == 'torus' else '')).split(',')
        for row in csv.DictReader((ROOT / 'analysis/gpu/all_models/confirmation_plan.csv').open(newline='')):
            if canonical(row['Geometry']) == canonical(geometry):
                return row['Methods'].split(',')
    else:
        return {
            'screen': 'Source,SourceControl,CDT,CDTFlip,Greedy,LMT,GP,Heap3D,HeapPlane,HeapQuadView,Star3D',
            'confirm': 'Source,SourceControl,CDT,CDTFlip,Heap3D,HeapPlane,HeapQuadView',
            'control': 'Source,SourceControl,Heap3D,HeapPlane',
            'large': 'Source,SourceControl,CDT,Heap3D,HeapPlane',
        }[stage].split(',')
    raise ValueError((study, stage, geometry))


def main():
    runs, catalog, totals = [], {}, Counter()
    plan = io.StringIO(newline='')
    writer = csv.writer(plan, lineterminator='\n')
    writer.writerow(['RunIndex', 'Case', 'Block', 'View', 'MSAA', 'Method'])
    for study in ['all_models', 'five_candidates']:
        folder = ROOT / 'analysis/gpu' / study
        for meta in sorted(folder.glob('*.metadata.txt')):
            text = meta.read_text()
            if 'Status: Complete' not in text:
                continue
            path = meta.with_name(meta.name.replace('.metadata.txt', '.csv'))
            rows = list(csv.DictReader(path.open(newline='')))
            assert rows and all(r['Protocol'] == '4' and r['CaseComplete'] == '1' for r in rows)
            fields = dict(line.split(': ', 1) for line in text.splitlines() if ': ' in line)
            first = rows[0]
            stage = path.stem.split('_')[0]
            geometry = first['Geometry']
            methods = methods_for(study, stage, geometry)
            assert set(methods) == {r['Method'] for r in rows}
            assert all(int(r['Case']) == i for i, r in enumerate(rows))
            blocks = len({r['Block'] for r in rows})
            assert len(rows) == blocks * 24 * 2 * len(methods)
            source = next(r for r in rows if r['Method'] == 'Source')
            source_fields = ['OutputVertices', 'OutputTriangles', 'UniqueEdges', 'TotalEdgeLength3D',
                             'SurfaceArea', 'ConnectedComponents', 'EulerCharacteristic', 'Radius',
                             'ReferenceErrorBudget', 'GridCols', 'GridRows']
            expected = {k: source[k] for k in source_fields}
            key = canonical(geometry)
            catalog.setdefault(key, {k: expected[k] for k in source_fields if k not in ['GridCols', 'GridRows']})
            output = f'analysis/gpu/mac_replication/{study}/{path.name}'
            argv = ['--perf', '--lit', '--raster-metrics', '--rotation-axes', '8',
                    '--geometries', key, '--methods', ','.join(methods),
                    '--samples', first['Samples'], '--warmup', first['Warmup'],
                    '--blocks', str(blocks), '--instances', first['Instances'],
                    '--width', '1920', '--height', '1080', '--msaa', '1,4',
                    '--queue-depth', '8', '--ordering', 'cache-overdraw', '--output', output]
            count = sum(int(r['Samples']) for r in rows)
            record = dict(index=len(runs), study=study, stage=stage, run=path.stem,
                          original_geometry=geometry, geometry=key, methods=methods,
                          blocks=blocks, warmup=int(first['Warmup']), samples=int(first['Samples']),
                          instances=int(first['Instances']), width=1920, height=1080, msaa=[1, 4],
                          views=24, axis_steps=8, queue_depth=8, ordering='cache-overdraw',
                          cases=len(rows), raw_samples=count, expected_source=expected,
                          measured_source_revision=fields['Source revision'],
                          measured_compiled_source_digest=fields['Compiled source digest'],
                          original_summary_sha256=sha(path), metadata=text, argv_contract=argv)
            runs.append(record)
            for r in rows:
                writer.writerow([record['index'], r['Case'], r['Block'], r['View'], r['MSAA'], r['Method']])
            for suffix, value in [('runs', 1), ('cases', len(rows)), ('raw_samples', count)]:
                totals[f'{study}/{stage}/{suffix}'] += value

    assert len(runs) == 90
    assert sum(r['cases'] for r in runs) == 109536
    assert sum(r['raw_samples'] for r in runs) == 7790592
    plan_path = DEST / 'surface_case_order.csv.gz'
    with gzip.GzipFile(filename='', fileobj=plan_path.open('wb'), mode='wb', mtime=0) as stream:
        stream.write(plan.getvalue().encode('utf-8'))

    archive = DEST / 'surface_measured_sources.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for label, relative in [('broad_screen', 'analysis/gpu/all_models/compiled-source'),
                                ('broad_confirmation', 'analysis/gpu/all_models/confirmation-source')]:
            for file in sorted((ROOT / relative).rglob('*')):
                if file.is_file():
                    info = zipfile.ZipInfo(label + '/' + file.relative_to(ROOT / relative).as_posix(), (2026, 10, 15, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    z.writestr(info, file.read_bytes())
        revision = '6d3a9225bee88fed37c08daf685c973212a30a3e'
        paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', revision], cwd=ROOT, text=True).splitlines()
        for name in paths:
            p = Path(name)
            if name == 'CMakeLists.txt' or (p.suffix in ['.cpp', '.h', '.slang'] and
                 (len(p.parts) == 1 or p.parts[0] in ['Experiments', 'Rendering', 'Triangulation', 'Import', 'Shaders'])):
                data = subprocess.check_output(['git', 'show', f'{revision}:{name}'], cwd=ROOT)
                info = zipfile.ZipInfo('five_candidates_6d3a922/' + name, (2026, 10, 15, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, data)
        provenance = ROOT / 'analysis/gpu/five_candidates/measured_source'
        for file in sorted(provenance.iterdir()):
            if file.is_file():
                info = zipfile.ZipInfo('five_candidates_6d3a922/provenance/' + file.name, (2026, 10, 15, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, file.read_bytes())

    assets = {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted((ROOT / 'Data/Models').rglob('*.obj'))}
    manifest = dict(schema=1, purpose='Exact recorded workload inventory for a separate macOS/Metal implementation; not an implemented Metal runner.',
                    documented_checkout=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    includes_raw_gpu_samples=False, includes_uploaded_mesh_buffers=False,
                    case_order_file=plan_path.name, case_order_sha256=sha(plan_path),
                    measured_sources_file=archive.name, measured_sources_sha256=sha(archive),
                    totals=dict(sorted(totals.items())), asset_sha256=assets, geometry_catalog=catalog, runs=runs)
    destination = DEST / 'surface_replication_manifest.json'
    destination.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    guide = DEST.parent / 'MACOS_SURFACE_REPLICATION.md'
    if guide.exists():
        content = guide.read_text(encoding='utf-8')
        catalogue = ['| Portable geometry identifier | Vertices | Triangles | Radius | Strict budget (world units) |',
                     '|---|---:|---:|---:|---:|']
        for name, values in sorted(catalog.items()):
            catalogue.append(f"| `{name}` | {values['OutputVertices']} | {values['OutputTriangles']} | {values['Radius']} | {values['ReferenceErrorBudget']} |")
        hashes = ['```text'] + [f'{value}  {name}' for name, value in assets.items()] + ['```']
        source_path = ROOT / 'analysis/gpu/all_models/confirm_sphere_lit_1080_q8.csv'
        schemas = []
        for label, path in [('Case summary', source_path), ('Raw samples', source_path.with_suffix('.raw.csv')),
                            ('Raster metrics', source_path.with_suffix('.raster.csv'))]:
            with path.open() as stream:
                header = stream.readline().strip()
            schemas += [f'**{label}** (line wrapping below is for display only):', '', '```text',
                        '\n'.join(textwrap.wrap(header, width=112, break_long_words=False, break_on_hyphens=False)), '```', '']
        for label, lines in [('SOURCE_CATALOGUE', catalogue), ('ASSET_HASHES', hashes), ('SCHEMAS', schemas)]:
            start, end = f'<!-- GENERATED_{label} -->', f'<!-- END_GENERATED_{label} -->'
            block = start + '\n\n' + '\n'.join(lines) + '\n\n' + end
            if end in content:
                content = re.sub(re.escape(start) + '.*?' + re.escape(end), lambda _: block, content, flags=re.S)
            else:
                content = content.replace(start, block)
        guide.write_text(content, encoding='utf-8', newline='\n')
    print(f'Wrote {destination.name}: {len(runs)} runs, 109536 cases, 7790592 raw samples; {len(catalog)} geometry inputs.')
    print(f'{plan_path.name}: {plan_path.stat().st_size} bytes; {archive.name}: {archive.stat().st_size} bytes.')


if __name__ == '__main__':
    main()
