"""Reproduction record and interpretation separate from raw observations."""
import csv,json,hashlib,subprocess
from pathlib import Path
root=Path(__file__).parent; sample=root.parents[2]
def read(name):
    with (root/name).open(newline='') as f: return list(csv.DictReader(f))
rows=read('summary.csv'); recommendations=read('geometry_recommendations.csv')
runs={p.stem:p.read_text() for p in root.glob('*.metadata.txt') if 'Status: Complete' in p.read_text()}
manifest={'FinalSampleGitCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=sample,text=True).strip(),
          'ProvenanceNote':'Historical run metadata and compiled-source/confirmation-source snapshots identify the measured binaries; final HEAD adds explicit includes and commits the measured implementation.',
          'Driver':subprocess.check_output(['nvidia-smi','--query-gpu=driver_version','--format=csv,noheader'],text=True).strip(),
          'Runs':runs,'Files':{}}
inputs=list(root.glob('*.py'))+list(root.glob('*.ps1'))+list(root.glob('*.cjs'))+list((sample/'Data/Models').glob('*.obj'))+list((root/'../orientation/models').glob('*.obj'))
inputs += [p for p in root.glob('*.csv') if p.with_suffix('.metadata.txt').exists() or p.name.endswith(('.raw.csv','.raster.csv'))]
for p in inputs:
    digest=hashlib.sha256()
    with p.open('rb') as f:
        while chunk:=f.read(1024*1024): digest.update(chunk)
    manifest['Files'][str(p)]=digest.hexdigest()
(root/'study_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
lines=['Triangle connectivity and GPU cost at 1080p','',
       'Scope',
       '27 named procedural fixtures and imported models, including Suzanne, bunny, teapot and two GraphEx implicits exported through OBJ.',
       '23 general variants cover Source/control, CDT, projected/world CDT flips, greedy point-set, LMT variants, projection-inspired greedy, world/plane/projected heaps, current/stale/warm projected heaps, relaxed budgets and vertex-star neighborhoods.',
       'Applicable procedural grids additionally test global alignment/opposite diagonals; the regular torus tests true component/genus surgery.',
       'Vertex positions, vertex storage/order, used vertex counts and final triangle counts stay fixed within every comparison. No LOD, subdivision or density reduction is a candidate.',
       'All render the same reviewed GraphEx raster lighting, source normals, materials, background and camera/instance layout. No ray tracing.', '',
       'Measurement',
       '1920x1080, 64 instances, 24 separate-axis rotations (8 around each axis; 22 unique static poses), 1x and 4x MSAA.',
       'Randomized method/view order, queue depth 8 and one second GPU priming after all candidate/PSO preparation.',
       'Screen: 2 blocks, 64 warmup draws, 32 measured samples/case. Confirm/selected/special: 3 blocks, 256 warmups, 128 samples/case.',
       'GPU draw timestamps exclude UI, present, clear, resolve and diagnostic counters. GPU pass includes clear/draw and 4x resolve.',
       'D3D12 pipeline-statistics queries count a separate uninstrumented draw after timing. Helpers/active lanes come from another diagnostic shader.',
       'API pixel/vertex invocation counts measure work; they do not directly identify stalls, wave occupancy or a critical GPU stage.',
       'Every raw sample remains; means/medians/stddev/min/max reconstructed from raw timestamps, sequential sample IDs checked, pass>=draw checked.',
       'SourceControl is identical geometry independently uploaded. Practical gate: <=2% mean and <=3% worst-block deviation per pose; not a statistical significance test.',
       'Reports average matched pose percentage changes; aggregate-ratio columns are also available. Excluded cases and angle ranges remain recorded.',
       'Meshoptimizer cache+overdraw policy is identical, with fixed vertex storage. Output triangle/index order can still differ, so connectivity can interact with cache and early-depth order.', '',
       'Geometry and method eligibility',
       'Duplicate/degenerate triangles, nonmanifold edges, winding consistency, boundary preservation, used counts, connected components and Euler characteristic are audited.',
       'Source-to-candidate and candidate-to-source distances sample vertices, unique edge midpoints and triangle centroids. This is not a Hausdorff-distance or self-intersection guarantee.',
       'Strict means <=0.1% source bounding-box diagonal in both sampled directions. Loose/Wide explicitly permit 0.5%/2%. Patch methods have a 2% audit ceiling, with strict equivalence reported separately.',
       'SourceFallback is an algorithm failure to produce a new mesh, even if rendering that source is fast. It cannot support a connectivity gain recommendation.',
       'The reverse audit newly reveals several cases missed by one-way validation. Marginal threshold failures are reported as geometry tradeoffs, not silently removed or called equivalent.',
       'The source mesh is the reference. A procedural source with bad chords can itself poorly approximate its analytic surface; this audit measures deviation from the source, not analytic accuracy.', '',
       'What triangulation is standard?',
       'Imported triangle faces are normally retained. Quads/n-gons need face triangulation: common choices include ear clipping, beauty/shortest-diagonal splits and planar constrained Delaunay.',
       'Our Source preserves triangle faces and ear-clips polygon faces in their local projection; it is not automatically a deliberately bad baseline.',
       'Our CDT projects nearly planar surface patches and constrains their boundaries. It optimizes triangle angles in that chart; it does not optimize screen-space coverage or GPU time.',
       'A surface remesher/reconstructor and a volumetric tetrahedral mesher solve different problems. Our methods render surface triangles, not volume tetrahedra.',
       'The GP backend is PCL-inspired projected-neighborhood/greedy-graph code, not a call to PCL reconstruction. PCL itself targets unordered points with normals.',
       'LMT here is a local-witness/greedy completion/flip heuristic, not a proof of global minimum-weight triangulation.', '',
       'Connectivity experiments',
       'Heap3D/Plane use local max-heap edge flips with feature-angle, convexity and fixed-source sampled guards.',
       'Star variants first run the corresponding world heap, replace convex interior stars (valence 4..12) using polygon dynamic programming plus reinsertion of the same center into one triangle, then settle with the heap.',
       'The star transaction changes several edges and valence while retaining the same disk boundary, every vertex and triangle count. It explores a bounded subset of cavity retriangulations; it does not change genus.',
       'Global grid alignment/opposite diagonals are fixture-specific larger connectivity changes, with an explicit 2% error ceiling.',
       'TopologySplit re-closes two half-tubes: one torus becomes two closed tori with the same vertex/triangle counts and total Euler characteristic. Closing chords change the surface and are excluded from strict equivalence.',
       'For a closed orientable triangulated manifold, 3F=2E and chi=V-E+F. Fixed V/F and fixed component count therefore fix total genus. The surgery trades components against total genus rather than adding a free handle.',
       'The rasterizer does not consume a genus label; topology can affect cost through the resulting triangles, coverage, ordering and visibility.', '',
       'How to interpret gains',
       'Poor planar fans have long thin projected triangles. Coplanarity allows extensive connectivity changes without surface deformation; a well-triangulated planar grid need not benefit.',
       'At 1x, an unchanged planar silhouette/area can shade the same pixel centers with different triangle traversal and packet efficiency. Equal PS invocation counts do not imply equal raster cost.',
       'At MSAA, adjacent primitives can shade the same pixel on different covered sample subsets. Internal boundary connectivity can therefore change pixel-frequency shader invocations.',
       'Large bbox/sliver changes with stable or increased PS/VS counts support a raster-traversal explanation, but do not prove the limiting hardware unit.',
       'Lower VS counts identify changed cache/vertex invocation work; lower PS counts identify changed shading work, including possible early-depth/order effects.',
       'The total edge length is useful for bad connectivity, but is neither a universal predictor nor a complete objective. Regular sources can have little world-length headroom despite many diagonal changes.',
       'A linear expected projected-edge objective can resemble world length under broad orthographic orientations. It has no intrinsic pixel-grid or MSAA term, so it cannot encode every observed resolution/MSAA-dependent effect.',
       'More promising refinement: evaluate triangle/tile/quad coverage at the intended resolution/MSAA, preserve features, and optimize offline or in a resident view bank when amortization supports it.',
       'Avoid per-frame CPU rebuilding without a measured amortization case. Setup includes final audits/order/upload preparation but excludes lazy normal/PSO setup and upload completion.',
       'Confirmation reuses canonical source and grown patches; SourceSetupCPU/PatchSetupCPU/ReferenceSetupCPU report shared preparation separately. Screening included repeated patch growth in backend CPU construction.', '',
       'Data and conclusions',
       'summary.csv contains every method/model/MSAA result with validity, control eligibility, counts, error, CPU cost and counter changes.',
       'view_comparisons.csv retains pose results; mechanisms.csv records measured work evidence without claiming unknown bottlenecks.',
       'geometry_recommendations.csv and conclusions.txt give the geometry-specific recommendations from longer strict comparisons.',
       'Only material mean draw gains beyond 3% with at least 12 stable strict view conditions enter recommendations; this practical threshold is not a p-value.', '',
       'Sources',
       'https://docs.blender.org/manual/en/5.0/modeling/modifiers/generate/triangulate.html',
       'https://doc.cgal.org/latest/Polygon_mesh_processing/index.html',
       'https://cgal.geometryfactory.com/CGAL/doc/main/PMP_Remeshing/index.html',
       'https://doc.cgal.org/latest/Mesh_3/index.html',
       'https://pointclouds.org/documentation/tutorials/greedy_projection.html',
       'https://learn.microsoft.com/en-gb/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules',
       'https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_query_data_pipeline_statistics',
       'https://www.cs.cmu.edu/~kmcrane/Projects/LoopsOnSurfaces/', '']
def row(stage,geometry,method,msaa):
    return next(r for r in rows if r['Stage']==stage and r['Geometry']==geometry and r['Method']==method and r['MSAA']==str(msaa))
def fmt(r,k): return f"{float(r[k]):+.2f}" if r[k]!='' else 'NA'
lines += ['Measured comparisons',
          'Negative changes mean less work/time. Core confirmation unless marked special/selected.',
          'Draw is indexed-draw GPU time; pass also includes clear and MSAA resolve. Neither is complete application/present frame time.',
          'method/model | MSAA | strict eligible/24 | draw % | pass % | world edge % | projected edge % | helpers % | PS % | VS %']
for stage,g,m,msaa in [('confirm','ellipse2d','Heap3D',1),('confirm','ellipse2d','Heap3D',4),
                     ('confirm','ellipse2d','CDT',1),('confirm','ellipse2d','CDT',4),
                     ('special','torus_skew','Heap3D',1),('special','torus_skew','Heap3D',4),
                     ('special','torus_skew','GridAligned',4),
                     ('confirm','cylinder_skew','Heap3D',1),('confirm','cylinder_skew','Heap3D',4),
                     ('confirm','skew2','CDT',4),('confirm','skew2','Heap3D',1),('confirm','skew2','Heap3D',4),
                     ('confirm','skew3','Heap3D',4),('confirm','skew_fold','Heap3D',4),('confirm','skew_saddle','Heap3D',4),
                     ('confirm','ellipsoid','Heap3D',4),('selected','ellipsoid','HeapQuadView',4),
                     ('selected','jittered_planar','HeapQuadView',4),
                     ('selected','obj:analysis/gpu/orientation/models/GraphExTangleCube.obj','HeapPlane',4),
                     ('confirm','suzanne','CDT',4),('confirm','bunny','CDT',4),('confirm','teapot','CDT',4)]:
    r=row(stage,g,m,msaa)
    values=[fmt(r,k) for k in ['DrawVsSourceStrict_pct','PassVsSourceStrict_pct','WorldLengthVsSource_pct',
                              'ScreenLengthVsSource_pct','HelpersVsSource_pct','PSVsSource_pct','VSVsSource_pct']]
    lines.append(f"{stage}:{g}/{m} | {msaa} | {r['StrictEligibleConditions']}/24 | "+' | '.join(values))
lines += ['', 'Practical choice by geometry',
          'Bad planar boundary fans: offline world-edge heap. Ellipse changes retain the exact planar surface and produce very large gains at both MSAA settings. The source fan is especially poor; regular planar grids do not share this benefit.',
          'Skewed smooth torus grids: world heap, or fix connectivity directly in the procedural generator. GridAligned matches the heap GPU result within the control tolerance and avoids searching thousands of candidate flips.',
          'Skewed sheets and tubes: optimize only for the actual MSAA/workload. The skewed cylinder and skew2/3/fold/saddle fixtures become slower at 1x, faster at 4x. A shortest-edge policy is therefore unsafe as an unconditional runtime replacement.',
          'CDT can be preferable on nearly planar patches: in skew2 it is faster than the world heap despite less total edge reduction, and much cheaper to construct. In skew3 the heap is faster. Choose using the intended projected footprint, not a universal CDT/heap ranking.',
          'Regular sphere/torus/grid, small ellipsoid and blob variants: no useful strict mean gain confirmed. Keep Source and index optimization; the existing connectivity often has little edge-length headroom.',
          'Curved imported Suzanne/bunny/teapot and some bumpy surfaces: patch boundaries and strict source preservation restrict useful connectivity changes. Several methods fall back or fail the new reverse audit. No result establishes that these sources are globally optimal; these algorithms simply fail to show a useful strict gain here.',
          'Ellipsoid and jittered sheet: per-view projected optimization gives modest gains. It is a camera-specific preprocessing option, not evidence that a CPU rebuild every frame pays for itself.',
          'GraphEx TangleCube: plane-penalized heap has a small 4x gain (~3.5%), near the practical threshold. Heart has no confirmed strict gain. These results do not establish a generic rule for all implicit surfaces.', '',
          'Neighborhood, global rewiring and topology findings',
          'Star3D after the heap usually performs no replacements, or very few. On ellipse, skew2/3/saddle and skewed torus it reaches the same connectivity/work counters as the heap. Tiny timing differences cannot establish an extra benefit.',
          'Skewed cylinder performs seven star replacements: about 0.017% extra world-edge reduction and ~0.27% extra draw reduction relative to heap, below the control tolerance; construction is several times more expensive.',
          'Ellipsoid performs two star replacements: ~0.011% extra edge reduction and ~0.58% extra 4x draw reduction versus heap. This is not a compelling general neighborhood result.',
          'Regular torus opposite diagonals change connectivity without meaningful edge reduction and show no meaningful draw gain. Large numbers of changed triangles alone are not a useful optimization target.',
          'Cylinder global alignment reduces edges more than the strict heap but exceeds the strict source-error limit (~0.13% rather than 0.1%). It is a surface tradeoff; do not rank it as equivalent remeshing.',
          'TopologySplit changes one torus into two closed tori with unchanged V/F/E and chi=0. The sampled source error is ~9.4%; world edge length rises ~30.8%; draw rises ~1.35% at 1x and ~12.87% at 4x. This changed-surface experiment provides no useful acceleration and no clean topology-only causal conclusion.', '',
          'CPU construction comparison (shared source/patch/reference preparation excluded; final audited setup listed separately)',
          'model | method | construction ms | final audited setup ms | result status (1 complete, 2 fallback)']
for g,methods in [('ellipse2d',['CDT','Heap3D','Star3D']),('ellipsoid',['CDT','Heap3D','Star3D']),
                 ('skew2',['CDT','Heap3D','Star3D']),('torus_skew',['CDT','Heap3D','Star3D']),
                 ('cylinder_skew',['CDT','Heap3D','Star3D'])]:
    for m in methods:
        r=row('confirm',g,m,4)
        lines.append(f"{g} | {m} | {float(r['ConstructionCPU_ms']):.2f} | {float(r['SetupCPU_ms']):.2f} | {float(r['TriangulationStatus']):.0f}")
r=row('special','torus_skew','GridAligned',4)
lines += [f"torus_skew | GridAligned | {float(r['ConstructionCPU_ms']):.2f} | {float(r['SetupCPU_ms']):.2f} | 1", '',
          'Which original algorithms pull their weight?',
          'Retain Source, CDT, and CDT+flips as the main references. Keep the world heap for fixed-count local optimization.',
          'At 4x in the all-model screen, GP falls back on 19/27 models, Greedy on 17/27, LMT/LMT3D on 21/27. Strict eligible comparisons with >=12 stable conditions exist for only 7, 8, 3 and 3 respectively.',
          'PCL greedy projection makes sense as point-cloud reconstruction with normals. This code is only projection-inspired patch triangulation; it is not PCL, and rebuilding already connected meshes does not demonstrate its reconstruction value.',
          'GreedyPointSet sorts feasible projected-patch edges, using optional world weights, and completes a planar constrained graph. LMT adds local minimum-weight witnesses/greedy completion and flips; neither is a global minimum-weight solution.',
          'No unique confirmed GPU benefit justifies promoting GP/Greedy/LMT to production defaults here. Keep them as experimental baselines if research comparisons are useful; their frequent fallback must be visible. Star neighborhoods likewise remain an experiment.', '',
          'Most promising next experiment',
          'A feature-preserving offline cost for projected triangle raster traversal at the intended 1080p footprint and MSAA, with world/surface constraints. Include axis-aligned tile coverage, subpixel width/altitude, sample coverage and cache/order effects rather than optimizing linear perimeter alone.',
          'Use a small precomputed view bank only when its measured extra GPU saving amortizes preprocessing/storage/switching. Current per-view setup is hundreds of ms on substantial meshes, for modest incremental gains; do not rebuild on the CPU every frame.',
          'Use local symmetric source/candidate sampling before committing flips if strict surface preservation is required. The final reverse audit currently rejects some candidates that pass the optimizer\'s one-way local guards.',
          'Vendor raster/ROP/occupancy counters remain the main missing evidence for the MSAA sign reversal. D3D12 PS/VS queries are available; Nsight vendor counter access failed with ERR_NVGPUCTRPERM. Do not infer that helper count directly equals wasted lit-shader execution.', '',
          'Correlations and geometry-held-out checks',
          'See associations.txt. At 4x, mean projected-edge reduction has a strong descriptive association with draw reduction (Spearman ~0.81); at 1x it does not (~-0.28, including the ellipse outlier).',
          'Leaving one geometry out, a projected-edge-only ridge predictor gives R2 ~0.66 at 4x and negative R2 at 1x. Adding bbox/helper features gives ~0.70 at 4x. Active PS/VS counts alone generalize poorly across geometries.',
          'These features are correlated, counts are workload rather than timing, and the dataset is small and GPU-specific. No coefficient identifies an independent causal effect or a universal threshold.', '']
lines += (root/'conclusions.txt').read_text().splitlines()
(root/'interpretation.txt').write_text('\n'.join(lines)+'\n')
print('Interpretation and reproduction manifest written.')
