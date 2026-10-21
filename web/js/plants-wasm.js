// Thin wrapper over the WebAssembly build of the C++ core (core/include/plants/api.h).
// Copies each LOD's vertex streams out of the wasm heap into three.js geometries.
import * as THREE from 'three';
import createPlantsModule from '../build/plants.js';

export const TAG = { solid: 0, ground: 1, noGrow: 2 };

export class PlantGenerator {
  static async create() {
    const module = await createPlantsModule();
    return new PlantGenerator(module);
  }

  constructor(module) {
    this.m = module;
    this.ctx = module._plants_create();
    this.lodCount = module._plants_lod_count();
    this.params = [];
    for (let i = 0; i < module._plants_param_count(); i++) {
      this.params.push({
        id: i,
        name: module.UTF8ToString(module._plants_param_name(i)),
        default: module._plants_param_default(i),
        min: module._plants_param_min(i),
        max: module._plants_param_max(i),
      });
    }
    this.paramIds = Object.fromEntries(this.params.map((p) => [p.name, p.id]));
  }

  setParam(name, value) {
    this.m._plants_set_param(this.ctx, this.paramIds[name], value);
  }
  getParam(name) {
    return this.m._plants_get_param(this.ctx, this.paramIds[name]);
  }
  resetParams() {
    this.m._plants_reset_params(this.ctx);
  }
  // Apply a { name: value } map on top of the defaults.
  withParams(overrides) {
    this.resetParams();
    for (const [k, v] of Object.entries(overrides ?? {})) this.setParam(k, v);
  }

  // positions: Float32Array xyz, indices: Uint32Array, tags: Uint8Array (one per triangle)
  setHost(positions, indices, tags) {
    const m = this.m;
    const pPos = m._malloc(positions.byteLength);
    const pIdx = m._malloc(indices.byteLength);
    const pTag = m._malloc(tags.byteLength);
    m.HEAPF32.set(positions, pPos >> 2);
    m.HEAPU32.set(indices, pIdx >> 2);
    m.HEAPU8.set(tags, pTag);
    m._plants_set_host(this.ctx, pPos, positions.length / 3, pIdx, indices.length, pTag);
    m._free(pPos);
    m._free(pIdx);
    m._free(pTag);
  }

  growOnHost(seed) {
    this.m._plants_grow_on_host(this.ctx, seed >>> 0);
    return this.collect();
  }
  makeBush(seed, style, scale = 1) {
    this.m._plants_make_bush(this.ctx, seed >>> 0, style, scale);
    return this.collect();
  }
  makeTree(seed, size) {
    this.m._plants_make_tree(this.ctx, seed >>> 0, size);
    return this.collect();
  }
  makeGrass(seed) {
    this.m._plants_make_grass(this.ctx, seed >>> 0);
    return this.collect();
  }

  stats() {
    const s = (i) => this.m._plants_stat(this.ctx, i);
    return { nodes: s(0), stems: s(1), leaves: s(2), flowers: s(3), clumps: s(4), blades: s(5), climbers: s(6), bushes: s(7), trees: s(8) };
  }

  // One THREE.BufferGeometry per LOD (empty LODs give null).
  collect() {
    const m = this.m;
    const lods = [];
    for (let lod = 0; lod < this.lodCount; lod++) {
      const vc = m._plants_mesh_vertex_count(this.ctx, lod);
      const ic = m._plants_mesh_index_count(this.ctx, lod);
      if (vc === 0 || ic === 0) {
        lods.push(null);
        continue;
      }
      // Views must be taken after generation: memory growth replaces the heap buffers.
      const pos = m.HEAPF32.slice(m._plants_mesh_positions(this.ctx, lod) >> 2, (m._plants_mesh_positions(this.ctx, lod) >> 2) + vc * 3);
      const nrmPtr = m._plants_mesh_normals(this.ctx, lod);
      const nrm = m.HEAP8.slice(nrmPtr, nrmPtr + vc * 4);
      const shadePtr = m._plants_mesh_shade(this.ctx, lod);
      const shade = m.HEAPU8.slice(shadePtr, shadePtr + vc * 4);
      const auxPtr = m._plants_mesh_aux(this.ctx, lod);
      const aux = m.HEAPU8.slice(auxPtr, auxPtr + vc * 4);
      const idxPtr = m._plants_mesh_indices(this.ctx, lod) >> 2;
      const idx = m.HEAPU32.slice(idxPtr, idxPtr + ic);
      const bPtr = m._plants_mesh_bounds(this.ctx, lod) >> 2;
      const b = m.HEAPF32.slice(bPtr, bPtr + 6);

      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
      g.setAttribute('aNormal', new THREE.BufferAttribute(nrm, 4, true));
      g.setAttribute('aShade', new THREE.BufferAttribute(shade, 4, true));
      g.setAttribute('aAux', new THREE.BufferAttribute(aux, 4, true));
      g.setIndex(new THREE.BufferAttribute(idx, 1));
      g.boundingBox = new THREE.Box3(new THREE.Vector3(b[0], b[1], b[2]), new THREE.Vector3(b[3], b[4], b[5]));
      g.boundingSphere = g.boundingBox.getBoundingSphere(new THREE.Sphere());
      g.userData.triangles = ic / 3;
      lods.push(g);
    }
    return lods;
  }
}
