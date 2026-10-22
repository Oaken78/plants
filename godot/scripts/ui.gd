class_name DemoUi
extends CanvasLayer
## Panels of the demo, laid out like web/index.html: stats top left, controls and the hero house generator top right,
## keys bottom left. Paper-coloured panels with an ink border, matching the scene.

const INK := Color("2b3242")
const PAPER := Color("f4efe2")
const RUST := Color("c4573a")

var main: Node
var _status: Label
var _stats: Label
var _gen: Label
var _speed: Label
var _sliders := {}
var _checks := {}
var _seed: SpinBox
var _panels: Array[Control] = []
var _stat_timer := 0.0
var _fps := 60.0
var _syncing := false


func _ready() -> void:
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.theme = _theme()
	add_child(root)
	_status = Label.new()
	_status.set_anchors_preset(Control.PRESET_CENTER)
	_status.add_theme_font_size_override("font_size", 18)
	_status.add_theme_color_override("font_color", INK)
	_status.grow_horizontal = Control.GROW_DIRECTION_BOTH
	root.add_child(_status)


func status(text: String) -> void:
	_status.text = text
	_status.visible = text != ""


func setup(m: Node) -> void:
	main = m
	var root: Control = get_child(0)

	var hud := _panel(root, Control.PRESET_TOP_LEFT, 300)
	var title := RichTextLabel.new()
	title.bbcode_enabled = true
	title.fit_content = true
	title.scroll_active = false
	title.text = "[b]Overgrowth[/b] [color=#c4573a]/ plant generator in Godot[/color]"
	title.add_theme_font_size_override("normal_font_size", 16)
	title.add_theme_font_size_override("bold_font_size", 16)
	title.add_theme_color_override("default_color", INK)
	hud.add_child(title)
	_stats = _label(hud, "", true)
	_gen = _label(hud, "")
	_gen.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART

	var controls := _panel(root, Control.PRESET_TOP_RIGHT, 320)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.custom_minimum_size = Vector2(296, 0)
	controls.add_child(scroll)
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(box)
	_caption(box, "VIEWPOINTS")
	var views := GridContainer.new()
	views.columns = 4
	box.add_child(views)
	for i in main.VIEWS.size():
		var b := _button(views, main.VIEWS[i].name)
		b.custom_minimum_size = Vector2(68, 0)
		b.add_theme_font_size_override("font_size", 11)
		b.pressed.connect(main.go_to_view.bind(i))
	_slider(box, "Growth", "grow", 0.0, 1.0, 0.001, func(v: float) -> void: main.growth = v)
	_slider(box, "Wind", "wind", 0.0, 3.0, 0.01, func(v: float) -> void: main.wind_strength = v)
	_slider(box, "Sun height", "sun_el", 8.0, 85.0, 1.0, func(v: float) -> void: main.set_sun(v, main.sun_azimuth))
	_slider(box, "Sun direction", "sun_az", 0.0, 360.0, 1.0, func(v: float) -> void: main.set_sun(main.sun_elevation, v))
	_slider(box, "Resolution", "res", 0.5, 2.0, 0.05, main.set_render_scale)
	var toggles := GridContainer.new()
	toggles.columns = 2
	box.add_child(toggles)
	_check(toggles, "LOD colours", "lod", main.set_lod_debug)
	_check(toggles, "Shadows", "shadows", main.set_shadows)
	_check(toggles, "Ink outlines", "outlines", main.set_outlines)
	var animate := _button(box, "Animate growth", true)
	animate.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	animate.pressed.connect(main.animate_growth)

	_caption(box, "HERO HOUSE GENERATOR")
	var seed_row := HBoxContainer.new()
	box.add_child(seed_row)
	_seed = SpinBox.new()
	_seed.max_value = 999999
	_seed.value = main.hero_seed
	seed_row.add_child(_seed)
	_button(seed_row, "Regrow").pressed.connect(func() -> void: main.regrow_hero(int(_seed.value)))
	_button(seed_row, "New seed", true).pressed.connect(func() -> void:
		_seed.value = randi() % 1000000
		main.regrow_hero(int(_seed.value)))
	var params_toggle := _button(box, "Parameters")
	params_toggle.toggle_mode = true
	params_toggle.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	var params := GridContainer.new()
	params.columns = 3
	params.visible = false
	box.add_child(params)
	params_toggle.toggled.connect(func(on: bool) -> void:
		params.visible = on
		_fit_scroll.call_deferred(scroll, box))
	_build_params(params)
	_fit_scroll.call_deferred(scroll, box)

	var help := _panel(root, Control.PRESET_BOTTOM_LEFT, 0)
	_speed = _label(help, "")
	_label(help, "1-8 viewpoints   L LOD colours   O outlines   G grow   R new seed   H hide panels")
	main.camera.speed_changed.connect(func(_s: float) -> void: _update_speed())
	_update_speed()
	sync()


## The controls scroll only once they are taller than the window.
func _fit_scroll(scroll: ScrollContainer, content: Control) -> void:
	var room := get_viewport().get_visible_rect().size.y - 80.0
	scroll.custom_minimum_size.y = minf(content.get_combined_minimum_size().y, room)


func _build_params(grid: GridContainer) -> void:
	for p in PlantGenerator.get_param_list():
		var name: String = p.name
		var value: float = main.HERO_PARAMS.get(name, p.default)
		var integer: bool = is_equal_approx(p.default, roundf(p.default)) and p.max > 4.0
		var l := _label(grid, name)
		l.add_theme_font_size_override("font_size", 11)
		l.custom_minimum_size = Vector2(150, 0)
		var s := HSlider.new()
		s.min_value = p.min
		s.max_value = p.max
		s.step = 1.0 if integer else (p.max - p.min) / 200.0
		s.value = value
		s.custom_minimum_size = Vector2(80, 0)
		grid.add_child(s)
		var out := _label(grid, _num(value))
		out.add_theme_font_size_override("font_size", 11)
		s.value_changed.connect(func(v: float) -> void:
			main.hero_overrides[name] = v
			out.text = _num(v))


func sync() -> void:
	if main == null:
		return
	_syncing = true
	_sliders.grow.value = main.growth
	_sliders.wind.value = main.wind_strength
	_sliders.sun_el.value = main.sun_elevation
	_sliders.sun_az.value = main.sun_azimuth
	_sliders.res.value = main.get_viewport().scaling_3d_scale
	_checks.lod.button_pressed = main.lod_debug()
	_checks.shadows.button_pressed = main.sun.shadow_enabled
	_checks.outlines.button_pressed = main.outlines()
	_syncing = false


func show_generation(hero: Dictionary, jobs: int, wall_msec: int, job_msec: float) -> void:
	var t: PackedInt32Array = hero.get("triangles", PackedInt32Array())
	var tris := []
	for n in t:
		tris.append(_thousands(n))
	var text := "hero: %d climbers, %d trees, %d bushes, %s leaves, %d flowers in %d ms - LOD tris %s" % [
		hero.get("climbers", 0), hero.get("trees", 0), hero.get("bushes", 0), _thousands(hero.get("leaves", 0)),
		hero.get("flowers", 0), hero.get("msec", 0), " / ".join(tris)]
	if wall_msec >= 0:
		text += "\nall %d plant sets: %d ms on %d threads (%d ms of work)" % [jobs, wall_msec, OS.get_processor_count(), job_msec]
	_gen.text = text


func update_stats(delta: float) -> void:
	_fps = _fps * 0.95 + (1.0 / maxf(delta, 1e-4)) * 0.05
	_stat_timer += delta
	if _stat_timer < 0.25 or not visible or _stats == null:
		return
	_stat_timer = 0.0
	var s: Dictionary = main.layer_stats()
	var draws := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME)
	var prims := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME)
	var p: Vector3 = main.camera.global_position
	var inst: Dictionary = s.instances
	_stats.text = "%d fps | %d draws | %.2f M tris (all passes)\n" % [_fps, draws, prims / 1e6] + \
		"houses %d  trees %d  bushes %d  grass %d\n" % [inst.Houses, inst.Trees, inst.Bushes, inst.Grass] + \
		"parts by LOD  0:%d 1:%d 2:%d 3:%d 4:%d\n" % s.per_lod + \
		"instancing %.2f ms, %d buckets\n" % [s.usec / 1000.0, s.draws] + \
		"pos %.0f, %.0f, %.0f" % [p.x, p.y, p.z]


func _update_speed() -> void:
	_speed.text = "Click to fly   WASD move   Space/Q up/down   Shift boost   wheel speed: %.1f m/s   Esc release mouse" % main.camera.speed


func _slider(parent: Control, text: String, key: String, lo: float, hi: float, step: float, apply: Callable) -> void:
	var row := HBoxContainer.new()
	parent.add_child(row)
	var l := _label(row, text)
	l.custom_minimum_size = Vector2(96, 0)
	var s := HSlider.new()
	s.min_value = lo
	s.max_value = hi
	s.step = step
	s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.value_changed.connect(func(v: float) -> void:
		if not _syncing:
			apply.call(v))
	row.add_child(s)
	_sliders[key] = s


func _check(parent: Control, text: String, key: String, apply: Callable) -> void:
	var c := CheckBox.new()
	c.text = text
	c.toggled.connect(func(on: bool) -> void:
		if not _syncing:
			apply.call(on))
	parent.add_child(c)
	_checks[key] = c


func _panel(root: Control, preset: int, width: int) -> VBoxContainer:
	var panel := PanelContainer.new()
	root.add_child(panel)
	panel.set_anchors_and_offsets_preset(preset, Control.PRESET_MODE_MINSIZE, 16)
	panel.grow_horizontal = Control.GROW_DIRECTION_BEGIN if preset == Control.PRESET_TOP_RIGHT else Control.GROW_DIRECTION_END
	panel.grow_vertical = Control.GROW_DIRECTION_BEGIN if preset == Control.PRESET_BOTTOM_LEFT else Control.GROW_DIRECTION_END
	if width > 0:
		panel.custom_minimum_size = Vector2(width, 0)
	var box := VBoxContainer.new()
	panel.add_child(box)
	_panels.append(panel)
	return box


func _label(parent: Control, text: String, mono := false) -> Label:
	var l := Label.new()
	l.text = text
	if mono:
		var f := SystemFont.new()
		f.font_names = PackedStringArray(["Consolas", "monospace"])
		l.add_theme_font_override("font", f)
		l.add_theme_font_size_override("font_size", 12)
	parent.add_child(l)
	return l


func _caption(parent: Control, text: String) -> void:
	var l := _label(parent, text)
	l.add_theme_font_size_override("font_size", 11)
	l.add_theme_color_override("font_color", Color(INK, 0.75))


func _button(parent: Control, text: String, alt := false) -> Button:
	var b := Button.new()
	b.text = text
	if alt:
		for state in ["normal", "hover", "pressed"]:
			var sb: StyleBoxFlat = b.get_theme_stylebox(state).duplicate()
			sb.bg_color = RUST if state == "normal" else RUST.lightened(0.15)
			b.add_theme_stylebox_override(state, sb)
	parent.add_child(b)
	return b


func _num(v: float) -> String:
	return str(snappedf(v, 0.001))


func _thousands(n: int) -> String:
	var s := str(n)
	var out := ""
	while s.length() > 3:
		out = "," + s.right(3) + out
		s = s.left(s.length() - 3)
	return s + out


func _theme() -> Theme:
	var t := Theme.new()
	var panel := StyleBoxFlat.new()
	panel.bg_color = Color(PAPER, 0.92)
	panel.border_color = INK
	panel.set_border_width_all(2)
	panel.set_corner_radius_all(6)
	panel.set_content_margin_all(10)
	panel.shadow_color = Color(INK, 0.25)
	panel.shadow_offset = Vector2(3, 3)
	t.set_stylebox("panel", "PanelContainer", panel)
	for state in ["normal", "hover", "pressed", "focus"]:
		var b := StyleBoxFlat.new()
		b.bg_color = INK if state == "normal" else INK.lightened(0.15)
		b.set_corner_radius_all(4)
		b.content_margin_left = 8
		b.content_margin_right = 8
		b.content_margin_top = 4
		b.content_margin_bottom = 4
		if state == "focus":
			b.draw_center = false
			b.border_color = RUST
			b.set_border_width_all(2)
		t.set_stylebox(state, "Button", b)
	for state in ["normal", "hover", "pressed", "focus", "hover_pressed"]:
		t.set_stylebox(state, "CheckBox", StyleBoxEmpty.new())
	t.set_color("font_color", "Button", PAPER)
	t.set_color("font_hover_color", "Button", PAPER)
	t.set_color("font_pressed_color", "Button", PAPER)
	t.set_color("font_color", "Label", INK)
	t.set_color("font_color", "CheckBox", INK)
	t.set_color("font_hover_color", "CheckBox", INK)
	t.set_color("font_pressed_color", "CheckBox", INK)
	t.set_font_size("font_size", "Label", 13)
	t.set_font_size("font_size", "Button", 13)
	t.set_font_size("font_size", "CheckBox", 13)
	return t
