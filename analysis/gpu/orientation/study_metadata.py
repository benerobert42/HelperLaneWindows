"""Local reproduction manifest and coarse telemetry summary; no GPU settings changed."""
import csv
import hashlib
import json
from pathlib import Path
import subprocess

root=Path(__file__).parent
sample=root.parent.parent.parent
files=list((sample/'Data/Models').glob('*.obj'))+list((root/'models').glob('*.obj'))
files+=list(root.glob('*.py'))+[root/'run_sweep.ps1']
timer=sample.parent.parent.parent/'Source/Falcor/Core/API/GpuTimer.cpp'
files += [timer] if timer.exists() else []
manifest=dict(SampleCommit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=sample,text=True).strip(),
              Driver=subprocess.check_output(['nvidia-smi','--query-gpu=driver_version','--format=csv,noheader'],text=True).strip(),
              Files={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
              Runs={p.stem:p.read_text() for p in root.glob('*.metadata.txt') if 'Status: Complete' in p.read_text()})
(root/'study_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
lines=[]
for path in sorted(root.glob('*.telemetry.csv')):
    complete=[]; malformed=0
    with path.open(newline='') as f:
        for r in csv.DictReader(f):
            try:
                normalized={k.strip():v for k,v in r.items() if k}
                fields={k:float(normalized[k].strip().split()[0]) for k in
                    ['clocks.current.graphics [MHz]','clocks.current.memory [MHz]',
                     'temperature.gpu','power.draw [W]','utilization.gpu [%]']}
                fields['pstate']=normalized['pstate'].strip()
            except (KeyError,ValueError,AttributeError,IndexError):
                malformed+=1; continue
            complete.append(fields)
    # Sampling is coarse, not aligned with draw queries. Avoid treating this as
    # per-case clock attribution or a hardware counter measurement.
    active=[r for r in complete if r['utilization.gpu [%]']>=70]
    if active:
        lines.append(f'{path.name}: {len(complete)} complete readings, {len(active)} at >=70% utilization; {malformed} incomplete rows.')
        for field in ['clocks.current.graphics [MHz]','clocks.current.memory [MHz]','temperature.gpu','power.draw [W]']:
            lines.append(f'  {field.strip()}: {min(r[field] for r in active):g} .. {max(r[field] for r in active):g}')
        lines.append('  power states: '+', '.join(sorted({r['pstate'] for r in active})))
lines.insert(0,'One-second nvidia-smi observations. Clocks are not locked; telemetry cannot identify individual timed draws.')
(root/'telemetry_summary.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
