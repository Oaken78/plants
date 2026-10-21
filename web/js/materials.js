// Anime toon materials.
//
// Lighting model (shared by every surface, see LIGHTING below):
//   - two flat tones with a crisp terminator: palette "shade" colour and "base" colour
//   - a third, warmer highlight band on surfaces facing the sun (foliage tops)
//   - shadows from the sun shadow map collapse to the shade colour (blue-tinted, never grey)
//   - ambient occlusion from the generator darkens clump interiors
// Foliage uses the generator's smooth "cloud" normals, so bushes and ivy shade as soft masses.
//
// Every instanced mesh carries two per-instance attributes:
//   iPosYaw    xyz position, w rotation about Y
//   iScaleFade x uniform scale, y LOD dither code (see lodDither below)
import * as THREE from 'three';
import { PALETTE, hexToLinear } from './palette.js';

const lin = (hex) => new THREE.Vector3(...hexToLinear(hex));

// Uniforms shared by all materials; main.js updates them every frame.
export const shared = {
  uTime: { value: 0 },
  uSunDir: { value: new THREE.Vector3(0.4, 0.75, 0.5).normalize() },
  uSunLight: { value: lin(PALETTE.sunLight) },
  uShadowMul: { value: new THREE.Vector3(0.4, 0.45, 0.63) },
  uShadowMap: { value: null },
  uShadowMatrix: { value: new THREE.Matrix4() },
  uShadowTexel: { value: 1 / 4096 },
  uShadowBias: { value: 0.0002 },
  uShadowsOn: { value: 1 },
  uCameraPos: { value: new THREE.Vector3() },
  uWind: { value: new THREE.Vector4(0.8, 0.6, 1.0, 0) },
  uGrow: { value: 1 },
  uLodDebug: { value: 0 },
  uLeafLines: { value: 1 },
};

// Foliage palette indexed by the core's material id (core/include/plants/plant_set.h: Material).
//   0 stem, 1 ivy leaf, 2 bush leaf, 3 grass, 4 white flower, 5 yellow flower, 6 flower centre, 7 tree leaf, 8 bark
const FOLIAGE_LIGHT = ['#8e7a58', PALETTE.ivyLight, PALETTE.bushLight, PALETTE.grassLight, '#fffdf6', '#ffe27e', '#f4b450',
  PALETTE.treeLight, PALETTE.barkLight];
const FOLIAGE_BASE = [PALETTE.stem, PALETTE.ivy, PALETTE.bush, PALETTE.grass, PALETTE.flowerWhite, PALETTE.flowerYellow,
  PALETTE.flowerCenter, PALETTE.tree, PALETTE.bark];
const FOLIAGE_SHADE = ['#46404e', PALETTE.ivyShadow, PALETTE.bushShadow, PALETTE.grassShadow, '#a9b6d6', '#b49a6c', '#a0674a',
  PALETTE.treeShadow, PALETTE.barkShadow];

const foliageUniforms = {
  uMatLight: { value: FOLIAGE_LIGHT.map(lin) },
  uMatBase: { value: FOLIAGE_BASE.map(lin) },
  uMatShade: { value: FOLIAGE_SHADE.map(lin) },
};

export function setFoliageColor(matId, which, hex) {
  const table = { light: foliageUniforms.uMatLight, base: foliageUniforms.uMatBase, shade: foliageUniforms.uMatShade }[which];
  table.value[matId].copy(lin(hex));
}

const COMMON = /* glsl */ `
uniform float uTime;
uniform vec3 uSunDir;
uniform vec3 uSunLight;
uniform vec3 uShadowMul;
uniform sampler2D uShadowMap;
uniform mat4 uShadowMatrix;
uniform float uShadowTexel;
uniform float uShadowBias;
uniform float uShadowsOn;
uniform vec3 uCameraPos;
uniform vec4 uWind;
uniform float uGrow;
uniform float uLodDebug;
uniform float uLeafLines;
uniform vec3 uLodColor;
`;

const INSTANCING = /* glsl */ `
attribute vec4 iPosYaw;
attribute vec2 iScaleFade;
vec3 rotY(vec3 v, float a) { float c = cos(a), s = sin(a); return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z); }
`;

// Wind: slow sway + gusts + fast flutter, scaled by each vertex's flex (metres).
const WIND = /* glsl */ `
vec3 windOffset(vec3 wp, float flex, float phase) {
  vec2 dir = uWind.xy;
  float t = uTime;
  float gust = 0.55 + 0.45 * sin(t * 0.37 + dot(wp.xz, vec2(0.021, 0.017)));
  float sway = sin(t * 1.55 + phase + dot(wp.xz, dir) * 0.35) * 0.65 + sin(t * 2.9 + phase * 1.7 + wp.y * 0.8) * 0.35;
  float flutter = sin(t * 7.3 + phase * 5.0 + wp.x * 2.1 + wp.z * 1.3);
  vec3 d = vec3(dir.x, 0.0, dir.y) * (sway * gust + 0.35) + vec3(0.0, flutter * 0.3, 0.0) + vec3(dir.y, 0.0, -dir.x) * flutter * 0.25;
  return d * flex * uWind.z;
}
`;

// Per-pixel dither for LOD cross-fades. Code c: c <= 1 shows a fraction c of pixels (fading in),
// c > 1 shows the complementary 2 - c fraction (fading out). Two LODs with codes t and 1 + t never overlap.
const DITHER = /* glsl */ `
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
void lodDither(float code) {
  float th = ign(gl_FragCoord.xy);
  if (code <= 1.0) { if (th >= code) discard; }
  else if (th < code - 1.0) discard;
}
`;

const SHADOW_SAMPLE = /* glsl */ `
float sunShadow(vec3 wp, vec3 n) {
  if (uShadowsOn < 0.5) return 1.0;
  vec4 sc = uShadowMatrix * vec4(wp + n * 0.04, 1.0);
  vec3 p = sc.xyz / sc.w * 0.5 + 0.5;
  if (p.x < 0.0 || p.y < 0.0 || p.x > 1.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
  float z = p.z - uShadowBias;
  float s = 0.0;
  vec2 o = vec2(uShadowTexel * 1.1, 0.0);
  s += step(z, texture2D(uShadowMap, p.xy).r);
  s += step(z, texture2D(uShadowMap, p.xy + o.xy).r);
  s += step(z, texture2D(uShadowMap, p.xy - o.xy).r);
  s += step(z, texture2D(uShadowMap, p.xy + o.yx).r);
  s += step(z, texture2D(uShadowMap, p.xy - o.yx).r);
  s /= 5.0;
  // Fade out toward the border of the shadow map instead of cutting off.
  float edge = min(min(p.x, 1.0 - p.x), min(p.y, 1.0 - p.y));
  return mix(1.0, s, smoothstep(0.0, 0.06, edge));
}
`;

// Toon light: returns the final colour from three palette tones.
const LIGHTING = /* glsl */ `
vec3 toonLight(vec3 shadeCol, vec3 baseCol, vec3 lightCol, vec3 n, vec3 wp, float ao, float highlight) {
  float ndl = dot(n, uSunDir);
  float shadow = sunShadow(wp, n);
  float lit = smoothstep(-0.03, 0.09, ndl) * shadow;
  vec3 litCol = mix(baseCol, lightCol, smoothstep(0.52, 0.66, ndl) * shadow * highlight) * uSunLight;
  vec3 col = mix(shadeCol, litCol, lit);
  // Occlusion sinks the colour toward a deeper, bluer shade.
  col = mix(shadeCol * vec3(0.55, 0.62, 0.8), col, mix(0.35, 1.0, ao));
  return col;
}
`;

// ---------------------------------------------------------------- foliage (plants from the core)

const FOLIAGE_VERT = /* glsl */ `
${COMMON}
${INSTANCING}
${WIND}
attribute vec4 aNormal;
attribute vec4 aShade;
attribute vec4 aAux;
uniform vec2 uShrink;  // scale down to nothing between these camera distances
varying vec3 vWorldPos;
varying vec3 vNormal;
varying vec4 vShade;
varying float vMat;
varying float vBirth;
varying float vFade;
void main() {
  float s = iScaleFade.x * (1.0 - smoothstep(uShrink.x, uShrink.y, distance(iPosYaw.xyz, uCameraPos)));
  vec3 wp = rotY(position, iPosYaw.w) * s + iPosYaw.xyz;
  wp += windOffset(wp, aAux.x * 0.25 * s, aAux.y * 6.2831853);
  vWorldPos = wp;
  vNormal = rotY(aNormal.xyz, iPosYaw.w);
  vShade = aShade;
  vMat = aAux.w * 255.0;
  vBirth = aAux.z;
  vFade = iScaleFade.y;
  gl_Position = projectionMatrix * viewMatrix * vec4(wp, 1.0);
}
`;

const FOLIAGE_FRAG = /* glsl */ `
${COMMON}
${DITHER}
${SHADOW_SAMPLE}
${LIGHTING}
uniform vec3 uMatLight[9];
uniform vec3 uMatBase[9];
uniform vec3 uMatShade[9];
varying vec3 vWorldPos;
varying vec3 vNormal;
varying vec4 vShade;
varying float vMat;
varying float vBirth;
varying float vFade;
void main() {
  lodDither(vFade);
  if (vBirth > uGrow + 0.002) discard;
  int mat = int(vMat + 0.5);
  vec3 lightCol = uMatLight[mat], baseCol = uMatBase[mat], shadeCol = uMatShade[mat];
  float hue = vShade.x * 2.0 - 1.0;
  float value = vShade.y + 0.5;
  float ao = vShade.z;
  // Hue variation: positive leans toward the warm highlight, negative toward the cool shade.
  baseCol = mix(baseCol, lightCol, max(hue, 0.0) * 0.45);
  baseCol = mix(baseCol, shadeCol, max(-hue, 0.0) * 0.3);
  shadeCol = mix(shadeCol, baseCol * vec3(0.7, 0.8, 0.95), max(hue, 0.0) * 0.25);
  vec3 n = normalize(vNormal);
  bool woody = mat == 0 || mat == 8;
  vec3 col = toonLight(shadeCol, baseCol, lightCol, n, vWorldPos, ao, woody ? 0.0 : 1.0) * value;
  if (mat == 8) {
    // Bark: soft vertical streaks so trunks and roots are not flat tubes up close.
    float streak = fract(sin(dot(floor(vec2(atan(n.z, n.x) * 6.0, vWorldPos.y * 1.3)), vec2(12.9898, 78.233))) * 43758.5453);
    col *= 0.9 + 0.12 * streak;
  }

  // Back-lit leaves glow (sub-surface look of sunlit foliage).
  vec3 v = normalize(uCameraPos - vWorldPos);
  if ((mat >= 1 && mat <= 3) || mat == 7) {
    float trans = pow(max(dot(-v, uSunDir), 0.0), 5.0) * (1.0 - max(dot(n, uSunDir), 0.0));
    col += lightCol * trans * 0.35;
  }

  // Ink: leaf outline (edge -> 1) and a lighter midrib (edge -> 0), only up close.
  float e = vShade.w;
  float fw = fwidth(e);
  float dist = distance(uCameraPos, vWorldPos);
  float near = (1.0 - smoothstep(7.0, 16.0, dist)) * (1.0 - smoothstep(0.12, 0.3, fw)) * uLeafLines;
  if (abs(e - 0.5) > 0.01 && near > 0.0) {
    float outline = smoothstep(1.0 - 2.2 * fw, 1.0 - 0.7 * fw, e);
    float rib = 1.0 - smoothstep(0.5 * fw, 1.6 * fw, e);
    col = mix(col, col * vec3(0.38, 0.46, 0.52), outline * near * 0.9);
    col = mix(col, mix(col, lightCol, 0.6), rib * near * 0.45);
  }
  col = mix(col, uLodColor, uLodDebug * 0.65);
  gl_FragColor = vec4(col, 1.0);
}
`;

// ---------------------------------------------------------------- building (house model)

const BUILDING_VERT = /* glsl */ `
${COMMON}
${INSTANCING}
attribute vec3 aColor;
attribute float aMat;
varying vec3 vWorldPos;
varying vec3 vLocalPos;
varying vec3 vNormal;
varying vec3 vColor;
varying float vMat;
varying float vFade;
void main() {
  float s = iScaleFade.x;
  vec3 wp = rotY(position, iPosYaw.w) * s + iPosYaw.xyz;
  vWorldPos = wp;
  vLocalPos = position;
  vNormal = rotY(normal, iPosYaw.w);
  vColor = aColor;
  vMat = aMat;
  vFade = iScaleFade.y;
  gl_Position = projectionMatrix * viewMatrix * vec4(wp, 1.0);
}
`;

const NOISE = /* glsl */ `
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 4; i++) { s += a * vnoise(p); p *= 2.03; a *= 0.5; } return s; }
float fbm3(vec2 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 3; i++) { s += a * vnoise(p); p *= 2.03; a *= 0.5; } return s / 0.875 * 0.9375; }
`;

const BUILDING_FRAG = /* glsl */ `
${COMMON}
${DITHER}
${SHADOW_SAMPLE}
${LIGHTING}
${NOISE}
uniform vec3 uSkyColor;
varying vec3 vWorldPos;
varying vec3 vLocalPos;
varying vec3 vNormal;
varying vec3 vColor;
varying float vMat;
varying float vFade;
void main() {
  lodDither(vFade);
  vec3 n = normalize(vNormal);
  vec3 albedo = vColor;
  int mat = int(vMat + 0.5);
  // Weathering, painted in world space so every house differs: mottling, rain streaks, damp base.
  vec2 face = abs(n.x) > 0.5 ? vLocalPos.zy : vLocalPos.xy;
  float mottle = fbm(face * 0.9 + vWorldPos.xz * 0.05);
  float streak = smoothstep(0.55, 0.85, fbm(vec2(face.x * 3.1, face.y * 0.22) + 3.7)) * (abs(n.y) < 0.5 ? 1.0 : 0.3);
  float damp = 1.0 - smoothstep(0.0, 1.1, vLocalPos.y);
  if (mat <= 2) {
    albedo *= 0.9 + 0.18 * mottle;
    albedo = mix(albedo, albedo * vec3(0.72, 0.74, 0.7), streak * 0.65);
    albedo = mix(albedo, albedo * vec3(0.62, 0.7, 0.58), damp * 0.6);
  } else if (mat == 3) {
    albedo = mix(albedo, albedo * vec3(0.62, 0.48, 0.42), smoothstep(0.4, 0.8, fbm(face * 4.0)));
  }
  vec3 shade = albedo * uShadowMul;
  vec3 col = toonLight(shade, albedo, albedo * 1.08, n, vWorldPos, 1.0, 0.0);
  if (mat == 5) {
    // Dark glass with a sky reflection that brightens at grazing angles, plus a diagonal glint.
    vec3 v = normalize(uCameraPos - vWorldPos);
    float fres = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    col = mix(albedo, uSkyColor * 0.75, 0.15 + fres * 0.55);
    float glint = smoothstep(0.92, 0.97, fract((vLocalPos.x + vLocalPos.z) * 0.9 + vLocalPos.y * 0.6));
    col += glint * 0.08;
  }
  col = mix(col, uLodColor, uLodDebug * 0.65);
  gl_FragColor = vec4(col, 1.0);
}
`;

// ---------------------------------------------------------------- terrain

const TERRAIN_VERT = /* glsl */ `
${COMMON}
varying vec3 vWorldPos;
varying vec3 vNormal;
void main() {
  vWorldPos = position;
  vNormal = normal;
  gl_Position = projectionMatrix * viewMatrix * vec4(position, 1.0);
}
`;

const TERRAIN_FRAG = /* glsl */ `
${COMMON}
${SHADOW_SAMPLE}
${LIGHTING}
${NOISE}
uniform vec3 uGrassLight;
uniform vec3 uGrass;
uniform vec3 uGrassShade;
uniform vec3 uDirt;
varying vec3 vWorldPos;
varying vec3 vNormal;
void main() {
  vec3 n = normalize(vNormal);
  vec2 p = vWorldPos.xz;
  // Painterly patches: large meadows, smaller clumps and a few worn dirt spots.
  float big = fbm3(p * 0.012);
  float mid = vnoise(p * 0.09 + 11.0) * 0.6 + vnoise(p * 0.21) * 0.4;
  float dirt = smoothstep(0.71, 0.75, fbm3(p * 0.07 + 4.0)) * step(0.85, n.y);
  float lush = smoothstep(0.35, 0.6, vnoise(p * 0.004 + 3.0));
  vec3 base = mix(uGrass, mix(uGrass, uGrassLight, 0.55), smoothstep(0.42, 0.62, big));
  base = mix(base, uGrass * vec3(0.78, 0.9, 0.92), lush * 0.45);
  base = mix(base, base * vec3(0.86, 0.92, 0.9), smoothstep(0.5, 0.7, mid) * 0.8);
  base = mix(base, uDirt, dirt * 0.85);
  vec3 lightCol = mix(uGrassLight, uDirt * 1.1, dirt);
  vec3 shade = mix(uGrassShade, uDirt * uShadowMul, dirt);
  vec3 col = toonLight(shade, base, lightCol, n, vWorldPos, 1.0, 0.35);
  gl_FragColor = vec4(col, 1.0);
}
`;

// Flat toon colour for far-away scenery (mountains).
const SCENERY_FRAG = /* glsl */ `
${COMMON}
uniform vec3 uColor;
varying vec3 vWorldPos;
varying vec3 vNormal;
void main() {
  vec3 n = normalize(vNormal);
  float lit = smoothstep(-0.05, 0.15, dot(n, uSunDir));
  gl_FragColor = vec4(mix(uColor * vec3(0.62, 0.7, 0.9), uColor, lit), 1.0);
}
`;

// ---------------------------------------------------------------- shadow pass

const SHADOW_FRAG_FOLIAGE = /* glsl */ `
${COMMON}
${DITHER}
varying float vBirth;
varying float vFade;
void main() {
  lodDither(vFade);
  if (vBirth > uGrow + 0.002) discard;
  gl_FragColor = vec4(1.0);
}
`;

const SHADOW_FRAG_SIMPLE = /* glsl */ `
${COMMON}
${DITHER}
varying float vFade;
void main() {
  lodDither(vFade);
  gl_FragColor = vec4(1.0);
}
`;

const SHADOW_FRAG_TERRAIN = /* glsl */ `void main() { gl_FragColor = vec4(1.0); }`;

function material(vertexShader, fragmentShader, uniforms, opts = {}) {
  return new THREE.ShaderMaterial({
    vertexShader,
    fragmentShader,
    uniforms: { ...shared, uLodColor: { value: new THREE.Vector3(1, 1, 1) }, ...uniforms },
    side: opts.side ?? THREE.FrontSide,
  });
}

// One material instance per LOD bucket so LOD debug colours can differ; the GPU program is shared.
const NO_SHRINK = [1e7, 2e7];
export function foliageMaterial(shrink = NO_SHRINK) {
  return material(FOLIAGE_VERT, FOLIAGE_FRAG, { ...foliageUniforms, uShrink: { value: new THREE.Vector2(...shrink) } },
    { side: THREE.DoubleSide });
}
export function foliageShadowMaterial(shrink = NO_SHRINK) {
  return material(FOLIAGE_VERT, SHADOW_FRAG_FOLIAGE, { uShrink: { value: new THREE.Vector2(...shrink) } }, { side: THREE.DoubleSide });
}
export function buildingMaterial(skyColor) {
  return material(BUILDING_VERT, BUILDING_FRAG, { uSkyColor: { value: skyColor } });
}
export function buildingShadowMaterial() {
  return material(BUILDING_VERT, SHADOW_FRAG_SIMPLE, {}, { side: THREE.DoubleSide });
}
export function terrainMaterial() {
  return material(TERRAIN_VERT, TERRAIN_FRAG, {
    uGrassLight: { value: lin(PALETTE.grassLight) },
    uGrass: { value: lin(PALETTE.grass) },
    uGrassShade: { value: lin(PALETTE.grassShadow) },
    uDirt: { value: lin(PALETTE.dirt) },
  });
}
export function terrainShadowMaterial() {
  return material(TERRAIN_VERT, SHADOW_FRAG_TERRAIN, {});
}
export function sceneryMaterial(hex) {
  return material(TERRAIN_VERT, SCENERY_FRAG, { uColor: { value: lin(hex) } });
}

export const LOD_COLORS = [
  new THREE.Vector3(0.95, 0.25, 0.25),
  new THREE.Vector3(0.95, 0.8, 0.2),
  new THREE.Vector3(0.25, 0.85, 0.35),
  new THREE.Vector3(0.25, 0.5, 0.95),
];
