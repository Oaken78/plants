// Rolling meadow terrain: painterly grass patches and worn dirt spots. Ported from TERRAIN_FRAG in
// web/js/materials.js.
shader_type spatial;
render_mode ambient_light_disabled, specular_disabled;

#include "res://shaders/common.gdshaderinc"

uniform vec3 grass_light : source_color;
uniform vec3 grass : source_color;
uniform vec3 grass_shade : source_color;
uniform vec3 dirt : source_color;
const vec3 SHADOW_MUL = vec3(0.4, 0.45, 0.63);

varying vec3 v_world;
varying vec3 v_shade;
varying vec3 v_light;

void vertex() {
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 n = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec2 p = v_world.xz;
	float big = fbm3(p * 0.012);
	float mid = vnoise(p * 0.09 + 11.0) * 0.6 + vnoise(p * 0.21) * 0.4;
	float bare = smoothstep(0.71, 0.75, fbm3(p * 0.07 + 4.0)) * step(0.85, n.y);
	float lush = smoothstep(0.35, 0.6, vnoise(p * 0.004 + 3.0));
	vec3 base = mix(grass, mix(grass, grass_light, 0.55), smoothstep(0.42, 0.62, big));
	base = mix(base, grass * vec3(0.78, 0.9, 0.92), lush * 0.45);
	base = mix(base, base * vec3(0.86, 0.92, 0.9), smoothstep(0.5, 0.7, mid) * 0.8);
	base = mix(base, dirt, bare * 0.85);
	ALBEDO = base;
	ROUGHNESS = TAG_HARD;
	// Fragment-to-light varyings: the shade and highlight tones depend on the dirt mask.
	v_light = mix(grass_light, dirt * 1.1, bare);
	v_shade = mix(grass_shade, dirt * SHADOW_MUL, bare);
}

void light() {
	vec3 col = toon(v_shade, ALBEDO, v_light, dot(NORMAL, LIGHT), ATTENUATION, 1.0, 0.35, LIGHT_COLOR / PI);
	DIFFUSE_LIGHT += col / max(ALBEDO, vec3(1e-4));
}
