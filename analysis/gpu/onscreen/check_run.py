"""Validate the selected one-window study without modifying its observations.

Safe while the writer is running: inspect only newline-terminated records.
Raw samples and raster metrics are checked for fully recorded cases.
"""
import csv
import io
import json
import math
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STEM = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "axis12_n100_w30/study"
LIVE_TOLERANCE = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
METHODS = {"Source", "SourceControl", "CDT", "CDTFlip", "CDTFlip3D", "Greedy", "Heap3D", "HeapPlane", "HeapQuadView"}
MODELS = {
    "obj:Data/Models/ResearchCandidates/SweptWing.obj": (1538, 3072),
    "obj:Data/Models/ResearchCandidates/FacetedArch.obj": (2114, 4224),
    "torus_skew": (8192, 16384),
    "obj:Data/Models/ResearchCandidates/SpiralRamp.obj": (7913, 15360),
    "obj:Data/Models/ResearchCandidates/TrefoilKnot.obj": (24576, 49152),
    "suzanne": (7958, 15744),
    "obj:Data/Models/ResearchCandidates/ToothedWheel.obj": (3072, 6144),
    "cylinder_skew": (18624, 36864),
}


# Optional plan extends the same checks to an explicitly specified model/method set.
if len(sys.argv) > 3:
    plan = json.loads(Path(sys.argv[3]).read_text())
    METHODS = set(plan["methods"])
    MODELS = {name: tuple(counts) for name, counts in plan["geometries"].items()}
CASES_PER_MODEL = len(METHODS) * 24


def read(suffix):
    path = STEM.with_suffix(suffix)
    if not path.exists():
        return []
    data = path.read_bytes()
    if b"\n" not in data:
        return []
    return list(csv.DictReader(io.StringIO(data[:data.rfind(b"\n") + 1].decode("utf-8-sig"))))


report = {"fatal_errors": [], "timing_control_failures": [], "live_timing_control_failures": [],
          "live_tolerance_pct": LIVE_TOLERANCE, "minimum_stable_views": 6, "models": [], "complete": False}
try:
    rows, raw, raster = read(".csv"), read(".raw.csv"), read(".raster.csv")
    meta = STEM.with_suffix(".metadata.txt").read_text() if STEM.with_suffix(".metadata.txt").exists() else ""
    report["complete"] = "\nStatus: Complete\n" in meta
    forward_only = "GeometryValidationPolicy: ForwardOnlyAdmissionV1" in meta
    if forward_only and len(sys.argv) <= 3:
        METHODS = {"Source", "SourceControl", "CDT", "CDTFlip", "CDTFlip3D", "Heap3D", "HeapPlane"}
        CASES_PER_MODEL = len(METHODS) * 24
    assert len({r["Case"] for r in rows}) == len(rows), "Duplicate case IDs"
    lookup, grouped, samples = {}, defaultdict(list), defaultdict(list)
    for r in raw:
        samples[r["Case"]].append(r)
    raster = {r["Case"]: r for r in raster}
    for r in rows:
        assert r["Geometry"] in MODELS and r["Method"] in METHODS, "Unexpected model or method"
        assert all(r[k] == v for k, v in {
            "Width": "1920", "Height": "1080", "Instances": "64", "Samples": "100", "ExpectedSamples": "100",
            "Warmup": "30", "Block": "0", "QueueDepth": "8", "Shader": "LitPreview", "Depth": "1", "Meshopt": "1",
            "CaseComplete": "1"}.items()), "Wrong render/measurement settings"
        if forward_only:
            assert r["ReferenceAuditDirections"] == "1", "Forward-only policy must report one audit direction"
            assert float(r["ReverseSampledReferenceError"]) == 0, "Reverse error must be marked unmeasured"
        v, msaa = int(r["View"]), int(r["MSAA"])
        assert 0 <= v < 12 and msaa in (1, 4)
        expected = [0., 0., 0.]
        expected[v // 4] = (45., 135., 225., 315.)[v % 4]
        assert [float(r[k]) for k in ("XRotationDegrees", "YRotationDegrees", "ZRotationDegrees")] == expected, "Wrong transform"
        key = r["Geometry"], v, msaa, r["Method"]
        assert key not in lookup, "Duplicate case configuration"
        lookup[key] = r
        grouped[r["Geometry"]].append(r)
        rr = samples[r["Case"]]
        # Summary is flushed before raw/raster; the current tail can still be in flight.
        if len(rr) != 100:
            assert not report["complete"] and r is rows[-1], "Missing samples in an earlier case"
            continue
        assert [int(x["Sample"]) for x in rr] == list(range(100)), "Missing/reordered samples"
        assert all(x["RunId"] == r["RunId"] for x in rr), "Wrong raw run identity"
        for scope, prefix in (("Draw", "GPU_"), ("Pass", "GPU_Pass")):
            values = [float(x[f"GPU_{scope}_ms"]) for x in rr]
            assert all(math.isfinite(x) and x > 0 for x in values), "Invalid timestamps"
            stats = (st.mean(values), st.median(values), st.pstdev(values), min(values), max(values))
            for name, value in zip(("Mean", "Median", "StdDev", "Min", "Max"), stats):
                assert math.isclose(value, float(r[f"{prefix}{name}_ms"]), rel_tol=2e-8, abs_tol=2e-9), "Raw/summary mismatch"
        assert all(float(x["GPU_Pass_ms"]) >= float(x["GPU_Draw_ms"]) for x in rr), "Pass does not enclose draw"
        rm = raster.get(r["Case"])
        # Raster metrics use a buffered stream; unlike raw timestamps, they
        # need not be flushed per case during live monitoring.
        assert rm is not None or not report["complete"], "Missing final raster metrics"
        if rm is not None:
            assert rm["Ordering"] == "cache-overdraw" and int(rm["OutsideViewportTriangles"]) == 0, "Wrong ordering or clipped metrics"
            assert int(rm["ProjectedTriangles"]) == int(r["OutputTriangles"]) * 64
        if r["Method"] in ("Source", "SourceControl"):
            assert (int(r["OutputVertices"]), int(r["OutputTriangles"])) == MODELS[r["Geometry"]], "Wrong source geometry"
            assert r["MeshValid"] == r["SurfaceEquivalent"] == "1" and float(r["DiagnosticActiveLanes"]) > 0, "Invalid source/control draw"
            assert r["PipelineStatisticsAvailable"] == "1" and int(r["HardwareInputPrimitives"]) == 64 * int(r["OutputTriangles"])
    for model, rs in grouped.items():
        item = {"model": model, "cases": len(rs), "finished": len(rs) == CASES_PER_MODEL, "controls": []}
        for msaa in (1, 4):
            controls = []
            for v in range(12):
                source = lookup.get((model, v, msaa, "Source"))
                control = lookup.get((model, v, msaa, "SourceControl"))
                if source is None or control is None:
                    continue
                for k in ("OutputVertices", "OutputTriangles", "TotalEdgeLength3D", "ProjectedEdgeLength", "SurfaceArea"):
                    assert source[k] == control[k], "Source and SourceControl geometries differ"
                controls.append(100 * (float(control["GPU_Mean_ms"]) / float(source["GPU_Mean_ms"]) - 1))
            item["controls"].append({"msaa": msaa, "paired_views": len(controls),
                "stable_views": sum(abs(x) <= 2 for x in controls),
                "within_live_tolerance_views": sum(abs(x) <= LIVE_TOLERANCE for x in controls), "deviations_pct": controls,
                "mean_abs_deviation_pct": st.mean(map(abs, controls)) if controls else None})
        if item["finished"]:
            item["timing_usable"] = any(c["stable_views"] >= 6 for c in item["controls"])
            if not item["timing_usable"]:
                report["timing_control_failures"].append(model + ": fewer than 6/12 strict SourceControl views in both MSAA settings")
            item["live_tolerance_passed"] = any(c["within_live_tolerance_views"] >= 6 for c in item["controls"])
            if not item["live_tolerance_passed"]:
                report["live_timing_control_failures"].append(model + ": sustained control disagreement beyond live tolerance in both MSAA settings")
        report["models"].append(item)
    meta = STEM.with_suffix(".metadata.txt").read_text() if STEM.with_suffix(".metadata.txt").exists() else ""
    report["complete"] = "\nStatus: Complete\n" in meta
    report["cases"] = len(rows)
    report["raw_samples"] = sum(len(samples[r["Case"]]) for r in rows)
    if report["complete"]:
        assert len(rows) == len(MODELS) * CASES_PER_MODEL and len(raw) == len(rows) * 100 and len(raster) == len(rows), "Incomplete final case/sample coverage"
        assert all(x["finished"] for x in report["models"]) and len(grouped) == len(MODELS)
except (AssertionError, KeyError, ValueError) as e:
    report["fatal_errors"].append(str(e))

STEM.with_suffix(".quality.json").write_text(json.dumps(report, indent=2) + "\n")
print("Cases:", report.get("cases"), "raw samples:", report.get("raw_samples"), "complete:", report["complete"])
for item in report["models"]:
    print(item["model"], f'{item["cases"]}/{CASES_PER_MODEL}', "; ".join(
        f'{c["msaa"]}x strict {c["stable_views"]}/{c["paired_views"]}, within live tolerance {c["within_live_tolerance_views"]}/{c["paired_views"]}' for c in item["controls"]))
print("Fatal errors:", report["fatal_errors"])
print("Strict timing flags:", report["timing_control_failures"])
print("Live timing failures:", report["live_timing_control_failures"])
sys.exit(1 if report["fatal_errors"] or report["live_timing_control_failures"] else 0)
