// Foliage with alpha-to-coverage leaf edges (needs MSAA). depth_prepass_alpha puts the solid part of every leaf in
// the depth prepass and the shadow maps; only the anti-aliased rim is drawn in the transparent pass.
shader_type spatial;
render_mode world_vertex_coords, cull_disabled, ambient_light_disabled, specular_disabled, depth_prepass_alpha, alpha_to_coverage;

#include "res://shaders/foliage.gdshaderinc"
