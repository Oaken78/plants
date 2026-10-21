# Porting to Godot 4

The demo was split so that the expensive, hard-to-rewrite part (the generator) moves to Godot unchanged, and the
browser-specific part (three.js rendering) maps one-to-one onto built-in Godot features.

| Web demo | Godot |
| --- | --- |
| `core/` C++17, compiled with Emscripten | Same sources compiled into a GDExtension (godot-cpp) |
| `core/include/plants/api.h` C API | Called from a `PlantGenerator` class registered by the extension |
| `web/js/plants-wasm.js` (heap -> BufferGeometry) | Build an `ArrayMesh` per LOD from the same buffers |
| `web/js/materials.js` toon shaders | `.gdshader` spatial shaders (vertex, fragment, `light()`) |
| LOD dither cross-fade in shaders | `GeometryInstance3D.visibility_range_*` with fade mode `SELF` |
| `web/js/instancer.js` cells and buckets | `MultiMeshInstance3D` per cell and (variant, LOD), or a GDScript port |
| `web/js/post.js` sky | Sky shader (`shader_type sky`) |
| `web/js/post.js` fog | `Environment` fog or a custom pass |
| `web/js/post.js` ink lines | Full-screen quad spatial shader reading `hint_depth_texture` |
| `web/js/world.js` layout rules | GDScript (pure functions; port line by line) |
| `web/assets/house.glb` | Import as is (materials keep their names) |

## 1. Generator as a GDExtension

1. Add `godot-cpp` and a `SConstruct` that compiles `core/src/*.cpp` plus a small `register_types.cpp`.
2. Register `PlantGenerator : RefCounted` with methods that wrap the C API:
   - `set_host(mesh: Mesh, tags: PackedByteArray)`: read `Mesh.surface_get_arrays()`. Derive a tag per triangle
     from the surface material name: `window` and `door` are `noGrow`, a ground patch is `ground`, everything else
     is `solid`.
   - `grow_on_host(seed) -> Array[ArrayMesh]`, `make_bush(seed, style, scale)`, `make_grass(seed)`.
   - `get_param_list()` from `plants_param_*`, so the inspector shows the same table as the web UI.
3. Build each LOD's `ArrayMesh` from the core buffers:
   - `ARRAY_VERTEX` from the positions.
   - `ARRAY_NORMAL` from the snorm normals.
   - `ARRAY_CUSTOM0` from `shade`, as RGBA8 unorm: hue, value, ao, leaf v (255 = not a leaf).
   - The normal's fourth byte holds leaf u. Godot's `ARRAY_NORMAL` has no fourth component, so move it into a spare
     channel, for example `ARRAY_CUSTOM2` as R8 or packed into CUSTOM1.
   - `ARRAY_CUSTOM1` from `aux`, as RGBA8 unorm: flex, phase, birth, material id. Material ids are listed in
     `core/include/plants/plant_set.h`: 0 stem, 1 ivy leaf, 2 bush leaf, 3 grass, 4 to 6 flower parts, 7 tree
     leaf, 8 bark, 9 oak leaf.
   - `ARRAY_INDEX`.

   Pass `ARRAY_FORMAT_CUSTOM0_RGBA8_UNORM | ARRAY_FORMAT_CUSTOM1_RGBA8_UNORM` in the format flags.
4. Generation is deterministic and takes 0.1 to 0.6 s per house. Run it on a `WorkerThreadPool` task and hand the
   meshes to the main thread.

The core has no globals except immutable lookup tables, so separate `PlantsContext` instances can run on separate
threads.

GDExtensions also work in Godot's web export when the extension is built with Emscripten as a side module and the
export has extension support enabled. The demo's WebAssembly build already proves the code compiles for that
target.

## 2. Shaders

Port `materials.js` into three spatial shaders: foliage, building and terrain. The GLSL is plain and the uniforms
keep their names.

- **Wind (`windOffset`):** goes in `vertex()`. Use `CUSTOM1.x * 0.25` for flex and `CUSTOM1.y` for phase. Use
  `NODE_POSITION_WORLD` (or `MODEL_MATRIX[3].xyz`) wherever the web version reads the instance position.
- **Growth animation:** in `fragment()`, `if (CUSTOM1.z > grow) discard;`, passing `CUSTOM1.z` through a
  varying.
- **Toon light (`toonLight`):** implement it in `light()`.
  - `DIFFUSE_LIGHT += mix(shade, lit, smoothstep(-0.03, 0.09, dot(NORMAL, LIGHT)) * ATTENUATION);`
  - `ATTENUATION` already contains Godot's directional shadow, so `sunShadow()` goes away.
  - The palette colours become uniforms or a small lookup texture indexed by material id (`CUSTOM1.w * 255`).
- **Foliage normals:** `NORMAL` is already the generator's cloud normal. Keep `cull_disabled` and do not flip it
  for back faces.
- **Leaf shapes:** port `leafHalfWidth()` and `leafInside()` from `materials.js`. In `fragment()`, set `ALPHA` from
  the anti-aliased inside test and use `render_mode depth_draw_opaque` with `ALPHA_SCISSOR_THRESHOLD`, or
  `ALPHA_ANTIALIASING_EDGE` for alpha-to-coverage. The ink outline, midrib and close-up veins follow the web shader.
  The shadow pass needs the same discard.
- **LOD dither:** drop it. Godot's visibility-range fade produces the same screen-door cross-fade.

## 3. LOD and streaming

For each generated plant set, create one `MeshInstance3D` (or a `MultiMesh` for repeated variants) per LOD and set:

```
lod0: visibility_range_end = 24,   visibility_range_end_margin = 3
lod1: visibility_range_begin = 24, visibility_range_end = 65,  margins 3 / 8
lod2: visibility_range_begin = 65, visibility_range_end = 190, margins 8 / 20
lod3: visibility_range_begin = 190
visibility_range_fade_mode = VISIBILITY_RANGE_FADE_SELF
```

These are the distances from `RANGES` in `web/js/main.js`. Godot then cross-fades with dithering, exactly like the
web instancer.

For the town and the scattered bushes, use one `MultiMeshInstance3D` per (cell, variant, LOD), with the same
visibility ranges, filled by the cell rules from `world.js`. Godot culls whole multimeshes by their AABB, so cells
of 32 to 64 m keep culling effective. Turn shadow casting off for LOD 3, as the web demo does.

## 4. Post and environment

- **Sky:** the gradient, sun and the two-tone cumulus from `post.js` become a `shader_type sky` shader.
- **Aerial perspective:** `Environment.fog_enabled` with a height falloff gets close. Or keep the custom formula in
  the outline pass.
- **Ink lines:** a full-screen `MeshInstance3D` quad with a spatial shader that samples `hint_depth_texture`. Use
  the same silhouette and 1/z-Laplacian crease test, and the same fade with distance.
