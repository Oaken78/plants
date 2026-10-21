# Overgrowth: procedural anime plants

A procedural plant generator that grows **climbing plants (ivy)**, **bushes** and **trees**, including **oaks**
(plus grass tufts and wild flowers), on arbitrary 3D models. Trees can grow on buildings too, sending their roots
down the walls to the ground. It is styled after "The Last Signal" concept sheet: lush ivy draping over
weathered concrete, cauliflower-shaped foliage masses, white and yellow flowers, blue-tinted shadows and ink lines.

- **Core:** portable C++17 with no dependencies, compiled to WebAssembly today. It is written to become a Godot
  GDExtension later; see [docs/godot-port.md](docs/godot-port.md).
- **Tech demo:** a three.js fly-through with the proof-of-concept house and three tall apartment towers. Around them
  streams a town of hundreds of overgrown houses and towers, groves of round trees and oaks, thousands of bushes and
  a grass meadow, all on a four-level LOD scheme with dithered cross-fades.

## Run the demo

**Live demo: <https://oaken78.github.io/plants/>.** It is redeployed by
[.github/workflows/pages.yml](.github/workflows/pages.yml) on every push to `main`. A desktop browser with a
dedicated GPU gives the smoothest flight.

To run it locally instead:

```bash
npm install
```

```bash
node tools/serve.mjs
```

Open <http://localhost:8080/web/>. The WebAssembly build (`web/build/`) and the models (`web/assets/*.glb`)
are checked in, so no compiler is needed just to run it.

### Controls

| Input | Action |
| --- | --- |
| Click | Capture the mouse and fly |
| W A S D | Move |
| Space / E, Q / C | Up, down |
| Shift | 5x speed |
| Mouse wheel | Cruise speed (1 to 400 m/s) |
| 1 to 8 | Viewpoints: ivy close-up, annex and sign, hero house, aerial, village at ground level, village from above, towers, lone oak |
| L | Colour every mesh by LOD (red 0, yellow 1, green 2, blue 3) |
| O | Ink outlines on/off |
| G | Replay the growth animation |
| R | Regrow the hero house with a new seed |
| H | Hide the UI |

On a touch screen, drag with one finger to look and hold two fingers to fly forward. Where pointer lock isn't
available, dragging with the mouse looks around. The Viewpoints buttons work without a keyboard.

The panel on the right has sliders for growth, wind, sun and render resolution. It also has the full generator
parameter table, which is read from the core, so you can regrow the hero house with other settings.

## Build

The core is built with the Emscripten SDK, which is expected in `~/emsdk`:

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
```

```bash
~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
```

Then build from PowerShell. This writes `web/build/plants.{js,wasm}`, then compiles and runs the core tests under node:

```powershell
./tools/build.ps1          # -Debug for assertions and symbols, -SkipTests to skip the tests
```

To regenerate the house and tower models after editing [tools/make-house.mjs](tools/make-house.mjs):

```bash
node tools/make-house.mjs
```

### Hosted version

The demo is also published as a private Claude artifact; share it from the page's Share menu. To rebuild the page
for publishing, run:

```bash
node tools/make-artifact-page.mjs <out>/index.html
```

This drops the document wrapper that the host adds itself and loads three.js from jsDelivr instead of
`node_modules`. Publish `index.html` with `js/*.js`, `build/plants.{js,wasm}`, `assets/house-glb.js` and
`assets/towers-glb.js` beside it. The models load from these base64 copies of the `.glb` files because the host
does not serve `.glb` files.

## Layout

```
core/                 portable C++ generator (no dependencies)
  include/plants/
    api.h             plain C API: the only surface the web demo (and a future GDExtension) uses
    params.h          every tunable as one table (name, default, min, max)
    plant_set.h       intermediate representation: stems, leaves, flowers, clumps, blades
    mesh.h            per-LOD vertex streams (24 bytes per vertex)
    bvh.h, math.h, rng.h
  src/
    climber.cpp       ivy growth
    bush.cpp          bushes, grass tufts, and the leaf cover shared with trees
    tree.cpp          trees and oaks: crown, pipe-model skeleton, crowding, ground-seeking roots
    scatter.cpp       where plants start on a host mesh
    shading.cpp       "cloud" normals and occlusion from clumps
    mesher.cpp        PlantSet -> LOD 0..3 meshes
  tests/tests.cpp     determinism, LOD and BVH tests (run under node)
web/
  index.html          demo page
  js/palette.js       art-direction palette (the concept sheet swatches plus derived tones)
  js/plants-wasm.js   wasm wrapper -> three.js geometries
  js/materials.js     toon shaders: foliage, building, terrain, shadow pass
  js/post.js          sky, clouds, aerial perspective, ink outlines, grading
  js/instancer.js     streaming, cell-based LOD instancing
  js/world.js         terrain, town layout, scatter rules (pure functions of position)
  js/flycam.js, js/main.js
  assets/house.glb    proof-of-concept host model (House_LOD0, House_LOD1)
  assets/towers.glb   three apartment towers, 6 to 13 floors (TowerA/B/C, each _LOD0 and _LOD1)
tools/
  build.ps1           wasm build and tests
  make-house.mjs      generates house.glb and towers.glb (plus their -glb.js copies)
  serve.mjs           static server
```

## How plants grow

**Host input.** The generator takes any triangle mesh plus one tag per triangle: `solid` (may be overgrown),
`ground` (a terrain patch for bushes and roots), or `noGrow` (glass and doors). The demo derives tags from the glTF
material names, so any model works. A BVH answers closest-point and ray queries against the host.

**Climbers.** Roots are picked on walls just above walkable ground, plus a few on roofs. Every tip advances one step
per round, steered by five things:
- its previous direction
- noise
- "up" projected onto the surface, so it climbs
- adhesion toward the nearest surface, so it clings
- gravity, once it loses contact

Tips slide along whatever they hit, turn away from glass, branch, and die in crowded cells so coverage spreads
instead of piling up. When a tip rolls over a top edge it can turn into a hanging strand that falls with a little
sway, which produces the curtains over ledges, balconies and parapets. Stem thickness follows how much growth each
node carries. Leaves (and an occasional flower) are placed along the stems. Tips are smaller and yellower, older
leaves are bigger and darker.

**Bushes.** A bush is a mound of overlapping spherical clumps, the cauliflower silhouette of anime foliage. Leaves
cover only the outer surface of the clump union, and branches run from the root to every clump. On a host, bushes
gather at the foot of walls and on roofs and ledges that have head room and support. There are four styles: round,
tall, spreading and flowering.

**Trees.** The crown comes first: overlapping clumps in a wide rounded envelope, covered with the same
cloud-shaded leaves as the bushes. A skeleton is then built to carry it:
- a slightly curved trunk up to a fork,
- two to five limbs, each serving the clumps on one side,
- a branch into every clump, and twigs inside it.

Branch thickness follows the pipe model: radius grows with the square root of the foliage a branch carries, and
the trunk flares at its base. Sizes run from about 3 m saplings to 13 m giants (`tree.height` x size).

On a host, trees grow on roofs and ledges, preferring spots near an edge, and on the ground patch away from the
walls. They lean toward open sky and shrink to fit under overhangs.

A tree can only grow as big as its soil allows. The soil is the flat area at the tree's level that it can reach
without crossing a wall or parapet. Height is capped at 1 m + `tree.soil` (1.1) x the square root of that area,
and a spot with less than `tree.minSoil` (1.5 m²) grows no tree at all.

| Spot | Soil | Tallest tree |
| --- | --- | --- |
| Window sill or parapet top | almost none | no tree |
| Balcony | about 2.5 to 4 m² | about 3 m |
| Stair-hut roof | about 5 m² | about 3.5 m |
| House roof | about 37 m² | about 8 m |
| Tower roof | about 90 m² | about 11 m |
| Ground | unlimited | no cap |

Their roots crawl over the host's surfaces:
- **Close to the ground** (less than `tree.rootReach`, 8 m by default): roots steer toward the ground. They run
  across the roof, climb over parapets, run down the walls and dig into the soil.
- **Higher up:** roots only spread over the roof.
- **On the ground:** roots spread a short way and sink in.

Roots heading for the ground fork into child roots that also seek it, so they fan out into a fig-like net over the
wall. Thin rootlets web the surface between them, and `tree.rootBranching` sets how much of both. Free-standing
trees get a forked root flare.

**Oaks.** Oaks are a second species. They have lobed leaves and a deeper green, a thicker trunk with a stronger
flare, gnarled branches, and they grow about twice as tall (`oak.height`, 15 m at size 1). Like real oaks, their
form depends on how crowded they are:
- **Alone:** a short trunk forks low into long, near-horizontal limbs under a crown wider than the tree is tall.
  A lone oak at size 1 is about 24 m wide and 15 m tall.
- **Crowded:** among other trees or near buildings, an oak grows taller (up to +30%) with a high clear trunk and a
  narrow crown. At size 1 that is about 19 m tall and 16 m wide.

Round trees respond the same way, but only a little. Crowding is a number from 0 to 1. On a host it is measured
from the walls and the other trunks around each tree. In the demo world it comes from nearby trees and houses, and
each oak picks one of three pre-grown forms (broad, medium, tall). Giant crowns are capped at `tree.maxLeaves`
leaves, which are made bigger to keep the coverage.

**Nothing starts inside a building.** A tree, bush or ivy root is never placed where something is overhead and
nearly every horizontal direction is blocked: the floor under a house, the inside of a roof hut. Bushes leaning on
a wall are squashed against it instead of growing through it, and foliage that would end up behind a wall is
dropped. Only roots may slip inside, since they follow the surfaces.

**Grass.** Tufts of curved, tapered blades with up-facing normals, so they shade like the ground they stand on, plus
wild flowers on stalks.

**Anime shading.** A leaf is not lit by its own normal. Its lighting normal is the gradient of a smooth field built
from its plant's clumps, so a bush shades like a few soft lumps and ivy like a padded blanket. The field also gives
ambient occlusion, which darkens clump interiors. The distant puff LODs sample the same field, so lighting matches
across LOD transitions. The shaders use two flat tones with a crisp terminator and a warm highlight band; shadows
fall to blue-tinted palette tones, never grey. A post pass adds the ink lines, the sky and clouds, and aerial
perspective. Ink follows the anime convention:
- **Buildings** get their silhouettes and convex creases.
- **Foliage** is outlined only around its masses, never leaf by leaf.

To tell the two apart, a cheap depth-only pass renders just the buildings and terrain. Where the scene is nearer than
that, the pixel is foliage, and only large depth jumps (a crown against what lies well behind it) are inked there.

**Leaf shapes and close-up detail.** A leaf is drawn on a coarse ribbon mesh that carries leaf-space coordinates.
The fragment shader cuts the true outline from a smooth shape function:
- heart-shaped, three-lobed ivy;
- pointed-oval bush and round-tree leaves;
- oak leaves, rounded at both ends with four broad lobes a side and deep, narrow sinuses;
- rounded petals.

The edge is anti-aliased with alpha-to-coverage, so it stays smooth however close the camera gets, with no extra
triangles. The same shape cuts the shadow map. Leaves carry no ink or veins of their own, which keeps crowns calm.

The shape functions live in `leafHalfWidth()` in both `core/src/mesher.cpp` (which sizes the hulls) and
`web/js/materials.js` (which cuts the outline); keep them identical.

## LOD strategy

Each generated plant set is meshed four times from the same data:

| LOD | Content | Used (house plants) | Hero house | Bush |
| --- | --- | --- | --- | --- |
| 0 | Every leaf (a 12-vertex ribbon that bends it; the outline comes from the shader), 6-sided stems, full flowers | < 24 m | ~1.0 M tris | ~14 k |
| 1 | 30% of leaves (a deterministic subset), enlarged to keep coverage, 6-vertex ribbons, 4-sided thick stems | < 65 m | ~145 k | ~1.5 k |
| 2 | Lumpy puffs from the clump field (ivy clumps merged), 6% big silhouette leaves, flower dots, trunks | < 190 m | ~63 k | ~1 k |
| 3 | Merged low-poly puffs only (plus tree trunks) | < 2.9 km | ~1.8 k | 80 |

Trees use the same four levels at longer distances (34 m, 100 m, 340 m, then out to 2.6 km). A 7 m tree is about
105 k, 13 k, 4 k and 220 triangles. Bark tubes keep their trunk at LOD 3 so distant crowns don't float, and thick
trunks and roots get twice the sides up close.

- **Shared rendering:** every LOD of a plant shares the same palette, the same cloud normals and the same wind
  (vertex flex and phase are baked per vertex), so a switch changes detail, not look.
- **Cross-fades:** near a switch distance an instance is drawn in both LODs with complementary screen-door
  dither, so nothing pops and no transparency sorting is needed. Grass shrinks into the ground at the end of its
  range instead of ending in a visible ring.
- **Few unique meshes:** the town uses four generated house variants and the hero house is unique. Bushes use ten
  variants and grass eight. Every instance gets its own position, rotation and scale, and its wind phase comes from
  its world position.
- **Streaming:** [js/instancer.js](web/js/instancer.js) covers the world with cells that are generated lazily
  from deterministic rules. Each frame it walks the cells within range and inside the view frustum, picks a LOD
  per instance and part, and appends it to one instanced draw call per (variant, part, LOD). A typical view draws
  around 1,000 houses and 2,500 bushes in about 70 draw calls.
- **Weather and light:** the shadow map follows the camera and is snapped to whole texels so it does not shimmer.
  Beyond it, the toon terminator alone carries the form, which is how distant objects read in anime backgrounds
  anyway.

## Performance notes

Generation runs in WebAssembly: a town house variant takes about 0.1 to 0.2 s and the denser hero house about
0.6 s. On an Intel Iris Xe laptop GPU the demo measured roughly 25 to 50 fps at a pixel ratio of 1.25, in an
embedded browser that throttles background pages, so those numbers are a lower bound. The Resolution slider and the
Shadows toggle are the two biggest levers.

## Palette

[web/js/palette.js](web/js/palette.js) holds the five swatches from the concept sheet's footer (sky blue `#3d7cc4`,
foliage green `#4f9e4c`, concrete `#dcd3bf`, rust `#c4573a`, ink navy `#2b3242`) and the light and shade tones derived
from them. Change a colour there and the plants, house, terrain, sky and lines all follow. Run `node tools/make-house.mjs`
afterwards to rebake the house colours.
