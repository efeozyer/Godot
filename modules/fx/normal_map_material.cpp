#include "normal_map_material.h"

#include "effect_common.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex NormalMapMaterial::shader_mutex;
HashMap<int, RID> NormalMapMaterial::shaders;

int NormalMapMaterial::_get_shader_key() const {
	int key = double_sided ? 1 : 0;
	if (alpha_blend) {
		key += 2 * (1 + blend_src_factor * BLEND_FACTOR_MAX + blend_dst_factor);
	}
	return key;
}

void NormalMapMaterial::_update_shader() {
	MutexLock shader_lock(shader_mutex);

	const int key = _get_shader_key();
	RID *existing = shaders.getptr(key);
	if (existing) {
		RS::get_singleton()->material_set_shader(_get_material(), *existing);
		return;
	}

	// The variants differ in the render states CCPart::Render sets around the draw. The effect is
	// unlit as far as the engine is concerned: its own light term is computed in fragment(), and
	// light() only takes the directional light's shadow.
	static const char *factor_names[BLEND_FACTOR_MAX] = { "zero", "one", "src_color", "inv_src_color", "src_alpha", "inv_src_alpha" };
	String code = "shader_type spatial;\nrender_mode ambient_light_disabled, specular_disabled, fog_disabled";
	code += double_sided ? ", cull_disabled" : ", cull_back";
	if (alpha_blend) {
		// The client leaves depth writing on for its blended parts.
		code += vformat(", blend_src_%s, blend_dst_%s, depth_draw_always;\n", factor_names[blend_src_factor], factor_names[blend_dst_factor]);
	} else {
		code += ";\n";
	}
	code += effect_shader_functions;
	code += R"(
// baseMap: wrapped, linear.
uniform sampler2D base_texture : filter_linear_mipmap, repeat_enable;
uniform float texel = 0.00390625; // 1 / TextureSize
uniform vec3 ambient = vec3(1.0); // fvAmbient
uniform vec3 diffuse = vec3(1.0); // fvDiffuse
uniform vec3 emissive = vec3(0.0); // fvEmisive
uniform bool emissive_zero = false; // bSetEmisiveZero
uniform bool character = true; // bNormarChar
uniform float alpha = 1.0; // fAlpha
uniform float alpha_ref = 1.0; // D3DRS_ALPHAREF, with D3DRS_ALPHAFUNC = D3DCMP_GREATER

uniform bool night = false; // bNight
uniform bool shadow_enabled = true; // bShadow
uniform vec3 normal_light = vec3(0.0, 1.0, 0.0); // see effect_normal_light()
uniform vec3 bump_light = vec3(0.0, -1.0, 0.0); // LightDirection, normalized, in the client's space
uniform vec4 light_specular = vec4(1.0); // fvSpecular

uniform bool fog_enabled = true;
uniform vec3 fog_color = vec3(1.0); // D3DRS_FOGCOLOR
uniform vec2 fog_range = vec2(0.7, 512.0); // g_Fog

const vec4 LIGHTNESS = vec4(0.3, 0.59, 0.11, 0.0); // the effect's "lightness" parameter

varying vec3 world_normal;

// Written by fragment() for light().
varying vec3 effect_color;
varying float fog_factor;

void vertex() {
	// oT3 of the vertex shader is this normal in the shadow light's view; see normal_light.
	world_normal = normalize(mat3(MODEL_MATRIX) * NORMAL);
}

// The end of the pixel shader and the fog stage behind it. attenuation is Godot's directional
// shadow, 1 where lit.
vec3 shaded(vec3 color, float attenuation, float fog) {
	if (shadow_enabled) {
		color *= effect_shadow_factor(attenuation) * 0.8 + 0.2;
	}
	color = clamp(color, 0.0, 1.0); // the back buffer does not hold more
	color = mix(fog_color, color, fog);
	return effect_to_linear(color);
}

void fragment() {
	vec4 tex = texture(base_texture, UV);

	// The texture stands in for a height map: a Sobel filter over its eight neighbours, one texel
	// apart, gives a slope, (slope, 1) is taken as a normal and packed as n * 0.75 + 0.25.
	float e = texel;
	vec4 lt = texture(base_texture, UV + vec2(-e, -e));
	vec4 ct = texture(base_texture, UV + vec2(0.0, -e));
	vec4 rt = texture(base_texture, UV + vec2(e, -e));
	vec4 lm = texture(base_texture, UV + vec2(-e, 0.0));
	vec4 rm = texture(base_texture, UV + vec2(e, 0.0));
	vec4 lb = texture(base_texture, UV + vec2(-e, e));
	vec4 cb = texture(base_texture, UV + vec2(0.0, e));
	vec4 rb = texture(base_texture, UV + vec2(e, e));
	vec3 n = normalize(vec3(dot(lt + 2.0 * lm + lb - rt - 2.0 * rm - rb, LIGHTNESS), dot(lt + 2.0 * ct + rt - lb - 2.0 * cb - rb, LIGHTNESS), 1.0));
	n = normalize(n * 0.75 + 0.25);

	// That normal is lit by the sun direction as it is, without regard to how the surface is
	// turned; the vertex normal adds the term that does depend on it.
	vec3 lit = tex.rgb * diffuse * (dot(n, bump_light) + dot(normalize(world_normal), normal_light));
	vec3 base = tex.rgb * ambient;

	// Objects, and character parts with bSetEmisiveZero, hold the lit colour between the ambient
	// share of the texture and three times it (twice at night). Other character parts add the
	// ambient share, once more multiplied by the texture, and half the emissive colour.
	vec3 color = min(base * (night ? 2.0 : 3.0), max(clamp(lit, 0.0, 1.0), base));
	if (character && !emissive_zero) {
		color = tex.rgb * emissive * 0.5 + clamp(tex.rgb * base + lit, 0.0, 1.0);
	}

	float a = tex.a;
	if (!character && night) {
		color *= light_specular.rgb;
		a *= light_specular.a;
	}
	// The body fade of CChr::Render.
	if (character && alpha != 1.0) {
		a *= alpha;
	}

	// The effect has no alpha test of its own; the device's stays on while it draws.
	if (floor(clamp(a, 0.0, 1.0) * 255.0 + 0.5) <= alpha_ref) {
		discard;
	}

	effect_color = color;
	fog_factor = fog_enabled ? effect_fog_factor(-VERTEX.z, fog_range.x, fog_range.y) : 1.0;

	// The lit result goes out as emission; light() takes the shadow off it.
	ALBEDO = vec3(1.0);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
	EMISSION = shaded(color, 1.0, fog_factor);
	WRITE_ALPHA
}

void light() {
	if (LIGHT_IS_DIRECTIONAL) {
		DIFFUSE_LIGHT += shaded(effect_color, ATTENUATION, fog_factor) - shaded(effect_color, 1.0, fog_factor);
	}
}
)";

	// Writing ALPHA moves a material to the transparent pass, so only blended parts do.
	code = code.replace("WRITE_ALPHA", alpha_blend ? "ALPHA = a;" : "");

	RID shader = RS::get_singleton()->shader_create();
	RS::get_singleton()->shader_set_code(shader, code);
	shaders.insert(key, shader);
	RS::get_singleton()->material_set_shader(_get_material(), shader);
}

void NormalMapMaterial::_update_light() {
	_apply_world(effect_world_get());
}

// Everything the sky decides. While a sky publishes its state the material follows it; its own
// properties are what it shows without one.
void NormalMapMaterial::_apply_world(const EffectWorld &p_world) {
	const bool world_night = p_world.active ? p_world.night : night;
	// The client normalizes the direction before it uploads it.
	const Vector3 direction = (p_world.active ? p_world.light_direction : light_direction).normalized();
	const Vector3 color = p_world.active ? p_world.light_color : Vector3(light_specular.r, light_specular.g, light_specular.b);
	const Vector3 fog = p_world.active ? p_world.fog_color : Vector3(fog_color.r, fog_color.g, fog_color.b);
	RS::get_singleton()->material_set_param(_get_material(), "night", world_night);
	RS::get_singleton()->material_set_param(_get_material(), "normal_light", effect_normal_light(direction, shadow_distance, world_night));
	RS::get_singleton()->material_set_param(_get_material(), "bump_light", Vector3(direction.x, direction.y, -direction.z));
	RS::get_singleton()->material_set_param(_get_material(), "light_specular", Color(color.x, color.y, color.z, light_specular.a));
	RS::get_singleton()->material_set_param(_get_material(), "fog_color", fog);
	RS::get_singleton()->material_set_param(_get_material(), "fog_range", Vector2(fog_near, p_world.active ? p_world.fog_far : fog_far));
}

void NormalMapMaterial::_effect_world_changed(const EffectWorld &p_world) {
	_apply_world(p_world);
}

void NormalMapMaterial::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "base_texture", tex_rid);
	// CCPart::Render passes the texture's width as TextureSize, or 0 without a texture. The
	// effect divides by it, so the CShader default of 256 is kept for that case.
	const int width = p_texture.is_valid() ? p_texture->get_width() : 0;
	RS::get_singleton()->material_set_param(_get_material(), "texel", 1.0f / float(width > 0 ? width : 256));
}

Ref<Texture2D> NormalMapMaterial::get_texture() const {
	return texture;
}

void NormalMapMaterial::set_ambient(const Color &p_color) {
	ambient = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "ambient", Vector3(ambient.r, ambient.g, ambient.b));
}

Color NormalMapMaterial::get_ambient() const {
	return ambient;
}

void NormalMapMaterial::set_diffuse(const Color &p_color) {
	diffuse = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "diffuse", Vector3(diffuse.r, diffuse.g, diffuse.b));
}

Color NormalMapMaterial::get_diffuse() const {
	return diffuse;
}

void NormalMapMaterial::set_emissive(const Color &p_color) {
	emissive = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "emissive", Vector3(emissive.r, emissive.g, emissive.b));
}

Color NormalMapMaterial::get_emissive() const {
	return emissive;
}

void NormalMapMaterial::set_emissive_zero(bool p_enable) {
	emissive_zero = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "emissive_zero", emissive_zero);
	notify_property_list_changed();
}

bool NormalMapMaterial::is_emissive_zero() const {
	return emissive_zero;
}

void NormalMapMaterial::set_character(bool p_enable) {
	character = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "character", character);
	notify_property_list_changed();
}

bool NormalMapMaterial::is_character() const {
	return character;
}

void NormalMapMaterial::set_alpha(float p_alpha) {
	alpha = p_alpha;
	RS::get_singleton()->material_set_param(_get_material(), "alpha", alpha);
}

float NormalMapMaterial::get_alpha() const {
	return alpha;
}

void NormalMapMaterial::set_alpha_blend(bool p_enable) {
	alpha_blend = p_enable;
	_update_shader();
	notify_property_list_changed();
}

bool NormalMapMaterial::is_alpha_blend() const {
	return alpha_blend;
}

void NormalMapMaterial::set_blend_src_factor(BlendFactor p_factor) {
	ERR_FAIL_INDEX(p_factor, BLEND_FACTOR_MAX);
	blend_src_factor = p_factor;
	_update_shader();
}

NormalMapMaterial::BlendFactor NormalMapMaterial::get_blend_src_factor() const {
	return blend_src_factor;
}

void NormalMapMaterial::set_blend_dst_factor(BlendFactor p_factor) {
	ERR_FAIL_INDEX(p_factor, BLEND_FACTOR_MAX);
	blend_dst_factor = p_factor;
	_update_shader();
}

NormalMapMaterial::BlendFactor NormalMapMaterial::get_blend_dst_factor() const {
	return blend_dst_factor;
}

void NormalMapMaterial::set_double_sided(bool p_enable) {
	double_sided = p_enable;
	_update_shader();
}

bool NormalMapMaterial::is_double_sided() const {
	return double_sided;
}

void NormalMapMaterial::set_alpha_ref(int p_ref) {
	alpha_ref = CLAMP(p_ref, 0, 255);
	RS::get_singleton()->material_set_param(_get_material(), "alpha_ref", float(alpha_ref));
}

int NormalMapMaterial::get_alpha_ref() const {
	return alpha_ref;
}

void NormalMapMaterial::set_night(bool p_night) {
	night = p_night;
	_update_light();
	notify_property_list_changed();
}

bool NormalMapMaterial::is_night() const {
	return night;
}

void NormalMapMaterial::set_light_direction(const Vector3 &p_direction) {
	light_direction = p_direction;
	_update_light();
}

Vector3 NormalMapMaterial::get_light_direction() const {
	return light_direction;
}

void NormalMapMaterial::set_light_specular(const Color &p_color) {
	light_specular = p_color;
	_update_light();
}

Color NormalMapMaterial::get_light_specular() const {
	return light_specular;
}

void NormalMapMaterial::set_shadow_enabled(bool p_enable) {
	shadow_enabled = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "shadow_enabled", shadow_enabled);
}

bool NormalMapMaterial::is_shadow_enabled() const {
	return shadow_enabled;
}

void NormalMapMaterial::set_shadow_distance(float p_distance) {
	shadow_distance = p_distance;
	_update_light();
}

float NormalMapMaterial::get_shadow_distance() const {
	return shadow_distance;
}

void NormalMapMaterial::set_fog_enabled(bool p_enable) {
	fog_enabled = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "fog_enabled", fog_enabled);
	notify_property_list_changed();
}

bool NormalMapMaterial::is_fog_enabled() const {
	return fog_enabled;
}

void NormalMapMaterial::set_fog_color(const Color &p_color) {
	fog_color = p_color;
	_update_light();
}

Color NormalMapMaterial::get_fog_color() const {
	return fog_color;
}

void NormalMapMaterial::set_fog_near(float p_near) {
	fog_near = p_near;
	_update_light();
}

float NormalMapMaterial::get_fog_near() const {
	return fog_near;
}

void NormalMapMaterial::set_fog_far(float p_far) {
	fog_far = p_far;
	_update_light();
}

float NormalMapMaterial::get_fog_far() const {
	return fog_far;
}

Shader::Mode NormalMapMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID NormalMapMaterial::get_shader_rid() const {
	MutexLock shader_lock(shader_mutex);
	const RID *shader = shaders.getptr(_get_shader_key());
	return shader ? *shader : RID();
}

void NormalMapMaterial::_validate_property(PropertyInfo &p_property) const {
	if ((p_property.name == "blend_src_factor" || p_property.name == "blend_dst_factor") && !alpha_blend) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	// The effect only reads these in the branches named here.
	if ((p_property.name == "emissive_zero" || p_property.name == "alpha") && !character) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if (p_property.name == "emissive" && (!character || emissive_zero)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if (p_property.name == "light_specular" && (character || !night)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if ((p_property.name == "fog_color" || p_property.name == "fog_near" || p_property.name == "fog_far") && !fog_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void NormalMapMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &NormalMapMaterial::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &NormalMapMaterial::get_texture);
	ClassDB::bind_method(D_METHOD("set_ambient", "color"), &NormalMapMaterial::set_ambient);
	ClassDB::bind_method(D_METHOD("get_ambient"), &NormalMapMaterial::get_ambient);
	ClassDB::bind_method(D_METHOD("set_diffuse", "color"), &NormalMapMaterial::set_diffuse);
	ClassDB::bind_method(D_METHOD("get_diffuse"), &NormalMapMaterial::get_diffuse);
	ClassDB::bind_method(D_METHOD("set_emissive", "color"), &NormalMapMaterial::set_emissive);
	ClassDB::bind_method(D_METHOD("get_emissive"), &NormalMapMaterial::get_emissive);
	ClassDB::bind_method(D_METHOD("set_emissive_zero", "enable"), &NormalMapMaterial::set_emissive_zero);
	ClassDB::bind_method(D_METHOD("is_emissive_zero"), &NormalMapMaterial::is_emissive_zero);
	ClassDB::bind_method(D_METHOD("set_character", "enable"), &NormalMapMaterial::set_character);
	ClassDB::bind_method(D_METHOD("is_character"), &NormalMapMaterial::is_character);
	ClassDB::bind_method(D_METHOD("set_alpha", "alpha"), &NormalMapMaterial::set_alpha);
	ClassDB::bind_method(D_METHOD("get_alpha"), &NormalMapMaterial::get_alpha);
	ClassDB::bind_method(D_METHOD("set_alpha_blend", "enable"), &NormalMapMaterial::set_alpha_blend);
	ClassDB::bind_method(D_METHOD("is_alpha_blend"), &NormalMapMaterial::is_alpha_blend);
	ClassDB::bind_method(D_METHOD("set_blend_src_factor", "factor"), &NormalMapMaterial::set_blend_src_factor);
	ClassDB::bind_method(D_METHOD("get_blend_src_factor"), &NormalMapMaterial::get_blend_src_factor);
	ClassDB::bind_method(D_METHOD("set_blend_dst_factor", "factor"), &NormalMapMaterial::set_blend_dst_factor);
	ClassDB::bind_method(D_METHOD("get_blend_dst_factor"), &NormalMapMaterial::get_blend_dst_factor);
	ClassDB::bind_method(D_METHOD("set_double_sided", "enable"), &NormalMapMaterial::set_double_sided);
	ClassDB::bind_method(D_METHOD("is_double_sided"), &NormalMapMaterial::is_double_sided);
	ClassDB::bind_method(D_METHOD("set_alpha_ref", "ref"), &NormalMapMaterial::set_alpha_ref);
	ClassDB::bind_method(D_METHOD("get_alpha_ref"), &NormalMapMaterial::get_alpha_ref);
	ClassDB::bind_method(D_METHOD("set_night", "night"), &NormalMapMaterial::set_night);
	ClassDB::bind_method(D_METHOD("is_night"), &NormalMapMaterial::is_night);
	ClassDB::bind_method(D_METHOD("set_light_direction", "direction"), &NormalMapMaterial::set_light_direction);
	ClassDB::bind_method(D_METHOD("get_light_direction"), &NormalMapMaterial::get_light_direction);
	ClassDB::bind_method(D_METHOD("set_light_specular", "color"), &NormalMapMaterial::set_light_specular);
	ClassDB::bind_method(D_METHOD("get_light_specular"), &NormalMapMaterial::get_light_specular);
	ClassDB::bind_method(D_METHOD("set_shadow_enabled", "enable"), &NormalMapMaterial::set_shadow_enabled);
	ClassDB::bind_method(D_METHOD("is_shadow_enabled"), &NormalMapMaterial::is_shadow_enabled);
	ClassDB::bind_method(D_METHOD("set_shadow_distance", "distance"), &NormalMapMaterial::set_shadow_distance);
	ClassDB::bind_method(D_METHOD("get_shadow_distance"), &NormalMapMaterial::get_shadow_distance);
	ClassDB::bind_method(D_METHOD("set_fog_enabled", "enable"), &NormalMapMaterial::set_fog_enabled);
	ClassDB::bind_method(D_METHOD("is_fog_enabled"), &NormalMapMaterial::is_fog_enabled);
	ClassDB::bind_method(D_METHOD("set_fog_color", "color"), &NormalMapMaterial::set_fog_color);
	ClassDB::bind_method(D_METHOD("get_fog_color"), &NormalMapMaterial::get_fog_color);
	ClassDB::bind_method(D_METHOD("set_fog_near", "distance"), &NormalMapMaterial::set_fog_near);
	ClassDB::bind_method(D_METHOD("get_fog_near"), &NormalMapMaterial::get_fog_near);
	ClassDB::bind_method(D_METHOD("set_fog_far", "distance"), &NormalMapMaterial::set_fog_far);
	ClassDB::bind_method(D_METHOD("get_fog_far"), &NormalMapMaterial::get_fog_far);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "ambient", PROPERTY_HINT_COLOR_NO_ALPHA), "set_ambient", "get_ambient");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "diffuse", PROPERTY_HINT_COLOR_NO_ALPHA), "set_diffuse", "get_diffuse");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "character"), "set_character", "is_character");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emissive_zero"), "set_emissive_zero", "is_emissive_zero");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "emissive", PROPERTY_HINT_COLOR_NO_ALPHA), "set_emissive", "get_emissive");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "alpha", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_alpha", "get_alpha");

	ADD_GROUP("Render States", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "alpha_blend"), "set_alpha_blend", "is_alpha_blend");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_src_factor", PROPERTY_HINT_ENUM, "Zero,One,Source Color,Inverse Source Color,Source Alpha,Inverse Source Alpha"), "set_blend_src_factor", "get_blend_src_factor");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_dst_factor", PROPERTY_HINT_ENUM, "Zero,One,Source Color,Inverse Source Color,Source Alpha,Inverse Source Alpha"), "set_blend_dst_factor", "get_blend_dst_factor");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "double_sided"), "set_double_sided", "is_double_sided");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "alpha_ref", PROPERTY_HINT_RANGE, "0,255,1"), "set_alpha_ref", "get_alpha_ref");

	ADD_GROUP("Light", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "night"), "set_night", "is_night");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "light_direction"), "set_light_direction", "get_light_direction");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "light_specular"), "set_light_specular", "get_light_specular");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shadow_enabled"), "set_shadow_enabled", "is_shadow_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shadow_distance", PROPERTY_HINT_RANGE, "1,1024,1,or_greater,suffix:m"), "set_shadow_distance", "get_shadow_distance");

	ADD_GROUP("Fog", "fog_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fog_enabled"), "set_fog_enabled", "is_fog_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fog_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_fog_color", "get_fog_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_near", PROPERTY_HINT_RANGE, "0.01,16,0.01,or_greater,suffix:m"), "set_fog_near", "get_fog_near");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_far", PROPERTY_HINT_RANGE, "1,4096,1,or_greater,suffix:m"), "set_fog_far", "get_fog_far");

	BIND_ENUM_CONSTANT(BLEND_FACTOR_ZERO);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_ONE);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_INV_SRC_COLOR);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_SRC_ALPHA);
	BIND_ENUM_CONSTANT(BLEND_FACTOR_INV_SRC_ALPHA);
}

void NormalMapMaterial::cleanup_shader() {
	MutexLock shader_lock(shader_mutex);
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	for (const KeyValue<int, RID> &E : shaders) {
		RS::get_singleton()->free_rid(E.value);
	}
	shaders.clear();
}

NormalMapMaterial::NormalMapMaterial() {
	_set_material(RS::get_singleton()->material_create());

	_update_shader();
	set_texture(Ref<Texture2D>());
	set_ambient(Color(1, 1, 1, 1));
	set_diffuse(Color(1, 1, 1, 1));
	set_emissive(Color(0, 0, 0, 0));
	set_emissive_zero(false);
	set_character(true);
	set_alpha(1.0);
	set_alpha_ref(1);
	set_night(false);
	set_light_specular(light_specular);
	set_shadow_enabled(true);
	set_fog_enabled(true);
	set_fog_color(fog_color);
	set_fog_far(512.0);
	_update_light();
}

NormalMapMaterial::~NormalMapMaterial() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
