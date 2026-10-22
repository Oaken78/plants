extends Node3D
## Overgrowth tech demo (port of web/js/main.js): grows every plant variant with the native core, all of them at
## once on the WorkerThreadPool, builds the streaming town and flies around it.
##
## Command-line options (after `--`):
##   --view=N               start at viewpoint N (1 to 8, default 3)
##   --cam=x,y,z,yaw,pitch  start camera (metres, degrees)
##   --shot=path.png|.jpg   save a screenshot after --frames frames, then quit
##   --frames=N             frames to wait before the screenshot (default 120)
##   --bench=S              measure S seconds, print frame and GPU times, then quit
##   --no-ui                hide the panels
##   --leaves=scissor       hard alpha-tested leaf edges instead of alpha to coverage
##   --hide=a,b             hide layers: houses, trees, bushes, grass, terrain, mountains
##   --regrow=SEED          regrow the hero house with this seed once running (tests the background regrow)
##   --stats                print the triangle count of every mesh variant and LOD
##   --serial               grow the plant sets one after another on the main thread (to time the parallel growth)
##   --full-shadows         every LOD casts its own shadow instead of a coarser proxy (to compare)
##   --debug-lod, --no-outlines, --no-shadows, --grow=T, --msaa=N, --sun=elevation,azimuth
##   --shadow-bias=B, --normal-bias=B, --shadow-blur=B   sun shadow tuning

const ASSETS := "../web/assets/"  # shared with the web demo, relative to this project folder

## LOD switch distances in metres: LOD i is used while distance < ranges[i]. Houses and towers have a fifth plant
## level the web demo lacks, see FAR_LOD.
const RANGES := {
	"house": [90.0, 2900.0],
	"house_plants": [24.0, 65.0, 190.0, 600.0, 2900.0],
	"tower": [130.0, 3200.0],
	"tower_plants": [30.0, 85.0, 260.0, 700.0, 3200.0],
	"bush": [26.0, 70.0, 200.0, 1100.0],
	"tree": [34.0, 100.0, 340.0, 2600.0],
	"grass": [26.0, 52.0],
}
const HERO_PARAMS := {"climber.seeds": 34, "climber.roofSeeds": 10, "climber.budget": 20000, "bush.count": 22,
	"tree.count": 3, "tree.groundCount": 2, "tree.oakShare": 0.5}
## Towers are tall: ivy needs more steps to climb them, and curtains hang longer from their roofs.
const TOWER_PARAMS := {"climber.seeds": 44, "climber.roofSeeds": 14, "climber.budget": 32000, "climber.maxSteps": 420,
	"climber.hangLength": 5, "bush.count": 18, "bush.roofShare": 0.5, "tree.count": 2, "tree.groundCount": 2}
const TOWER_NAMES := ["TowerA", "TowerB", "TowerC"]
const BUSH_STYLES := [0, 1, 2, 3, 0, 2, 1, 3, 0, 2]
const TREE_SIZES := [0.4, 0.5, 0.75, 0.9, 1.0, 1.15, 1.35, 1.6, 1.85]
const OAK_SIZES := [0.8, 1.05, 1.3]
const OAK_FORMS := [0.0, 0.5, 1.0]  # crowding: broad (alone), medium, tall (crowded)
const GRASS_VARIANTS := 8
## LOD 4 of the plants on houses and towers: LOD 3 rebuilt with clumps merged per 8 m and only stems over 0.15 m
## (trunks, not roots). Far away a house is a few pixels tall, and this cuts its plants from about 3,000 triangles
## to about 600, while the shapes and colours stay those of LOD 3.
const FAR_LOD := {"merge_cell": 8.0, "min_stem_radius": 0.15}

## Viewpoints: heights are metres above the terrain at that spot.
const VIEWS := [
	{"name": "Ivy close-up", "pos": [3.0, 2.0, 7.6], "target": [1.2, 2.6, 3.0]},
	{"name": "Annex", "pos": [-15.0, 1.4, 4.0], "target": [-7.0, 1.6, 0.0]},
	{"name": "Hero house", "pos": [26.0, 14.0, 30.0], "target": [-2.0, 3.0, 0.0]},
	{"name": "Aerial", "pos": [180.0, 95.0, 260.0], "target": [0.0, 0.0, 0.0]},
	{"name": "Village", "pos": [445.0, 2.2, -95.0], "target": [551.0, 4.0, -174.0]},
	{"name": "Village above", "pos": [650.0, 45.0, -70.0], "target": [551.0, 0.0, -174.0]},
	{"name": "Towers", "pos": [18.0, 7.0, 2.0], "target": [-2.0, 9.0, -52.0]},
	{"name": "Lone oak", "pos": [-50.0, 2.0, -22.0], "target": [-27.0, 5.0, -3.0]},
]

var world := DemoWorld.new()
var camera: FlyCamera
var sun: DirectionalLight3D
var env: Environment
var ui: DemoUi
var ink: MeshInstance3D
var house_layer: PlantLayer
var tree_layer: PlantLayer
var bush_layer: PlantLayer
var grass_layer: PlantLayer
var layers: Array[PlantLayer] = []

var foliage_material: ShaderMaterial
var grass_material: ShaderMaterial
var building_material: ShaderMaterial

var sun_elevation := 48.0
var sun_azimuth := 215.0
var wind_strength := 1.0
var growth := 1.0
var hero_seed := 1
var hero_overrides := {}

var _args := {}
var _time := 0.0
var _frame := 0
var _started := false
var _house_meshes: Array = []
var _house_host := {}
var _jobs: Array = []
var _results: Array = []
var _done := 0
var _mutex := Mutex.new()
var _hero_generator: PlantGenerator
var _hero_task := -1
var _hero_stats := {}
var _load_msec := 0
var _job_msec := 0.0
var _lod_debug := false
var _instancing_usec := 0.0
var _animating := false
var _anim_start := 0.0
var _bench_times: PackedFloat32Array = []
var _bench_gpu: PackedFloat32Array = []
var _bench_layers: PackedFloat32Array = []
var _bench_per_layer := {}
var _bench_start := -1


func _ready() -> void:
	_parse_args()
	ui = DemoUi.new()
	add_child(ui)
	ui.status("Loading models...")
	await get_tree().process_frame
	var t0 := Time.get_ticks_msec()

	var assets := ProjectSettings.globalize_path("res://").path_join(ASSETS)
	var house_scene := HostModel.load_scene(assets.path_join("house.glb"))
	var tower_scene := HostModel.load_scene(assets.path_join("towers.glb"))
	if house_scene == null or tower_scene == null:
		ui.status("Cannot load the models from %s" % assets)
		return
	var house0 := HostModel.merge(house_scene, "House_LOD0")
	var house1 := HostModel.merge(house_scene, "House_LOD1")
	_house_meshes = [house0.mesh, house1.mesh]
	_house_host = HostModel.with_ground_patch(house0, 4.5)
	var tower_meshes := []
	var tower_hosts := []
	for name in TOWER_NAMES:
		var t0_lod := HostModel.merge(tower_scene, name + "_LOD0")
		var t1_lod := HostModel.merge(tower_scene, name + "_LOD1")
		tower_meshes.append([t0_lod.mesh, t1_lod.mesh])
		tower_hosts.append(HostModel.with_ground_patch(t0_lod, 6.0))
	_build_materials(HostModel.material_colors(house_scene))
	house_scene.free()
	tower_scene.free()

	# Every plant set, grown in parallel: one generator (one core context) per job. Big jobs first.
	var grow_start := Time.get_ticks_msec()
	_jobs.append({"type": "host", "host": _house_host, "params": _hero_params(), "seed": hero_seed, "hero": true})
	for i in TOWER_NAMES.size():
		_jobs.append({"type": "host", "host": tower_hosts[i], "params": TOWER_PARAMS, "seed": 400 + i * 41})
	for v in range(1, 5):
		_jobs.append({"type": "host", "host": _house_host, "params": {}, "seed": 100 + v * 17})
	for si in OAK_SIZES.size():
		for fi in OAK_FORMS.size():
			_jobs.append({"type": "tree", "seed": 700 + si * 31 + fi * 7, "size": OAK_SIZES[si], "species": 1,
				"crowding": OAK_FORMS[fi]})
	for i in TREE_SIZES.size():
		_jobs.append({"type": "tree", "seed": 300 + i * 23, "size": TREE_SIZES[i], "species": 0, "crowding": 0.5})
	for i in BUSH_STYLES.size():
		_jobs.append({"type": "bush", "seed": 500 + i * 31, "style": BUSH_STYLES[i]})
	for i in GRASS_VARIANTS:
		_jobs.append({"type": "grass", "seed": 900 + i * 13})
	_results.resize(_jobs.size())
	if _args.has("serial"):
		for i in _jobs.size():
			_grow_job(i)
	else:
		var task := WorkerThreadPool.add_group_task(_grow_job, _jobs.size(), -1, true, "Growing plants")
		while not WorkerThreadPool.is_group_task_completed(task):
			ui.status("Growing %d plant sets on %d threads... %d done" % [_jobs.size(), OS.get_processor_count(), _done])
			await get_tree().process_frame
		WorkerThreadPool.wait_for_group_task_completion(task)
	var grow_msec := Time.get_ticks_msec() - grow_start

	ui.status("Building the town...")
	await get_tree().process_frame
	_build_layers(tower_meshes)
	_build_scenery()
	_build_environment()
	_build_ink()

	camera = FlyCamera.new()
	camera.world = world
	camera.fov = 55.0
	camera.near = 0.15
	camera.far = 9000.0
	add_child(camera)
	_place_camera()

	if _args.has("stats"):
		_print_mesh_stats(tower_meshes)
	_load_msec = Time.get_ticks_msec() - t0
	print("Plants grown in %d ms (%d jobs, %.0f ms of generation in total, %d threads); scene ready in %d ms" % [
		grow_msec, _jobs.size(), _job_msec, OS.get_processor_count(), _load_msec])
	_hero_generator = PlantGenerator.new()
	_hero_generator.set_host(_house_host.positions, _house_host.indices, _house_host.tags)
	_apply_args()
	ui.setup(self)
	ui.show_generation(_hero_stats, _jobs.size(), grow_msec, _job_msec)
	ui.status("")
	_started = true


func _hero_params() -> Dictionary:
	var p := HERO_PARAMS.duplicate()
	p.merge(hero_overrides, true)
	return p


## Runs on a worker thread. Touches nothing but its own generator and its slot in _results.
func _grow_job(i: int) -> void:
	var job: Dictionary = _jobs[i]
	var gen := PlantGenerator.new()
	gen.set_params(job.get("params", {}))
	var lods: Array
	match job.type:
		"host":
			gen.set_host(job.host.positions, job.host.indices, job.host.tags)
			lods = gen.grow_on_host(job.seed)
		"bush":
			lods = gen.make_bush(job.seed, job.style, 1.0)
		"tree":
			lods = gen.make_tree(job.seed, job.size, job.species, job.crowding)
		"grass":
			lods = gen.make_grass(job.seed)
	var stats := gen.get_stats()
	if job.type == "host":
		lods.append(_far_lod(gen, stats))
	_mutex.lock()
	_results[i] = lods
	_job_msec += stats.msec
	if job.get("hero", false):
		_hero_stats = stats
	_done += 1
	_mutex.unlock()


## Adds LOD 4 to the plants just grown on a host (and its triangle count to their stats).
func _far_lod(gen: PlantGenerator, stats: Dictionary) -> ArrayMesh:
	var far := gen.build_far_lod(FAR_LOD.merge_cell, FAR_LOD.min_stem_radius)
	stats.triangles.append(far.surface_get_array_index_len(0) / 3 if far else 0)
	return far


func _results_of(type: String) -> Array:
	var out := []
	for i in _jobs.size():
		if _jobs[i].type == type:
			out.append(_results[i])
	return out


func _build_materials(house_colors: PackedVector3Array) -> void:
	var leaves := "res://shaders/foliage_scissor.gdshader" if _args.get("leaves") == "scissor" else "res://shaders/foliage.gdshader"
	foliage_material = ShaderMaterial.new()
	foliage_material.shader = load(leaves)
	foliage_material.set_shader_parameter("mat_light", Palette.linear_list(Palette.FOLIAGE_LIGHT))
	foliage_material.set_shader_parameter("mat_base", Palette.linear_list(Palette.FOLIAGE_BASE))
	foliage_material.set_shader_parameter("mat_shade", Palette.linear_list(Palette.FOLIAGE_SHADE))
	# Grass shrinks into the ground at the end of its range instead of ending in a visible ring.
	grass_material = foliage_material.duplicate()
	grass_material.set_shader_parameter("shrink", Vector2(30.0, 50.0))
	building_material = ShaderMaterial.new()
	building_material.shader = load("res://shaders/building.gdshader")
	building_material.set_shader_parameter("colors", house_colors)
	building_material.set_shader_parameter("sky_color", Palette.color("sky_horizon"))


## shadow_lods[i] is the LOD whose mesh casts LOD i's shadow (-1: none). LOD 3 and beyond are distant puffs and cast
## nothing, as in the web demo. LOD 0 casts with the LOD 1 mesh, and houses' heavy LOD 2 with LOD 3: see
## Part::proxy in extension/src/plant_layer.h.
func _plant_part(lods: Array, ranges: Array, material: Material = null, shadow_lods := [1, 1, 2]) -> Dictionary:
	if _args.has("full-shadows"):
		shadow_lods = [0, 1, 2]
	return {"lods": lods, "ranges": PackedFloat32Array(ranges), "material": material if material else foliage_material,
		"shadow_lods": PackedInt32Array(shadow_lods)}


func _building_variant(meshes: Array, plant_lods: Array, building_ranges: Array, plant_ranges: Array) -> Array:
	return [
		{"lods": meshes, "ranges": PackedFloat32Array(building_ranges), "material": building_material,
			"shadow_lods": PackedInt32Array([0, 1])},
		_plant_part(plant_lods, plant_ranges, null, [1, 1, 3]),
	]


func _build_layers(tower_meshes: Array) -> void:
	world.configure_variants(BUSH_STYLES.size(), GRASS_VARIANTS, TREE_SIZES.size(), OAK_SIZES.size(), OAK_FORMS.size())
	var hosts := _results_of("host")  # hero, towers A to C, then town houses 1 to 4

	house_layer = _layer("Houses", PlantLayer.KIND_HOUSES, 48.0, 3200.0, -2.0, 52.0)
	house_layer.set_variant(0, _building_variant(_house_meshes, hosts[0], RANGES.house, RANGES.house_plants))
	for v in range(1, 5):
		house_layer.set_variant(v, _building_variant(_house_meshes, hosts[3 + v], RANGES.house, RANGES.house_plants))
	for t in TOWER_NAMES.size():
		house_layer.set_variant(5 + t, _building_variant(tower_meshes[t], hosts[1 + t], RANGES.tower, RANGES.tower_plants))

	# Round trees first, then oaks by size and form; the jobs list grew oaks first.
	var trees := _results_of("tree")
	var oaks := trees.slice(0, OAK_SIZES.size() * OAK_FORMS.size())
	var rounds := trees.slice(oaks.size())
	tree_layer = _layer("Trees", PlantLayer.KIND_TREES, 48.0, 2600.0, -1.0, 32.0)
	for i in rounds.size():
		tree_layer.set_variant(i, [_plant_part(rounds[i], RANGES.tree)])
	for i in oaks.size():
		tree_layer.set_variant(rounds.size() + i, [_plant_part(oaks[i], RANGES.tree)])

	var bushes := _results_of("bush")
	bush_layer = _layer("Bushes", PlantLayer.KIND_BUSHES, 32.0, 1100.0, -1.0, 4.0)
	for i in bushes.size():
		bush_layer.set_variant(i, [_plant_part(bushes[i], RANGES.bush)])

	var grass := _results_of("grass")
	grass_layer = _layer("Grass", PlantLayer.KIND_GRASS, 6.0, 52.0, -1.0, 1.5, 0.25)
	for i in grass.size():
		grass_layer.set_variant(i, [_plant_part(grass[i], RANGES.grass, grass_material)])


func _layer(name: String, kind: int, cell: float, max_distance: float, y_min: float, y_max: float, fade := 0.12) -> PlantLayer:
	var l := PlantLayer.new()
	l.name = name
	l.configure(world, kind, cell, max_distance, y_min, y_max, fade)
	add_child(l)
	layers.append(l)
	return l


func _build_scenery() -> void:
	var terrain_mat := ShaderMaterial.new()
	terrain_mat.shader = load("res://shaders/terrain.gdshader")
	for key in ["grass_light", "grass", "grass_shade", "dirt"]:
		terrain_mat.set_shader_parameter(key, Palette.color("grass_shadow" if key == "grass_shade" else key))
	var terrain := MeshInstance3D.new()
	terrain.name = "Terrain"
	terrain.mesh = world.build_terrain_mesh(420)
	terrain.material_override = terrain_mat
	add_child(terrain)

	var mountain_mat := ShaderMaterial.new()
	mountain_mat.shader = load("res://shaders/scenery.gdshader")
	mountain_mat.set_shader_parameter("color", Palette.color("mountain"))
	var mountains := MeshInstance3D.new()
	mountains.name = "Mountains"
	mountains.mesh = world.build_mountain_mesh()
	mountains.material_override = mountain_mat
	mountains.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mountains)


func _build_environment() -> void:
	var sky_mat := ShaderMaterial.new()
	sky_mat.shader = load("res://shaders/sky.gdshader")
	for key in ["zenith", "horizon", "haze", "cloud", "cloud_shade"]:
		var palette_key: String = {"zenith": "sky_zenith", "horizon": "sky_horizon"}.get(key, key)
		sky_mat.set_shader_parameter(key, Palette.color(palette_key))
	sky_mat.set_shader_parameter("sun_color", Palette.color("sun"))
	var sky := Sky.new()
	sky.sky_material = sky_mat
	sky.process_mode = Sky.PROCESS_MODE_REALTIME
	sky.radiance_size = Sky.RADIANCE_SIZE_256

	env = Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	env.reflected_light_source = Environment.REFLECTION_SOURCE_DISABLED
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	# Aerial perspective: distance fades toward the sky's own horizon colour.
	env.fog_enabled = true
	env.fog_mode = Environment.FOG_MODE_EXPONENTIAL
	env.fog_density = 0.00055
	env.fog_aerial_perspective = 1.0
	env.fog_sky_affect = 0.0
	env.adjustment_enabled = true
	env.adjustment_saturation = 1.08
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	sun = DirectionalLight3D.new()
	sun.name = "Sun"
	sun.light_color = Palette.color("sun_light")
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 400.0
	sun.directional_shadow_split_1 = 0.05
	sun.directional_shadow_split_2 = 0.15
	sun.directional_shadow_split_3 = 0.4
	sun.directional_shadow_blend_splits = true
	sun.shadow_blur = 1.0
	# The web demo's shadow map has a depth bias of about 18 cm, so leaves shade each other by clump rather than leaf by
	# leaf. This bias gives crowns the same calm, mostly lit tops.
	sun.shadow_bias = 1.0
	add_child(sun)
	_update_sun()


## Ink lines: a full-screen quad drawn last (see shaders/ink.gdshader).
func _build_ink() -> void:
	var quad := QuadMesh.new()
	quad.size = Vector2(2, 2)
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/ink.gdshader")
	mat.render_priority = Material.RENDER_PRIORITY_MAX
	ink = MeshInstance3D.new()
	ink.name = "Ink"
	ink.mesh = quad
	ink.material_override = mat
	ink.extra_cull_margin = 16384.0
	ink.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(ink)


func sun_direction() -> Vector3:
	var el := deg_to_rad(sun_elevation)
	var az := deg_to_rad(sun_azimuth)
	return Vector3(cos(el) * sin(az), sin(el), cos(el) * cos(az)).normalized()


func _update_sun() -> void:
	if sun:
		sun.basis = Basis.looking_at(-sun_direction(), Vector3.UP if absf(sun_elevation) < 89.0 else Vector3.FORWARD)


func go_to_view(index: int) -> void:
	var v: Dictionary = VIEWS[index]
	var at := func(p: Array) -> Vector3: return Vector3(p[0], world.terrain_height(p[0], p[2]) + p[1], p[2])
	camera.look_from(at.call(v.pos), at.call(v.target))


func _place_camera() -> void:
	if _args.has("cam"):
		var c: PackedFloat64Array = str(_args.cam).split_floats(",")
		camera.position = Vector3(c[0], c[1], c[2])
		camera.yaw = deg_to_rad(c[3])
		camera.pitch = deg_to_rad(c[4])
		camera.basis = Basis.from_euler(Vector3(camera.pitch, camera.yaw, 0.0))
	else:
		go_to_view(clampi(int(_args.get("view", 3)) - 1, 0, VIEWS.size() - 1))


func _apply_args() -> void:
	if _args.has("sun"):
		var s: PackedFloat64Array = str(_args.sun).split_floats(",")
		sun_elevation = s[0]
		sun_azimuth = s[1]
		_update_sun()
	if _args.has("shadow-bias"):
		sun.shadow_bias = float(_args["shadow-bias"])
	if _args.has("normal-bias"):
		sun.shadow_normal_bias = float(_args["normal-bias"])
	if _args.has("shadow-blur"):
		sun.shadow_blur = float(_args["shadow-blur"])
	if _args.has("terrain-shadows"):
		$Terrain.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if _args["terrain-shadows"] == "on" else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	if _args.has("splits"):
		sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS if _args.splits == "2" else DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	if _args.has("shadow-distance"):
		sun.directional_shadow_max_distance = float(_args["shadow-distance"])
	if _args.has("grow"):
		growth = float(_args.grow)
	set_lod_debug(_args.has("debug-lod"))
	set_outlines(not _args.has("no-outlines"))
	set_shadows(not _args.has("no-shadows"))
	if _args.has("msaa"):
		get_viewport().msaa_3d = int(_args.msaa) as Viewport.MSAA
	for layer in str(_args.get("hide", "")).split(",", false):
		get_node(NodePath(layer.capitalize())).visible = false
	if _args.has("no-ui"):
		ui.visible = false
	if _args.has("bench"):
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
		RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)


# ---------------------------------------------------------------- settings used by the UI

func set_lod_debug(on: bool) -> void:
	_lod_debug = on
	RenderingServer.global_shader_parameter_set("debug_lod", 1.0 if on else 0.0)


func lod_debug() -> bool:
	return _lod_debug


func set_outlines(on: bool) -> void:
	ink.material_override.set_shader_parameter("outlines", 1.0 if on else 0.0)


func outlines() -> bool:
	return ink.material_override.get_shader_parameter("outlines") > 0.5


func set_shadows(on: bool) -> void:
	sun.shadow_enabled = on


func set_sun(elevation: float, azimuth: float) -> void:
	sun_elevation = elevation
	sun_azimuth = azimuth
	_update_sun()


## Render scale: below 1 upscales with FSR, above 1 supersamples.
func set_render_scale(scale: float) -> void:
	var vp := get_viewport()
	vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR if scale < 1.0 else Viewport.SCALING_3D_MODE_BILINEAR
	vp.scaling_3d_scale = scale


func animate_growth() -> void:
	_animating = true
	_anim_start = _time


## Regrows the hero house on a worker thread; the old plants stay until the new ones are ready.
func regrow_hero(seed: int) -> void:
	if _hero_task >= 0:
		return
	hero_seed = seed
	var params := _hero_params()
	ui.status("Regrowing the hero house (seed %d)..." % seed)
	_hero_task = WorkerThreadPool.add_task(func() -> void:
		_hero_generator.set_params(params)
		var lods := _hero_generator.grow_on_host(seed)
		var stats := _hero_generator.get_stats()
		lods.append(_far_lod(_hero_generator, stats))
		_hero_ready.call_deferred(lods, stats))


func _hero_ready(lods: Array, stats: Dictionary) -> void:
	WorkerThreadPool.wait_for_task_completion(_hero_task)
	_hero_task = -1
	_hero_stats = stats
	house_layer.set_variant(0, _building_variant(_house_meshes, lods, RANGES.house, RANGES.house_plants))
	ui.show_generation(_hero_stats, _jobs.size(), -1, -1)
	ui.status("")


# ---------------------------------------------------------------- frame loop

func _unhandled_input(event: InputEvent) -> void:
	if not _started or not (event is InputEventKey and event.pressed and not event.echo):
		return
	var key: int = event.keycode
	if key >= KEY_1 and key <= KEY_8:
		go_to_view(key - KEY_1)
	match key:
		KEY_L:
			set_lod_debug(not lod_debug())
		KEY_O:
			set_outlines(not outlines())
		KEY_G:
			animate_growth()
		KEY_R:
			regrow_hero(randi() % 1000000)
		KEY_H:
			ui.visible = not ui.visible
	ui.sync()


func _process(delta: float) -> void:
	if not _started:
		return
	_frame += 1
	_time += delta
	if _animating:
		growth = minf(1.0, (_time - _anim_start) / 9.0)
		_animating = growth < 1.0
		ui.sync()
	RenderingServer.global_shader_parameter_set("plant_time", _time)
	RenderingServer.global_shader_parameter_set("main_cam", camera.global_position)
	RenderingServer.global_shader_parameter_set("wind", Vector4(0.8, 0.6, wind_strength, 0.0))
	RenderingServer.global_shader_parameter_set("grow", growth)
	ink.material_override.set_shader_parameter("line_width", maxf(1.0, get_viewport().get_visible_rect().size.y / 1100.0))
	var sun_dir := sun_direction()
	var shadow_reach := sun.directional_shadow_max_distance if sun.shadow_enabled else 0.0
	_instancing_usec = PlantLayer.update_layers(layers, camera, sun_dir, shadow_reach)
	ui.update_stats(delta)

	if _args.has("regrow") and _frame == 10:
		regrow_hero(int(_args.regrow))
	if _args.has("shot") and _frame == int(_args.get("frames", 120)):
		await _screenshot(str(_args.shot))
		get_tree().quit()
	if _args.has("bench") and _frame > 30:
		if _bench_start < 0:
			_bench_start = Time.get_ticks_msec()
		_bench_times.append(delta * 1000.0)
		_bench_gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid()))
		_bench_layers.append(layer_stats().usec / 1000.0)
		for l in layers:
			var ls := l.get_stats()
			var acc: Array = _bench_per_layer.get(l.name, [0.0, 0.0, 0, 0, 0])
			_bench_per_layer[l.name] = [acc[0] + ls.walk_usec, acc[1] + ls.upload_usec, ls.cells, ls.instances, ls.pushes]
		if Time.get_ticks_msec() - _bench_start > float(_args.bench) * 1000.0:
			_print_bench()
			get_tree().quit()


func layer_stats() -> Dictionary:
	var out := {"instances": {}, "per_lod": [0, 0, 0, 0, 0], "usec": _instancing_usec, "upload_usec": 0.0, "draws": 0}
	for l in layers:
		var s := l.get_stats()
		out.instances[l.name] = s.instances
		for i in 5:
			out.per_lod[i] += s.per_lod[i]
		out.upload_usec += s.upload_usec
		out.draws += s.draws
	return out


func _print_mesh_stats(tower_meshes: Array) -> void:
	var tris := func(meshes: Array) -> Array:
		return meshes.map(func(m: Mesh) -> int: return 0 if m == null else m.surface_get_array_index_len(0) / 3)
	print("house building LODs ", tris.call(_house_meshes))
	for t in tower_meshes.size():
		print("tower %s building LODs %s" % [TOWER_NAMES[t], tris.call(tower_meshes[t])])
	for i in _jobs.size():
		var j: Dictionary = _jobs[i]
		print("%s seed %d: plant LODs %s" % [j.type, j.seed, tris.call(_results[i])])


func _screenshot(path: String) -> void:
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	var err := img.save_jpg(path, 0.9) if path.ends_with(".jpg") else img.save_png(path)
	print("Screenshot %s (%s)" % [path, error_string(err)])


func _print_bench() -> void:
	for pair in [["frame", _bench_times], ["gpu", _bench_gpu], ["instancing", _bench_layers]]:
		var sorted: PackedFloat32Array = pair[1].duplicate()
		sorted.sort()
		var total := 0.0
		for v in sorted:
			total += v
		var n := sorted.size()
		print("Bench %s: %d frames, mean %.2f ms, median %.2f ms, p95 %.2f ms, worst %.2f ms" % [
			pair[0], n, total / n, sorted[n / 2], sorted[int(n * 0.95)], sorted[n - 1]])
	var vp := get_viewport().get_viewport_rid()
	var info := func(type: int, what: int) -> int: return RenderingServer.viewport_get_render_info(vp, type, what)
	print("Main pass: %.2f M tris, %d draws, %d objects; shadow passes: %.2f M tris, %d draws, %d objects" % [
		info.call(RenderingServer.VIEWPORT_RENDER_INFO_TYPE_VISIBLE, RenderingServer.VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME) / 1e6,
		info.call(RenderingServer.VIEWPORT_RENDER_INFO_TYPE_VISIBLE, RenderingServer.VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME),
		info.call(RenderingServer.VIEWPORT_RENDER_INFO_TYPE_VISIBLE, RenderingServer.VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME),
		info.call(RenderingServer.VIEWPORT_RENDER_INFO_TYPE_SHADOW, RenderingServer.VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME) / 1e6,
		info.call(RenderingServer.VIEWPORT_RENDER_INFO_TYPE_SHADOW, RenderingServer.VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME),
		info.call(RenderingServer.VIEWPORT_RENDER_INFO_TYPE_SHADOW, RenderingServer.VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME)])
	for name in _bench_per_layer:
		var a: Array = _bench_per_layer[name]
		var n := _bench_layers.size()
		print("  %s: walk %.3f ms (worker thread), upload %.3f ms, %d cells, %d instances, %d pushes" % [
			name, a[0] / n / 1000.0, a[1] / n / 1000.0, a[2], a[3], a[4]])
	var s := layer_stats()
	print("Instances %s, parts by LOD %s, %d buckets drawn, layer update %.2f ms (%.2f ms of it uploading)" % [
		s.instances, s.per_lod, s.draws, s.usec / 1000.0, s.upload_usec / 1000.0])


func _parse_args() -> void:
	for arg in OS.get_cmdline_user_args():
		var a := arg.trim_prefix("--")
		var eq := a.find("=")
		if eq >= 0:
			_args[a.substr(0, eq)] = a.substr(eq + 1)
		else:
			_args[a] = true
