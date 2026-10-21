// Tech demo entry point: generates the plants in WebAssembly, builds the world and flies around it.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { PlantGenerator, TAG } from './plants-wasm.js';
import { LodLayer } from './instancer.js';
import { FlyCam } from './flycam.js';
import { PostPass } from './post.js';
import { PALETTE, hexToLinear } from './palette.js';
import * as M from './materials.js';
import { WORLD, terrainHeight, houseCell, makeBushCell, makeGrassCell, makeTreeCell, buildTerrain, buildMountains } from './world.js';

// LOD switch distances in metres: LOD i is used while distance < ranges[i].
const RANGES = {
  house: [90, 2900],
  housePlants: [24, 65, 190, 2900],
  tower: [130, 3200],
  towerPlants: [30, 85, 260, 3200],
  bush: [26, 70, 200, 1100],
  tree: [34, 100, 340, 2600],
  grass: [26, 52],
};
const HERO_PARAMS = { 'climber.seeds': 34, 'climber.roofSeeds': 10, 'climber.budget': 20000, 'bush.count': 22, 'tree.count': 3,
  'tree.groundCount': 2, 'tree.oakShare': 0.5 };
// Towers are tall: ivy needs more steps to climb them, and curtains hang longer from their roofs.
const TOWER_PARAMS = { 'climber.seeds': 44, 'climber.roofSeeds': 14, 'climber.budget': 32000, 'climber.maxSteps': 420,
  'climber.hangLength': 5, 'bush.count': 18, 'bush.roofShare': 0.5, 'tree.count': 2, 'tree.groundCount': 2 };
const TOWER_NAMES = ['TowerA', 'TowerB', 'TowerC'];
const SHADOW_SIZE = 4096;
const SHADOW_HALF = 85;

const $ = (id) => document.getElementById(id);
const status = (text) => { $('loading-text').textContent = text; };
// Lets the loading text repaint between generation steps. Background tabs pause animation frames, so a timer
// keeps loading going there too.
const nextFrame = () => new Promise((r) => {
  requestAnimationFrame(() => r());
  setTimeout(r, 120);
});

// ---------------------------------------------------------------- renderer and targets

const canvas = $('view');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: false, powerPreference: 'high-performance' });
let pixelRatio = Math.min(window.devicePixelRatio, 1.25);
renderer.setPixelRatio(pixelRatio);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(55, 1, 0.15, 9000);

function makeMainTarget(w, h) {
  const depth = new THREE.DepthTexture(w, h);
  depth.type = THREE.UnsignedIntType;
  return new THREE.WebGLRenderTarget(w, h, { type: THREE.HalfFloatType, samples: 4, depthTexture: depth });
}
let mainTarget = makeMainTarget(2, 2);

const shadowDepth = new THREE.DepthTexture(SHADOW_SIZE, SHADOW_SIZE);
shadowDepth.type = THREE.UnsignedIntType;
shadowDepth.minFilter = shadowDepth.magFilter = THREE.NearestFilter;
const shadowTarget = new THREE.WebGLRenderTarget(SHADOW_SIZE, SHADOW_SIZE, { format: THREE.RedFormat, depthTexture: shadowDepth });
const shadowCamera = new THREE.OrthographicCamera(-SHADOW_HALF, SHADOW_HALF, SHADOW_HALF, -SHADOW_HALF, 1, 900);
shadowCamera.layers.set(1);
M.shared.uShadowMap.value = shadowDepth;
M.shared.uShadowTexel.value = 1 / SHADOW_SIZE;

const post = new PostPass();

function resize() {
  const w = window.innerWidth, h = window.innerHeight;
  renderer.setPixelRatio(pixelRatio);
  renderer.setSize(w, h, false);
  canvas.style.width = `${w}px`;
  canvas.style.height = `${h}px`;
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  const pw = Math.floor(w * pixelRatio), ph = Math.floor(h * pixelRatio);
  mainTarget.dispose();
  mainTarget = makeMainTarget(pw, ph);
}
window.addEventListener('resize', resize);
resize();

// ---------------------------------------------------------------- sun

const sun = { elevation: 48, azimuth: 215 };
function updateSun() {
  const el = THREE.MathUtils.degToRad(sun.elevation), az = THREE.MathUtils.degToRad(sun.azimuth);
  M.shared.uSunDir.value.set(Math.cos(el) * Math.sin(az), Math.sin(el), Math.cos(el) * Math.cos(az)).normalize();
}
updateSun();

// Keep the shadow map centred ahead of the camera and snapped to whole texels (no shimmering).
const _fwd = new THREE.Vector3();
function updateShadowCamera() {
  camera.getWorldDirection(_fwd);
  _fwd.y = 0;
  if (_fwd.lengthSq() < 1e-6) _fwd.set(0, 0, -1);
  _fwd.normalize();
  const center = camera.position.clone().addScaledVector(_fwd, SHADOW_HALF * 0.45);
  center.y = Math.min(camera.position.y, terrainHeight(center.x, center.z) + 10);
  const dir = M.shared.uSunDir.value;
  shadowCamera.position.copy(center).addScaledVector(dir, 450);
  shadowCamera.lookAt(center);
  shadowCamera.updateMatrixWorld();
  const texel = (2 * SHADOW_HALF) / SHADOW_SIZE;
  const ls = center.clone().applyMatrix4(shadowCamera.matrixWorldInverse);
  const snapped = new THREE.Vector3(Math.round(ls.x / texel) * texel, Math.round(ls.y / texel) * texel, ls.z);
  const shift = snapped.sub(ls).applyMatrix3(new THREE.Matrix3().setFromMatrix4(shadowCamera.matrixWorld));
  shadowCamera.position.add(shift);
  shadowCamera.updateMatrixWorld();
  M.shared.uShadowMatrix.value.multiplyMatrices(shadowCamera.projectionMatrix, shadowCamera.matrixWorldInverse);
}

// ---------------------------------------------------------------- house model

const HOUSE_MATS = ['concrete', 'concrete_dark', 'trim', 'rust', 'sign', 'window', 'door'];
const NO_GROW = new Set(['window', 'door']);

// Merges the glTF primitives of one LOD node into a single geometry with colour and material id.
function houseGeometry(root) {
  const pos = [], nrm = [], col = [], mat = [], idx = [], tags = [];
  root.updateMatrixWorld(true);
  root.traverse((o) => {
    if (!o.isMesh) return;
    const g = o.geometry;
    const base = pos.length / 3;
    const p = g.getAttribute('position'), n = g.getAttribute('normal');
    const id = Math.max(0, HOUSE_MATS.indexOf(o.material.name));
    const c = o.material.color;
    for (let i = 0; i < p.count; i++) {
      pos.push(p.getX(i), p.getY(i), p.getZ(i));
      nrm.push(n.getX(i), n.getY(i), n.getZ(i));
      col.push(c.r, c.g, c.b);
      mat.push(id);
    }
    const index = g.index;
    for (let i = 0; i < index.count; i++) idx.push(base + index.getX(i));
    for (let i = 0; i < index.count / 3; i++) tags.push(NO_GROW.has(o.material.name) ? TAG.noGrow : TAG.solid);
  });
  const geom = new THREE.BufferGeometry();
  geom.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geom.setAttribute('normal', new THREE.Float32BufferAttribute(nrm, 3));
  geom.setAttribute('aColor', new THREE.Float32BufferAttribute(col, 3));
  geom.setAttribute('aMat', new THREE.Float32BufferAttribute(mat, 1));
  geom.setIndex(new THREE.Uint32BufferAttribute(idx, 1));
  return { geom, host: { pos: new Float32Array(pos), idx: new Uint32Array(idx), tags: new Uint8Array(tags) } };
}

// Host = building triangles + a ground patch around them (margin metres wide) for bushes, trees and roots.
function withGroundPatch(host, margin) {
  let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
  for (let i = 0; i < host.pos.length; i += 3) {
    x0 = Math.min(x0, host.pos[i]); x1 = Math.max(x1, host.pos[i]);
    z0 = Math.min(z0, host.pos[i + 2]); z1 = Math.max(z1, host.pos[i + 2]);
  }
  [x0, x1, z0, z1] = [x0 - margin, x1 + margin, z0 - margin, z1 + margin];
  const n = host.pos.length / 3;
  const pos = new Float32Array(host.pos.length + 12);
  pos.set(host.pos);
  pos.set([x0, 0, z0, x1, 0, z0, x1, 0, z1, x0, 0, z1], host.pos.length);
  const idx = new Uint32Array(host.idx.length + 6);
  idx.set(host.idx);
  idx.set([n, n + 3, n + 2, n, n + 2, n + 1], host.idx.length);
  const tags = new Uint8Array(host.tags.length + 2);
  tags.set(host.tags);
  tags.set([TAG.ground, TAG.ground], host.tags.length);
  return { pos, idx, tags };
}

// ---------------------------------------------------------------- build

const skyColor = new THREE.Vector3(...hexToLinear(PALETTE.skyHorizon));
let generator, houseLayer, bushLayer, treeLayer, grassLayer, houseGeoms, houseHost, heroSeed = 1;
const layers = [];
const genStats = {};

function plantPart(lods, ranges, shrink) {
  return {
    lods, ranges,
    material: () => M.foliageMaterial(shrink),
    shadowMaterial: (lod) => (lod <= 2 ? M.foliageShadowMaterial(shrink) : null),
  };
}

function buildingVariant(geoms, plantLods, buildingRanges, plantRanges) {
  return {
    parts: [
      { lods: geoms, ranges: buildingRanges, material: () => M.buildingMaterial(skyColor), shadowMaterial: () => M.buildingShadowMaterial() },
      plantPart(plantLods, plantRanges),
    ],
  };
}
const houseVariant = (plantLods) => buildingVariant(houseGeoms, plantLods, RANGES.house, RANGES.housePlants);

// glTF models are embedded as base64 modules (see tools/make-house.mjs) so hosts that refuse .glb files still work.
async function loadModel(module) {
  const { GLB_BASE64 } = await import(module);
  const glb = Uint8Array.from(atob(GLB_BASE64), (c) => c.charCodeAt(0)).buffer;
  return new GLTFLoader().parseAsync(glb, '');
}

function growHero(seed) {
  generator.setHost(houseHost.pos, houseHost.idx, houseHost.tags);
  generator.withParams({ ...HERO_PARAMS, ...heroOverrides });
  const t0 = performance.now();
  const lods = generator.growOnHost(seed);
  genStats.hero = { ms: performance.now() - t0, ...generator.stats(), tris: lods.map((g) => g?.userData.triangles ?? 0) };
  return lods;
}

async function build() {
  status('Loading WebAssembly plant core...');
  generator = await PlantGenerator.create();

  status('Loading models...');
  const houseModel = await loadModel('../assets/house-glb.js');
  const towerModel = await loadModel('../assets/towers-glb.js');
  const lod0 = houseGeometry(houseModel.scene.getObjectByName('House_LOD0'));
  const lod1 = houseGeometry(houseModel.scene.getObjectByName('House_LOD1'));
  houseGeoms = [lod0.geom, lod1.geom];
  houseHost = withGroundPatch(lod0.host, 4.5);
  generator.setHost(houseHost.pos, houseHost.idx, houseHost.tags);

  status('Growing the hero house...');
  await nextFrame();
  const variants = [houseVariant(growHero(heroSeed))];
  for (let v = 1; v <= WORLD.houseVariants; v++) {
    status(`Growing town house variant ${v}/${WORLD.houseVariants}...`);
    await nextFrame();
    generator.withParams({});
    variants.push(houseVariant(generator.growOnHost(100 + v * 17)));
  }
  for (const [i, name] of TOWER_NAMES.entries()) {
    status(`Growing tower ${i + 1}/${TOWER_NAMES.length}...`);
    await nextFrame();
    const t0 = houseGeometry(towerModel.scene.getObjectByName(`${name}_LOD0`));
    const t1 = houseGeometry(towerModel.scene.getObjectByName(`${name}_LOD1`));
    const host = withGroundPatch(t0.host, 6);
    generator.setHost(host.pos, host.idx, host.tags);
    generator.withParams(TOWER_PARAMS);
    variants.push(buildingVariant([t0.geom, t1.geom], generator.growOnHost(400 + i * 41), RANGES.tower, RANGES.towerPlants));
  }
  houseLayer = new LodLayer({ name: 'houses', cellSize: WORLD.siteSpacing, maxDistance: 3200, variants,
    generateCell: houseCell, yRange: [-2, 52] });

  status('Growing bushes and grass...');
  await nextFrame();
  generator.withParams({});
  const bushVariants = [];
  const styles = [0, 1, 2, 3, 0, 2, 1, 3, 0, 2];
  styles.forEach((style, i) => bushVariants.push({ parts: [plantPart(generator.makeBush(500 + i * 31, style, 1), RANGES.bush)] }));
  bushLayer = new LodLayer({ name: 'bushes', cellSize: 32, maxDistance: 1100, variants: bushVariants,
    generateCell: makeBushCell(bushVariants.length, 32), yRange: [-1, 4] });
  status('Growing trees...');
  await nextFrame();
  // Round trees in nine sizes, then oaks: three sizes, each grown broad (alone), medium and tall (crowded).
  const treeSizes = [0.4, 0.5, 0.75, 0.9, 1.0, 1.15, 1.35, 1.6, 1.85];
  const treeVariants = treeSizes.map((size, i) => ({ parts: [plantPart(generator.makeTree(300 + i * 23, size, 0, 0.5), RANGES.tree)] }));
  const oakSizes = [0.8, 1.05, 1.3], oakForms = [0, 0.5, 1];
  const kinds = { round: treeVariants.length, oakBase: treeVariants.length, oakSizes: oakSizes.length, oakForms: oakForms.length };
  for (const [si, size] of oakSizes.entries()) {
    status(`Growing oaks ${si + 1}/${oakSizes.length}...`);
    await nextFrame();
    for (const [fi, crowding] of oakForms.entries()) {
      treeVariants.push({ parts: [plantPart(generator.makeTree(700 + si * 31 + fi * 7, size, 1, crowding), RANGES.tree)] });
    }
  }
  treeLayer = new LodLayer({ name: 'trees', cellSize: 48, maxDistance: 2600, variants: treeVariants,
    generateCell: makeTreeCell(kinds, 48), yRange: [-1, 32] });
  const grassVariants = [];
  for (let i = 0; i < 8; i++) grassVariants.push({ parts: [plantPart(generator.makeGrass(900 + i * 13), RANGES.grass, [30, 50])] });
  grassLayer = new LodLayer({ name: 'grass', cellSize: 6, maxDistance: 52, variants: grassVariants,
    generateCell: makeGrassCell(grassVariants.length, 6), yRange: [-1, 1.5], fade: 0.25 });
  layers.push(houseLayer, treeLayer, bushLayer, grassLayer);
  for (const l of layers) scene.add(l.group);

  status('Shaping the terrain...');
  await nextFrame();
  const terrain = new THREE.Mesh(buildTerrain(), M.terrainMaterial());
  terrain.userData.mainMaterial = terrain.material;
  terrain.userData.shadowMaterial = M.terrainShadowMaterial();
  terrain.layers.enable(1);
  terrain.frustumCulled = false;
  scene.add(terrain);
  const mountains = new THREE.Mesh(buildMountains(), M.sceneryMaterial(PALETTE.mountain));
  mountains.frustumCulled = false;
  scene.add(mountains);
}

// ---------------------------------------------------------------- UI

const heroOverrides = {};
// Viewpoints: heights are metres above the terrain at that spot.
const views = {
  Digit1: { pos: [9.5, 2.2, 10.5], target: [1, 2.5, 3] },
  Digit2: { pos: [-15, 1.4, 4], target: [-7, 1.6, 0] },
  Digit3: { pos: [26, 14, 30], target: [-2, 3, 0] },
  Digit4: { pos: [180, 95, 260], target: [0, 0, 0] },
  Digit5: { pos: [445, 2.2, -95], target: [551, 4, -174] },
  Digit6: { pos: [650, 45, -70], target: [551, 0, -174] },
  Digit7: { pos: [18, 7, 2], target: [-2, 9, -52] },
  Digit8: { pos: [-50, 2, -22], target: [-27, 5, -3] },
};

function goTo(flycam, view) {
  const at = ([x, h, z]) => new THREE.Vector3(x, terrainHeight(x, z) + h, z);
  flycam.lookAt(at(view.pos), at(view.target));
}

function setupUi(flycam) {
  const speed = $('speed');
  flycam.onSpeedChange = (s) => { speed.textContent = s.toFixed(1); };
  speed.textContent = flycam.speed.toFixed(1);

  for (const button of document.querySelectorAll('[data-view]')) {
    button.addEventListener('click', () => goTo(flycam, views[button.dataset.view]));
  }
  const toggleUi = () => {
    const hidden = document.body.classList.toggle('hide-ui');
    $('ui-toggle').textContent = hidden ? 'Show panels' : 'Hide panels';
  };
  $('ui-toggle').addEventListener('click', toggleUi);
  if (window.matchMedia?.('(prefers-reduced-motion: reduce)').matches) $('wind').value = '0.25';

  const bind = (id, fn) => { const el = $(id); el.addEventListener('input', () => fn(el)); fn(el); };
  bind('grow', (el) => { M.shared.uGrow.value = Number(el.value); });
  bind('wind', (el) => { M.shared.uWind.value.z = Number(el.value); });
  bind('sun-el', (el) => { sun.elevation = Number(el.value); updateSun(); });
  bind('sun-az', (el) => { sun.azimuth = Number(el.value); updateSun(); });
  bind('lod-debug', (el) => { M.shared.uLodDebug.value = el.checked ? 1 : 0; for (const l of layers) l.setLodDebug(M.LOD_COLORS); });
  bind('outlines', (el) => { post.material.uniforms.uOutlines.value = el.checked ? 1 : 0; });
  bind('leaf-lines', (el) => { M.shared.uLeafLines.value = el.checked ? 1 : 0; });
  bind('shadows', (el) => { M.shared.uShadowsOn.value = el.checked ? 1 : 0; });
  $('res').value = String(pixelRatio);
  bind('res', (el) => {
    pixelRatio = Number(el.value);
    resize();
  });

  let animating = false;
  $('animate').addEventListener('click', () => {
    animating = true;
    const t0 = performance.now();
    const tick = () => {
      const t = Math.min(1, (performance.now() - t0) / 9000);
      $('grow').value = String(t);
      M.shared.uGrow.value = t;
      if (t < 1 && animating) requestAnimationFrame(tick);
      else animating = false;
    };
    tick();
  });

  // Generator parameters for the hero house, built from the core's own parameter table.
  const list = $('params');
  for (const p of generator.params) {
    const row = document.createElement('label');
    row.className = 'param';
    const value = HERO_PARAMS[p.name] ?? p.default;
    const step = Number.isInteger(p.default) && p.max > 4 ? 1 : (p.max - p.min) / 200;
    row.innerHTML = `<span>${p.name}</span><input type="range" min="${p.min}" max="${p.max}" step="${step}" value="${value}"><output>${+value.toFixed(3)}</output>`;
    const input = row.querySelector('input');
    input.addEventListener('input', () => {
      heroOverrides[p.name] = Number(input.value);
      row.querySelector('output').textContent = String(+Number(input.value).toFixed(3));
    });
    list.appendChild(row);
  }
  const regrow = (seed) => {
    heroSeed = seed;
    $('seed').value = String(seed);
    const lods = growHero(heroSeed);
    houseLayer.replaceVariant(0, houseVariant(lods));
    if (M.shared.uLodDebug.value) houseLayer.setLodDebug(M.LOD_COLORS);
    showGenStats();
  };
  $('regrow').addEventListener('click', () => regrow(Number($('seed').value) || 1));
  $('reseed').addEventListener('click', () => regrow((Math.random() * 1e6) | 0));

  window.addEventListener('keydown', (e) => {
    if (e.target instanceof HTMLInputElement) return;
    if (views[e.code]) goTo(flycam, views[e.code]);
    if (e.code === 'KeyL') $('lod-debug').click();
    if (e.code === 'KeyO') $('outlines').click();
    if (e.code === 'KeyG') $('animate').click();
    if (e.code === 'KeyR') $('reseed').click();
    if (e.code === 'KeyH') toggleUi();
  });
}

function showGenStats() {
  const h = genStats.hero;
  $('gen').textContent = `hero: ${h.climbers} climbers, ${h.trees} trees, ${h.bushes} bushes, ${h.leaves.toLocaleString()} leaves, ` +
    `${h.flowers} flowers in ${h.ms.toFixed(0)} ms - LOD tris ${h.tris.map((t) => t.toLocaleString()).join(' / ')}`;
}

// ---------------------------------------------------------------- frame loop

const frustum = new THREE.Frustum();
const projView = new THREE.Matrix4();
let last = performance.now(), fpsAvg = 60, statTimer = 0;

function frame(flycam) {
  const now = performance.now();
  const dt = Math.min(0.1, (now - last) / 1000);
  last = now;
  flycam.update(dt);
  camera.updateMatrixWorld();
  M.shared.uTime.value = now / 1000;
  M.shared.uCameraPos.value.copy(camera.position);

  projView.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
  frustum.setFromProjectionMatrix(projView);
  for (const l of layers) l.update(camera, frustum);

  // Shadow pass: casters (layer 1) drawn with their depth-only material.
  if (M.shared.uShadowsOn.value) {
    updateShadowCamera();
    const casters = [];
    scene.traverse((o) => {
      if (o.isMesh && o.userData.shadowMaterial && o.visible) {
        casters.push(o);
        o.material = o.userData.shadowMaterial;
      }
    });
    renderer.setRenderTarget(shadowTarget);
    renderer.clear();
    renderer.render(scene, shadowCamera);
    for (const o of casters) o.material = o.userData.mainMaterial;
  }

  renderer.info.reset();
  renderer.setRenderTarget(mainTarget);
  renderer.setClearColor(0x000000, 1);
  renderer.clear();
  renderer.render(scene, camera);
  const calls = renderer.info.render.calls, tris = renderer.info.render.triangles;
  post.render(renderer, mainTarget, camera, M.shared.uSunDir.value, now / 1000);

  fpsAvg = fpsAvg * 0.95 + (1 / Math.max(dt, 1e-4)) * 0.05;
  statTimer += dt;
  if (statTimer > 0.25) {
    statTimer = 0;
    const lod = [0, 0, 0, 0];
    for (const l of layers) l.stats.perLod.forEach((n, i) => { lod[i] += n; });
    const p = camera.position;
    $('stats').textContent =
      `${fpsAvg.toFixed(0)} fps | ${calls} draws | ${(tris / 1e6).toFixed(2)} M tris\n` +
      `houses ${houseLayer.stats.instances}  trees ${treeLayer.stats.instances}  bushes ${bushLayer.stats.instances}  grass ${grassLayer.stats.instances}\n` +
      `parts by LOD  0:${lod[0]}  1:${lod[1]}  2:${lod[2]}  3:${lod[3]}\n` +
      `pos ${p.x.toFixed(0)}, ${p.y.toFixed(0)}, ${p.z.toFixed(0)}`;
  }
  requestAnimationFrame(() => frame(flycam));
}

// ---------------------------------------------------------------- start

renderer.autoClear = false;
renderer.info.autoReset = false;
try {
  await build();
  const flycam = new FlyCam(camera, canvas, (x, z) => terrainHeight(x, z));
  goTo(flycam, views.Digit3);
  setupUi(flycam);
  showGenStats();
  $('loading').remove();
  window.__demo = { scene, camera, renderer, layers, generator, flycam, shared: M.shared };
  frame(flycam);
} catch (err) {
  console.error(err);
  status(`Failed: ${err.message}`);
}
