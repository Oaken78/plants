class_name FlyCamera
extends Camera3D
## Free-flight camera (web/js/flycam.js). Click to capture the mouse, Esc releases it. WASD moves, Space/E up,
## Q/C/Ctrl down, Shift boosts x5, the mouse wheel changes the cruise speed. It never goes below the terrain.

signal speed_changed(speed: float)

var world: DemoWorld
var speed := 8.0
var yaw := 0.0
var pitch := 0.0

var _velocity := Vector3.ZERO


func _ready() -> void:
	process_priority = -10  # move before the layers pick their LODs for this frame


func look_from(from: Vector3, target: Vector3) -> void:
	position = from
	var d := (target - from).normalized()
	yaw = atan2(-d.x, -d.z)
	pitch = asin(clampf(d.y, -1.0, 1.0))
	_velocity = Vector3.ZERO
	basis = Basis.from_euler(Vector3(pitch, yaw, 0.0))


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed:
		match event.button_index:
			MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT:
				Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			MOUSE_BUTTON_WHEEL_UP:
				speed = minf(speed * 1.2, 400.0)
				speed_changed.emit(speed)
			MOUSE_BUTTON_WHEEL_DOWN:
				speed = maxf(speed / 1.2, 1.0)
				speed_changed.emit(speed)
	elif event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		yaw -= event.relative.x * 0.0022
		pitch = clampf(pitch - event.relative.y * 0.0022, -1.55, 1.55)
	elif event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _process(delta: float) -> void:
	basis = Basis.from_euler(Vector3(pitch, yaw, 0.0))
	var wish := Vector3.ZERO
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		wish -= basis.z
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		wish += basis.z
	if Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
		wish += basis.x
	if Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
		wish -= basis.x
	if Input.is_key_pressed(KEY_SPACE) or Input.is_key_pressed(KEY_E):
		wish.y += 1.0
	if Input.is_key_pressed(KEY_CTRL) or Input.is_key_pressed(KEY_Q) or Input.is_key_pressed(KEY_C):
		wish.y -= 1.0
	var boost := 5.0 if Input.is_key_pressed(KEY_SHIFT) else 1.0
	wish = wish.normalized() * speed * boost
	_velocity = _velocity.lerp(wish, 1.0 - exp(-delta * 7.0))
	position += _velocity * delta
	if world:
		position.y = maxf(position.y, world.terrain_height(position.x, position.z) + 0.35)
