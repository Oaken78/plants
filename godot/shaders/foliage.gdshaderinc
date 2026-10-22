// Foliage: every plant mesh from the core (stems, bark, leaves, flowers, grass, puffs). Ported from FOLIAGE_VERT and
// FOLIAGE_FRAG in web/js/materials.js. Included by foliage.gdshader (alpha-to-coverage leaf edges) and
// foliage_scissor.gdshader (hard alpha-tested edges), which differ only in render modes.
//
// Vertex streams (see extension/src/plant_generator.h):
//   CUSTOM0 hue, value, ao, leaf v    CUSTOM1 flex, phase, birth, material id    CUSTOM2 normal xyz, leaf u
// Per instance (plant_layer.cpp): MODEL_MATRIX = yaw and scale; INSTANCE_CUSTOM.x = LOD dither code, .y = LOD.

#include "res://shaders/common.gdshaderinc"

uniform vec3 mat_light[10];
uniform vec3 mat_base[10];
uniform vec3 mat_shade[10];
uniform vec2 shrink = vec2(1e7, 2e7);  // scale down to nothing between these camera distances (grass)

varying vec3 v_shade_col;
varying vec3 v_base_col;
varying vec3 v_light_col;
varying float v_ao;
varying float v_mat;
varying float v_birth;
varying float v_fade;
varying float v_lod;
varying vec3 v_leaf;  // leaf coordinates (x across, y base to tip), z > 0.5 for leaf vertices
varying vec3 v_world;

void vertex() {
	// world_vertex_coords: VERTEX is already in world space here.
	vec3 origin = MODEL_MATRIX[3].xyz;
	float scale = length(MODEL_MATRIX[0].xyz);
	float s = 1.0 - smoothstep(shrink.x, shrink.y, distance(origin, main_cam));
	VERTEX = origin + (VERTEX - origin) * s;
	float leaf_v = CUSTOM0.w * 255.0;
	v_leaf = leaf_v < 252.0 ? vec3(CUSTOM2.w * 0.6, leaf_v / 250.0 * 1.2 - 0.1, 1.0) : vec3(0.0);
	VERTEX += wind_offset(VERTEX, CUSTOM1.x * 0.25 * scale * s, CUSTOM1.y * 6.2831853);
	v_world = VERTEX;
	NORMAL = normalize(mat3(MODEL_MATRIX) * CUSTOM2.xyz);

	int mat = int(CUSTOM1.w * 255.0 + 0.5);
	vec3 light_col = mat_light[mat];
	vec3 base_col = mat_base[mat];
	vec3 shade_col = mat_shade[mat];
	float hue = CUSTOM0.x * 2.0 - 1.0;
	float value = CUSTOM0.y + 0.5;
	// Hue variation: positive leans toward the warm highlight, negative toward the cool shade.
	base_col = mix(base_col, light_col, max(hue, 0.0) * 0.45);
	base_col = mix(base_col, shade_col, max(-hue, 0.0) * 0.3);
	shade_col = mix(shade_col, base_col * vec3(0.7, 0.8, 0.95), max(hue, 0.0) * 0.25);
	// The toon mix is linear in the three tones, so scaling them scales the result like the web's "* value".
	v_shade_col = shade_col * value;
	v_base_col = base_col * value;
	v_light_col = light_col * value;
	v_ao = CUSTOM0.z;
	v_mat = float(mat);
	v_birth = CUSTOM1.z;
	v_fade = INSTANCE_CUSTOM.x;
	v_lod = INSTANCE_CUSTOM.y;
}

void fragment() {
	if (lod_dithered_out(v_fade, FRAGCOORD.xy) || v_birth > grow + 0.002) {
		discard;
	}
	int mat = int(v_mat + 0.5);
	// Leaves: cut the smooth outline from the hull, anti-aliased from the distance to it in pixels.
	float alpha = 1.0;
	if (v_leaf.z > 0.5 && is_leaf_mat(mat)) {
		float d = leaf_inside(mat, v_leaf.xy);
		alpha = clamp(d / max(fwidth(d), 1e-5) + 0.5, 0.0, 1.0);
		if (alpha < 0.02) {
			discard;
		}
	}
	// Godot flips the normal of back faces on double-sided materials; cloud normals must not flip.
	NORMAL = FRONT_FACING ? NORMAL : -NORMAL;
	vec3 albedo = v_base_col;
	if (mat == 8) {
		// Bark: soft vertical streaks so trunks and roots are not flat tubes up close.
		vec3 n = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
		float streak = fract(sin(dot(floor(vec2(atan(n.z, n.x) * 6.0, v_world.y * 1.3)), vec2(12.9898, 78.233))) * 43758.5453);
		albedo *= 0.9 + 0.12 * streak;
	}
	ALBEDO = albedo;
	ROUGHNESS = TAG_FOLIAGE;
#ifdef LEAF_SCISSOR
	ALPHA = alpha;
	ALPHA_SCISSOR_THRESHOLD = 0.5;
#else
	ALPHA = alpha;
#endif
}

void light() {
	// DIFFUSE_LIGHT is multiplied by ALBEDO afterwards, so divide the palette colour by the base tone.
	vec3 sun = LIGHT_COLOR / PI;
	float ndl = dot(NORMAL, LIGHT);
	bool woody = v_mat < 0.5 || abs(v_mat - 8.0) < 0.5;
	vec3 col = toon(v_shade_col, v_base_col, v_light_col, ndl, ATTENUATION, v_ao, woody ? 0.0 : 1.0, sun);
	// Back-lit leaves glow (sub-surface look of sunlit foliage).
	int mat = int(v_mat + 0.5);
	if ((mat >= 1 && mat <= 3) || mat == 7 || mat == 9) {
		float trans = pow(max(dot(-VIEW, LIGHT), 0.0), 5.0) * (1.0 - max(ndl, 0.0));
		col += v_light_col * trans * 0.35;
	}
	col = mix(col, lod_color(v_lod), debug_lod * 0.65);
	DIFFUSE_LIGHT += col / max(v_base_col, vec3(1e-4));
}
