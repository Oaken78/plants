// Foliage with hard alpha-tested leaf edges: fully opaque pipeline, for comparison with foliage.gdshader
// (--leaves=scissor).
shader_type spatial;
render_mode world_vertex_coords, cull_disabled, ambient_light_disabled, specular_disabled;

#define LEAF_SCISSOR
#include "res://shaders/foliage.gdshaderinc"
