#include "terrain_layer_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

// A layer without a texture is drawn as a grey checkerboard, one square per cell. The first
// layer keeps it plain; the others tint it so that painted layers can be told apart.
static const Color default_layer_colors[TerrainData::LAYER_COUNT] = {
	Color(1, 1, 1), Color(0.47, 0.38, 0.27), Color(0.5, 0.5, 0.5), Color(0.82, 0.76, 0.58),
	Color(0.9, 0.9, 0.92), Color(0.25, 0.3, 0.18), Color(0.6, 0.35, 0.25), Color(0.2, 0.2, 0.22)
};

Mutex TerrainLayerMaterial::shader_mutex;
RID TerrainLayerMaterial::shader;

void TerrainLayerMaterial::_update_shader() {
	MutexLock shader_lock(shader_mutex);

	if (shader.is_null()) {
		String code = R"(shader_type spatial;

uniform sampler2D weights_0 : hint_default_transparent, filter_linear, repeat_disable;
uniform sampler2D weights_1 : hint_default_transparent, filter_linear, repeat_disable;
uniform float terrain_size = 129.0;
uniform float cell_size = 1.0;
uniform float normal_strength = 1.0;
)";
		for (int i = 0; i < TerrainData::LAYER_COUNT; i++) {
			code += vformat("uniform sampler2D albedo_%d : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;\n", i);
			code += vformat("uniform sampler2D normal_%d : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;\n", i);
			code += vformat("uniform vec3 color_%d : source_color = vec3(1.0);\n", i);
			code += vformat("uniform float textured_%d = 0.0;\n", i);
			code += vformat("uniform float repeat_%d = 0.25;\n", i);
			code += vformat("uniform float roughness_%d = 1.0;\n", i);
		}
		code += R"(
varying vec2 ground;

void vertex() {
	ground = VERTEX.xz;
	// Texture coordinates follow the ground: u along +X, v along +Z.
	TANGENT = normalize(cross(vec3(0.0, 0.0, -1.0), NORMAL));
	BINORMAL = normalize(cross(NORMAL, TANGENT));
}

void fragment() {
	// The grid's z axis runs along -Z; one texel per height sample.
	vec2 weight_uv = (vec2(ground.x, -ground.y) / cell_size + 0.5) / terrain_size;
	vec4 w0 = texture(weights_0, weight_uv);
	vec4 w1 = texture(weights_1, weight_uv);
	float total = dot(w0, vec4(1.0)) + dot(w1, vec4(1.0));
	if (total < 0.001) {
		w0.r = 1.0;
		total = 1.0;
	}
	w0 /= total;
	w1 /= total;

	// What a layer without a texture shows.
	vec2 square = floor(vec2(ground.x, -ground.y) / cell_size);
	vec3 checker = mix(vec3(0.52), vec3(0.87), mod(square.x + square.y, 2.0));

	vec3 albedo = vec3(0.0);
	vec3 normal = vec3(0.0);
	float roughness = 0.0;
)";
		for (int i = 0; i < TerrainData::LAYER_COUNT; i++) {
			const String w = vformat("w%d.%s", i / 4, String("rgba").substr(i % 4, 1));
			code += vformat("\talbedo += mix(checker, texture(albedo_%d, ground * repeat_%d).rgb, textured_%d) * color_%d * %s;\n", i, i, i, i, w);
			code += vformat("\tnormal += texture(normal_%d, ground * repeat_%d).rgb * %s;\n", i, i, w);
			code += vformat("\troughness += roughness_%d * %s;\n", i, w);
		}
		code += R"(
	ALBEDO = albedo;
	ROUGHNESS = roughness;
	NORMAL_MAP = normal;
	NORMAL_MAP_DEPTH = normal_strength;
}
)";
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, code);
	}
	RS::get_singleton()->material_set_shader(_get_material(), shader);
}

void TerrainLayerMaterial::set_layer_albedo(int p_layer, const Ref<Texture2D> &p_texture) {
	ERR_FAIL_INDEX(p_layer, TerrainData::LAYER_COUNT);
	layers[p_layer].albedo = p_texture;
	Variant rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "albedo_" + itos(p_layer), rid);
	RS::get_singleton()->material_set_param(_get_material(), "textured_" + itos(p_layer), p_texture.is_valid() ? 1.0f : 0.0f);
	if (p_texture.is_valid() && layers[p_layer].color == default_layer_colors[p_layer]) {
		// The placeholder colour would tint the texture.
		set_layer_color(p_layer, Color(1, 1, 1));
	}
	emit_changed();
}

Ref<Texture2D> TerrainLayerMaterial::get_layer_albedo(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, TerrainData::LAYER_COUNT, Ref<Texture2D>());
	return layers[p_layer].albedo;
}

void TerrainLayerMaterial::set_layer_normal(int p_layer, const Ref<Texture2D> &p_texture) {
	ERR_FAIL_INDEX(p_layer, TerrainData::LAYER_COUNT);
	layers[p_layer].normal = p_texture;
	Variant rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "normal_" + itos(p_layer), rid);
}

Ref<Texture2D> TerrainLayerMaterial::get_layer_normal(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, TerrainData::LAYER_COUNT, Ref<Texture2D>());
	return layers[p_layer].normal;
}

void TerrainLayerMaterial::set_layer_color(int p_layer, const Color &p_color) {
	ERR_FAIL_INDEX(p_layer, TerrainData::LAYER_COUNT);
	layers[p_layer].color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "color_" + itos(p_layer), p_color);
	emit_changed();
}

Color TerrainLayerMaterial::get_layer_color(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, TerrainData::LAYER_COUNT, Color());
	return layers[p_layer].color;
}

void TerrainLayerMaterial::set_layer_tile_size(int p_layer, float p_size) {
	ERR_FAIL_INDEX(p_layer, TerrainData::LAYER_COUNT);
	layers[p_layer].tile_size = MAX(p_size, 0.01f);
	RS::get_singleton()->material_set_param(_get_material(), "repeat_" + itos(p_layer), 1.0f / layers[p_layer].tile_size);
}

float TerrainLayerMaterial::get_layer_tile_size(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, TerrainData::LAYER_COUNT, 1.0);
	return layers[p_layer].tile_size;
}

void TerrainLayerMaterial::set_layer_roughness(int p_layer, float p_roughness) {
	ERR_FAIL_INDEX(p_layer, TerrainData::LAYER_COUNT);
	layers[p_layer].roughness = p_roughness;
	RS::get_singleton()->material_set_param(_get_material(), "roughness_" + itos(p_layer), p_roughness);
}

float TerrainLayerMaterial::get_layer_roughness(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, TerrainData::LAYER_COUNT, 1.0);
	return layers[p_layer].roughness;
}

void TerrainLayerMaterial::set_normal_strength(float p_strength) {
	normal_strength = p_strength;
	RS::get_singleton()->material_set_param(_get_material(), "normal_strength", normal_strength);
}

float TerrainLayerMaterial::get_normal_strength() const {
	return normal_strength;
}

void TerrainLayerMaterial::set_weight_texture(int p_index, RID p_texture) {
	ERR_FAIL_INDEX(p_index, TerrainData::LAYER_COUNT / 4);
	RS::get_singleton()->material_set_param(_get_material(), "weights_" + itos(p_index), p_texture.is_valid() ? Variant(p_texture) : Variant());
}

void TerrainLayerMaterial::set_terrain_dimensions(int p_size, float p_cell_size) {
	RS::get_singleton()->material_set_param(_get_material(), "terrain_size", float(p_size));
	RS::get_singleton()->material_set_param(_get_material(), "cell_size", p_cell_size);
}

Shader::Mode TerrainLayerMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID TerrainLayerMaterial::get_shader_rid() const {
	MutexLock shader_lock(shader_mutex);
	return shader;
}

void TerrainLayerMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_layer_albedo", "layer", "texture"), &TerrainLayerMaterial::set_layer_albedo);
	ClassDB::bind_method(D_METHOD("get_layer_albedo", "layer"), &TerrainLayerMaterial::get_layer_albedo);
	ClassDB::bind_method(D_METHOD("set_layer_normal", "layer", "texture"), &TerrainLayerMaterial::set_layer_normal);
	ClassDB::bind_method(D_METHOD("get_layer_normal", "layer"), &TerrainLayerMaterial::get_layer_normal);
	ClassDB::bind_method(D_METHOD("set_layer_color", "layer", "color"), &TerrainLayerMaterial::set_layer_color);
	ClassDB::bind_method(D_METHOD("get_layer_color", "layer"), &TerrainLayerMaterial::get_layer_color);
	ClassDB::bind_method(D_METHOD("set_layer_tile_size", "layer", "size"), &TerrainLayerMaterial::set_layer_tile_size);
	ClassDB::bind_method(D_METHOD("get_layer_tile_size", "layer"), &TerrainLayerMaterial::get_layer_tile_size);
	ClassDB::bind_method(D_METHOD("set_layer_roughness", "layer", "roughness"), &TerrainLayerMaterial::set_layer_roughness);
	ClassDB::bind_method(D_METHOD("get_layer_roughness", "layer"), &TerrainLayerMaterial::get_layer_roughness);
	ClassDB::bind_method(D_METHOD("set_normal_strength", "strength"), &TerrainLayerMaterial::set_normal_strength);
	ClassDB::bind_method(D_METHOD("get_normal_strength"), &TerrainLayerMaterial::get_normal_strength);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_normal_strength", "get_normal_strength");
	for (int i = 0; i < TerrainData::LAYER_COUNT; i++) {
		const String prefix = vformat("layer_%d/", i + 1);
		ADD_GROUP(vformat("Layer %d", i + 1), prefix);
		ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, prefix + "albedo", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_layer_albedo", "get_layer_albedo", i);
		ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, prefix + "normal", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_layer_normal", "get_layer_normal", i);
		ADD_PROPERTYI(PropertyInfo(Variant::COLOR, prefix + "color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_layer_color", "get_layer_color", i);
		ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, prefix + "tile_size", PROPERTY_HINT_RANGE, "0.1,256,0.01,or_greater,suffix:m"), "set_layer_tile_size", "get_layer_tile_size", i);
		ADD_PROPERTYI(PropertyInfo(Variant::FLOAT, prefix + "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_layer_roughness", "get_layer_roughness", i);
	}
}

void TerrainLayerMaterial::cleanup_shader() {
	MutexLock shader_lock(shader_mutex);
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	if (shader.is_valid()) {
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
}

TerrainLayerMaterial::TerrainLayerMaterial() {
	_set_material(RS::get_singleton()->material_create());
	_update_shader();
	for (int i = 0; i < TerrainData::LAYER_COUNT; i++) {
		set_layer_color(i, default_layer_colors[i]);
		set_layer_tile_size(i, 4.0);
		set_layer_roughness(i, 1.0);
	}
	set_normal_strength(1.0);
}

TerrainLayerMaterial::~TerrainLayerMaterial() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
