// Full-screen composite: anime sky and clouds, aerial perspective, ink outlines and grading.
// It reads the scene colour and depth, so every material gets the same lines and haze for free.
import * as THREE from 'three';
import { PALETTE, hexToLinear } from './palette.js';

const lin = (hex) => new THREE.Vector3(...hexToLinear(hex));

const VERT = /* glsl */ `
varying vec2 vUv;
void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }
`;

const FRAG = /* glsl */ `
uniform sampler2D tColor;
uniform sampler2D tDepth;
uniform vec2 uResolution;
uniform float uNear, uFar;
uniform mat4 uInvProj;
uniform mat4 uCamWorld;
uniform vec3 uCamPos;
uniform vec3 uCamFwd;
uniform vec3 uSunDir;
uniform float uTime;
uniform vec3 uZenith, uHorizon, uHaze, uSun, uCloud, uCloudShade;
uniform float uFogDistance;
uniform float uOutlines;
uniform float uLineWidth;
varying vec2 vUv;

float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 4; i++) { s += a * vnoise(p); p = p * 2.02 + 17.1; a *= 0.5; } return s; }
float fbm2(vec2 p) { return 0.5 * vnoise(p) + 0.25 * vnoise(p * 2.02 + 17.1); }

float linearDepth(float d) {
  float z = d * 2.0 - 1.0;
  return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
vec3 viewDir(vec2 uv) {
  vec4 v = uInvProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
  return normalize((uCamWorld * vec4(v.xyz / v.w, 0.0)).xyz);
}

vec3 skyGradient(vec3 dir) {
  float h = dir.y;
  vec3 col = mix(uHorizon, uZenith, pow(clamp(h, 0.0, 1.0), 0.55));
  col = mix(col, uHaze, clamp(-h * 5.0, 0.0, 1.0));
  float sd = max(dot(dir, uSunDir), 0.0);
  col += uSun * (pow(sd, 48.0) * 0.5 + pow(sd, 6.0) * 0.1);
  return col;
}

vec3 sky(vec3 dir) {
  vec3 col = skyGradient(dir);
  float sd = dot(dir, uSunDir);
  col = mix(col, vec3(1.0, 0.99, 0.95) * 1.4, smoothstep(0.99955, 0.9997, sd));
  if (dir.y > 0.0) {
    // Cumulus on a virtual cloud deck: crisp edges, warm tops, blue undersides.
    vec2 p = dir.xz / (dir.y + 0.12) * 1.3 + vec2(uTime * 0.004, uTime * 0.0015);
    float n = fbm(p * 0.55) + 0.25 * fbm2(p * 0.13 + 5.0) - 0.1;
    float cover = smoothstep(0.5, 0.53, n) * smoothstep(0.0, 0.18, dir.y);
    float toward = fbm((p + uSunDir.xz * 0.08) * 0.55) + 0.25 * fbm2((p + uSunDir.xz * 0.08) * 0.13 + 5.0) - 0.1;
    float lit = smoothstep(-0.015, 0.02, n - toward + 0.006);
    vec3 cloud = mix(uCloudShade, uCloud, lit);
    cloud = mix(cloud, uHorizon, smoothstep(0.25, 0.0, dir.y) * 0.55);
    col = mix(col, cloud, cover);
  }
  return col;
}

vec3 linearToSrgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main() {
  vec2 px = uLineWidth / uResolution;
  float raw = texture2D(tDepth, vUv).r;
  vec3 dir = viewDir(vUv);
  vec3 col;
  if (raw >= 0.999999) {
    col = sky(dir);
  } else {
    col = texture2D(tColor, vUv).rgb;
    float z = linearDepth(raw);
    float dist = z / max(dot(dir, uCamFwd), 0.05);

    // Ink lines from depth: silhouettes (big depth jumps) and creases (1/z is affine on planes,
    // so its Laplacian is ~0 on flat faces and spikes on convex edges).
    if (uOutlines > 0.5) {
      float zl = linearDepth(texture2D(tDepth, vUv - vec2(px.x, 0.0)).r);
      float zr = linearDepth(texture2D(tDepth, vUv + vec2(px.x, 0.0)).r);
      float zd = linearDepth(texture2D(tDepth, vUv - vec2(0.0, px.y)).r);
      float zu = linearDepth(texture2D(tDepth, vUv + vec2(0.0, px.y)).r);
      float far = max(max(zl, zr), max(zd, zu));
      float sil = smoothstep(0.035, 0.09, (far - z) / z - 0.012 * z / 100.0);
      float iz = 1.0 / z;
      float lap = (1.0 / zl + 1.0 / zr + 1.0 / zd + 1.0 / zu - 4.0 * iz) / iz;
      float crease = smoothstep(0.012, 0.035, -lap);
      float line = max(sil, crease * 0.8) * mix(1.0, 0.25, smoothstep(60.0, 400.0, dist)) * (1.0 - smoothstep(600.0, 1600.0, dist));
      col = mix(col, col * vec3(0.3, 0.34, 0.45), line * 0.82);
    }

    // Aerial perspective: distance fades toward the horizon colour, a little less at altitude.
    vec3 fogDir = normalize(vec3(dir.x, max(dir.y, 0.0) * 0.35, dir.z));
    vec3 fogCol = skyGradient(fogDir);
    float height = max(uCamPos.y + dir.y * dist, 0.0);
    float fog = 1.0 - exp(-pow(dist / uFogDistance, 1.2) * exp(-height * 0.0025));
    col = mix(col, fogCol, clamp(fog, 0.0, 1.0));
  }
  // Light grading: a touch more saturation and a soft vignette.
  float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
  col = mix(vec3(l), col, 1.08);
  vec2 q = vUv - 0.5;
  col *= 1.0 - dot(q, q) * 0.28;
  gl_FragColor = vec4(linearToSrgb(col), 1.0);
}
`;

export class PostPass {
  constructor() {
    this.material = new THREE.ShaderMaterial({
      vertexShader: VERT,
      fragmentShader: FRAG,
      depthTest: false,
      depthWrite: false,
      uniforms: {
        tColor: { value: null },
        tDepth: { value: null },
        uResolution: { value: new THREE.Vector2(1, 1) },
        uNear: { value: 0.1 },
        uFar: { value: 1000 },
        uInvProj: { value: new THREE.Matrix4() },
        uCamWorld: { value: new THREE.Matrix4() },
        uCamPos: { value: new THREE.Vector3() },
        uCamFwd: { value: new THREE.Vector3() },
        uSunDir: { value: new THREE.Vector3(0, 1, 0) },
        uTime: { value: 0 },
        uZenith: { value: lin(PALETTE.skyZenith) },
        uHorizon: { value: lin(PALETTE.skyHorizon) },
        uHaze: { value: lin(PALETTE.haze) },
        uSun: { value: lin(PALETTE.sun) },
        uCloud: { value: lin(PALETTE.cloud) },
        uCloudShade: { value: lin(PALETTE.cloudShade) },
        uFogDistance: { value: 1700 },
        uOutlines: { value: 1 },
        uLineWidth: { value: 1 },
      },
    });
    this.scene = new THREE.Scene();
    this.camera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    const quad = new THREE.Mesh(new THREE.PlaneGeometry(2, 2), this.material);
    quad.frustumCulled = false;
    this.scene.add(quad);
  }

  render(renderer, target, camera, sunDir, time) {
    const u = this.material.uniforms;
    u.tColor.value = target.texture;
    u.tDepth.value = target.depthTexture;
    u.uResolution.value.set(target.width, target.height);
    u.uLineWidth.value = Math.max(1, target.height / 1100);
    u.uNear.value = camera.near;
    u.uFar.value = camera.far;
    u.uInvProj.value.copy(camera.projectionMatrixInverse);
    u.uCamWorld.value.copy(camera.matrixWorld);
    u.uCamPos.value.copy(camera.position);
    camera.getWorldDirection(u.uCamFwd.value);
    u.uSunDir.value.copy(sunDir);
    u.uTime.value = time;
    renderer.setRenderTarget(null);
    renderer.render(this.scene, this.camera);
  }
}
