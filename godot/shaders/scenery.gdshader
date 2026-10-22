// Flat toon colour for far-away scenery (the mountain ring). Ported from SCENERY_FRAG in web/js/materials.js.
shader_type spatial;
render_mode ambient_light_disabled, specular_disabled;

uniform vec3 color : source_color = vec3(0.435, 0.624, 0.812);

void fragment() {
	ALBEDO = color;
	ROUGHNESS = 0.0;
}

void light() {
	float lit = smoothstep(-0.05, 0.15, dot(NORMAL, LIGHT));
	DIFFUSE_LIGHT += mix(vec3(0.62, 0.7, 0.9), vec3(1.0), lit);
}
