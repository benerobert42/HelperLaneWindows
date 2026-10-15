> Historical investigation record. Current measurements, fixes and conclusions are consolidated in [docs/TRIANGULATION_PERFORMANCE.md](../docs/TRIANGULATION_PERFORMANCE.md). Pre-correction GPU timing claims are superseded; proposals and code line references below reflect the state when written.

# GraphEx integration: implemented scope

The user narrowed this feature to three OBJ presets, the existing GraphEx raster material/light shader, and minimal programmatic PNG capture. The earlier broader plan is superseded. Comparison capture and the heap GUI bridge (point 4) are deferred.

## Models

Suzanne (7,958 vertices / 15,744 triangles), Bunny (2,503 / 4,968), and Teapot (1,292 / 2,464) are unmodified asset copies under `Data/Models`. [Asset provenance and hashes](../Data/Models/README.md) identifies the GraphEx/Falcor originals. Their source boundaries are preserved (168 / 42 / 120 edges). [Inventory](graphex_model_inventory.json) is the earlier static audit; the native `--self-test` now loads the three deployed presets through `ObjMeshLoader`, runs Source Faces and verifies counts/topology. GUI presets share the custom OBJ import path; no additional geometry importer is introduced.

## Raster preview

[LitPreview.slang](../Shaders/LitPreview.slang) adapts exactly the ambient, diffuse, specular and attenuation equations from GraphEx `Shaders/Example/Mesh/Lighting.ps.slang`, including its 0.75 output multiplier. World position and inverse-transpose normal transforms follow its `PosNormTex.vs.slang`. A single point or directional light uses the existing material and light fields in GraphEx `Source/GraphEx/Core/CoreTypes.h`; background color corresponds to its RenderManager setting.

[LitPreview.cpp](../Rendering/LitPreview.cpp) owns the separate pipeline, lazily creates area-weighted source normals, and reuses the selected geometry's position/index buffers. Source normals preserve vertex IDs and stay fixed across triangulations. No normals, interpolants or lighting are added to the old diagnostic/performance shaders. UI defaults choose a specular blue-grey material using these equations; they need no new rendering feature.

There is no GraphEx framework dependency, implicit/SDF extraction, ray tracing, second light, environment reflection, PBR, texture/MTL import, material preset library, faceted-normal mode, exposure control or tone-map pass.

## PNG capture

[PreviewCapture.cpp](../Rendering/PreviewCapture.cpp) contains the small capture function, preview draw adapter and JSON recipe runner. The capture is synchronous Falcor `Texture::captureToFile` on the final 8-bit display framebuffer before UI. MSAA resolves and blits through the same interactive path before capture; no custom half-float conversion, gamma conversion, vertical flip or timestamp naming remains. Performance smoke PNGs also use Falcor's capture API (their existing linear UNORM target is unchanged).

`--screenshot recipe.json output.png` renders one headless frame with fixed resolution, model, method, orientation and lighting, then exits. The [example recipe](screenshots/suzanne.json) documents every appearance field. JSON validation rejects unknown settings and invalid light/dimension values. Invalid or fallback triangulations fail instead of silently making a paper figure from another method. PNG output is lossless RGB, with explicit filenames and no added metadata/capture manager/comparison state machine.

The volume UI's grid column count is capped at the instance count: a single model now remains centered in a wide viewport. Camera distance and projection formulas are unchanged, and the preview obtains its camera position from that shared calculation. This framing correction affects the interactive path; the independent headless performance geometry/grid setup is unchanged.

## Verification

See [screenshot verification](screenshots/verification.json) for the current build's pixel and image-format checks. Build output is `analysis/build/lit-preview-build.log`; existing native regressions and the GUI render smoke exercise the old paths as well. This feature makes no new triangulation or GPU-performance claim for the three imported models.
