# Godot version

`godot/` is a Godot 4.7 port of the web demo: the same plants, the same town, the same anime look. The C++ core is
compiled unchanged into a GDExtension. The browser-specific parts are rebuilt on Godot features, and several of
them now run faster than in the browser.

## Run it

The extension is built and checked in (`godot/bin/`, Windows x86-64), so you only need Godot 4.7:

```powershell
godot --path godot --import
```

```powershell
godot --path godot
```

You can also open `godot/project.godot` in the editor and press Play. The first command imports the project once.

The house and tower models are read at runtime from `web/assets/`, which the two demos share, so the project has to
stay inside `Plants/`.

Generation takes about a second. Controls match the web demo: click to fly, WASD, Space/E up, Q/C/Ctrl down, Shift
x5, wheel for speed, Esc to release the mouse. 1 to 8 jump to the viewpoints, L toggles LOD colours (purple is the
extra far level, LOD 4), O the ink, G replays the growth, R regrows the hero house with a new seed and H hides the
panels. The panels have the same
sliders, toggles and generator parameter table as the web page.

### Command-line options

Pass these after `--`, for example `godot --path godot -- --view=8 --shot=oak.png`:

| Option | Effect |
| --- | --- |
| `--view=N`, `--cam=x,y,z,yaw,pitch` | Start at viewpoint N (1 to 8), or at a camera position in metres and degrees |
| `--shot=file.png\|.jpg`, `--frames=N`, `--no-ui` | Save a screenshot after N frames (default 120), then quit |
| `--bench=S` | Measure S seconds, print frame and GPU times, triangles and draws per pass, then quit |
| `--stats` | Print the triangle count of every mesh variant and LOD |
| `--serial` | Grow the plant sets one after another on the main thread, to time the parallel growth |
| `--regrow=SEED` | Regrow the hero house once running, to test the background regrow |
| `--leaves=scissor` | Hard alpha-tested leaf edges instead of alpha to coverage |
| `--full-shadows` | LOD 0 casts its own shadow instead of the LOD 1 proxy |
| `--hide=houses,trees,bushes,grass,terrain,mountains` | Hide layers, for profiling |
| `--debug-lod`, `--no-outlines`, `--no-shadows`, `--grow=T`, `--msaa=N`, `--sun=elevation,azimuth` | Settings |
| `--shadow-bias=B`, `--normal-bias=B`, `--shadow-blur=B`, `--splits=2\|4`, `--shadow-distance=M`, `--terrain-shadows=on\|off` | Sun shadow tuning |

## Build the extension

```powershell
./tools/build-godot.ps1
```

This needs git, Python and Visual Studio's C++ compiler. The first run clones godot-cpp (`godot-4.5-stable`, so the
extension loads in Godot 4.5 and later) into `godot/extension/godot-cpp` and installs SCons with pip if it is
missing. It writes `godot/bin/overgrowth.windows.template_release.x86_64.dll`; objects go to
`godot/extension/build/`. Other platforms need a matching line in `godot/overgrowth.gdextension` and a build on
that platform.

The core tests still run through `tools/build.ps1` under node.

## How the web demo maps to Godot

| Web demo | Godot |
| --- | --- |
| `core/` compiled with Emscripten | The same sources in `overgrowth.dll` (`godot/extension/SConstruct`) |
| `web/js/plants-wasm.js` | `PlantGenerator` (`extension/src/plant_generator.*`): the C API as a class, one `ArrayMesh` per LOD |
| `web/js/instancer.js` | `PlantLayer` (`extension/src/plant_layer.*`), a native node driving RenderingServer multimeshes |
| `web/js/world.js` | `DemoWorld` (`extension/src/demo_world.*`), ported line by line in C++ |
| `web/js/materials.js` | `shaders/foliage.gdshaderinc`, `building.gdshader`, `terrain.gdshader`, `scenery.gdshader` |
| `web/js/post.js` sky | `shaders/sky.gdshader` |
| `web/js/post.js` aerial perspective, grading | `Environment` fog with aerial perspective, saturation adjustment |
| `web/js/post.js` ink lines | `shaders/ink.gdshader`, a full-screen quad drawn last with multiplicative blending |
| `web/js/palette.js` | `scripts/palette.gd` (keep the two in step) |
| `web/js/main.js`, `flycam.js`, `index.html` | `scripts/main.gd`, `fly_camera.gd`, `ui.gd` |
| glTF loading in `main.js` | `scripts/host_model.gd` |

### Vertex format

Plant meshes keep the core's 24-byte vertex: `VERTEX`, then three RGBA8 custom streams. `CUSTOM0` is the shade
(hue, value, ao, leaf v), `CUSTOM1` is aux (flex, phase, birth, material id), and `CUSTOM2` holds the snorm normal
with leaf u in its fourth byte. There is no `ARRAY_NORMAL`: the shader reads the normal from `CUSTOM2`, which keeps
the leaf coordinate without a fourth stream.

### Shading

The toon light is in each material's `light()`, with ambient light disabled, so the shade tones come only from the
palette (blue-tinted, never grey). Godot's `ATTENUATION` carries the sun's shadow. Wind, the growth animation and
the LOD dither are global shader uniforms (`plant_time`, `wind`, `grow`, `main_cam`, `debug_lod` in
`project.godot`). The demo keeps its own time, because Godot's `TIME` wraps every hour.

### LOD

The web demo's design is kept on purpose. Every instance picks its LOD from its own distance and cross-fades with a
screen-door dither (`INSTANCE_CUSTOM.x`). Godot's `visibility_range` measures distance per node, so it would need
one node per instance per LOD (thousands of draw calls), or cells so small that the far LODs become tens of
thousands of nodes.

## What is better than in the browser

All figures are for 1600x900 with MSAA 4x on an RTX 3050 Ti laptop GPU.

**Generation on every core.** The web demo grows its 44 plant sets one after another on the main thread, while the
page waits. Godot grows each set in its own core context on the `WorkerThreadPool`. All 44 are ready in about
0.7 s on 20 threads, against 3.3 s with `--serial`. Regrowing the hero house also runs on a worker thread (about
0.35 s): the old plants stay on screen until the new ones are swapped in, with no stall. Native code and wasm grow
the same plant for the same seed. The exception is plants grown on a host (see Differences).

**A native instancer.** `PlantLayer` does what `instancer.js` does, in C++. It walks cells in blocks of 8x8 and
culls whole blocks before touching a cell. `PlantLayer.update_layers()` walks the four layers at once on the
`WorkerThreadPool`; only the uploads, one buffer per multimesh, run on the main thread. A frame costs about 1 ms
of main-thread time for some 8,000 instances and 10,000 bucket entries: each walk takes about 0.3 ms on its
worker, and the uploads take 0.4 ms in total. The walk is limited by cache misses, not arithmetic: the same work
in a warm loop takes a third of the time. Each bucket gets tight bounds, so Godot can skip it for shadow cascades
it cannot reach.

**Ink without a second geometry pass.** To keep ink off leaves, the web demo renders the buildings and terrain a
second time into a depth target. In Godot every material writes a surface tag into `ROUGHNESS`. Nothing else reads
roughness, because all materials are `specular_disabled` with their own `light()`. Godot writes the tag into the
normal-roughness buffer during the depth prepass it runs anyway, and the ink shader reads it. The ink pass costs
less than the measurement noise (under 0.1 ms).

**Leaf edges in the depth prepass.** Alpha to coverage only works in Godot's transparent pass, which on its own would
also drop shadows and the depth prepass. `render_mode depth_prepass_alpha` keeps the solid part of every leaf in
the depth prepass and the shadow maps, so only the anti-aliased rim is drawn transparent. That costs 0.4 to 1 ms
over hard alpha-scissor edges (`--leaves=scissor`).

**Shadows to 400 m.** The web demo has one 4096 shadow map, 170 m across, that follows the camera. Godot uses four
cascades out to 400 m, so trees and towers in the middle distance cast shadows too. Two things keep this
affordable:
- **Near/far buckets.** A shadow-casting bucket is split into a near copy that casts and a far copy beyond the shadow
  distance that does not, so the distant town is not drawn into every cascade.
- **Shadow proxies.** A LOD can cast its shadow with a coarser mesh, through a shadows-only multimesh of the same
  instances. LOD 0 casts with the LOD 1 mesh, which has a third of the leaves, enlarged to the same coverage. On
  houses and towers, LOD 2 (55 k to 127 k triangles of puffs and silhouette leaves) casts with LOD 3. Leaves shade
  one another by clump anyway: the shadow bias of 1.0 matches the web demo's roughly 18 cm depth bias, so crowns
  keep calm, mostly lit tops instead of leaf-by-leaf speckle. This cuts the shadow passes at ground level by half
  to two thirds: from 15.4 M to 5.5 M triangles at the lone oak (18.6 to 14.1 ms GPU), and from 14.9 M to 7.1 M in
  the village (19.8 to 16.3 ms). The dappled light under crowns is somewhat coarser; compare with
  `--full-shadows`.

**A fifth LOD for distant houses.** In the web demo, every house out to 2.9 km draws LOD 3 with 2,000 to 6,700
triangles, which the houses' trees, roots and many puffs add up to. That makes houses most of the triangles on
screen, though beyond 600 m a house is only a few pixels tall. A new core call, `plants_rebuild_far_lod`, rebuilds
LOD 3 of the plants just grown, with clumps merged per 8 m and only stems over 0.15 m kept (trunks, not roots). It
does not grow them again. Houses use the result beyond 600 m and towers beyond 700 m: a town house drops from about
2,900 triangles to 600. At the hero viewpoint, the main pass drops from 6.75 M to 4.2 M triangles. The web demo
could use the same call.

**Exact host meshes.** Godot's glTF loader quantises vertex positions by default (4.08 m reads back as 4.07986 m). The
models are loaded with `IMPORT_FLAG_FORCE_DISABLE_MESH_COMPRESSION`, so the host the generator gets is byte for byte
the web demo's.

### Performance

GPU time per viewpoint (`--view=N --bench=6`), with triangles per frame in the main pass and in all shadow passes:

| Viewpoint | GPU | Main pass | Shadow passes |
| --- | ---: | ---: | ---: |
| 1 Ivy close-up | 15.8 ms | 6.4 M | 6.9 M |
| 2 Annex | 17.2 ms | 6.4 M | 6.3 M |
| 3 Hero house | 11.9 ms | 4.2 M | 5.0 M |
| 4 Aerial | 8.8 ms | 3.4 M | 1.9 M |
| 5 Village | 16.3 ms | 6.5 M | 7.1 M |
| 6 Village above | 11.8 ms | 5.6 M | 3.8 M |
| 7 Towers | 14.5 ms | 6.0 M | 6.2 M |
| 8 Lone oak | 14.1 ms | 6.3 M | 5.5 M |

At the hero viewpoint, shadows cost about 4 ms and MSAA about 2.5 ms. Before the far LOD and the shadow changes,
the first straight port took 14.6 ms there; before the shadow proxies, the ivy close-up, the village and the lone
oak took 20.2, 21.2 and 18.8 ms.

## Differences from the web demo

- **Hosted growth differs in detail.** Standalone bushes, trees and grass come out bit for bit as in the wasm build.
  Plants grown on a house do not: the climbers' long growth is chaotic, and the C runtimes' `sinf` and `cosf` (MSVC
  here, musl in Emscripten) differ in the last bit for some inputs. So seed 1 grows a different but equally valid
  hero house. Making the two match would need the core to carry its own trigonometry.
- **Leaf shadows** come from the LOD 1 proxy, as described above.
- **Shadows reach 400 m** instead of about 130 m ahead of the camera.
- **Fog** is Godot's exponential fog, which takes its colour from the sky, instead of the web demo's formula. It is
  close, but not identical.
- **Windows only** for now: `overgrowth.gdextension` lists the Windows build. The code is portable.

## Files

```
godot/
  project.godot           Forward+, MSAA 4x, the global shader uniforms
  overgrowth.gdextension  loads bin/overgrowth.*.dll
  main.tscn               the scene: one node running scripts/main.gd
  bin/                    the built extension (checked in, like web/build)
  extension/              C++ sources of the extension (.gdignore keeps Godot out of it)
    SConstruct            builds the core and src/ against godot-cpp
    src/plant_generator.* PlantGenerator: the core's C API as a Godot class
    src/plant_layer.*     PlantLayer: streaming, per-instance LOD instancing
    src/demo_world.*      DemoWorld: the town, terrain and scatter rules of web/js/world.js
  scripts/
    main.gd               generation jobs, layers, environment, viewpoints, command-line options
    host_model.gd         glTF host models: render mesh and generator host
    ui.gd                 panels
    fly_camera.gd, palette.gd
  shaders/
    common.gdshaderinc    globals, wind, LOD dither, leaf shapes, noise, toon light, surface tags
    foliage.gdshaderinc   every plant mesh; foliage.gdshader and foliage_scissor.gdshader pick the leaf edges
    building.gdshader, terrain.gdshader, scenery.gdshader, sky.gdshader, ink.gdshader
```

The leaf shape functions now live in three places: `core/src/mesher.cpp`, `web/js/materials.js` and
`godot/shaders/common.gdshaderinc`. Keep them identical, as well as the material ids and vertex layout (see
`core/include/plants/plant_set.h` and `mesh.h`).
