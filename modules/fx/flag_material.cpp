#include "flag_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex FlagMaterial::shader_mutex;
RID FlagMaterial::shaders[FlagMaterial::TRANSPARENCY_MAX];

void FlagMaterial::_update_shader() {
	MutexLock shader_lock(shader_mutex);

	if (shaders[transparency].is_null()) {
		String code = "shader_type spatial;\nrender_mode ";
		if (transparency == TRANSPARENCY_ALPHA) {
			code += "blend_mix, depth_draw_always, ";
		}
		code += R"(unshaded, cull_disabled, fog_disabled, shadows_disabled;

uniform sampler2D flag_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform float amplitude = 0.5;
uniform float columns = 7.0;
uniform float wave_phase = 0.0;

void vertex() {
	// CFlag::BuildVertices. The client's Z axis points the other way.
	float column = UV2.x;
	VERTEX.z = -sin(column - wave_phase * 6.0) * (column * amplitude) / columns;
}

void fragment() {
	vec4 tex = texture(flag_texture, UV);
	ALBEDO = tex.rgb;
)";
		if (transparency == TRANSPARENCY_ALPHA) {
			code += "\tALPHA = tex.a;\n";
		}
		code += "}\n";

		shaders[transparency] = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shaders[transparency], code);
	}
	RS::get_singleton()->material_set_shader(_get_material(), shaders[transparency]);
}

void FlagMaterial::set_transparency(Transparency p_transparency) {
	ERR_FAIL_INDEX(p_transparency, TRANSPARENCY_MAX);
	transparency = p_transparency;
	_update_shader();
}

FlagMaterial::Transparency FlagMaterial::get_transparency() const {
	return transparency;
}

void FlagMaterial::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "flag_texture", tex_rid);
}

Ref<Texture2D> FlagMaterial::get_texture() const {
	return texture;
}

void FlagMaterial::set_amplitude(float p_amplitude) {
	amplitude = p_amplitude;
	RS::get_singleton()->material_set_param(_get_material(), "amplitude", amplitude);
}

float FlagMaterial::get_amplitude() const {
	return amplitude;
}

void FlagMaterial::set_columns(int p_columns) {
	columns = MAX(p_columns, 1);
	RS::get_singleton()->material_set_param(_get_material(), "columns", float(columns));
}

int FlagMaterial::get_columns() const {
	return columns;
}

void FlagMaterial::set_wave_phase(float p_phase) {
	wave_phase = p_phase;
	RS::get_singleton()->material_set_param(_get_material(), "wave_phase", wave_phase);
}

float FlagMaterial::get_wave_phase() const {
	return wave_phase;
}

Shader::Mode FlagMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID FlagMaterial::get_shader_rid() const {
	MutexLock shader_lock(shader_mutex);
	return shaders[transparency];
}

void FlagMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_transparency", "transparency"), &FlagMaterial::set_transparency);
	ClassDB::bind_method(D_METHOD("get_transparency"), &FlagMaterial::get_transparency);
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &FlagMaterial::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &FlagMaterial::get_texture);
	ClassDB::bind_method(D_METHOD("set_amplitude", "amplitude"), &FlagMaterial::set_amplitude);
	ClassDB::bind_method(D_METHOD("get_amplitude"), &FlagMaterial::get_amplitude);
	ClassDB::bind_method(D_METHOD("set_columns", "columns"), &FlagMaterial::set_columns);
	ClassDB::bind_method(D_METHOD("get_columns"), &FlagMaterial::get_columns);
	ClassDB::bind_method(D_METHOD("set_wave_phase", "phase"), &FlagMaterial::set_wave_phase);
	ClassDB::bind_method(D_METHOD("get_wave_phase"), &FlagMaterial::get_wave_phase);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "transparency", PROPERTY_HINT_ENUM, "Disabled,Alpha"), "set_transparency", "get_transparency");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amplitude"), "set_amplitude", "get_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "columns", PROPERTY_HINT_RANGE, "1,256,1"), "set_columns", "get_columns");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wave_phase"), "set_wave_phase", "get_wave_phase");

	BIND_ENUM_CONSTANT(TRANSPARENCY_DISABLED);
	BIND_ENUM_CONSTANT(TRANSPARENCY_ALPHA);
}

void FlagMaterial::cleanup_shader() {
	MutexLock shader_lock(shader_mutex);
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	for (int i = 0; i < TRANSPARENCY_MAX; i++) {
		if (shaders[i].is_valid()) {
			RS::get_singleton()->free_rid(shaders[i]);
			shaders[i] = RID();
		}
	}
}

FlagMaterial::FlagMaterial() {
	_set_material(RS::get_singleton()->material_create());
	_update_shader();
	set_amplitude(0.5);
	set_columns(7);
	set_wave_phase(0.0);
}

FlagMaterial::~FlagMaterial() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
