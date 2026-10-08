#include "effect_quad_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex EffectQuadMaterial::shader_mutex;
HashMap<int, RID> EffectQuadMaterial::shaders;

int EffectQuadMaterial::_get_shader_key() const {
	if (blend_mode != BLEND_MODE_CUSTOM) {
		return blend_mode;
	}
	return BLEND_MODE_CUSTOM + blend_src_factor * BLEND_FACTOR_MAX + blend_dst_factor;
}

void EffectQuadMaterial::_update_shader() {
	MutexLock shader_lock(shader_mutex);

	const int key = _get_shader_key();
	RID *existing = shaders.getptr(key);
	if (existing) {
		RS::get_singleton()->material_set_shader(_get_material(), *existing);
		return;
	}

	// The variants differ only in how the quad is blended with what is behind it.
	static const char *factor_names[BLEND_FACTOR_MAX] = { "zero", "one", "src_color", "inv_src_color", "src_alpha", "inv_src_alpha" };
	String code = "shader_type spatial;\nrender_mode ";
	switch (blend_mode) {
		case BLEND_MODE_ADD: {
			code += "blend_add";
		} break;
		case BLEND_MODE_MIX: {
			code += "blend_mix";
		} break;
		case BLEND_MODE_CUSTOM: {
			code += vformat("blend_src_%s, blend_dst_%s", factor_names[blend_src_factor], factor_names[blend_dst_factor]);
		} break;
	}
	code += R"(, unshaded, cull_disabled, depth_draw_never;

uniform int facing = 0; // 0 = camera, 1 = camera around Y only, 2 = fixed.
uniform mat3 fixed_basis = mat3(1.0);
uniform mat3 corner_basis = mat3(1.0);
uniform float camera_offset = 0.0;
uniform vec4 color : source_color = vec4(1.0);
uniform vec2 atlas_size = vec2(1.0, 1.0);
uniform sampler2D atlas_texture : source_color, repeat_disable;
uniform bool per_particle = false;
uniform float spin_speed = 0.0; // Radians per second, per particle.
uniform float angle = 0.0;
uniform float frame = 0.0;

void vertex() {
	// Particles carry their flipbook frame in INSTANCE_CUSTOM.z and their age in INSTANCE_CUSTOM.w.
	float quad_angle = per_particle ? INSTANCE_CUSTOM.w * spin_speed : angle;
	float quad_frame = per_particle ? INSTANCE_CUSTOM.z : frame;

	vec3 corner = corner_basis * vec3(VERTEX.xy * vec2(length(MODEL_MATRIX[0].xyz), length(MODEL_MATRIX[1].xyz)), 0.0);
	float c = cos(quad_angle);
	float s = sin(quad_angle);
	corner.xy = vec2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);

	mat3 face = fixed_basis;
	if (facing == 0) {
		face = mat3(MAIN_CAM_INV_VIEW_MATRIX);
	} else if (facing == 1) {
		face = mat3(
				normalize(cross(vec3(0.0, 1.0, 0.0), MAIN_CAM_INV_VIEW_MATRIX[2].xyz)),
				vec3(0.0, 1.0, 0.0),
				normalize(cross(MAIN_CAM_INV_VIEW_MATRIX[0].xyz, vec3(0.0, 1.0, 0.0))));
	}

	vec3 center = MODEL_MATRIX[3].xyz;
	if (camera_offset != 0.0) {
		// Pull the quad towards the camera, stopping just short of it.
		vec3 to_camera = CAMERA_POSITION_WORLD - center;
		float distance_to_camera = length(to_camera);
		if (distance_to_camera > 0.0001) {
			vec3 pull = to_camera / distance_to_camera * min(camera_offset, max(distance_to_camera - 0.3, 0.0));
			if (facing == 1) {
				pull.y = 0.0;
			}
			center += pull;
		}
	}

	VERTEX = corner;
	MODELVIEW_MATRIX = VIEW_MATRIX * mat4(vec4(face[0], 0.0), vec4(face[1], 0.0), vec4(face[2], 0.0), vec4(center, 1.0));
	MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);

	UV = (UV + vec2(mod(quad_frame, atlas_size.x), floor(quad_frame / atlas_size.x))) / atlas_size;
}

void fragment() {
	vec4 tex = texture(atlas_texture, UV);
	ALBEDO = tex.rgb * COLOR.rgb * color.rgb;
	ALPHA = tex.a * COLOR.a * color.a;
}
)";

	RID shader = RS::get_singleton()->shader_create();
	RS::get_singleton()->shader_set_code(shader, code);
	shaders.insert(key, shader);
	RS::get_singleton()->material_set_shader(_get_material(), shader);
}

void EffectQuadMaterial::set_blend_mode(BlendMode p_mode) {
	ERR_FAIL_INDEX(p_mode, BLEND_MODE_CUSTOM + 1);
	blend_mode = p_mode;
	_update_shader();
	notify_property_list_changed();
}

EffectQuadMaterial::BlendMode EffectQuadMaterial::get_blend_mode() const {
	return blend_mode;
}

void EffectQuadMaterial::set_blend_src_factor(BlendFactor p_factor) {
	ERR_FAIL_INDEX(p_factor, BLEND_FACTOR_MAX);
	blend_src_factor = p_factor;
	_update_shader();
}

EffectQuadMaterial::BlendFactor EffectQuadMaterial::get_blend_src_factor() const {
	return blend_src_factor;
}

void EffectQuadMaterial::set_blend_dst_factor(BlendFactor p_factor) {
	ERR_FAIL_INDEX(p_factor, BLEND_FACTOR_MAX);
	blend_dst_factor = p_factor;
	_update_shader();
}

EffectQuadMaterial::BlendFactor EffectQuadMaterial::get_blend_dst_factor() const {
	return blend_dst_factor;
}

void EffectQuadMaterial::set_facing(Facing p_facing) {
	ERR_FAIL_INDEX(p_facing, FACING_FIXED + 1);
	facing = p_facing;
	RS::get_singleton()->material_set_param(_get_material(), "facing", int(facing));
	notify_property_list_changed();
}

EffectQuadMaterial::Facing EffectQuadMaterial::get_facing() const {
	return facing;
}

void EffectQuadMaterial::set_fixed_basis(const Basis &p_basis) {
	fixed_basis = p_basis;
	RS::get_singleton()->material_set_param(_get_material(), "fixed_basis", fixed_basis);
}

Basis EffectQuadMaterial::get_fixed_basis() const {
	return fixed_basis;
}

void EffectQuadMaterial::set_corner_basis(const Basis &p_basis) {
	corner_basis = p_basis;
	RS::get_singleton()->material_set_param(_get_material(), "corner_basis", corner_basis);
}

Basis EffectQuadMaterial::get_corner_basis() const {
	return corner_basis;
}

void EffectQuadMaterial::set_camera_offset(float p_offset) {
	camera_offset = p_offset;
	RS::get_singleton()->material_set_param(_get_material(), "camera_offset", camera_offset);
}

float EffectQuadMaterial::get_camera_offset() const {
	return camera_offset;
}

void EffectQuadMaterial::set_color(const Color &p_color) {
	color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "color", color);
}

Color EffectQuadMaterial::get_color() const {
	return color;
}

void EffectQuadMaterial::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "atlas_texture", tex_rid);
}

Ref<Texture2D> EffectQuadMaterial::get_texture() const {
	return texture;
}

void EffectQuadMaterial::set_h_frames(int p_frames) {
	h_frames = MAX(p_frames, 1);
	RS::get_singleton()->material_set_param(_get_material(), "atlas_size", Vector2(h_frames, v_frames));
}

int EffectQuadMaterial::get_h_frames() const {
	return h_frames;
}

void EffectQuadMaterial::set_v_frames(int p_frames) {
	v_frames = MAX(p_frames, 1);
	RS::get_singleton()->material_set_param(_get_material(), "atlas_size", Vector2(h_frames, v_frames));
}

int EffectQuadMaterial::get_v_frames() const {
	return v_frames;
}

void EffectQuadMaterial::set_per_particle(bool p_enable) {
	per_particle = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "per_particle", per_particle);
	notify_property_list_changed();
}

bool EffectQuadMaterial::is_per_particle() const {
	return per_particle;
}

void EffectQuadMaterial::set_spin_speed(float p_speed) {
	spin_speed = p_speed;
	RS::get_singleton()->material_set_param(_get_material(), "spin_speed", spin_speed);
}

float EffectQuadMaterial::get_spin_speed() const {
	return spin_speed;
}

void EffectQuadMaterial::set_angle(float p_angle) {
	angle = p_angle;
	RS::get_singleton()->material_set_param(_get_material(), "angle", angle);
}

float EffectQuadMaterial::get_angle() const {
	return angle;
}

void EffectQuadMaterial::set_frame(int p_frame) {
	frame = MAX(p_frame, 0);
	RS::get_singleton()->material_set_param(_get_material(), "frame", float(frame));
}

int EffectQuadMaterial::get_frame() const {
	return frame;
}

Shader::Mode EffectQuadMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID EffectQuadMaterial::get_shader_rid() const {
	MutexLock shader_lock(shader_mutex);
	const RID *shader = shaders.getptr(_get_shader_key());
	return shader ? *shader : RID();
}

void EffectQuadMaterial::_validate_property(PropertyInfo &p_property) const {
	if ((p_property.name == "blend_src_factor" || p_property.name == "blend_dst_factor") && blend_mode != BLEND_MODE_CUSTOM) {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
	if (p_property.name == "fixed_basis" && facing != FACING_FIXED) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if (p_property.name == "spin_speed" && !per_particle) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if ((p_property.name == "angle" || p_property.name == "frame") && per_particle) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void EffectQuadMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_blend_mode", "mode"), &EffectQuadMaterial::set_blend_mode);
	ClassDB::bind_method(D_METHOD("get_blend_mode"), &EffectQuadMaterial::get_blend_mode);
	ClassDB::bind_method(D_METHOD("set_blend_src_factor", "factor"), &EffectQuadMaterial::set_blend_src_factor);
	ClassDB::bind_method(D_METHOD("get_blend_src_factor"), &EffectQuadMaterial::get_blend_src_factor);
	ClassDB::bind_method(D_METHOD("set_blend_dst_factor", "factor"), &EffectQuadMaterial::set_blend_dst_factor);
	ClassDB::bind_method(D_METHOD("get_blend_dst_factor"), &EffectQuadMaterial::get_blend_dst_factor);
	ClassDB::bind_method(D_METHOD("set_facing", "facing"), &EffectQuadMaterial::set_facing);
	ClassDB::bind_method(D_METHOD("get_facing"), &EffectQuadMaterial::get_facing);
	ClassDB::bind_method(D_METHOD("set_fixed_basis", "basis"), &EffectQuadMaterial::set_fixed_basis);
	ClassDB::bind_method(D_METHOD("get_fixed_basis"), &EffectQuadMaterial::get_fixed_basis);
	ClassDB::bind_method(D_METHOD("set_corner_basis", "basis"), &EffectQuadMaterial::set_corner_basis);
	ClassDB::bind_method(D_METHOD("get_corner_basis"), &EffectQuadMaterial::get_corner_basis);
	ClassDB::bind_method(D_METHOD("set_camera_offset", "offset"), &EffectQuadMaterial::set_camera_offset);
	ClassDB::bind_method(D_METHOD("get_camera_offset"), &EffectQuadMaterial::get_camera_offset);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &EffectQuadMaterial::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &EffectQuadMaterial::get_color);
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &EffectQuadMaterial::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &EffectQuadMaterial::get_texture);
	ClassDB::bind_method(D_METHOD("set_h_frames", "frames"), &EffectQuadMaterial::set_h_frames);
	ClassDB::bind_method(D_METHOD("get_h_frames"), &EffectQuadMaterial::get_h_frames);
	ClassDB::bind_method(D_METHOD("set_v_frames", "frames"), &EffectQuadMaterial::set_v_frames);
	ClassDB::bind_method(D_METHOD("get_v_frames"), &EffectQuadMaterial::get_v_frames);
	ClassDB::bind_method(D_METHOD("set_per_particle", "enable"), &EffectQuadMaterial::set_per_particle);
	ClassDB::bind_method(D_METHOD("is_per_particle"), &EffectQuadMaterial::is_per_particle);
	ClassDB::bind_method(D_METHOD("set_spin_speed", "speed"), &EffectQuadMaterial::set_spin_speed);
	ClassDB::bind_method(D_METHOD("get_spin_speed"), &EffectQuadMaterial::get_spin_speed);
	ClassDB::bind_method(D_METHOD("set_angle", "angle"), &EffectQuadMaterial::set_angle);
	ClassDB::bind_method(D_METHOD("get_angle"), &EffectQuadMaterial::get_angle);
	ClassDB::bind_method(D_METHOD("set_frame", "frame"), &EffectQuadMaterial::set_frame);
	ClassDB::bind_method(D_METHOD("get_frame"), &EffectQuadMaterial::get_frame);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_mode", PROPERTY_HINT_ENUM, "Add,Mix,Custom"), "set_blend_mode", "get_blend_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_src_factor", PROPERTY_HINT_ENUM, "Zero,One,Source Color,Inverse Source Color,Source Alpha,Inverse Source Alpha"), "set_blend_src_factor", "get_blend_src_factor");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_dst_factor", PROPERTY_HINT_ENUM, "Zero,One,Source Color,Inverse Source Color,Source Alpha,Inverse Source Alpha"), "set_blend_dst_factor", "get_blend_dst_factor");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "h_frames", PROPERTY_HINT_RANGE, "1,128,1"), "set_h_frames", "get_h_frames");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "v_frames", PROPERTY_HINT_RANGE, "1,128,1"), "set_v_frames", "get_v_frames");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "facing", PROPERTY_HINT_ENUM, "Camera,Camera Y,Fixed"), "set_facing", "get_facing");
	ADD_PROPERTY(PropertyInfo(Variant::BASIS, "fixed_basis"), "set_fixed_basis", "get_fixed_basis");
	ADD_PROPERTY(PropertyInfo(Variant::BASIS, "corner_basis"), "set_corner_basis", "get_corner_basis");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "camera_offset", PROPERTY_HINT_NONE, "suffix:m"), "set_camera_offset", "get_camera_offset");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "per_particle"), "set_per_particle", "is_per_particle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_speed", PROPERTY_HINT_NONE, "suffix:rad/s"), "set_spin_speed", "get_spin_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle", PROPERTY_HINT_NONE, "radians_as_degrees"), "set_angle", "get_angle");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frame", PROPERTY_HINT_RANGE, "0,16384,1"), "set_frame", "get_frame");

	BIND_ENUM_CONSTANT(BLEND_MODE_ADD);
	BIND_ENUM_CONSTANT(BLEND_MODE_MIX);
	BIND_ENUM_CONSTANT(BLEND_MODE_CUSTOM);

	BIND_ENUM_CONSTANT(BLEND_FACTOR_ZERO);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_INV_SRC_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_INV_SRC_ALPHA);

	BIND_ENUM_CONSTANT(FACING_CAMERA);
	BIND_ENUM_CONSTANT(FACING_CAMERA_Y);
	BIND_ENUM_CONSTANT(FACING_FIXED);
}

void EffectQuadMaterial::cleanup_shader() {
	MutexLock shader_lock(shader_mutex);
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	for (const KeyValue<int, RID> &E : shaders) {
		RS::get_singleton()->free_rid(E.value);
	}
	shaders.clear();
}

EffectQuadMaterial::EffectQuadMaterial() {
	_set_material(RS::get_singleton()->material_create());

	_update_shader();
	set_facing(FACING_CAMERA);
	set_fixed_basis(Basis());
	set_corner_basis(Basis());
	set_camera_offset(0.0);
	set_color(Color(1, 1, 1, 1));
	set_h_frames(1);
	set_v_frames(1);
	set_per_particle(false);
	set_spin_speed(0.0);
	set_angle(0.0);
	set_frame(0);
}

EffectQuadMaterial::~EffectQuadMaterial() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
