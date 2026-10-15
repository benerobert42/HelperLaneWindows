"""Combine validated summaries without changing original observations."""
import csv
import json
from pathlib import Path

root = Path(__file__).parent
data = []
for run in ["single", "grid", "grid_cache"]:
    metadata = (root / f"{run}.metadata.txt").read_text()
    assert "Status: Complete" in metadata
    for row in json.loads((root / f"{run}.summary.json").read_text()):
        row = dict(Run=run, **row)
        row["Instances"] = 1 if run == "single" else 64
        row["Meshopt"] = run == "grid_cache"
        row["TotalSceneEdgeLength3D"] = row["TotalEdgeLength3D"] * row["Instances"]
        data.append(row)
with (root / "comparison.csv").open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=list(data[0]))
    writer.writeheader()
    writer.writerows(data)

models = ["ellipsoid", "blob", "torus_skew", "cylinder_skew", "suzanne", "bunny", "teapot"]
lookup = {(r["Run"], r["Geometry"], r["Method"], r["MSAA"]): r for r in data}

lines = ["Lit triangulation study", "", "RTX 4050 Laptop / D3D12 / Release / 1920x1080.",
         "Each method/MSAA/model uses 4 fixed views, 3 randomized blocks and 64 retained samples per case.",
         "Warmup: every timed/diagnostic/MSAA pipeline, 1 second sustained draws per model, 64 draws per case.",
         "Timing: fence-retired ring of 8 query pairs; Falcor retains its normal transient-frame resource limits.",
         "GPU draw is the existing GraphEx raster lighting. GPU pass is clear + draw + 4x resolve when applicable.",
         "Pass excludes display blit, UI, diagnostics/readback and presentation; it is not whole-app frametime.",
         "3D edge lengths count unique undirected indexed edges in normalized model units, once per mesh.",
         "TotalSceneEdgeLength3D multiplies that sum by instance count. Screen lengths sum pixels over all instances.",
         "Screen sums include occluded edges; cameras keep these fitted models inside the frustum.",
         "Diagnostic helpers are quad-waste proxies. The uninstrumented lit shader has no quad/derivative operations.",
         "Source normals, vertex/triangle counts, lighting, camera and viewport are fixed within each comparison.",
         "Meshopt is existing vertex-cache and overdraw reordering only; source vertex IDs/normals stay fixed.",
         "HeapScreen/HeapQuad blend 25% 3D length and 75% normalized expected-view Euclidean/L1 pixel length.",
         "Their central-instance training view set equals the evaluated view set; unseen-camera behavior is untested.",
         "Every heap keeps topology, convexity, winding, 30-degree crease and sampled source-distance guards.",
         "The 0.1%-bounds error budget is a sampled check, not a global Hausdorff/intersection or image-error guarantee.",
         "Raw data is intact. Invalid methods remain in comparison.csv with Valid=False and are not ranked.", "",
         "Findings:",
         "Large reductions are real on skewed inputs: torus about 51%, tweaked cylinder about 29%.",
         "The OBJ presets and the current ellipsoid/blob have little removable edge length under these guards.",
         "At 64 instances, tiny triangles weaken edge-length/helper correlation; primitive count and depth/order still matter.",
         "At 4x MSAA, the torus benefits much more than at 1x. Resolve cost dilutes whole-pass savings.",
         "Suzanne's large original-method advantage nearly disappears after applying the same index optimization to Source.",
         "Projected objectives help some cases modestly and regress others. Lower summed edge length is not a GPU-time guarantee.",
         "Next score: local projected triangle quad cost and MSAA coverage, with visibility weights, retaining surface guards.",
         "Tune against held-out views and measured GPU pass time; include cache/overdraw ordering before comparing.",
         "Treat <=2% timing differences cautiously; retain SourceControl and block ranges rather than claiming universal wins.", ""]
for run in ["single", "grid", "grid_cache"]:
    controls = [r for r in data if r["Run"] == run and r["Method"] == "SourceControl"]
    lines.append(f'{run}: largest absolute mean SourceControl pass change = '
                 f'{max(abs(r["GPU_PassMean_ms_ChangePct"]) for r in controls):.2f}%; '
                 f'largest block change = {max(max(abs(r["PassChange_BlockMinPct"]), abs(r["PassChange_BlockMaxPct"])) for r in controls):.2f}%.')
    lines.append("Model         Method     MSAA 3D units  Screen pixels Helpers avg Draw ms Pass ms Pass change")
    for model in models:
        for method in ["Source", "CDTFlip", "Heap3D", "HeapScreen", "HeapQuad"]:
            r = lookup[run, model, method, 4]
            lines.append(f'{model:13} {method:10} 4x {r["TotalEdgeLength3D"]:8.2f} '
                         f'{r["ProjectedEdgeLength"]:12.0f} {r["DiagnosticHelpers"]:11.0f} '
                         f'{r["GPU_Mean_ms"]:.5f} {r["GPU_PassMean_ms"]:.5f} '
                         f'{r["GPU_PassMean_ms_ChangePct"]:+.2f}% {"" if r["Valid"] else "INVALID"}')
    lines.append("")
(root / "results.txt").write_text("\n".join(lines))
print(f"Combined {len(data)} method/model/MSAA summaries in comparison.csv and results.txt.")
