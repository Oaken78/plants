// Streaming, cell-based LOD instancing.
//
// A layer covers the world with square cells. Cells are filled lazily by a deterministic generator
// callback, so the world can be as large as you like and nothing is stored until it is seen.
// Every frame, cells within range and inside the view frustum are walked; each instance picks a LOD
// per part from its distance and is appended to that (variant, part, LOD) bucket. Each bucket is one
// instanced draw call. Near a LOD switch an instance is drawn in both LODs with complementary dither
// codes (materials.js lodDither), which cross-fades without popping or transparency sorting.
//
// Godot mapping: one MultiMeshInstance3D per (cell, variant, part, LOD) with visibility_range_begin/end
// and visibility_range_fade_mode = SELF gives the same result.
import * as THREE from 'three';

const _box = new THREE.Box3();
const _v = new THREE.Vector3();

class Bucket {
  constructor(geometry, material, shadowMaterial, lod, layerName) {
    this.capacity = 0;
    this.count = 0;
    this.geometry = new THREE.InstancedBufferGeometry();
    for (const [name, attr] of Object.entries(geometry.attributes)) this.geometry.setAttribute(name, attr);
    this.geometry.setIndex(geometry.index);
    this.geometry.instanceCount = 0;
    this.triangles = geometry.index.count / 3;
    this.mesh = new THREE.Mesh(this.geometry, material);
    this.mesh.frustumCulled = false;
    this.mesh.name = `${layerName} lod${lod}`;
    this.mesh.userData.mainMaterial = material;
    this.mesh.userData.shadowMaterial = shadowMaterial;
    if (shadowMaterial) this.mesh.layers.enable(1);
    this.lod = lod;
    this.grow(256);
  }

  grow(capacity) {
    this.capacity = capacity;
    this.posYaw = new Float32Array(capacity * 4);
    this.scaleFade = new Float32Array(capacity * 2);
    const a = new THREE.InstancedBufferAttribute(this.posYaw, 4);
    const b = new THREE.InstancedBufferAttribute(this.scaleFade, 2);
    a.setUsage(THREE.DynamicDrawUsage);
    b.setUsage(THREE.DynamicDrawUsage);
    // Not disposing the geometry here: that would also free the base mesh buffers shared with other buckets.
    this.geometry.setAttribute('iPosYaw', a);
    this.geometry.setAttribute('iScaleFade', b);
  }

  push(inst, fade) {
    if (this.count >= this.capacity) {
      const oldPos = this.posYaw, oldSf = this.scaleFade, n = this.count;
      this.grow(this.capacity * 2);
      this.posYaw.set(oldPos.subarray(0, n * 4));
      this.scaleFade.set(oldSf.subarray(0, n * 2));
    }
    const i = this.count++;
    this.posYaw[i * 4] = inst.x;
    this.posYaw[i * 4 + 1] = inst.y;
    this.posYaw[i * 4 + 2] = inst.z;
    this.posYaw[i * 4 + 3] = inst.yaw;
    this.scaleFade[i * 2] = inst.scale;
    this.scaleFade[i * 2 + 1] = fade;
  }

  commit() {
    this.geometry.instanceCount = this.count;
    this.mesh.visible = this.count > 0;
    const a = this.geometry.getAttribute('iPosYaw');
    const b = this.geometry.getAttribute('iScaleFade');
    a.clearUpdateRanges();
    b.clearUpdateRanges();
    a.addUpdateRange(0, this.count * 4);
    b.addUpdateRange(0, this.count * 2);
    a.needsUpdate = true;
    b.needsUpdate = true;
  }
}

export class LodLayer {
  /**
   * @param {object} o
   * @param {string} o.name
   * @param {number} o.cellSize      metres
   * @param {number} o.maxDistance   instances beyond this are never drawn
   * @param {Array}  o.variants      [{ parts: [{ lods: [geometry|null...], ranges: [d0, d1, ...], material(lod), shadowMaterial(lod)|null }] }]
   * @param {(cx:number, cz:number) => Array<{x,y,z,yaw,scale,variant,radius?}>} o.generateCell
   * @param {number} [o.fade]        cross-fade band as a fraction of the switch distance
   * @param {[number,number]} [o.yRange]  vertical extent used for cell frustum tests
   */
  constructor(o) {
    const { variants, ...rest } = o;
    Object.assign(this, { fade: 0.12, yRange: [-50, 150] }, rest);
    this.group = new THREE.Group();
    this.group.name = this.name;
    this.cells = new Map();
    this.stats = { instances: 0, perLod: [0, 0, 0, 0], triangles: 0 };
    this.setVariants(variants);
  }

  setVariants(variants) {
    for (const v of this.variants ?? []) for (const p of v.parts) for (const b of p.buckets ?? []) {
      if (b) {
        this.group.remove(b.mesh);
        b.geometry.dispose();
      }
    }
    this.variants = variants;
    for (const v of variants) {
      for (const part of v.parts) {
        part.buckets = part.lods.map((geom, lod) => {
          if (!geom) return null;
          const b = new Bucket(geom, part.material(lod), part.shadowMaterial?.(lod) ?? null, lod, this.name);
          this.group.add(b.mesh);
          return b;
        });
      }
    }
  }

  // Replace one variant's geometry in place (used when regrowing the hero house).
  replaceVariant(index, variant) {
    const next = this.variants.slice();
    next[index] = variant;
    this.setVariants(next);
  }

  cell(cx, cz) {
    const key = `${cx},${cz}`;
    let c = this.cells.get(key);
    if (!c) {
      const instances = this.generateCell(cx, cz);
      let y0 = Infinity, y1 = -Infinity;
      for (const i of instances) {
        y0 = Math.min(y0, i.y);
        y1 = Math.max(y1, i.y);
      }
      c = { instances, y0: y0 + this.yRange[0], y1: y1 + this.yRange[1] };
      this.cells.set(key, c);
    }
    return c;
  }

  update(camera, frustum) {
    for (const v of this.variants) for (const p of v.parts) for (const b of p.buckets) if (b) b.count = 0;
    const cam = camera.position;
    const s = this.cellSize;
    const r = Math.ceil(this.maxDistance / s);
    const ccx = Math.floor(cam.x / s), ccz = Math.floor(cam.z / s);
    const maxD2 = this.maxDistance * this.maxDistance;
    let instances = 0;
    const perLod = [0, 0, 0, 0];
    for (let cz = ccz - r; cz <= ccz + r; cz++) {
      for (let cx = ccx - r; cx <= ccx + r; cx++) {
        const x0 = cx * s, z0 = cz * s;
        const dx = Math.max(x0 - cam.x, 0, cam.x - (x0 + s));
        const dz = Math.max(z0 - cam.z, 0, cam.z - (z0 + s));
        if (dx * dx + dz * dz > maxD2) continue;
        const c = this.cell(cx, cz);
        if (c.instances.length === 0) continue;
        _box.min.set(x0 - 12, c.y0, z0 - 12);
        _box.max.set(x0 + s + 12, c.y1, z0 + s + 12);
        if (!frustum.intersectsBox(_box)) continue;
        for (const inst of c.instances) {
          const d = _v.set(inst.x - cam.x, inst.y - cam.y, inst.z - cam.z).length();
          if (d > this.maxDistance) continue;
          instances++;
          const variant = this.variants[inst.variant];
          for (const part of variant.parts) {
            const ranges = part.ranges;
            let lod = 0;
            while (lod < ranges.length && d >= ranges[lod]) lod++;
            if (lod >= ranges.length) continue;
            perLod[lod]++;
            const band = ranges[lod] * this.fade;
            const t = (d - (ranges[lod] - band)) / band;
            const outgoing = part.buckets[lod];
            if (t <= 0) {
              outgoing?.push(inst, 1);
            } else {
              outgoing?.push(inst, 1 + t);
              const incoming = lod + 1 < ranges.length ? part.buckets[lod + 1] : null;
              incoming?.push(inst, t);
            }
          }
        }
      }
    }
    let triangles = 0;
    for (const v of this.variants) {
      for (const p of v.parts) {
        for (const b of p.buckets) {
          if (!b) continue;
          b.commit();
          triangles += b.count * b.triangles;
        }
      }
    }
    this.stats = { instances, perLod, triangles };
  }

  setLodDebug(colors) {
    for (const v of this.variants) for (const p of v.parts) for (const b of p.buckets) {
      if (b) b.mesh.userData.mainMaterial.uniforms.uLodColor.value.copy(colors[b.lod]);
    }
  }
}
