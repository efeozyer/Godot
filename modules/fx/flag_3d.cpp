#include "flag_3d.h"

#include "core/object/class_db.h"

// CFlag::FLAG_WAVE_END: the phase wraps after two turns.
static const float FLAG_WAVE_END = float(Math::TAU * 2.0);

// CFlag::Create: quads along each side.
int Flag3D::_get_segments() const {
	switch (flag_kind) {
		case 0:
			return 6;
		case 1:
			return 12;
		case 2:
			return 1;
		default:
			// The client's 16-bit indices allow 255 at most.
			return CLAMP(int(uint32_t(flag_kind) >> 2), 1, 255);
	}
}

// CFlagMgr::Tick.
float Flag3D::_get_amplitude() const {
	switch (flag_kind) {
		case 0:
		case 1:
			return 0.5f;
		case 2:
			return 0.0f;
		default:
			return float(uint32_t(flag_kind)) / 100.0f;
	}
}

// CFlag::BuildFlatVertices and BuildIndices.
void Flag3D::_rebuild() {
	const int segments = _get_segments();
	const int stride = segments + 1;
	const float amplitude = _get_amplitude();

	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedVector2Array uv2s;
	vertices.resize(stride * stride);
	normals.resize(stride * stride);
	uvs.resize(stride * stride);
	uv2s.resize(stride * stride);
	Vector3 *vertices_ptr = vertices.ptrw();
	Vector3 *normals_ptr = normals.ptrw();
	Vector2 *uvs_ptr = uvs.ptrw();
	Vector2 *uv2s_ptr = uv2s.ptrw();
	for (int j = 0; j < stride; j++) {
		for (int i = 0; i < stride; i++) {
			float u = float(i) / segments;
			if (i == segments) {
				// The free edge stops just short of the texture's edge.
				u -= 0.005f;
			}
			const float v = float(j) / segments;
			const int index = i + stride * j;
			vertices_ptr[index] = Vector3(u * FLAG_WIDTH, v * FLAG_HEIGHT, 0.0f);
			normals_ptr[index] = Vector3(0.0f, 0.0f, 1.0f);
			uvs_ptr[index] = Vector2(u, 1.0f - v);
			// The material displaces each column by its index.
			uv2s_ptr[index] = Vector2(float(i), float(j));
		}
	}

	PackedInt32Array indices;
	indices.resize(segments * segments * 6);
	int *indices_ptr = indices.ptrw();
	int cursor = 0;
	for (int i = 0; i < segments; i++) {
		for (int j = 0; j < segments; j++) {
			const int bottom = i + j * stride;
			const int top = i + (j + 1) * stride;
			// The client's triangles with two corners exchanged, as the Z axis is mirrored.
			indices_ptr[cursor++] = top;
			indices_ptr[cursor++] = bottom;
			indices_ptr[cursor++] = bottom + 1;

			indices_ptr[cursor++] = top;
			indices_ptr[cursor++] = bottom + 1;
			indices_ptr[cursor++] = top + 1;
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_TEX_UV2] = uv2s;
	arrays[Mesh::ARRAY_INDEX] = indices;

	mesh->clear_surfaces();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	mesh->surface_set_material(0, material);

	// The wave moves the free edge by up to the amplitude (times segments / columns) either way.
	aabb = AABB(Vector3(0.0f, 0.0f, -amplitude), Vector3(FLAG_WIDTH, FLAG_HEIGHT, amplitude * 2.0f));
	mesh->set_custom_aabb(aabb);

	material->set_columns(stride);
	material->set_amplitude(amplitude);
}

void Flag3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(true);
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			// CFlagMgr::Tick: a hidden banner and a banner of kind 2 keep their phase.
			if (flag_kind == 2 || !is_visible_in_tree()) {
				return;
			}
			float step = wave_speed * float(get_process_delta_time());
			step *= flag_kind == 1 ? 0.034f : 0.02f;
			wave_phase += step;
			if (wave_phase > FLAG_WAVE_END) {
				wave_phase -= FLAG_WAVE_END;
			}
			material->set_wave_phase(wave_phase);
		} break;
	}
}

void Flag3D::set_flag_id(const String &p_id) {
	flag_id = p_id;
}

String Flag3D::get_flag_id() const {
	return flag_id;
}

void Flag3D::set_flag_kind(int p_kind) {
	if (flag_kind == p_kind) {
		return;
	}
	flag_kind = p_kind;
	_rebuild();
}

int Flag3D::get_flag_kind() const {
	return flag_kind;
}

void Flag3D::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	material->set_texture(texture);
	// CFlag::Render draws nothing without a texture.
	set_base(texture.is_valid() ? mesh->get_rid() : RID());
}

Ref<Texture2D> Flag3D::get_texture() const {
	return texture;
}

void Flag3D::set_flag_transparency(FlagMaterial::Transparency p_transparency) {
	ERR_FAIL_INDEX(p_transparency, FlagMaterial::TRANSPARENCY_MAX);
	transparency = p_transparency;
	material->set_transparency(transparency);
}

FlagMaterial::Transparency Flag3D::get_flag_transparency() const {
	return transparency;
}

void Flag3D::set_wave_speed(float p_speed) {
	wave_speed = p_speed;
}

float Flag3D::get_wave_speed() const {
	return wave_speed;
}

void Flag3D::set_wave_phase(float p_phase) {
	wave_phase = p_phase;
	material->set_wave_phase(wave_phase);
}

float Flag3D::get_wave_phase() const {
	return wave_phase;
}

AABB Flag3D::get_aabb() const {
	return aabb;
}

void Flag3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_flag_id", "id"), &Flag3D::set_flag_id);
	ClassDB::bind_method(D_METHOD("get_flag_id"), &Flag3D::get_flag_id);
	ClassDB::bind_method(D_METHOD("set_flag_kind", "kind"), &Flag3D::set_flag_kind);
	ClassDB::bind_method(D_METHOD("get_flag_kind"), &Flag3D::get_flag_kind);
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &Flag3D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &Flag3D::get_texture);
	ClassDB::bind_method(D_METHOD("set_flag_transparency", "transparency"), &Flag3D::set_flag_transparency);
	ClassDB::bind_method(D_METHOD("get_flag_transparency"), &Flag3D::get_flag_transparency);
	ClassDB::bind_method(D_METHOD("set_wave_speed", "speed"), &Flag3D::set_wave_speed);
	ClassDB::bind_method(D_METHOD("get_wave_speed"), &Flag3D::get_wave_speed);
	ClassDB::bind_method(D_METHOD("set_wave_phase", "phase"), &Flag3D::set_wave_phase);
	ClassDB::bind_method(D_METHOD("get_wave_phase"), &Flag3D::get_wave_phase);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "flag_id"), "set_flag_id", "get_flag_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flag_kind"), "set_flag_kind", "get_flag_kind");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flag_transparency", PROPERTY_HINT_ENUM, "Disabled,Alpha"), "set_flag_transparency", "get_flag_transparency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wave_speed"), "set_wave_speed", "get_wave_speed");
}

Flag3D::Flag3D() {
	mesh.instantiate();
	material.instantiate();
	_rebuild();
}

Flag3D::~Flag3D() {
}
