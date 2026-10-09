#include "terrain_layer_material.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

// A layer without a texture is drawn as a grey checkerboard, one square per cell. The first
// layer keeps it plain; the others tint it so that painted layers can be told apart.
static const Color default_layer_colors[8] = {
	Color(1, 1, 1), Color(0.47, 0.38, 0.27), Color(0.5, 0.5, 0.5), Color(0.82, 0.76, 0.58),
	Color(0.9, 0.9, 0.92), Color(0.25, 0.3, 0.18), Color(0.6, 0.35, 0.25), Color(0.2, 0.2, 0.22)
};

static Color _default_layer_color(int p_layer) {
	return default_layer_colors[p_layer % 8];
}

Mutex TerrainLayerMaterial::shader_mutex;
RID TerrainLayerMaterial::shader;

void TerrainLayerMaterial::_update_shader() {
	MutexLock shader_lock(shader_mutex);

	if (shader.is_null()) {
		String code = R"(shader_type spatial;

// Per height sample: the numbers of the four layers it blends, and their weights.
uniform sampler2D weights_0 : hint_default_transparent, filter_nearest, repeat_disable;
uniform sampler2D weights_1 : hint_default_transparent, filter_nearest, repeat_disable;
uniform float has_weights = 0.0;
uniform float terrain_size = 129.0;
uniform float cell_size = 1.0;
uniform float normal_strength = 1.0;
uniform sampler2DArray albedo_array : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray normal_array : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform int layer_count = 1;
// Per layer: repeats per metre, roughness, whether it has a colour texture, whether it has a normal map.
uniform vec4 layer_params[MAX_LAYERS];
uniform vec4 layer_tints[MAX_LAYERS];

varying vec2 ground;

void vertex() {
	ground = VERTEX.xz;
	// Texture coordinates follow the ground: u along +X, v along +Z.
	TANGENT = normalize(cross(vec3(0.0, 0.0, -1.0), NORMAL));
	BINORMAL = normalize(cross(NORMAL, TANGENT));
}

void add_layer(int number, float weight, vec3 checker, vec2 dx, vec2 dy, inout vec3 albedo, inout vec3 normal, inout float roughness) {
	int layer = clamp(number, 0, layer_count - 1);
	vec4 params = layer_params[layer];
	vec3 uvw = vec3(ground * params.x, float(layer));
	vec3 colour = checker;
	if (params.z > 0.5) {
		colour = textureGrad(albedo_array, uvw, dx * params.x, dy * params.x).rgb;
	}
	vec3 bump = vec3(0.5, 0.5, 1.0);
	if (params.w > 0.5) {
		bump = textureGrad(normal_array, uvw, dx * params.x, dy * params.x).rgb;
	}
	albedo += colour * layer_tints[layer].rgb * weight;
	normal += bump * weight;
	roughness += params.y * weight;
}

void fragment() {
	// The grid's z axis runs along -Z; one texel per height sample.
	vec2 position = clamp(vec2(ground.x, -ground.y) / cell_size, vec2(0.0), vec2(terrain_size - 1.0));
	ivec2 base = ivec2(floor(position));
	vec2 f = position - vec2(base);
	ivec2 last = ivec2(int(terrain_size) - 1);
	vec2 dx = dFdx(ground);
	vec2 dy = dFdy(ground);

	// What a layer without a texture shows.
	vec2 square = floor(vec2(ground.x, -ground.y) / cell_size);
	vec3 checker = mix(vec3(0.52), vec3(0.87), mod(square.x + square.y, 2.0));

	vec3 albedo = vec3(0.0);
	vec3 normal = vec3(0.0);
	float roughness = 0.0;
	float total = 0.0;
	if (has_weights > 0.5) {
		// The four samples around the point, each with its own layers.
		for (int c = 0; c < 4; c++) {
			ivec2 corner = ivec2(c & 1, c >> 1);
			float share = (corner.x == 1 ? f.x : 1.0 - f.x) * (corner.y == 1 ? f.y : 1.0 - f.y);
			if (share <= 0.0) {
				continue;
			}
			ivec2 texel = min(base + corner, last);
			vec4 numbers = texelFetch(weights_0, texel, 0) * 255.0;
			vec4 amounts = texelFetch(weights_1, texel, 0) * share;
			for (int k = 0; k < 4; k++) {
				if (amounts[k] > 0.0) {
					add_layer(int(numbers[k] + 0.5), amounts[k], checker, dx, dy, albedo, normal, roughness);
					total += amounts[k];
				}
			}
		}
	}
	if (total < 0.001) {
		albedo = vec3(0.0);
		normal = vec3(0.0);
		roughness = 0.0;
		add_layer(0, 1.0, checker, dx, dy, albedo, normal, roughness);
		total = 1.0;
	}

	ALBEDO = albedo / total;
	ROUGHNESS = roughness / total;
	NORMAL_MAP = normal / total;
	NORMAL_MAP_DEPTH = normal_strength;
}
)";
		code = code.replace("MAX_LAYERS", itos(TerrainData::MAX_LAYERS));
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, code);
	}
	RS::get_singleton()->material_set_shader(_get_material(), shader);
}

void TerrainLayerMaterial::_queue_update(bool p_arrays) {
	arrays_dirty = arrays_dirty || p_arrays;
	if (!update_queued) {
		update_queued = true;
		callable_mp(this, &TerrainLayerMaterial::_update).call_deferred();
	}
}

// A layer's texture as a slice of the arrays: p_size a side, with all its levels.
Ref<Image> TerrainLayerMaterial::_prepare_image(const Ref<Texture2D> &p_texture, int p_size, bool p_normal, bool p_compress) const {
	Ref<Image> image;
	if (p_texture.is_valid()) {
		image = p_texture->get_image();
		if (image.is_valid() && !image->is_empty()) {
			image = image->duplicate();
			if (image->is_compressed() && image->decompress() != OK) {
				image.unref();
			}
		} else {
			image.unref();
		}
	}
	if (image.is_null()) {
		image = Image::create_empty(p_size, p_size, false, Image::FORMAT_RGBA8);
		image->fill(p_normal ? Color(0.5, 0.5, 1.0) : Color(1, 1, 1));
	} else {
		image->clear_mipmaps();
		image->convert(Image::FORMAT_RGBA8);
		if (image->get_width() != p_size || image->get_height() != p_size) {
			image->resize(p_size, p_size, Image::INTERPOLATE_LANCZOS);
		}
	}
	image->generate_mipmaps(p_normal);
	if (p_compress) {
		image->compress_from_channels(Image::COMPRESS_S3TC, p_normal ? Image::USED_CHANNELS_RG : Image::USED_CHANNELS_RGB);
	}
	return image;
}

void TerrainLayerMaterial::_update() {
	update_queued = false;
	const int count = layers.size();

	Vector<float> params;
	Vector<float> tints;
	params.resize_initialized(TerrainData::MAX_LAYERS * 4);
	tints.resize_initialized(TerrainData::MAX_LAYERS * 4);
	for (int i = 0; i < count; i++) {
		const Layer &layer = layers[i];
		params.write[i * 4 + 0] = 1.0f / layer.tile_size;
		params.write[i * 4 + 1] = layer.roughness;
		params.write[i * 4 + 2] = layer.albedo.is_valid() ? 1.0f : 0.0f;
		params.write[i * 4 + 3] = layer.normal.is_valid() ? 1.0f : 0.0f;
		const Color tint = layer.color.srgb_to_linear();
		tints.write[i * 4 + 0] = tint.r;
		tints.write[i * 4 + 1] = tint.g;
		tints.write[i * 4 + 2] = tint.b;
		tints.write[i * 4 + 3] = 1.0f;
	}
	RS::get_singleton()->material_set_param(_get_material(), "layer_params", params);
	RS::get_singleton()->material_set_param(_get_material(), "layer_tints", tints);
	RS::get_singleton()->material_set_param(_get_material(), "layer_count", count);

	if (!arrays_dirty) {
		return;
	}
	arrays_dirty = false;
	// Block compression keeps many large layers affordable; without it the arrays stay raw.
	const bool compress = Image::_image_compress_bc_func != nullptr;
	// An array none of whose layers has a texture only holds placeholders.
	int albedo_size = 4;
	int normal_size = 4;
	for (int i = 0; i < count; i++) {
		if (layers[i].albedo.is_valid()) {
			albedo_size = texture_size;
		}
		if (layers[i].normal.is_valid()) {
			normal_size = texture_size;
		}
	}
	Vector<Ref<Image>> albedo_images;
	Vector<Ref<Image>> normal_images;
	bool uniform = true;
	for (int i = 0; i < count; i++) {
		Layer &layer = layers[i];
		if (layer.albedo_image.is_null() || layer.albedo_image->get_width() != albedo_size) {
			layer.albedo_image = _prepare_image(layer.albedo, albedo_size, false, compress);
		}
		if (layer.normal_image.is_null() || layer.normal_image->get_width() != normal_size) {
			layer.normal_image = _prepare_image(layer.normal, normal_size, true, compress);
		}
		albedo_images.push_back(layer.albedo_image);
		normal_images.push_back(layer.normal_image);
		uniform = uniform && layer.albedo_image->get_format() == layers[0].albedo_image->get_format() && layer.normal_image->get_format() == layers[0].normal_image->get_format();
	}
	if (!uniform) {
		// A layer that could not be compressed like the others: none are.
		albedo_images.clear();
		normal_images.clear();
		for (int i = 0; i < count; i++) {
			layers[i].albedo_image = _prepare_image(layers[i].albedo, albedo_size, false, false);
			layers[i].normal_image = _prepare_image(layers[i].normal, normal_size, true, false);
			albedo_images.push_back(layers[i].albedo_image);
			normal_images.push_back(layers[i].normal_image);
		}
	}
	albedo_array.instantiate();
	normal_array.instantiate();
	if (albedo_array->create_from_images(albedo_images) != OK || normal_array->create_from_images(normal_images) != OK) {
		ERR_PRINT("TerrainLayerMaterial: cannot build the layers' texture arrays.");
		return;
	}
	RS::get_singleton()->material_set_param(_get_material(), "albedo_array", albedo_array->get_rid());
	RS::get_singleton()->material_set_param(_get_material(), "normal_array", normal_array->get_rid());
}

void TerrainLayerMaterial::set_layer_count(int p_count) {
	p_count = CLAMP(p_count, 1, int(TerrainData::MAX_LAYERS));
	const int old_count = layers.size();
	if (p_count == old_count) {
		return;
	}
	layers.resize(p_count);
	for (int i = old_count; i < p_count; i++) {
		layers[i].color = _default_layer_color(i);
	}
	_queue_update(true);
	notify_property_list_changed();
	emit_changed();
}

int TerrainLayerMaterial::get_layer_count() const {
	return layers.size();
}

void TerrainLayerMaterial::set_texture_size(int p_size) {
	// A power of two, so that every level down to a single pixel exists.
	int side = 16;
	while (side < p_size && side < 4096) {
		side *= 2;
	}
	p_size = side;
	if (p_size == texture_size) {
		return;
	}
	texture_size = p_size;
	for (Layer &layer : layers) {
		layer.albedo_image.unref();
		layer.normal_image.unref();
	}
	_queue_update(true);
	emit_changed();
}

int TerrainLayerMaterial::get_texture_size() const {
	return texture_size;
}

void TerrainLayerMaterial::set_layer_material(int p_layer, const Ref<BaseMaterial3D> &p_material) {
	ERR_FAIL_INDEX(p_layer, int(layers.size()));
	if (layers[p_layer].material == p_material) {
		return;
	}
	layers[p_layer].material = p_material;
	if (p_material.is_null()) {
		return;
	}
	set_layer_albedo(p_layer, p_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO));
	set_layer_normal(p_layer, p_material->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING) ? p_material->get_texture(BaseMaterial3D::TEXTURE_NORMAL) : Ref<Texture2D>());
	const Color tint = p_material->get_albedo();
	set_layer_color(p_layer, Color(tint.r, tint.g, tint.b));
	set_layer_roughness(p_layer, p_material->get_roughness());
	notify_property_list_changed();
}

Ref<BaseMaterial3D> TerrainLayerMaterial::get_layer_material(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, int(layers.size()), Ref<BaseMaterial3D>());
	return layers[p_layer].material;
}

void TerrainLayerMaterial::set_layer_albedo(int p_layer, const Ref<Texture2D> &p_texture) {
	ERR_FAIL_INDEX(p_layer, int(layers.size()));
	layers[p_layer].albedo = p_texture;
	layers[p_layer].albedo_image.unref();
	if (p_texture.is_valid() && layers[p_layer].color == _default_layer_color(p_layer)) {
		// The placeholder colour would tint the texture.
		layers[p_layer].color = Color(1, 1, 1);
	}
	_queue_update(true);
	emit_changed();
}

Ref<Texture2D> TerrainLayerMaterial::get_layer_albedo(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, int(layers.size()), Ref<Texture2D>());
	return layers[p_layer].albedo;
}

void TerrainLayerMaterial::set_layer_normal(int p_layer, const Ref<Texture2D> &p_texture) {
	ERR_FAIL_INDEX(p_layer, int(layers.size()));
	layers[p_layer].normal = p_texture;
	layers[p_layer].normal_image.unref();
	_queue_update(true);
}

Ref<Texture2D> TerrainLayerMaterial::get_layer_normal(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, int(layers.size()), Ref<Texture2D>());
	return layers[p_layer].normal;
}

void TerrainLayerMaterial::set_layer_color(int p_layer, const Color &p_color) {
	ERR_FAIL_INDEX(p_layer, int(layers.size()));
	layers[p_layer].color = p_color;
	_queue_update(false);
	emit_changed();
}

Color TerrainLayerMaterial::get_layer_color(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, int(layers.size()), Color());
	return layers[p_layer].color;
}

void TerrainLayerMaterial::set_layer_tile_size(int p_layer, float p_size) {
	ERR_FAIL_INDEX(p_layer, int(layers.size()));
	layers[p_layer].tile_size = MAX(p_size, 0.01f);
	_queue_update(false);
}

float TerrainLayerMaterial::get_layer_tile_size(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, int(layers.size()), 1.0);
	return layers[p_layer].tile_size;
}

void TerrainLayerMaterial::set_layer_roughness(int p_layer, float p_roughness) {
	ERR_FAIL_INDEX(p_layer, int(layers.size()));
	layers[p_layer].roughness = p_roughness;
	_queue_update(false);
}

float TerrainLayerMaterial::get_layer_roughness(int p_layer) const {
	ERR_FAIL_INDEX_V(p_layer, int(layers.size()), 1.0);
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
	ERR_FAIL_INDEX(p_index, 2);
	RS::get_singleton()->material_set_param(_get_material(), "weights_" + itos(p_index), p_texture.is_valid() ? Variant(p_texture) : Variant());
	if (p_index == 1) {
		RS::get_singleton()->material_set_param(_get_material(), "has_weights", p_texture.is_valid() ? 1.0f : 0.0f);
	}
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

// The layers' settings are properties named layer_<number from 1>/<setting>.
static bool _parse_layer_property(const StringName &p_name, int &r_layer, String &r_what) {
	const String name = p_name;
	if (!name.begins_with("layer_")) {
		return false;
	}
	const int slash = name.find_char('/');
	if (slash < 0) {
		return false;
	}
	r_layer = name.substr(6, slash - 6).to_int() - 1;
	r_what = name.substr(slash + 1);
	return r_layer >= 0 && r_layer < int(TerrainData::MAX_LAYERS);
}

bool TerrainLayerMaterial::_set(const StringName &p_name, const Variant &p_value) {
	int layer;
	String what;
	if (!_parse_layer_property(p_name, layer, what)) {
		return false;
	}
	if (layer >= int(layers.size())) {
		set_layer_count(layer + 1);
	}
	if (what == "material") {
		set_layer_material(layer, p_value);
	} else if (what == "albedo") {
		set_layer_albedo(layer, p_value);
	} else if (what == "normal") {
		set_layer_normal(layer, p_value);
	} else if (what == "color") {
		set_layer_color(layer, p_value);
	} else if (what == "tile_size") {
		set_layer_tile_size(layer, p_value);
	} else if (what == "roughness") {
		set_layer_roughness(layer, p_value);
	} else {
		return false;
	}
	return true;
}

bool TerrainLayerMaterial::_get(const StringName &p_name, Variant &r_ret) const {
	int layer;
	String what;
	if (!_parse_layer_property(p_name, layer, what) || layer >= int(layers.size())) {
		return false;
	}
	if (what == "material") {
		r_ret = layers[layer].material;
	} else if (what == "albedo") {
		r_ret = layers[layer].albedo;
	} else if (what == "normal") {
		r_ret = layers[layer].normal;
	} else if (what == "color") {
		r_ret = layers[layer].color;
	} else if (what == "tile_size") {
		r_ret = layers[layer].tile_size;
	} else if (what == "roughness") {
		r_ret = layers[layer].roughness;
	} else {
		return false;
	}
	return true;
}

void TerrainLayerMaterial::_get_property_list(List<PropertyInfo> *p_list) const {
	for (uint32_t i = 0; i < layers.size(); i++) {
		const String prefix = vformat("layer_%d/", i + 1);
		p_list->push_back(PropertyInfo(Variant::NIL, vformat("Layer %d", i + 1), PROPERTY_HINT_NONE, prefix, PROPERTY_USAGE_GROUP));
		// Before the values it fills in, so that a saved layer keeps what was adjusted afterwards.
		p_list->push_back(PropertyInfo(Variant::OBJECT, prefix + "material", PROPERTY_HINT_RESOURCE_TYPE, BaseMaterial3D::get_class_static()));
		p_list->push_back(PropertyInfo(Variant::OBJECT, prefix + "albedo", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()));
		p_list->push_back(PropertyInfo(Variant::OBJECT, prefix + "normal", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()));
		p_list->push_back(PropertyInfo(Variant::COLOR, prefix + "color", PROPERTY_HINT_COLOR_NO_ALPHA));
		p_list->push_back(PropertyInfo(Variant::FLOAT, prefix + "tile_size", PROPERTY_HINT_RANGE, "0.1,256,0.01,or_greater,suffix:m"));
		p_list->push_back(PropertyInfo(Variant::FLOAT, prefix + "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"));
	}
}

void TerrainLayerMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_layer_count", "count"), &TerrainLayerMaterial::set_layer_count);
	ClassDB::bind_method(D_METHOD("get_layer_count"), &TerrainLayerMaterial::get_layer_count);
	ClassDB::bind_method(D_METHOD("set_texture_size", "size"), &TerrainLayerMaterial::set_texture_size);
	ClassDB::bind_method(D_METHOD("get_texture_size"), &TerrainLayerMaterial::get_texture_size);
	ClassDB::bind_method(D_METHOD("set_layer_material", "layer", "material"), &TerrainLayerMaterial::set_layer_material);
	ClassDB::bind_method(D_METHOD("get_layer_material", "layer"), &TerrainLayerMaterial::get_layer_material);
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

	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer_count", PROPERTY_HINT_RANGE, vformat("1,%d,1", int(TerrainData::MAX_LAYERS))), "set_layer_count", "get_layer_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_size", PROPERTY_HINT_ENUM, "64:64,128:128,256:256,512:512,1024:1024,2048:2048,4096:4096"), "set_texture_size", "get_texture_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_normal_strength", "get_normal_strength");
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
	layers.resize(8);
	for (int i = 0; i < 8; i++) {
		layers[i].color = _default_layer_color(i);
	}
	set_normal_strength(1.0);
	_queue_update(true);
}

TerrainLayerMaterial::~TerrainLayerMaterial() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
