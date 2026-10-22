class_name HostModel
## Loads the host models (web/assets/house.glb and towers.glb, shared with the web demo) at runtime and prepares one
## LOD node of them twice:
## - a render mesh: every surface merged into one, so a house is one draw call, with the material id in CUSTOM0.r;
## - a host for the plant generator: positions, indices and one SurfaceTag per triangle.
## Material names carry the growth tags, as in the web demo: "window" and "door" are no-grow, the rest is solid.

const MATERIALS := ["concrete", "concrete_dark", "trim", "rust", "sign", "window", "door"]
const NO_GROW := ["window", "door"]


## Mesh compression stays off, so the host is byte for byte the one the web demo builds: quantised positions (4.08 m
## read back as 4.07986 m) would change where every plant grows.
static func load_scene(path: String) -> Node:
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_file(path, state, GLTFDocument.IMPORT_FLAG_FORCE_DISABLE_MESH_COMPRESSION)
	if err != OK:
		push_error("Cannot load %s: %s" % [path, error_string(err)])
		return null
	return doc.generate_scene(state)


## Linear albedo of each material id, read from the model's own materials.
static func material_colors(root: Node) -> PackedVector3Array:
	var colors := PackedVector3Array()
	colors.resize(MATERIALS.size())
	for mi in _mesh_nodes(root):
		var mesh := _mesh_of(mi)
		for s in mesh.get_surface_count():
			var m := mesh.surface_get_material(s) as BaseMaterial3D
			var id := MATERIALS.find(_material_name(mesh, s))
			if m and id >= 0:
				var c := m.albedo_color.srgb_to_linear()
				colors[id] = Vector3(c.r, c.g, c.b)
	return colors


## Merges the meshes under node_name. Returns {mesh, positions, indices, tags}: mesh for rendering (Godot's clockwise
## winding), and the host arrays for PlantGenerator.set_host. The core takes counter-clockwise triangles, as glTF and
## three.js store them, so the host indices are flipped back.
static func merge(root: Node, node_name: String) -> Dictionary:
	var node := root.find_child(node_name, true, false)
	assert(node != null, "No node %s in the model" % node_name)
	var pos := PackedVector3Array()
	var nrm := PackedVector3Array()
	var ids := PackedByteArray()
	var idx := PackedInt32Array()
	var host_idx := PackedInt32Array()
	var tags := PackedByteArray()
	for mi in _mesh_nodes(node):
		var xf := _relative_transform(mi, node)
		var mesh := _mesh_of(mi)
		for s in mesh.get_surface_count():
			var arrays := mesh.surface_get_arrays(s)
			var name := _material_name(mesh, s)
			var id := maxi(0, MATERIALS.find(name))
			var tag := PlantGenerator.TAG_NO_GROW if name in NO_GROW else PlantGenerator.TAG_SOLID
			var base := pos.size()
			var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var norms: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
			for i in verts.size():
				pos.append(xf * verts[i])
				nrm.append((xf.basis * norms[i]).normalized())
				ids.append_array([id, 0, 0, 0])
			var si: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
			if si.is_empty():
				si = PackedInt32Array(range(verts.size()))
			for i in range(0, si.size(), 3):
				idx.append_array([base + si[i], base + si[i + 1], base + si[i + 2]])
				host_idx.append_array([base + si[i], base + si[i + 2], base + si[i + 1]])
				tags.append(tag)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = pos
	arrays[Mesh.ARRAY_NORMAL] = nrm
	arrays[Mesh.ARRAY_CUSTOM0] = ids
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	var format := Mesh.ARRAY_CUSTOM_RGBA8_UNORM << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, format)
	return {"mesh": mesh, "positions": pos, "indices": host_idx, "tags": tags}


## Host plus a flat ground patch around it, margin metres wide, for bushes, trees and roots (withGroundPatch() in
## main.js).
static func with_ground_patch(host: Dictionary, margin: float) -> Dictionary:
	var pos: PackedVector3Array = host.positions.duplicate()
	var lo := Vector2(INF, INF)
	var hi := Vector2(-INF, -INF)
	for p in pos:
		lo = Vector2(minf(lo.x, p.x), minf(lo.y, p.z))
		hi = Vector2(maxf(hi.x, p.x), maxf(hi.y, p.z))
	lo -= Vector2(margin, margin)
	hi += Vector2(margin, margin)
	var n := pos.size()
	pos.append_array([Vector3(lo.x, 0, lo.y), Vector3(hi.x, 0, lo.y), Vector3(hi.x, 0, hi.y), Vector3(lo.x, 0, hi.y)])
	var idx: PackedInt32Array = host.indices.duplicate()
	idx.append_array([n, n + 3, n + 2, n, n + 2, n + 1])
	var tags: PackedByteArray = host.tags.duplicate()
	tags.append_array([PlantGenerator.TAG_GROUND, PlantGenerator.TAG_GROUND])
	return {"positions": pos, "indices": idx, "tags": tags}


static func _mesh_nodes(node: Node) -> Array:
	var out := []
	if node is MeshInstance3D or node is ImporterMeshInstance3D:
		out.append(node)
	for c in node.get_children():
		out.append_array(_mesh_nodes(c))
	return out


static func _mesh_of(node: Node) -> Mesh:
	if node is ImporterMeshInstance3D:
		return node.mesh.get_mesh()
	return node.mesh


static func _material_name(mesh: Mesh, surface: int) -> String:
	var m := mesh.surface_get_material(surface)
	return m.resource_name if m else ""


## The scene is not in the tree, so global transforms are not available: compose the local ones up to root.
static func _relative_transform(node: Node, root: Node) -> Transform3D:
	var xf := Transform3D.IDENTITY
	var n := node
	while n != root and n != null:
		if n is Node3D:
			xf = n.transform * xf
		n = n.get_parent()
	return xf
