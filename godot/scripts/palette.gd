class_name Palette
## The art-direction palette, mirrored from web/js/palette.js ("The Last Signal" concept sheet). Values are sRGB.
## Keep the two files in step: change a colour here and in palette.js together.

const P := {
	"sky": "#3d7cc4", "leaf": "#4f9e4c", "concrete": "#dcd3bf", "rust": "#c4573a", "ink": "#2b3242",

	"sky_zenith": "#3a7fd0", "sky_horizon": "#c4e4f4", "haze": "#b4d7ec", "sun": "#fff4dc",
	"cloud": "#fbfbf6", "cloud_shade": "#a6c0de", "mountain": "#6f9fcf",

	"sun_light": "#fff1d6", "shadow_tint": "#6d8cc6", "sky_ambient": "#9cc4ec", "ground_bounce": "#9fae74",

	"ivy_light": "#a3d45a", "ivy": "#4f9e4c", "ivy_shadow": "#2c5f57",
	"bush_light": "#b4d965", "bush": "#5aa64e", "bush_shadow": "#2f6656",
	"grass_light": "#b7da68", "grass": "#6cae4c", "grass_shadow": "#3d7656",
	"stem": "#6f5c43", "flower_white": "#f7f4ea", "flower_yellow": "#f2c94c", "flower_center": "#e49a36",
	"tree_light": "#a6d25e", "tree": "#438f45", "tree_shadow": "#25544f",
	"bark_light": "#a38d72", "bark": "#7d6a56", "bark_shadow": "#5b5262",
	"oak_light": "#9cc35a", "oak": "#3b7a3d", "oak_shadow": "#20453e",

	"concrete_dark": "#b9ae99", "trim": "#cdc3ac", "window": "#2b3242", "door": "#a9472f",
	"dirt": "#c2ae86",
}

## Foliage tones indexed by the core's material id (core/include/plants/plant_set.h): 0 stem, 1 ivy leaf, 2 bush leaf,
## 3 grass, 4 white flower, 5 yellow flower, 6 flower centre, 7 tree leaf, 8 bark, 9 oak leaf.
const FOLIAGE_LIGHT := ["#8e7a58", "ivy_light", "bush_light", "grass_light", "#fffdf6", "#ffe27e", "#f4b450",
	"tree_light", "bark_light", "oak_light"]
const FOLIAGE_BASE := ["stem", "ivy", "bush", "grass", "flower_white", "flower_yellow", "flower_center", "tree", "bark",
	"oak"]
const FOLIAGE_SHADE := ["#46404e", "ivy_shadow", "bush_shadow", "grass_shadow", "#a9b6d6", "#b49a6c", "#a0674a",
	"tree_shadow", "bark_shadow", "oak_shadow"]


## A palette entry (by name) or a literal "#rrggbb", as an sRGB Color.
static func color(key: String) -> Color:
	return Color(P.get(key, key))


## Linear-light values, for shader uniforms without a source_color hint (arrays).
static func linear(key: String) -> Vector3:
	var c := color(key).srgb_to_linear()
	return Vector3(c.r, c.g, c.b)


static func linear_list(keys: Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	for k in keys:
		out.append(linear(k))
	return out
