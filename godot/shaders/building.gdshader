// Houses and towers. Ported from BUILDING_FRAG in web/js/materials.js: toon light on the baked material colours,
// weathering painted in world space so every instance differs, and dark glass with a sky reflection.
// CUSTOM0.r holds the material id (scripts/host_model.gd): 0 concrete, 1 concrete_dark, 2 trim, 3 rust, 4 sign,
// 5 window, 6 door.
shader_type spatial;
render_mode ambient_light_disabled, specular_disabled;

#include "res://shaders/common.gdshaderinc"

uniform vec3 colors[7];          // linear albedo per material id
uniform vec3 sky_color : source_color = vec3(0.769, 0.894, 0.957);
const vec3 SHADOW_MUL = vec3(0.4, 0.45, 0.63);

varying vec3 v_local;
varying vec3 v_world;
varying vec3 v_normal;
varying float v_mat;
varying float v_fade;
varying float v_lod;

void vertex() {
	v_local = VERTEX;
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_normal = normalize(mat3(MODEL_MATRIX) * NORMAL);
	v_mat = CUSTOM0.r * 255.0;
	v_fade = INSTANCE_CUSTOM.x;
	v_lod = INSTANCE_CUSTOM.y;
}

void fragment() {
	if (lod_dithered_out(v_fade, FRAGCOORD.xy)) {
		discard;
	}
	vec3 n = normalize(v_normal);
	int mat = int(v_mat + 0.5);
	vec3 albedo = colors[clamp(mat, 0, 6)];
	// Weathering: mottling, rain streaks, damp base.
	vec2 face = abs(n.x) > 0.5 ? v_local.zy : v_local.xy;
	float mottle = fbm(face * 0.9 + v_world.xz * 0.05);
	float streak = smoothstep(0.55, 0.85, fbm(vec2(face.x * 3.1, face.y * 0.22) + 3.7)) * (abs(n.y) < 0.5 ? 1.0 : 0.3);
	float damp = 1.0 - smoothstep(0.0, 1.1, v_local.y);
	if (mat <= 2) {
		albedo *= 0.9 + 0.18 * mottle;
		albedo = mix(albedo, albedo * vec3(0.72, 0.74, 0.7), streak * 0.65);
		albedo = mix(albedo, albedo * vec3(0.62, 0.7, 0.58), damp * 0.6);
	} else if (mat == 3) {
		albedo = mix(albedo, albedo * vec3(0.62, 0.48, 0.42), smoothstep(0.4, 0.8, fbm(face * 4.0)));
	}
	ROUGHNESS = TAG_HARD;
	if (mat == 5) {
		// Dark glass with a sky reflection that brightens at grazing angles, plus a diagonal glint. Unlit.
		vec3 v = normalize(CAMERA_POSITION_WORLD - v_world);
		float fres = pow(1.0 - max(dot(n, v), 0.0), 3.0);
		vec3 col = mix(albedo, sky_color * 0.75, 0.15 + fres * 0.55);
		float glint = smoothstep(0.92, 0.97, fract((v_local.x + v_local.z) * 0.9 + v_local.y * 0.6));
		ALBEDO = vec3(0.0);
		EMISSION = mix(col + glint * 0.08, lod_color(v_lod), debug_lod * 0.65);
	} else {
		ALBEDO = albedo;
	}
}

void light() {
	if (length(ALBEDO) > 0.0) {
		vec3 col = toon(ALBEDO * SHADOW_MUL, ALBEDO, ALBEDO * 1.08, dot(NORMAL, LIGHT), ATTENUATION, 1.0, 0.0, LIGHT_COLOR / PI);
		col = mix(col, lod_color(v_lod), debug_lod * 0.65);
		DIFFUSE_LIGHT += col / max(ALBEDO, vec3(1e-4));
	}
}
