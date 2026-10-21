// Generates the host models in the style of the concept art:
//   web/assets/house.glb   a weathered two-storey concrete house with an annex, balcony, roof hut and water tank
//                          (House_LOD0 detailed, House_LOD1 for distance)
//   web/assets/towers.glb  three tall apartment towers (TowerA 8 floors, TowerB 13 floors, TowerC 6 floors wide),
//                          each as <name>_LOD0 and <name>_LOD1
// Each .glb also gets a <name>-glb.js module holding the same bytes as base64, which the demo imports.
// Material names carry the growth tags:
// "window" and "door" are no-grow surfaces, everything else may be overgrown.
//
//   node tools/make-house.mjs
import { mkdirSync, writeFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { PALETTE, hexToLinear } from '../web/js/palette.js';

const MATERIALS = [
  { name: 'concrete', color: PALETTE.concrete, grow: true },
  { name: 'concrete_dark', color: PALETTE.concreteDark, grow: true },
  { name: 'trim', color: PALETTE.trim, grow: true },
  { name: 'rust', color: PALETTE.rust, grow: true },
  { name: 'sign', color: '#3b4c6e', grow: true },
  { name: 'window', color: PALETTE.window, grow: false },
  { name: 'door', color: PALETTE.door, grow: false },
];

const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const UP = [0, 1, 0];

class MeshBuilder {
  constructor() {
    this.parts = new Map();
  }
  part(mat) {
    if (!this.parts.has(mat)) this.parts.set(mat, { pos: [], nrm: [], idx: [] });
    return this.parts.get(mat);
  }
  // Quad with corners in order around the face; winding is fixed up to face along n.
  quad(mat, a, b, c, d, n) {
    const p = this.part(mat);
    let pts = [a, b, c, d];
    if (dot(cross(sub(b, a), sub(c, a)), n) < 0) pts = [a, d, c, b];
    const base = p.pos.length / 3;
    for (const v of pts) {
      p.pos.push(...v);
      p.nrm.push(...n);
    }
    p.idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  box(mat, lo, hi, { skipBottom = false } = {}) {
    const [x0, y0, z0] = lo;
    const [x1, y1, z1] = hi;
    this.quad(mat, [x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1], [1, 0, 0]);
    this.quad(mat, [x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [-1, 0, 0]);
    this.quad(mat, [x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [0, 1, 0]);
    if (!skipBottom) this.quad(mat, [x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [0, -1, 0]);
    this.quad(mat, [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [0, 0, 1]);
    this.quad(mat, [x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [x1, y0, z0], [0, 0, -1]);
  }
  cylinder(mat, center, radius, height, segments) {
    const p = this.part(mat);
    const ring = (y) => Array.from({ length: segments }, (_, i) => {
      const a = (i / segments) * Math.PI * 2;
      return [center[0] + Math.cos(a) * radius, y, center[2] + Math.sin(a) * radius];
    });
    const bottom = ring(center[1]);
    const top = ring(center[1] + height);
    for (let i = 0; i < segments; i++) {
      const j = (i + 1) % segments;
      const a = (((i + 0.5) / segments) * Math.PI * 2);
      this.quad(mat, bottom[i], bottom[j], top[j], top[i], [Math.cos(a), 0, Math.sin(a)]);
    }
    const base = p.pos.length / 3;
    p.pos.push(center[0], center[1] + height, center[2]);
    p.nrm.push(...UP);
    for (const v of top) {
      p.pos.push(...v);
      p.nrm.push(...UP);
    }
    for (let i = 0; i < segments; i++) p.idx.push(base, base + 1 + ((i + 1) % segments), base + 1 + i);
  }
}

// A wall slab with rectangular openings. origin: bottom-left of the outer face seen from outside,
// u: rightward along the face, n: outward normal. Openings get recessed glass/door panels, frames and sills.
// frames: window frames and mullions; inner: the wall's inside face (towers skip both to stay light).
function wall(m, { origin, u, n, width, height, thickness = 0.25, openings = [], mat = 'concrete', detail = true,
  frames = true, inner = true }) {
  const P = (x, y, d) => add(add(add(origin, mul(u, x)), mul(UP, y)), mul(n, -d));
  const neg = mul(n, -1);
  const negU = mul(u, -1);
  if (!detail) {
    // LOD1: solid slab, openings painted on as panels just in front of the face.
    m.quad(mat, P(0, 0, 0), P(width, 0, 0), P(width, height, 0), P(0, height, 0), n);
    m.quad(mat, P(width, 0, 0), P(width, 0, thickness), P(width, height, thickness), P(width, height, 0), u);
    m.quad(mat, P(0, 0, 0), P(0, 0, thickness), P(0, height, thickness), P(0, height, 0), negU);
    m.quad(mat, P(0, height, 0), P(width, height, 0), P(width, height, thickness), P(0, height, thickness), UP);
    for (const o of openings) {
      const d = -0.02;
      m.quad(o.door ? 'door' : 'window', P(o.x, o.y, d), P(o.x + o.w, o.y, d), P(o.x + o.w, o.y + o.h, d), P(o.x, o.y + o.h, d), n);
    }
    return;
  }
  const xs = [...new Set([0, width, ...openings.flatMap((o) => [o.x, o.x + o.w])])].sort((a, b) => a - b);
  const ys = [...new Set([0, height, ...openings.flatMap((o) => [o.y, o.y + o.h])])].sort((a, b) => a - b);
  const filled = (i, j) => {
    if (i < 0 || j < 0 || i >= xs.length - 1 || j >= ys.length - 1) return false;
    const cx = (xs[i] + xs[i + 1]) / 2;
    const cy = (ys[j] + ys[j + 1]) / 2;
    return !openings.some((o) => cx > o.x && cx < o.x + o.w && cy > o.y && cy < o.y + o.h);
  };
  for (let i = 0; i < xs.length - 1; i++) {
    for (let j = 0; j < ys.length - 1; j++) {
      if (!filled(i, j)) continue;
      const [x0, x1, y0, y1] = [xs[i], xs[i + 1], ys[j], ys[j + 1]];
      const t = thickness;
      m.quad(mat, P(x0, y0, 0), P(x1, y0, 0), P(x1, y1, 0), P(x0, y1, 0), n);
      if (inner) m.quad(mat, P(x0, y0, t), P(x1, y0, t), P(x1, y1, t), P(x0, y1, t), neg);
      if (!filled(i - 1, j)) m.quad(mat, P(x0, y0, 0), P(x0, y0, t), P(x0, y1, t), P(x0, y1, 0), negU);
      if (!filled(i + 1, j)) m.quad(mat, P(x1, y0, 0), P(x1, y0, t), P(x1, y1, t), P(x1, y1, 0), u);
      if (!filled(i, j - 1)) m.quad(mat, P(x0, y0, 0), P(x1, y0, 0), P(x1, y0, t), P(x0, y0, t), mul(UP, -1));
      if (!filled(i, j + 1)) m.quad(mat, P(x0, y1, 0), P(x1, y1, 0), P(x1, y1, t), P(x0, y1, t), UP);
    }
  }
  for (const o of openings) {
    const panel = o.door ? 'door' : 'window';
    const d = 0.14;
    m.quad(panel, P(o.x, o.y, d), P(o.x + o.w, o.y, d), P(o.x + o.w, o.y + o.h, d), P(o.x, o.y + o.h, d), n);
    // Frame: four thin bars just in front of the panel.
    const f = 0.06;
    const frameMat = o.door ? 'rust' : 'trim';
    const bar = (x0, y0, x1, y1) => {
      const a = P(x0, y0, 0.04), b = P(x1, y1, 0.12);
      m.box(frameMat, [Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.min(a[2], b[2])],
        [Math.max(a[0], b[0]), Math.max(a[1], b[1]), Math.max(a[2], b[2])]);
    };
    if (frames) {
      bar(o.x, o.y + o.h - f, o.x + o.w, o.y + o.h);
      bar(o.x, o.y, o.x + f, o.y + o.h - f);
      bar(o.x + o.w - f, o.y, o.x + o.w, o.y + o.h - f);
    }
    if (!o.door) {
      if (frames) {
        bar(o.x + f, o.y, o.x + o.w - f, o.y + f);
        bar(o.x + o.w / 2 - f / 2, o.y + f, o.x + o.w / 2 + f / 2, o.y + o.h - f); // mullion
      }
      // Sill: a concrete ledge that sticks out under the window.
      const a = P(o.x - 0.1, o.y - 0.08, 0.05), b = P(o.x + o.w + 0.1, o.y, -0.11);
      m.box('concrete_dark', [Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.min(a[2], b[2])],
        [Math.max(a[0], b[0]), Math.max(a[1], b[1]), Math.max(a[2], b[2])]);
    }
  }
}

function buildHouse(detail) {
  const m = new MeshBuilder();
  const H = 6.4; // main walls
  const win = (x, y, w = 1.2, h = 1.4) => ({ x, y, w, h });
  const door = (x, y, w, h) => ({ x, y, w, h, door: true });

  // Plinth hides the seam with uneven terrain.
  m.box('concrete_dark', [-4.08, -1.5, -3.08], [4.08, 0.3, 3.08]);
  m.box('concrete_dark', [-7.58, -1.5, -2.58], [-4.0, 0.3, 1.58]);

  // Main block.
  wall(m, { origin: [-4, 0, 3], u: [1, 0, 0], n: [0, 0, 1], width: 8, height: H, detail,
    openings: [door(1.2, 0.3, 1.1, 2.2), win(4.0, 1.0), win(6.0, 1.0), win(1.2, 4.1), door(3.5, 3.35, 1.0, 2.2), win(5.6, 4.1)] });
  wall(m, { origin: [4, 0, -3], u: [-1, 0, 0], n: [0, 0, -1], width: 8, height: H, detail,
    openings: [win(1.0, 1.0), win(3.4, 1.0), win(5.8, 1.0), win(1.0, 4.1), win(3.4, 4.1), win(5.8, 4.1)] });
  wall(m, { origin: [4, 0, 2.75], u: [0, 0, -1], n: [1, 0, 0], width: 5.5, height: H, detail,
    openings: [win(1.0, 1.0), win(3.3, 1.0), win(2.15, 4.1)] });
  wall(m, { origin: [-4, 0, -2.75], u: [0, 0, 1], n: [-1, 0, 0], width: 5.5, height: H, detail,
    openings: [win(1.0, 4.1), win(3.3, 4.1)] });
  m.box('concrete', [-3.75, 6.2, -2.75], [3.75, 6.4, 2.75]); // roof slab

  // Parapet with a cap.
  const ph = 0.65;
  for (const [lo, hi] of [
    [[-4, H, 2.8], [4, H + ph, 3]], [[-4, H, -3], [4, H + ph, -2.8]],
    [[3.8, H, -2.8], [4, H + ph, 2.8]], [[-4, H, -2.8], [-3.8, H + ph, 2.8]],
  ]) m.box('concrete', lo, hi, { skipBottom: true });
  m.box('trim', [-4.05, H + ph, -3.05], [4.05, H + ph + 0.07, -2.75]);
  m.box('trim', [-4.05, H + ph, 2.75], [4.05, H + ph + 0.07, 3.05]);
  m.box('trim', [3.75, H + ph, -2.75], [4.05, H + ph + 0.07, 2.75]);
  m.box('trim', [-4.05, H + ph, -2.75], [-3.75, H + ph + 0.07, 2.75]);

  // Floor band between storeys.
  const b0 = 3.1, b1 = 3.28, o = 0.07;
  m.box('trim', [-4 - o, b0, 3], [4 + o, b1, 3 + o]);
  m.box('trim', [-4 - o, b0, -3 - o], [4 + o, b1, -3]);
  m.box('trim', [4, b0, -3], [4 + o, b1, 3]);
  m.box('trim', [-4 - o, b0, -3], [-4, b1, 3]);

  // Balcony on the front with a rusty railing.
  m.box('concrete', [-1.6, 3.12, 3.0], [1.6, 3.35, 4.2]);
  if (detail) {
    for (let x = -1.55; x <= 1.56; x += 0.31) m.box('rust', [x - 0.02, 3.35, 4.11], [x + 0.02, 4.3, 4.15]);
    for (let z = 3.35; z < 4.1; z += 0.38) {
      m.box('rust', [-1.57, 3.35, z - 0.02], [-1.53, 4.3, z + 0.02]);
      m.box('rust', [1.53, 3.35, z - 0.02], [1.57, 4.3, z + 0.02]);
    }
    m.box('rust', [-1.6, 4.28, 4.09], [1.6, 4.34, 4.17]);
    m.box('rust', [-1.6, 4.28, 3.0], [-1.52, 4.34, 4.17]);
    m.box('rust', [1.52, 4.28, 3.0], [1.6, 4.34, 4.17]);
    m.box('rust', [-1.6, 3.78, 4.11], [1.6, 3.82, 4.15]);
  } else {
    m.box('rust', [-1.6, 3.35, 4.1], [1.6, 4.32, 4.16]);
  }

  // Annex on the left: one storey, flat roof, a rusty sign on its end wall.
  const AH = 3.2;
  wall(m, { origin: [-7.5, 0, 1.5], u: [1, 0, 0], n: [0, 0, 1], width: 3.5, height: AH, detail,
    openings: [win(1.1, 1.0, 1.3, 1.3)] });
  wall(m, { origin: [-4, 0, -2.5], u: [-1, 0, 0], n: [0, 0, -1], width: 3.5, height: AH, detail,
    openings: [win(1.1, 1.0)] });
  wall(m, { origin: [-7.5, 0, -2.25], u: [0, 0, 1], n: [-1, 0, 0], width: 3.5, height: AH, detail });
  m.box('concrete', [-7.25, 3.0, -2.25], [-4, AH, 1.25]);
  const aph = 0.4;
  m.box('concrete', [-7.5, AH, 1.3], [-4, AH + aph, 1.5], { skipBottom: true });
  m.box('concrete', [-7.5, AH, -2.5], [-4, AH + aph, -2.3], { skipBottom: true });
  m.box('concrete', [-7.5, AH, -2.3], [-7.3, AH + aph, 1.3], { skipBottom: true });
  m.box('sign', [-7.62, 1.25, -1.2], [-7.5, 2.35, 0.8]);
  if (detail) {
    m.box('rust', [-7.66, 1.2, -1.25], [-7.6, 1.28, 0.85]);
    m.box('rust', [-7.66, 2.32, -1.25], [-7.6, 2.4, 0.85]);
    m.box('rust', [-7.66, 1.2, -1.25], [-7.6, 2.4, -1.17]);
    m.box('rust', [-7.66, 1.2, 0.77], [-7.6, 2.4, 0.85]);
  }

  // Roof: stair hut and water tank.
  m.box('concrete', [-3.4, H, -2.6], [-1.6, H + 2.2, -0.8], { skipBottom: true });
  m.box('trim', [-3.5, H + 2.2, -2.7], [-1.5, H + 2.3, -0.7]);
  m.quad('door', [-3.0, H + 0.05, -0.79], [-2.2, H + 0.05, -0.79], [-2.2, H + 1.95, -0.79], [-3.0, H + 1.95, -0.79], [0, 0, 1]);
  m.cylinder('rust', [2.3, H + 0.8, -1.4], 0.7, 1.4, detail ? 14 : 6);
  for (const [dx, dz] of [[-0.45, -0.45], [0.45, -0.45], [-0.45, 0.45], [0.45, 0.45]])
    m.box('rust', [2.3 + dx - 0.05, H, -1.4 + dz - 0.05], [2.3 + dx + 0.05, H + 0.8, -1.4 + dz + 0.05]);

  if (detail) {
    m.box('rust', [3.82, 0.3, 3.0], [3.92, H + 0.6, 3.1]);      // drain pipe
    m.box('trim', [4.0, 1.6, 0.1], [4.45, 2.3, 0.9]);           // air conditioner
    m.quad('window', [4.451, 1.7, 0.2], [4.451, 1.7, 0.8], [4.451, 2.2, 0.8], [4.451, 2.2, 0.2], [1, 0, 0]);
  }
  return m;
}

// Tall apartment towers. Every floor has a concrete band (ledges for plants to catch on), the front gets rusty
// balconies on alternate floors, and the roof carries a parapet, stair hut, water tank and antenna.
const TOWERS = [
  { name: 'TowerA', floors: 8, width: 10, depth: 10, cols: [3, 3], balconyEvery: 2 },
  { name: 'TowerB', floors: 13, width: 8, depth: 12, cols: [2, 3], balconyEvery: 3 },
  { name: 'TowerC', floors: 6, width: 14, depth: 9, cols: [4, 3], balconyEvery: 2 },
];

function buildTower(spec, detail) {
  const m = new MeshBuilder();
  const FH = 3.1, H = spec.floors * FH, W = spec.width, D = spec.depth, hw = W / 2, hd = D / 2, t = 0.3;
  const openingsFor = (length, cols, front) => {
    const out = [];
    const spacing = length / cols, w = Math.min(1.5, spacing * 0.55);
    for (let f = 0; f < spec.floors; f++) {
      for (let c = 0; c < cols; c++) {
        const x = (c + 0.5) * spacing - w / 2;
        if (front && f === 0 && c === Math.floor(cols / 2)) out.push({ x: x - 0.1, y: 0.3, w: w + 0.2, h: 2.4, door: true });
        else out.push({ x, y: f * FH + 0.95, w, h: 1.45 });
      }
    }
    return out;
  };
  const common = { height: H, thickness: t, detail, frames: false, inner: false };
  m.box('concrete_dark', [-hw - 0.1, -1.5, -hd - 0.1], [hw + 0.1, 0.3, hd + 0.1]);
  wall(m, { ...common, origin: [-hw, 0, hd], u: [1, 0, 0], n: [0, 0, 1], width: W, openings: openingsFor(W, spec.cols[0], true) });
  wall(m, { ...common, origin: [hw, 0, -hd], u: [-1, 0, 0], n: [0, 0, -1], width: W, openings: openingsFor(W, spec.cols[0], false) });
  wall(m, { ...common, origin: [hw, 0, hd - t], u: [0, 0, -1], n: [1, 0, 0], width: D - 2 * t, openings: openingsFor(D - 2 * t, spec.cols[1], false) });
  wall(m, { ...common, origin: [-hw, 0, -hd + t], u: [0, 0, 1], n: [-1, 0, 0], width: D - 2 * t, openings: openingsFor(D - 2 * t, spec.cols[1], false) });
  m.box('concrete', [-hw + t, H - 0.25, -hd + t], [hw - t, H, hd - t]); // roof slab

  // Floor bands.
  for (let f = 1; f < spec.floors; f++) {
    if (!detail && f % 3) continue;
    const y0 = f * FH - 0.12, y1 = f * FH + 0.06, o = 0.08;
    m.box('trim', [-hw - o, y0, hd], [hw + o, y1, hd + o]);
    m.box('trim', [-hw - o, y0, -hd - o], [hw + o, y1, -hd]);
    m.box('trim', [hw, y0, -hd], [hw + o, y1, hd]);
    m.box('trim', [-hw - o, y0, -hd], [-hw, y1, hd]);
  }
  // Balconies on the front.
  const spacing = W / spec.cols[0];
  for (let f = 1; f < spec.floors; f += spec.balconyEvery) {
    for (let c = 0; c < spec.cols[0]; c++) {
      if ((c + f) % 2) continue;
      const cx = -hw + (c + 0.5) * spacing, bw = Math.min(2.4, spacing * 0.8) / 2, y = f * FH;
      m.box('concrete', [cx - bw, y - 0.15, hd], [cx + bw, y + 0.1, hd + 1.0]);
      if (detail) {
        m.box('rust', [cx - bw, y + 0.9, hd + 0.94], [cx + bw, y + 0.96, hd + 1.0]);
        for (let x = cx - bw; x <= cx + bw + 0.01; x += (2 * bw) / 5) m.box('rust', [x - 0.02, y + 0.1, hd + 0.95], [x + 0.02, y + 0.92, hd + 0.99]);
        m.box('rust', [cx - bw, y + 0.1, hd + 0.05], [cx - bw + 0.04, y + 0.96, hd + 1.0]);
        m.box('rust', [cx + bw - 0.04, y + 0.1, hd + 0.05], [cx + bw, y + 0.96, hd + 1.0]);
      } else {
        m.box('rust', [cx - bw, y + 0.1, hd + 0.95], [cx + bw, y + 0.96, hd + 1.0]);
      }
    }
  }
  // Roof: parapet, cap, stair hut, water tank and an antenna.
  const ph = 0.9;
  m.box('concrete', [-hw, H, hd - 0.25], [hw, H + ph, hd], { skipBottom: true });
  m.box('concrete', [-hw, H, -hd], [hw, H + ph, -hd + 0.25], { skipBottom: true });
  m.box('concrete', [hw - 0.25, H, -hd + 0.25], [hw, H + ph, hd - 0.25], { skipBottom: true });
  m.box('concrete', [-hw, H, -hd + 0.25], [-hw + 0.25, H + ph, hd - 0.25], { skipBottom: true });
  m.box('trim', [-hw - 0.05, H + ph, -hd - 0.05], [hw + 0.05, H + ph + 0.08, hd + 0.05]);
  const hutX = -hw + 1.0, hutZ = -hd + 1.0;
  m.box('concrete', [hutX, H, hutZ], [hutX + 2.2, H + 2.6, hutZ + 2.4], { skipBottom: true });
  m.box('trim', [hutX - 0.1, H + 2.6, hutZ - 0.1], [hutX + 2.3, H + 2.7, hutZ + 2.5]);
  m.quad('door', [hutX + 2.21, H + 0.05, hutZ + 0.6], [hutX + 2.21, H + 0.05, hutZ + 1.6], [hutX + 2.21, H + 2.2, hutZ + 1.6],
    [hutX + 2.21, H + 2.2, hutZ + 0.6], [1, 0, 0]);
  m.cylinder('rust', [hw - 2.0, H + 0.9, hd - 2.2], 0.9, 1.7, detail ? 14 : 6);
  for (const [dx, dz] of [[-0.6, -0.6], [0.6, -0.6], [-0.6, 0.6], [0.6, 0.6]])
    m.box('rust', [hw - 2.0 + dx - 0.06, H, hd - 2.2 + dz - 0.06], [hw - 2.0 + dx + 0.06, H + 0.9, hd - 2.2 + dz + 0.06]);
  m.box('rust', [hutX + 0.4, H + 2.7, hutZ + 0.4], [hutX + 0.5, H + (detail ? 6.5 : 6.0), hutZ + 0.5]);
  return m;
}

// --- glb writer ----------------------------------------------------------------------------------

function writeGlb(path, meshes) {
  const json = {
    asset: { version: '2.0', generator: 'GreatOak Plants tools/make-house.mjs' },
    scene: 0,
    scenes: [{ nodes: meshes.map((_, i) => i) }],
    nodes: meshes.map((m, i) => ({ name: m.name, mesh: i })),
    meshes: [],
    materials: MATERIALS.map((mat) => ({
      name: mat.name,
      pbrMetallicRoughness: { baseColorFactor: [...hexToLinear(mat.color), 1], metallicFactor: 0, roughnessFactor: 0.9 },
      extras: { plantsGrow: mat.grow },
    })),
    accessors: [],
    bufferViews: [],
    buffers: [],
  };
  const chunks = [];
  let offset = 0;
  const pushView = (typed, target) => {
    const bytes = new Uint8Array(typed.buffer, typed.byteOffset, typed.byteLength);
    const pad = (4 - (bytes.length % 4)) % 4;
    json.bufferViews.push({ buffer: 0, byteOffset: offset, byteLength: bytes.length, target });
    chunks.push(bytes, new Uint8Array(pad));
    offset += bytes.length + pad;
    return json.bufferViews.length - 1;
  };
  for (const mesh of meshes) {
    const primitives = [];
    for (const [matName, part] of mesh.builder.parts) {
      const pos = new Float32Array(part.pos);
      const nrm = new Float32Array(part.nrm);
      const vertexCount = pos.length / 3;
      const idx = vertexCount < 65536 ? new Uint16Array(part.idx) : new Uint32Array(part.idx);
      const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
      for (let i = 0; i < pos.length; i++) {
        min[i % 3] = Math.min(min[i % 3], pos[i]);
        max[i % 3] = Math.max(max[i % 3], pos[i]);
      }
      json.accessors.push({ bufferView: pushView(pos, 34962), componentType: 5126, count: vertexCount, type: 'VEC3', min, max });
      const posAcc = json.accessors.length - 1;
      json.accessors.push({ bufferView: pushView(nrm, 34962), componentType: 5126, count: vertexCount, type: 'VEC3' });
      const nrmAcc = json.accessors.length - 1;
      json.accessors.push({ bufferView: pushView(idx, 34963), componentType: idx instanceof Uint16Array ? 5123 : 5125,
        count: idx.length, type: 'SCALAR' });
      primitives.push({ attributes: { POSITION: posAcc, NORMAL: nrmAcc }, indices: json.accessors.length - 1,
        material: MATERIALS.findIndex((m) => m.name === matName) });
    }
    json.meshes.push({ name: mesh.name, primitives });
  }
  json.buffers.push({ byteLength: offset });

  let jsonBytes = new TextEncoder().encode(JSON.stringify(json));
  const jsonPad = (4 - (jsonBytes.length % 4)) % 4;
  jsonBytes = Uint8Array.from([...jsonBytes, ...new Array(jsonPad).fill(0x20)]);
  const bin = new Uint8Array(offset);
  let o = 0;
  for (const c of chunks) {
    bin.set(c, o);
    o += c.length;
  }
  const total = 12 + 8 + jsonBytes.length + 8 + bin.length;
  const out = new Uint8Array(total);
  const view = new DataView(out.buffer);
  view.setUint32(0, 0x46546c67, true); // 'glTF'
  view.setUint32(4, 2, true);
  view.setUint32(8, total, true);
  view.setUint32(12, jsonBytes.length, true);
  view.setUint32(16, 0x4e4f534a, true); // 'JSON'
  out.set(jsonBytes, 20);
  const binHeader = 20 + jsonBytes.length;
  view.setUint32(binHeader, bin.length, true);
  view.setUint32(binHeader + 4, 0x004e4942, true); // 'BIN\0'
  out.set(bin, binHeader + 8);
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, out);
  // The same bytes as a JS module: the demo imports this, so it also works on hosts that do not serve .glb files.
  const module = `// Generated by tools/make-house.mjs from ${basename(path)}. Do not edit.\nexport const GLB_BASE64 =\n  '${Buffer.from(out).toString('base64')}';\n`;
  writeFileSync(path.replace(/\.glb$/, '-glb.js'), module);
  return total;
}

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const outputs = [
  { file: 'house.glb', meshes: [{ name: 'House_LOD0', builder: buildHouse(true) }, { name: 'House_LOD1', builder: buildHouse(false) }] },
  { file: 'towers.glb', meshes: TOWERS.flatMap((t) => [
    { name: `${t.name}_LOD0`, builder: buildTower(t, true) },
    { name: `${t.name}_LOD1`, builder: buildTower(t, false) },
  ]) },
];
for (const { file, meshes } of outputs) {
  const outPath = join(root, 'web', 'assets', file);
  const bytes = writeGlb(outPath, meshes);
  for (const m of meshes) {
    let tris = 0;
    for (const part of m.builder.parts.values()) tris += part.idx.length / 3;
    console.log(`${m.name}: ${tris} triangles`);
  }
  console.log(`wrote ${outPath} (${(bytes / 1024).toFixed(1)} KiB)`);
}
