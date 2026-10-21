// Procedural world layout for the demo: rolling terrain, a town of overgrown houses, scattered
// bushes and grass. All of it is deterministic functions of position, so cells can be generated in
// any order (and the same rules can be re-run in Godot).
import * as THREE from 'three';

export const WORLD = {
  extent: 3200,        // terrain half-size in metres
  siteSpacing: 48,     // house lattice spacing
  heroClear: 70,       // no town houses this close to the hero house
  townRadius: 2700,
  houseVariants: 4,    // town variants (the hero house is variant 0)
};

// House footprint in local space (main block + annex + balcony), metres.
const FOOTPRINT = { x0: -7.7, x1: 4.2, z0: -3.2, z1: 4.3 };

// ---------------------------------------------------------------- noise

export function hash2(x, z, salt = 0) {
  let h = (Math.imul(x | 0, 374761393) + Math.imul(z | 0, 668265263) + Math.imul(salt | 0, 1442695041)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}

function vnoise(x, z) {
  const ix = Math.floor(x), iz = Math.floor(z);
  let fx = x - ix, fz = z - iz;
  fx = fx * fx * (3 - 2 * fx);
  fz = fz * fz * (3 - 2 * fz);
  const a = hash2(ix, iz, 9), b = hash2(ix + 1, iz, 9), c = hash2(ix, iz + 1, 9), d = hash2(ix + 1, iz + 1, 9);
  return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}

export function fbm(x, z, octaves = 4) {
  let s = 0, a = 0.5;
  for (let i = 0; i < octaves; i++) {
    s += a * vnoise(x, z);
    x = x * 2.03 + 17.3;
    z = z * 2.03 - 9.1;
    a *= 0.5;
  }
  return s;
}

const smoothstep = (e0, e1, x) => {
  const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
};

// ---------------------------------------------------------------- terrain and sites

function rawHeight(x, z) {
  const r = Math.hypot(x, z);
  const hills = fbm(x * 0.0017 + 3.1, z * 0.0017 - 1.7, 4) * 52 - 22;
  const detail = fbm(x * 0.011 + 31, z * 0.011 - 7, 3) * 4 - 2;
  return (hills + detail) * smoothstep(45, 230, r);
}

const siteCache = new Map();

// House site on lattice cell (i, j), or null. Sites cluster into villages.
export function latticeSite(i, j) {
  const key = `${i},${j}`;
  if (siteCache.has(key)) return siteCache.get(key);
  const s = WORLD.siteSpacing;
  const x = (i + 0.5) * s + (hash2(i, j, 1) - 0.5) * 16;
  const z = (j + 0.5) * s + (hash2(i, j, 2) - 0.5) * 16;
  const r = Math.hypot(x, z);
  let site = null;
  if (r > WORLD.heroClear && r < WORLD.townRadius) {
    const village = fbm(x * 0.0042 + 50, z * 0.0042, 3);
    const density = smoothstep(0.34, 0.58, village) * 0.9 + 0.08;
    if (hash2(i, j, 3) < density) {
      site = {
        x, z, y: rawHeight(x, z),
        yaw: hash2(i, j, 4) * Math.PI * 2,
        variant: 1 + Math.floor(hash2(i, j, 5) * WORLD.houseVariants),
      };
    }
  }
  siteCache.set(key, site);
  return site;
}

export const HERO_SITE = { x: 0, z: 0, y: 0, yaw: 0, variant: 0 };

function* sitesAround(x, z) {
  yield HERO_SITE;
  const s = WORLD.siteSpacing;
  const i0 = Math.floor(x / s), j0 = Math.floor(z / s);
  for (let j = j0 - 1; j <= j0 + 1; j++) {
    for (let i = i0 - 1; i <= i0 + 1; i++) {
      const site = latticeSite(i, j);
      if (site) yield site;
    }
  }
}

// Terrain height with flat plateaus under every house.
export function terrainHeight(x, z) {
  let h = rawHeight(x, z);
  for (const site of sitesAround(x, z)) {
    const d = Math.hypot(x - site.x, z - site.z);
    const w = 1 - smoothstep(11, 19, d);
    if (w > 0) h += (site.y - h) * w;
  }
  return h;
}

// Distance from (x, z) to the nearest house centre, and whether the point is inside a footprint.
export function houseProximity(x, z, margin = 0) {
  let nearest = Infinity, inside = false;
  for (const site of sitesAround(x, z)) {
    const dx = x - site.x, dz = z - site.z;
    nearest = Math.min(nearest, Math.hypot(dx, dz));
    const c = Math.cos(site.yaw), s = Math.sin(site.yaw);
    const lx = c * dx - s * dz, lz = s * dx + c * dz; // inverse of the shader's rotY
    if (lx > FOOTPRINT.x0 - margin && lx < FOOTPRINT.x1 + margin && lz > FOOTPRINT.z0 - margin && lz < FOOTPRINT.z1 + margin) inside = true;
  }
  return { nearest, inside };
}

// ---------------------------------------------------------------- cell generators for LodLayer

export function houseCell(cx, cz) {
  const out = [];
  if (cx === 0 && cz === 0) out.push({ ...HERO_SITE, scale: 1 });
  // House layer cells use the same spacing as the site lattice, so cell (i, j) owns site (i, j).
  const site = latticeSite(cx, cz);
  if (site) out.push({ x: site.x, y: site.y, z: site.z, yaw: site.yaw, scale: 1, variant: site.variant });
  return out;
}

export function makeBushCell(variantCount, cellSize) {
  return (cx, cz) => {
    const out = [];
    const x0 = cx * cellSize, z0 = cz * cellSize;
    const meadow = fbm((x0 + cellSize / 2) * 0.009 + 5, (z0 + cellSize / 2) * 0.009, 3);
    const count = Math.floor(hash2(cx, cz, 11) * 14 * smoothstep(0.3, 0.6, meadow)) + (hash2(cx, cz, 12) < 0.45 ? 1 : 0);
    for (let k = 0; k < count; k++) {
      const x = x0 + hash2(cx * 31 + k, cz, 13) * cellSize;
      const z = z0 + hash2(cx, cz * 31 + k, 14) * cellSize;
      if (houseProximity(x, z).nearest < 12) continue; // houses grow their own bushes
      out.push({
        x, z, y: terrainHeight(x, z) - 0.05,
        yaw: hash2(cx + k, cz - k, 15) * Math.PI * 2,
        scale: 0.55 + Math.pow(hash2(cx - k, cz + k, 16), 2) * 1.9,
        variant: Math.floor(hash2(cx + 7 * k, cz, 17) * variantCount),
      });
    }
    return out;
  };
}

// Trees gather in groves and line the edges of villages; sizes come from the variant (small to large).
export function makeTreeCell(variantCount, cellSize) {
  return (cx, cz) => {
    const out = [];
    const x0 = cx * cellSize, z0 = cz * cellSize;
    const grove = fbm((x0 + cellSize / 2) * 0.006 - 13, (z0 + cellSize / 2) * 0.006 + 4, 3);
    const count = Math.floor(hash2(cx, cz, 31) * 7 * smoothstep(0.45, 0.68, grove)) + (hash2(cx, cz, 32) < 0.25 ? 1 : 0);
    for (let k = 0; k < count; k++) {
      const x = x0 + hash2(cx * 53 + k, cz, 33) * cellSize;
      const z = z0 + hash2(cx, cz * 53 + k, 34) * cellSize;
      if (houseProximity(x, z).nearest < 15) continue; // houses grow their own trees
      if (Math.hypot(x, z) < 24) continue;            // keep the hero house in the open
      out.push({
        x, z, y: terrainHeight(x, z) - 0.1,
        yaw: hash2(cx + 3 * k, cz - k, 35) * Math.PI * 2,
        scale: 0.85 + hash2(cx - k, cz + 2 * k, 36) * 0.35,
        variant: Math.floor(hash2(cx + 11 * k, cz, 37) * variantCount),
      });
    }
    return out;
  };
}

export function makeGrassCell(variantCount, cellSize) {
  return (cx, cz) => {
    const out = [];
    const x0 = cx * cellSize, z0 = cz * cellSize;
    for (let k = 0; k < 40; k++) {
      const x = x0 + hash2(cx * 97 + k, cz, 21) * cellSize;
      const z = z0 + hash2(cx, cz * 97 + k, 22) * cellSize;
      const meadow = fbm(x * 0.05, z * 0.05, 2);
      if (hash2(cx + k, cz * 3 + k, 23) > smoothstep(0.25, 0.6, meadow) * 0.9 + 0.1) continue;
      if (houseProximity(x, z, 0.4).inside) continue;
      out.push({
        x, z, y: terrainHeight(x, z) - 0.02,
        yaw: hash2(cx - k, cz + 5 * k, 24) * Math.PI * 2,
        scale: 0.7 + hash2(cx + 3 * k, cz - k, 25) * 0.7,
        variant: Math.floor(hash2(cx * 5 + k, cz, 26) * variantCount),
      });
    }
    return out;
  };
}

// ---------------------------------------------------------------- meshes

// Terrain grid with spacing ~1.3 m at the centre growing to ~40 m at the rim.
export function buildTerrain(n = 420) {
  const E = WORLD.extent;
  const warp = (u) => E * (0.08 * u + 0.92 * u * u * u);
  const verts = n + 1;
  const pos = new Float32Array(verts * verts * 3);
  for (let j = 0; j <= n; j++) {
    const z = warp((j / n) * 2 - 1);
    for (let i = 0; i <= n; i++) {
      const x = warp((i / n) * 2 - 1);
      const o = (j * verts + i) * 3;
      pos[o] = x;
      pos[o + 1] = terrainHeight(x, z);
      pos[o + 2] = z;
    }
  }
  const idx = new Uint32Array(n * n * 6);
  let k = 0;
  for (let j = 0; j < n; j++) {
    for (let i = 0; i < n; i++) {
      const a = j * verts + i, b = a + 1, c = a + verts, d = c + 1;
      idx.set([a, c, b, b, c, d], k);
      k += 6;
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setIndex(new THREE.BufferAttribute(idx, 1));
  g.computeVertexNormals();
  g.computeBoundingSphere();
  return g;
}

// A ring of distant blue mountains beyond the terrain.
export function buildMountains() {
  const seg = 320;
  const rings = [
    { r: 3300, h: () => -40 },
    { r: 4300, h: (a) => 180 + fbm(a * 3.0, 1.3, 4) * 520 + Math.max(0, fbm(a * 9, 4.2, 2) - 0.5) * 300 },
    { r: 5600, h: (a) => 120 + fbm(a * 2.0, 7.9, 3) * 380 },
    { r: 7000, h: () => -40 },
  ];
  const pos = [];
  for (const ring of rings) {
    for (let s = 0; s <= seg; s++) {
      const a = (s / seg) * Math.PI * 2;
      pos.push(Math.cos(a) * ring.r, ring.h(a % (Math.PI * 2)), Math.sin(a) * ring.r);
    }
  }
  const idx = [];
  for (let r = 0; r < rings.length - 1; r++) {
    for (let s = 0; s < seg; s++) {
      const a = r * (seg + 1) + s, b = a + 1, c = a + seg + 1, d = c + 1;
      idx.push(a, b, c, b, d, c);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}
