// Shared by every plant, building and ground shader: globals, wind, LOD dither, leaf shapes, noise and toon light.
// Ported from web/js/materials.js; the GLSL keeps the same formulas and constants.

global uniform float plant_time;  // seconds; set from main.gd (Godot's TIME wraps every hour)
global uniform vec4 wind;         // xy direction, z strength
global uniform float grow;        // growth animation: geometry born after this is hidden
global uniform vec3 main_cam;     // the view camera, also during shadow passes
global uniform float debug_lod;

// Wind: slow sway + gusts + fast flutter, scaled by each vertex's flex (metres).
vec3 wind_offset(vec3 wp, float flex, float phase) {
	vec2 dir = wind.xy;
	float t = plant_time;
	float gust = 0.55 + 0.45 * sin(t * 0.37 + dot(wp.xz, vec2(0.021, 0.017)));
	float sway = sin(t * 1.55 + phase + dot(wp.xz, dir) * 0.35) * 0.65 + sin(t * 2.9 + phase * 1.7 + wp.y * 0.8) * 0.35;
	float flutter = sin(t * 7.3 + phase * 5.0 + wp.x * 2.1 + wp.z * 1.3);
	vec3 d = vec3(dir.x, 0.0, dir.y) * (sway * gust + 0.35) + vec3(0.0, flutter * 0.3, 0.0) + vec3(dir.y, 0.0, -dir.x) * flutter * 0.25;
	return d * flex * wind.z;
}

// Per-pixel dither for LOD cross-fades (plant_layer.cpp writes the code into INSTANCE_CUSTOM.x). Code c <= 1 shows
// a fraction c of pixels (fading in), c > 1 shows the complementary 2 - c fraction (fading out). Two LODs with codes
// t and 1 + t never overlap.
float ign(vec2 p) {
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}
bool lod_dithered_out(float code, vec2 frag) {
	float th = ign(frag);
	return code <= 1.0 ? th >= code : th < code - 1.0;
}

vec3 lod_color(float lod) {
	if (lod < 0.5) return vec3(0.95, 0.25, 0.25);
	if (lod < 1.5) return vec3(0.95, 0.8, 0.2);
	if (lod < 2.5) return vec3(0.25, 0.85, 0.35);
	if (lod < 3.5) return vec3(0.25, 0.5, 0.95);
	return vec3(0.7, 0.35, 0.95);
}

// Leaf shapes, cut per pixel from leaf coordinates (x across, y base to tip). Must match leafHalfWidth() in
// core/src/mesher.cpp, which sizes the hull each leaf is drawn on. Shapes follow the material: 1 ivy, 2 and 7 bush
// and round-tree leaves, 9 oak, 4 and 5 petals.
float leaf_gauss(float y, float m, float s) {
	float t = (y - m) / s;
	return exp(-t * t);
}
float leaf_half_width(int mat, float y) {
	float yc = clamp(y, 0.0, 1.0);
	if (mat == 1) return 0.40 * leaf_gauss(y, 0.12, 0.15) + 0.48 * leaf_gauss(y, 0.42, 0.16) + 0.22 * leaf_gauss(y, 0.72, 0.16);
	if (mat == 9) {
		// Oak: rounded at base and tip, widest past the middle, four broad lobes a side with deep narrow sinuses.
		float env = 0.42 * sqrt(max(sin(3.14159265 * pow(yc, 1.25)), 0.0));
		float sinus = 0.5 - 0.5 * cos(6.2831853 * 4.0 * yc);
		return env * (1.0 - 0.5 * sinus * sinus * sinus);
	}
	if (mat == 4 || mat == 5) return 0.30 * pow(max(sin(3.14159265 * yc), 0.0), 0.55);
	return 0.30 * pow(max(sin(3.14159265 * yc), 0.0), 0.75) * (1.0 - 0.25 * yc);
}
// Distance inside the leaf outline, in leaf units (negative outside).
float leaf_inside(int mat, vec2 uv) {
	float x = abs(uv.x);
	float y = uv.y;
	float d = leaf_half_width(mat, y) - x;
	if (mat == 1) return min(d, y + 0.06 - 0.12 * leaf_gauss(x, 0.0, 0.1));  // heart-shaped base with a notch
	return min(d, min(y, 1.0 - y));
}
bool is_leaf_mat(int mat) {
	return mat == 1 || mat == 2 || mat == 4 || mat == 5 || mat == 7 || mat == 9;
}

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
	float a = 0.5;
	float s = 0.0;
	for (int i = 0; i < 4; i++) {
		s += a * vnoise(p);
		p *= 2.03;
		a *= 0.5;
	}
	return s;
}
float fbm3(vec2 p) {
	float a = 0.5;
	float s = 0.0;
	for (int i = 0; i < 3; i++) {
		s += a * vnoise(p);
		p *= 2.03;
		a *= 0.5;
	}
	return s / 0.875 * 0.9375;
}

// Toon light from three palette tones: two flat tones with a crisp terminator, plus a warmer highlight band on
// surfaces facing the sun. Shadows collapse to the shade tone (blue-tinted, never grey) and occlusion sinks the
// colour toward a deeper, bluer shade. Called from light(); the materials disable ambient light, so the shade tone
// comes only from here. attenuation is Godot's shadow term for the sun.
vec3 toon(vec3 shade_col, vec3 base_col, vec3 light_col, float ndl, float attenuation, float ao, float highlight, vec3 sun_col) {
	float shadow = attenuation;
	float lit = smoothstep(-0.03, 0.09, ndl) * shadow;
	vec3 lit_col = mix(base_col, light_col, smoothstep(0.52, 0.66, ndl) * shadow * highlight) * sun_col;
	vec3 col = mix(shade_col, lit_col, lit);
	return mix(shade_col * vec3(0.55, 0.62, 0.8), col, mix(0.35, 1.0, ao));
}

// Roughness doubles as a surface tag for the ink pass (shaders/ink.gdshader): foliage writes 1, hard surfaces 0.
// Nothing else reads roughness: every material here is specular_disabled and lit by its own light().
const float TAG_FOLIAGE = 1.0;
const float TAG_HARD = 0.0;
