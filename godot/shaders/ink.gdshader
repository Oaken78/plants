// Ink lines and vignette, drawn last over the whole screen with multiplicative blending. Ported from the composite
// pass in web/js/post.js; the sky, aerial perspective and grading it also did are Godot's sky, fog and adjustments.
//
// Anime convention: buildings get their silhouettes and convex creases, foliage only the outline of its masses. The
// web demo renders the buildings and terrain a second time into a depth target to tell the two apart. Here every
// material tags itself through ROUGHNESS (TAG_FOLIAGE / TAG_HARD in common.gdshaderinc), which Godot writes into the
// normal-roughness buffer during the depth prepass anyway, so no geometry is drawn twice.
shader_type spatial;
render_mode unshaded, blend_mul, depth_test_disabled, depth_draw_never, cull_disabled, fog_disabled, shadows_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_nearest;
uniform sampler2D tag_tex : hint_normal_roughness_texture, filter_nearest;
uniform float outlines = 1.0;
uniform float line_width = 1.0;
uniform vec3 ink_mul = vec3(0.3, 0.34, 0.45);

void vertex() {
	POSITION = vec4(VERTEX.xy, 1.0, 1.0);
}

// Distance along the view ray, and view-space depth, from the (reverse-Z) depth buffer.
vec2 depth_at(vec2 uv, mat4 inv_proj) {
	float raw = texture(depth_tex, uv).r;
	vec4 v = inv_proj * vec4(uv * 2.0 - 1.0, raw, 1.0);
	v.xyz /= v.w;
	return vec2(length(v.xyz), -v.z);
}

bool foliage_at(vec2 uv) {
	return texture(tag_tex, uv).a > 0.5;
}

void fragment() {
	vec2 uv = SCREEN_UV;
	vec2 px = line_width / VIEWPORT_SIZE;
	float mul_line = 0.0;
	float raw = texture(depth_tex, uv).r;
	if (outlines > 0.5 && raw > 0.0) {
		vec2 c = depth_at(uv, INV_PROJECTION_MATRIX);
		float z = c.y;
		float dist = c.x;
		float zl = depth_at(uv - vec2(px.x, 0.0), INV_PROJECTION_MATRIX).y;
		float zr = depth_at(uv + vec2(px.x, 0.0), INV_PROJECTION_MATRIX).y;
		float zd = depth_at(uv - vec2(0.0, px.y), INV_PROJECTION_MATRIX).y;
		float zu = depth_at(uv + vec2(0.0, px.y), INV_PROJECTION_MATRIX).y;
		bool foliage = foliage_at(uv);
		float far = max(max(zl, zr), max(zd, zu));
		float jump = (far - z) / z - 0.012 * z / 100.0;
		float sil = foliage ? smoothstep(0.16, 0.3, jump) * step(1.2, far - z) : smoothstep(0.035, 0.09, jump);
		float crease = 0.0;
		// Convex creases of hard surfaces: 1/z is affine on planes, so its Laplacian is ~0 on flat faces and spikes
		// on edges. Only where the whole stencil is hard, so leaves in front of a wall draw no crease of their own.
		if (!foliage && !foliage_at(uv - vec2(px.x, 0.0)) && !foliage_at(uv + vec2(px.x, 0.0)) &&
				!foliage_at(uv - vec2(0.0, px.y)) && !foliage_at(uv + vec2(0.0, px.y))) {
			float iz = 1.0 / z;
			float lap = (1.0 / zl + 1.0 / zr + 1.0 / zd + 1.0 / zu - 4.0 * iz) / iz;
			crease = smoothstep(0.012, 0.035, -lap);
		}
		mul_line = max(sil, crease * 0.8) * mix(1.0, 0.25, smoothstep(60.0, 400.0, dist)) * (1.0 - smoothstep(600.0, 1600.0, dist));
	}
	vec2 q = uv - 0.5;
	ALBEDO = mix(vec3(1.0), ink_mul, mul_line * 0.82) * (1.0 - dot(q, q) * 0.28);
}
