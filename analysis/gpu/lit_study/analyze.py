"""Validate untouched observations and summarize matched triangulation cases."""
import csv
import json
import math
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="")))
raw = defaultdict(list)
for r in csv.DictReader(path.with_suffix(".raw.csv").open(newline="")):
    raw[int(r["Case"])].append(r)
for r in rows:
    samples = raw[int(r["Case"])]
    assert len(samples) == int(r["Samples"]) == int(r["ExpectedSamples"])
    assert r["CaseComplete"] == "1"
    if "TotalSceneEdgeLength3D" in r:
        assert math.isclose(float(r["TotalSceneEdgeLength3D"]), float(r["TotalEdgeLength3D"]) * int(r["Instances"]), rel_tol=2e-9)
    assert all(s["RunId"] == r["RunId"] for s in samples)
    assert [int(s["Sample"]) for s in samples] == list(range(len(samples)))
    for kind, key in [("Draw", "GPU"), ("Pass", "GPU_Pass")]:
        values = [float(s[f"GPU_{kind}_ms"]) for s in samples]
        assert all(math.isfinite(v) and v > 0 for v in values)
        for name, value in [("Mean", st.mean(values)), ("Median", st.median(values)),
                            ("StdDev", st.pstdev(values)), ("Min", min(values)), ("Max", max(values))]:
            column = f"{key}{'_' if key == 'GPU' else ''}{name}_ms"
            assert math.isclose(value, float(r[column]), rel_tol=2e-8, abs_tol=2e-9)
    assert all(float(s["GPU_Pass_ms"]) >= float(s["GPU_Draw_ms"]) for s in samples)

def key(r):
    return tuple(r[n] for n in ["Geometry", "Block", "View", "MSAA", "Shader", "Instances", "Width", "Height"])

baseline = {key(r): r for r in rows if r["Method"] == "Source"}
groups = defaultdict(list)
for r in rows:
    assert key(r) in baseline
    b = baseline[key(r)]
    assert r["OutputTriangles"] == b["OutputTriangles"]
    if r["Method"] == "SourceControl":
        assert all(r[n] == b[n] for n in ["TotalEdgeLength3D", "ProjectedEdgeLength", "DiagnosticHelpers", "DiagnosticActiveLanes"])
    groups[(r["Geometry"], r["Method"], int(r["MSAA"]))].append((r, b))

metrics = ["TotalEdgeLength3D", "ProjectedEdgeLength", "DiagnosticHelpers", "DiagnosticActiveLanes",
           "GPU_Mean_ms", "GPU_PassMean_ms", "GPU_Median_ms", "GPU_PassMedian_ms"]
summary = []
for (geometry, method, msaa), pairs in sorted(groups.items()):
    out = dict(Geometry=geometry, Method=method, MSAA=msaa, Cases=len(pairs),
               Valid=all(r["ValidForRanking"] == "1" for r, _ in pairs),
               Triangles=int(pairs[0][0]["OutputTriangles"]),
               ConstructionCPU_ms=float(pairs[0][0]["ConstructionCPU_ms"]),
               Flips=int(pairs[0][0]["Flips"]))
    for metric in metrics:
        value = st.mean(float(r[metric]) for r, _ in pairs)
        reference = st.mean(float(b[metric]) for _, b in pairs)
        out[metric] = value
        out[metric + "_ChangePct"] = 100 * (value / reference - 1)
    block_ratios = defaultdict(list)
    for r, b in pairs:
        block_ratios[r["Block"]].append((float(r["GPU_PassMean_ms"]), float(b["GPU_PassMean_ms"])))
    changes = [100 * (sum(a for a, b in ps) / sum(b for a, b in ps) - 1) for ps in block_ratios.values()]
    out["PassChange_BlockMinPct"] = min(changes)
    out["PassChange_BlockMaxPct"] = max(changes)
    summary.append(out)

output = path.with_suffix(".summary.json")
output.write_text(json.dumps(summary, indent=2))
print(f"Validated {len(rows)} cases, {sum(map(len, raw.values()))} raw samples: {path.name}")
print("model       method       MSAA valid  3D%    screen% helpers% active% draw_ms draw% pass_ms pass% block range")
for r in summary:
    def v(n): return r[n + "_ChangePct"]
    print(f'{r["Geometry"]:12} {r["Method"]:13} {r["MSAA"]}x {str(r["Valid"]):5} '
          f'{v("TotalEdgeLength3D"):6.1f} {v("ProjectedEdgeLength"):7.1f} '
          f'{v("DiagnosticHelpers"):7.1f} {v("DiagnosticActiveLanes"):7.1f} '
          f'{r["GPU_Mean_ms"]:.4f} {v("GPU_Mean_ms"):6.1f} '
          f'{r["GPU_PassMean_ms"]:.4f} {v("GPU_PassMean_ms"):6.1f} '
          f'{r["PassChange_BlockMinPct"]:.1f}..{r["PassChange_BlockMaxPct"]:.1f}')
