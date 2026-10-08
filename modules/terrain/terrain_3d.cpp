#include "terrain_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/rendering/rendering_server.h"

void Terrain3D::_data_resized() {
	_create_chunks();
	_update_tile_texture();
	_update_weight_textures();
	_apply_material();
	update_collision();
	update_gizmos();
}

void Terrain3D::_data_region_changed(int p_channels, const Rect2i &p_rect) {
	if (data.is_null()) {
		return;
	}
	if ((p_channels & TerrainData::CHANNEL_HEIGHT) && chunks_per_side > 0) {
		// A sample is shared by the chunks on both sides of it, and its neighbours' normals
		// depend on it.
		const Rect2i rect = p_rect.grow(1);
		const int x0 = CLAMP((rect.position.x - 1) / chunk_cells, 0, chunks_per_side - 1);
		const int z0 = CLAMP((rect.position.y - 1) / chunk_cells, 0, chunks_per_side - 1);
		const int x1 = CLAMP((rect.position.x + rect.size.x) / chunk_cells, 0, chunks_per_side - 1);
		const int z1 = CLAMP((rect.position.y + rect.size.y) / chunk_cells, 0, chunks_per_side - 1);
		for (int z = z0; z <= z1; z++) {
			for (int x = x0; x <= x1; x++) {
				_build_chunk(x, z);
			}
		}
	}
	if (p_channels & TerrainData::CHANNEL_TILES) {
		_update_tile_texture();
	}
	if (p_channels & TerrainData::CHANNEL_WEIGHTS) {
		_update_weight_textures();
	}
}

void Terrain3D::_clear_chunks() {
	for (Chunk &chunk : chunks) {
		if (chunk.instance.is_valid()) {
			RS::get_singleton()->free_rid(chunk.instance);
		}
	}
	chunks.clear();
	chunks_per_side = 0;
}

void Terrain3D::_create_chunks() {
	_clear_chunks();
	if (data.is_null() || data->get_cells() < 1) {
		return;
	}
	chunks_per_side = (data->get_cells() + chunk_cells - 1) / chunk_cells;
	chunks.resize(chunks_per_side * chunks_per_side);
	for (int z = 0; z < chunks_per_side; z++) {
		for (int x = 0; x < chunks_per_side; x++) {
			Chunk &chunk = chunks.write[z * chunks_per_side + x];
			chunk.mesh.instantiate();
			chunk.instance = RS::get_singleton()->instance_create();
			RS::get_singleton()->instance_set_base(chunk.instance, chunk.mesh->get_rid());
			RS::get_singleton()->instance_attach_object_instance_id(chunk.instance, get_instance_id());
			_build_chunk(x, z);
		}
	}
	_update_instances();
	_apply_material();
}

void Terrain3D::_build_chunk(int p_chunk_x, int p_chunk_z) {
	Chunk &chunk = chunks.write[p_chunk_z * chunks_per_side + p_chunk_x];
	const int size = data->get_size();
	const float cell = data->get_cell_size();
	const int x0 = p_chunk_x * chunk_cells;
	const int z0 = p_chunk_z * chunk_cells;
	const int nx = MIN(chunk_cells, size - 1 - x0);
	const int nz = MIN(chunk_cells, size - 1 - z0);
	const float *heights = data->heights_ptr();

	Vector<Vector3> positions;
	Vector<Vector3> normals;
	Vector<int> indices;
	positions.resize((nx + 1) * (nz + 1));
	normals.resize(positions.size());
	indices.resize(nx * nz * 6);
	Vector3 *pw = positions.ptrw();
	Vector3 *nw = normals.ptrw();
	int *iw = indices.ptrw();

	for (int z = 0; z <= nz; z++) {
		for (int x = 0; x <= nx; x++) {
			const int gx = x0 + x;
			const int gz = z0 + z;
			pw[z * (nx + 1) + x] = Vector3(gx * cell, heights[gz * size + gx], -gz * cell);
			nw[z * (nx + 1) + x] = data->get_normal(gx, gz);
		}
	}
	for (int z = 0; z < nz; z++) {
		for (int x = 0; x < nx; x++) {
			const int a = z * (nx + 1) + x;
			const int b = a + 1;
			const int c = a + nx + 1;
			const int d = c + 1;
			*iw++ = a;
			*iw++ = c;
			*iw++ = d;
			*iw++ = a;
			*iw++ = d;
			*iw++ = b;
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = positions;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_INDEX] = indices;
	chunk.mesh->clear_surfaces();
	chunk.mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
}

void Terrain3D::_update_instances() {
	if (!is_inside_tree()) {
		return;
	}
	const RID scenario = get_world_3d()->get_scenario();
	const Transform3D xform = get_global_transform();
	const bool visible = is_visible_in_tree();
	for (const Chunk &chunk : chunks) {
		RS::get_singleton()->instance_set_scenario(chunk.instance, scenario);
		RS::get_singleton()->instance_set_transform(chunk.instance, xform);
		RS::get_singleton()->instance_set_visible(chunk.instance, visible);
	}
}

RID Terrain3D::_get_material_rid() const {
	if (mode == MODE_TILES) {
		if (tile_material.is_valid() && tile_material->get_tile_textures().is_valid()) {
			return tile_material->get_rid();
		}
	} else if (layer_material.is_valid()) {
		return layer_material->get_rid();
	}
	return default_material->get_rid();
}

void Terrain3D::_apply_material() {
	if (data.is_valid()) {
		if (tile_material.is_valid()) {
			tile_material->set_map_cells(data->get_cells());
			tile_material->set_cell_size(data->get_cell_size());
			tile_material->set_cell_map(tile_texture);
		}
		// Without weights the default material draws its first layer everywhere.
		default_material->set_terrain_dimensions(data->get_size(), data->get_cell_size());
		if (layer_material.is_valid()) {
			layer_material->set_terrain_dimensions(data->get_size(), data->get_cell_size());
			for (int i = 0; i < TerrainData::LAYER_COUNT / 4; i++) {
				layer_material->set_weight_texture(i, weight_textures[i].is_valid() ? weight_textures[i]->get_rid() : RID());
			}
		}
	}
	const RID material = _get_material_rid();
	for (const Chunk &chunk : chunks) {
		RS::get_singleton()->instance_geometry_set_material_override(chunk.instance, material);
	}
}

void Terrain3D::_update_tile_texture() {
	if (data.is_null()) {
		tile_texture.unref();
		return;
	}
	const Ref<Image> image = data->create_tile_image();
	if (image.is_null()) {
		return;
	}
	if (tile_texture.is_valid() && tile_texture->get_size() == Vector2(image->get_size())) {
		tile_texture->update(image);
	} else {
		tile_texture = ImageTexture::create_from_image(image);
		if (tile_material.is_valid()) {
			tile_material->set_cell_map(tile_texture);
		}
	}
}

void Terrain3D::_update_weight_textures() {
	for (int i = 0; i < TerrainData::LAYER_COUNT / 4; i++) {
		if (data.is_null()) {
			weight_textures[i].unref();
			continue;
		}
		const Ref<Image> image = data->create_weight_image(i);
		if (image.is_null()) {
			continue;
		}
		if (weight_textures[i].is_valid() && weight_textures[i]->get_size() == Vector2(image->get_size())) {
			weight_textures[i]->update(image);
		} else {
			weight_textures[i] = ImageTexture::create_from_image(image);
			if (layer_material.is_valid()) {
				layer_material->set_weight_texture(i, weight_textures[i]->get_rid());
			}
		}
	}
}

void Terrain3D::_clear_collision() {
	if (body.is_valid()) {
		PhysicsServer3D::get_singleton()->free_rid(body);
		body = RID();
	}
	shape.unref();
}

void Terrain3D::_create_collision() {
	_clear_collision();
	// Nothing collides with the terrain while it is being edited.
	if (!collision_enabled || data.is_null() || !is_inside_tree() || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	const int size = data->get_size();
	const float cell = data->get_cell_size();
	const float *heights = data->heights_ptr();

	// The shape's rows run along +Z, which is the far edge of the grid first.
	Vector<real_t> map;
	map.resize(size * size);
	real_t *mw = map.ptrw();
	for (int row = 0; row < size; row++) {
		const float *src = heights + (size - 1 - row) * size;
		for (int x = 0; x < size; x++) {
			mw[row * size + x] = src[x];
		}
	}
	shape.instantiate();
	shape->set_map_width(size);
	shape->set_map_depth(size);
	shape->set_map_data(map);

	// The shape is centred on its origin with one unit between samples.
	const float half = (size - 1) * cell * 0.5f;
	const Transform3D shape_xform(Basis().scaled(Vector3(cell, 1.0, cell)), Vector3(half, 0.0, -half));

	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	body = ps->body_create();
	ps->body_set_mode(body, PS3DE::BODY_MODE_STATIC);
	ps->body_add_shape(body, shape->get_rid(), shape_xform);
	ps->body_set_collision_layer(body, collision_layer);
	ps->body_set_collision_mask(body, collision_mask);
	ps->body_attach_object_instance_id(body, get_instance_id());
	ps->body_set_state(body, PS3DE::BODY_STATE_TRANSFORM, get_global_transform());
	ps->body_set_space(body, get_world_3d()->get_space());
}

void Terrain3D::update_collision() {
	_create_collision();
}

void Terrain3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			_update_instances();
			_create_collision();
		} break;

		case NOTIFICATION_EXIT_WORLD: {
			for (const Chunk &chunk : chunks) {
				RS::get_singleton()->instance_set_scenario(chunk.instance, RID());
			}
			_clear_collision();
		} break;

		case NOTIFICATION_TRANSFORM_CHANGED: {
			const Transform3D xform = get_global_transform();
			for (const Chunk &chunk : chunks) {
				RS::get_singleton()->instance_set_transform(chunk.instance, xform);
			}
			if (body.is_valid()) {
				PhysicsServer3D::get_singleton()->body_set_state(body, PS3DE::BODY_STATE_TRANSFORM, xform);
			}
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			const bool visible = is_visible_in_tree();
			for (const Chunk &chunk : chunks) {
				RS::get_singleton()->instance_set_visible(chunk.instance, visible);
			}
		} break;
	}
}

void Terrain3D::set_data(const Ref<TerrainData> &p_data) {
	if (data == p_data) {
		return;
	}
	if (data.is_valid()) {
		data->disconnect(SNAME("resized"), callable_mp(this, &Terrain3D::_data_resized));
		data->disconnect(SNAME("region_changed"), callable_mp(this, &Terrain3D::_data_region_changed));
	}
	data = p_data;
	if (data.is_valid()) {
		data->connect(SNAME("resized"), callable_mp(this, &Terrain3D::_data_resized));
		data->connect(SNAME("region_changed"), callable_mp(this, &Terrain3D::_data_region_changed));
	}
	tile_texture.unref();
	for (int i = 0; i < TerrainData::LAYER_COUNT / 4; i++) {
		weight_textures[i].unref();
	}
	_data_resized();
}

Ref<TerrainData> Terrain3D::get_data() const {
	return data;
}

void Terrain3D::set_mode(Mode p_mode) {
	mode = p_mode;
	_apply_material();
}

Terrain3D::Mode Terrain3D::get_mode() const {
	return mode;
}

void Terrain3D::set_tile_material(const Ref<TerrainMaterial> &p_material) {
	// The terrain shows its checkerboard until the material has tile textures.
	const Callable changed = callable_mp(this, &Terrain3D::_apply_material);
	if (tile_material.is_valid()) {
		tile_material->disconnect_changed(changed);
	}
	tile_material = p_material;
	if (tile_material.is_valid()) {
		tile_material->connect_changed(changed);
	}
	_apply_material();
}

Ref<TerrainMaterial> Terrain3D::get_tile_material() const {
	return tile_material;
}

void Terrain3D::set_layer_material(const Ref<TerrainLayerMaterial> &p_material) {
	layer_material = p_material;
	_apply_material();
}

Ref<TerrainLayerMaterial> Terrain3D::get_layer_material() const {
	return layer_material;
}

void Terrain3D::set_chunk_cells(int p_cells) {
	p_cells = CLAMP(p_cells, 4, 128);
	if (p_cells == chunk_cells) {
		return;
	}
	chunk_cells = p_cells;
	_create_chunks();
}

int Terrain3D::get_chunk_cells() const {
	return chunk_cells;
}

void Terrain3D::set_collision_enabled(bool p_enabled) {
	collision_enabled = p_enabled;
	_create_collision();
}

bool Terrain3D::is_collision_enabled() const {
	return collision_enabled;
}

void Terrain3D::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	if (body.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_layer(body, collision_layer);
	}
}

uint32_t Terrain3D::get_collision_layer() const {
	return collision_layer;
}

void Terrain3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	if (body.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_mask(body, collision_mask);
	}
}

uint32_t Terrain3D::get_collision_mask() const {
	return collision_mask;
}

Vector2 Terrain3D::to_grid(const Vector3 &p_local_position) const {
	const float cell = data.is_valid() ? data->get_cell_size() : 1.0f;
	return Vector2(p_local_position.x / cell, -p_local_position.z / cell);
}

float Terrain3D::get_height_at(const Vector3 &p_global_position) const {
	if (data.is_null()) {
		return 0.0;
	}
	const Transform3D xform = is_inside_tree() ? get_global_transform() : get_transform();
	const Vector3 local = xform.affine_inverse().xform(p_global_position);
	return xform.xform(Vector3(local.x, data->sample_height(to_grid(local)), local.z)).y;
}

bool Terrain3D::intersect_ray(const Vector3 &p_from, const Vector3 &p_direction, Vector3 &r_position) const {
	if (data.is_null() || data->get_cells() < 1) {
		return false;
	}
	const Transform3D inverse = (is_inside_tree() ? get_global_transform() : get_transform()).affine_inverse();
	const Vector3 from = inverse.xform(p_from);
	const Vector3 dir = inverse.basis.xform(p_direction).normalized();
	const float cell = data->get_cell_size();
	const float extent = data->get_cells() * cell;

	// Only the stretch of the ray over the terrain needs walking.
	float t0 = 0.0;
	float t1 = 100000.0;
	const float lo[2] = { 0.0f, -extent };
	const float hi[2] = { extent, 0.0f };
	const float o[2] = { from.x, from.z };
	const float d[2] = { dir.x, dir.z };
	for (int axis = 0; axis < 2; axis++) {
		if (Math::abs(d[axis]) < 1e-6f) {
			if (o[axis] < lo[axis] || o[axis] > hi[axis]) {
				return false;
			}
			continue;
		}
		float a = (lo[axis] - o[axis]) / d[axis];
		float b = (hi[axis] - o[axis]) / d[axis];
		if (a > b) {
			SWAP(a, b);
		}
		t0 = MAX(t0, a);
		t1 = MIN(t1, b);
	}
	if (t0 > t1) {
		return false;
	}

	const float step = cell * 0.5f;
	float previous = t0;
	Vector3 p = from + dir * t0;
	if (p.y < data->sample_height(to_grid(p))) {
		// The ray starts under the ground.
		return false;
	}
	for (float t = t0 + step; t <= t1 + step; t += step) {
		p = from + dir * t;
		if (p.y <= data->sample_height(to_grid(p))) {
			float a = previous;
			float b = t;
			for (int i = 0; i < 16; i++) {
				const float mid = (a + b) * 0.5f;
				const Vector3 q = from + dir * mid;
				if (q.y <= data->sample_height(to_grid(q))) {
					b = mid;
				} else {
					a = mid;
				}
			}
			const Vector3 hit = from + dir * b;
			r_position = (is_inside_tree() ? get_global_transform() : get_transform()).xform(Vector3(hit.x, data->sample_height(to_grid(hit)), hit.z));
			return true;
		}
		previous = t;
	}
	return false;
}

Variant Terrain3D::intersect_ray_bind(const Vector3 &p_from, const Vector3 &p_direction) const {
	Vector3 position;
	if (intersect_ray(p_from, p_direction, position)) {
		return position;
	}
	return Variant();
}

void Terrain3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_data", "data"), &Terrain3D::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &Terrain3D::get_data);
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &Terrain3D::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &Terrain3D::get_mode);
	ClassDB::bind_method(D_METHOD("set_tile_material", "material"), &Terrain3D::set_tile_material);
	ClassDB::bind_method(D_METHOD("get_tile_material"), &Terrain3D::get_tile_material);
	ClassDB::bind_method(D_METHOD("set_layer_material", "material"), &Terrain3D::set_layer_material);
	ClassDB::bind_method(D_METHOD("get_layer_material"), &Terrain3D::get_layer_material);
	ClassDB::bind_method(D_METHOD("set_chunk_cells", "cells"), &Terrain3D::set_chunk_cells);
	ClassDB::bind_method(D_METHOD("get_chunk_cells"), &Terrain3D::get_chunk_cells);
	ClassDB::bind_method(D_METHOD("set_collision_enabled", "enabled"), &Terrain3D::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("is_collision_enabled"), &Terrain3D::is_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &Terrain3D::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &Terrain3D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &Terrain3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &Terrain3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("update_collision"), &Terrain3D::update_collision);
	ClassDB::bind_method(D_METHOD("get_height_at", "global_position"), &Terrain3D::get_height_at);
	ClassDB::bind_method(D_METHOD("intersect_ray", "from", "direction"), &Terrain3D::intersect_ray_bind);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "data", PROPERTY_HINT_RESOURCE_TYPE, TerrainData::get_class_static()), "set_data", "get_data");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Tiles,Layers"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tile_material", PROPERTY_HINT_RESOURCE_TYPE, TerrainMaterial::get_class_static()), "set_tile_material", "get_tile_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "layer_material", PROPERTY_HINT_RESOURCE_TYPE, TerrainLayerMaterial::get_class_static()), "set_layer_material", "get_layer_material");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "chunk_cells", PROPERTY_HINT_RANGE, "4,128,1"), "set_chunk_cells", "get_chunk_cells");
	ADD_GROUP("Collision", "collision_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"), "set_collision_enabled", "is_collision_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");

	BIND_ENUM_CONSTANT(MODE_TILES);
	BIND_ENUM_CONSTANT(MODE_LAYERS);
}

Terrain3D::Terrain3D() {
	default_material.instantiate();
	set_notify_transform(true);
}

Terrain3D::~Terrain3D() {
	_clear_chunks();
	_clear_collision();
}
