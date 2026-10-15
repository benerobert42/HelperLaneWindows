"""Pair methods with Source at the same block/view/MSAA/shader settings."""
import argparse
import csv
import json
import statistics as stats
from collections import defaultdict
from pathlib import Path


CASE_FIELDS = ("RunId", "Geometry", "Block", "View", "MSAA", "Shader", "Instances", "Width", "Height", "Depth", "Meshopt")
GROUP_FIELDS = ("RunId", "Geometry", "Method", "MSAA", "Shader", "Instances", "Width", "Height", "Depth", "Meshopt")
RESULT_FIELDS = (*GROUP_FIELDS, "Pairs", "Method_ms", "Source_ms", "TimeChange_pct", "MethodPass_ms", "SourcePass_ms", "PassChange_pct", "BlockMin_pct", "BlockMax_pct", "HelperChange_pct", "ActiveChange_pct", "ScheduledLaneChange_pct", "SourceHelperFraction", "EdgeChange_pct")


def identity(row, fields):
    return tuple(row.get(k, "") for k in fields)


def change(value, baseline):
    return 100 * (value / baseline - 1) if baseline else None


def analyze(path):
    with Path(path).open(newline="") as f:
        rows = list(csv.DictReader(f))
    source = {}
    for r in rows:
        if r["Method"] == "Source":
            key = identity(r, CASE_FIELDS)
            if key in source:
                raise ValueError(f"Duplicate Source case {key}; provide distinct RunId values for combined runs.")
            source[key] = r
    groups = defaultdict(list)
    geometry = {}
    for r in rows:
        geometry[identity(r, GROUP_FIELDS)] = {
            k: r.get(k, "") for k in ["ConstructionCPU_ms", "OutputTriangles", "SourceTriangles",
                             "FailedPatches", "DuplicateTriangles", "NonManifoldEdges",
                             "WindingErrors", "MissingBoundaryEdges", "UnexpectedBoundaryEdges",
                             "DegenerateTriangles", "SampledReferenceError",
                             "Flips", "TotalEdgeLength3D", "SurfaceArea"]}
        audit_row = geometry[identity(r, GROUP_FIELDS)]
        audit_row["MeshValid"] = r.get("MeshValid", "1" if r["ValidForRanking"] == "1" else
                                       "0" if int(r.get("DiagnosticActiveLanes", "0")) > 0 else None)
        # Visibility is per case; it is deliberately excluded from mesh audit data.
        baseline = source.get(identity(r, CASE_FIELDS))
        if baseline and r["ValidForRanking"] == "1" and baseline["ValidForRanking"] == "1":
            groups[identity(r, GROUP_FIELDS)].append((r, baseline))
    result = []
    for key, pairs in sorted(groups.items()):
        blocks = defaultdict(list)
        for r, b in pairs:
            blocks[r["Block"]].append((r, b))
        block_changes = []
        for block in blocks.values():
            mt = sum(float(r["GPU_Median_ms"]) for r, b in block)
            bt = sum(float(b["GPU_Median_ms"]) for r, b in block)
            block_changes.append(change(mt, bt))
        mt = stats.mean(float(r["GPU_Median_ms"]) for r, b in pairs)
        bt = stats.mean(float(b["GPU_Median_ms"]) for r, b in pairs)
        mh = sum(int(r["DiagnosticHelpers"]) for r, b in pairs)
        bh = sum(int(b["DiagnosticHelpers"]) for r, b in pairs)
        ma = sum(int(r["DiagnosticActiveLanes"]) for r, b in pairs)
        ba = sum(int(b["DiagnosticActiveLanes"]) for r, b in pairs)
        has_pass = all("GPU_PassMedian_ms" in r and "GPU_PassMedian_ms" in b for r, b in pairs)
        mp = stats.mean(float(r["GPU_PassMedian_ms"]) for r, b in pairs) if has_pass else None
        bp = stats.mean(float(b["GPU_PassMedian_ms"]) for r, b in pairs) if has_pass else None
        result.append(dict(zip(GROUP_FIELDS, key),
                           Pairs=len(pairs), Method_ms=mt, Source_ms=bt,
                           TimeChange_pct=change(mt, bt),
                           MethodPass_ms=mp, SourcePass_ms=bp, PassChange_pct=change(mp, bp) if has_pass else None,
                           BlockMin_pct=min(c for c in block_changes if c is not None) if any(c is not None for c in block_changes) else None, BlockMax_pct=max(c for c in block_changes if c is not None) if any(c is not None for c in block_changes) else None,
                           HelperChange_pct=change(mh, bh),
                           ActiveChange_pct=change(ma, ba),
                           ScheduledLaneChange_pct=change(mh + ma, bh + ba),
                           SourceHelperFraction=bh / (bh + ba) if bh + ba else None,
                           EdgeChange_pct=change(float(pairs[0][0]["TotalEdgeLength3D"]), float(pairs[0][1]["TotalEdgeLength3D"]))))
    output = Path(path).with_suffix(".paired.csv")
    with output.open("w", newline="") as f:
        w = csv.DictWriter(f, RESULT_FIELDS)
        w.writeheader()
        w.writerows(result)
    print(f"{path}: {len(rows)} cases; {sum(int(r['Samples']) for r in rows)} timed draws; {len(result)} valid groups")
    print("Geometry | Method | MSAA | shader | draw us | time delta % | block range % | helper delta % | edge delta %")
    def display(v, digits=2):
        return f"{v:+.{digits}f}" if v is not None else "undefined"
    for r in result:
        if r["Method"] in ("CDT", "CDTFlip", "Heap3D", "HeapPlane"):
            print(f"{r['Geometry']} | {r['Method']} | {r['MSAA']} | {r['Shader']} | "
                  f"{1000*r['Method_ms']:.2f} | {display(r['TimeChange_pct'])} | "
                  f"{display(r['BlockMin_pct'])}..{display(r['BlockMax_pct'])} | "
                  f"{display(r['HelperChange_pct'])} | {display(r['EdgeChange_pct'], 3)}")
    Path(path).with_suffix(".audit.json").write_text(json.dumps(
        {"/".join(key): v for key, v in geometry.items()}, indent=2))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+")
    for file in parser.parse_args().files:
        analyze(file)
