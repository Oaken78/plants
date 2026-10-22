// Anime sky: gradient, sun disc and two-tone cumulus with crisp edges, warm tops and blue undersides. Ported from
// sky() in web/js/post.js. Godot's fog takes its aerial-perspective colour from this sky, so distant hills fade into
// the horizon tone as they do in the web demo's composite pass.
shader_type sky;

global uniform float plant_time;

uniform vec3 zenith : source_color = vec3(0.227, 0.498, 0.816);
uniform vec3 horizon : source_color = vec3(0.769, 0.894, 0.957);
uniform vec3 haze : source_color = vec3(0.706, 0.843, 0.925);
uniform vec3 sun_color : source_color = vec3(1.0, 0.957, 0.863);
uniform vec3 cloud : source_color = vec3(0.984, 0.984, 0.965);
uniform vec3 cloud_shade : source_color = vec3(0.651, 0.753, 0.871);

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
		p = p * 2.02 + 17.1;
		a *= 0.5;
	}
	return s;
}
float fbm2(vec2 p) {
	return 0.5 * vnoise(p) + 0.25 * vnoise(p * 2.02 + 17.1);
}

vec3 gradient(vec3 dir, vec3 sun) {
	float h = dir.y;
	vec3 col = mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.55));
	col = mix(col, haze, clamp(-h * 5.0, 0.0, 1.0));
	float sd = max(dot(dir, sun), 0.0);
	col += sun_color * (pow(sd, 48.0) * 0.5 + pow(sd, 6.0) * 0.1);
	return col;
}

void sky() {
	vec3 dir = EYEDIR;
	vec3 sun = LIGHT0_ENABLED ? LIGHT0_DIRECTION : vec3(0.0, 1.0, 0.0);
	vec3 col = gradient(dir, sun);
	if (!AT_CUBEMAP_PASS) {
		col = mix(col, vec3(1.0, 0.99, 0.95) * 1.4, smoothstep(0.99955, 0.9997, dot(dir, sun)));
	}
	if (dir.y > 0.0) {
		// Cumulus on a virtual cloud deck: crisp edges, warm tops, blue undersides.
		vec2 p = dir.xz / (dir.y + 0.12) * 1.3 + vec2(plant_time * 0.004, plant_time * 0.0015);
		float n = fbm(p * 0.55) + 0.25 * fbm2(p * 0.13 + 5.0) - 0.1;
		float cover = smoothstep(0.5, 0.53, n) * smoothstep(0.0, 0.18, dir.y);
		vec2 q = p + sun.xz * 0.08;
		float toward = fbm(q * 0.55) + 0.25 * fbm2(q * 0.13 + 5.0) - 0.1;
		float lit = smoothstep(-0.015, 0.02, n - toward + 0.006);
		vec3 c = mix(cloud_shade, cloud, lit);
		c = mix(c, horizon, smoothstep(0.25, 0.0, dir.y) * 0.55);
		col = mix(col, c, cover);
	}
	COLOR = col;
}
