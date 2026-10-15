"""Compare existing MeshDraw PIX scopes with the fence-retired query samples.

Captures must use profile.ps1: lit, one view/MSAA/block, 128 samples and
64 warmup draws, two methods, no 1x screenshot until after each diagnostic.
Nsight times stay separate from the unprofiled connectivity study.
"""
from pathlib import Path
import csv
import sqlite3
import statistics as st

root = Path(__file__).resolve().parent
output = []
for path in sorted(root.glob('*.sqlite')):
    db = sqlite3.connect(path)
    markers = list(db.execute('''SELECT w.start,w.end FROM DX12_WORKLOAD w
        JOIN StringIds t ON t.id=w.textId WHERE t.value='MeshDraw' ORDER BY w.start'''))
    batches = list(db.execute('''SELECT w.start,w.end FROM DX12_WORKLOAD w
        JOIN StringIds n ON n.id=w.nameId
        WHERE n.value='ID3D12CommandQueue::ExecuteCommandLists' ORDER BY w.start'''))
    cases = list(csv.DictReader(path.with_suffix('.csv').open()))
    raw = list(csv.DictReader(path.with_suffix('.raw.csv').open()))
    assert len(cases) == 2
    # A Source screenshot adds one draw only for 1x; priming precedes all cases.
    tail_count = sum(int(c['Warmup']) + int(c['Samples']) + 1 + (c['Method'] == 'Source' and c['MSAA'] == '1') for c in cases)
    tail = markers[-tail_count:]
    offset = 0
    for c in cases:
        warm, count = int(c['Warmup']), int(c['Samples'])
        sampled = tail[offset + warm:offset + warm + count]
        offset += warm + count + 1 + (c['Method'] == 'Source' and c['MSAA'] == '1')
        gpu = [(b-a)/1e6 for a,b in sampled]
        queries = [float(r['GPU_Draw_ms']) for r in raw if r['Case'] == c['Case']]
        assert len(queries) == count
        # Verify chronological sample alignment, rather than trusting aggregate means.
        error = [abs(a-b) for a,b in zip(gpu,queries)]
        assert max(error) < .01, (path.name, c['Method'], max(error))
        # Queue gap between surrounding submitted batches, not draw-scope spacing
        # (which also includes clear/resolve/query work).
        surrounding = [b for b in batches if b[0] <= sampled[-1][1] and b[1] >= sampled[0][0]]
        gaps = [max(0, b[0]-a[1])/1e6 for a,b in zip(surrounding,surrounding[1:])]
        span = surrounding[-1][1] - surrounding[0][0]
        idle = sum(gaps)*1e6/span if span else 0
        output.append(dict(Capture=path.stem, Method=c['Method'], MSAA=c['MSAA'], Height=c['Height'],
                           Samples=count, NsightDrawMean_ms=st.mean(gpu), QueryDrawMean_ms=st.mean(queries),
                           MaxAbsoluteSampleDifference_ms=max(error), QueueGapMean_ms=st.mean(gaps),
                           QueueIdleFraction=idle))
    diagnostics = list(db.execute('SELECT * FROM DIAGNOSTIC_EVENT'))
    print(path.name, 'diagnostic events:', diagnostics)
assert output
with (root/'trace_summary.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(output[0]))
    writer.writeheader()
    writer.writerows(output)
for row in output:
    print(row)
