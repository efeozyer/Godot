#include "terrain_material.h"

#include "effect_common.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex TerrainMaterial::shader_mutex;
RID TerrainMaterial::shaders[2];

void TerrainMaterial::_update_shader() {
	MutexLock shader_lock(shader_mutex);

	const int variant = tile_alpha_test ? 1 : 0;
	if (shaders[variant].is_valid()) {
		RS::get_singleton()->material_set_shader(_get_material(), shaders[variant]);
		return;
	}

	// The effect is unlit as far as the engine is concerned: its own light term is computed in
	// fragment(), and light() only takes the directional light's shadow.
	String code = "shader_type spatial;\nrender_mode ambient_light_disabled, specular_disabled, fog_disabled;\n";
	code += effect_shader_functions;
	code += R"(
// baseMap, baseMap1: clamped, linear with mip maps.
uniform sampler2DArray tile_textures : filter_linear_mipmap, repeat_disable;
uniform sampler2D cell_map : filter_nearest, repeat_disable;
uniform sampler2D color_map : filter_linear_mipmap, repeat_disable;
uniform sampler2D light_map : filter_linear, repeat_disable;
uniform bool has_color_map = false;
uniform bool light_map_enabled = false;

uniform float cell_size = 4.0; // TILE_SIZE
uniform float map_cells = 256.0;
uniform float color_map_cells = 32.0; // UNITUV
uniform float color_map_texture_size = 128.0; // COLORMAPTEX_SIZE
uniform float light_map_cells = 4.0;
uniform float light_map_texture_size = 32.0;
uniform float tile_patches = 8.0; // CTerrain::m_iLodLevel

uniform bool bump_enabled = true; // bNormal
uniform bool night = false; // bNight
uniform bool shadow_enabled = true; // bShadow
uniform vec3 normal_light = vec3(0.0, 1.0, 0.0); // see effect_normal_light()
uniform vec3 bump_light = vec3(0.0, -1.0, 0.0); // LightDirection, normalized, in the client's space
uniform vec3 light_diffuse = vec3(1.0); // fvDiffuse
uniform vec3 light_specular = vec3(1.0); // fvSpecular

uniform bool fog_enabled = true;
uniform vec3 fog_color = vec3(1.0); // D3DRS_FOGCOLOR
uniform vec2 fog_range = vec2(0.7, 512.0); // g_Fog

const float PATCH_CELLS = 8.0; // PATCH_TILE_SIZE
const float SOBEL_STEP = 0.0078125; // c7 of the pixel shader: one texel of a 128 pixel texture
const vec3 LUMINANCE = vec3(0.4, 0.59, 0.11); // c8 of the pixel shader

// CTerrain::m_fTileDirU / m_fTileDirV: texture coordinates at the cell corners
// (x low z high, x high z high, x low z low, x high z low) for each direction.
const vec4 TILE_DIR_U[8] = { vec4(0, 1, 0, 1), vec4(0, 0, 1, 1), vec4(1, 0, 1, 0), vec4(1, 1, 0, 0),
		vec4(1, 0, 1, 0), vec4(0, 0, 1, 1), vec4(0, 1, 0, 1), vec4(1, 1, 0, 0) };
const vec4 TILE_DIR_V[8] = { vec4(0, 0, 1, 1), vec4(1, 0, 1, 0), vec4(1, 1, 0, 0), vec4(0, 1, 0, 1),
		vec4(0, 0, 1, 1), vec4(0, 1, 0, 1), vec4(1, 1, 0, 0), vec4(1, 0, 1, 0) };

varying vec3 model_pos;
varying float vertex_light;
varying flat vec2 camera_patch;

// Written by fragment() for light().
varying vec3 effect_color;
varying vec4 light_map_color;
varying float fog_factor;

void vertex() {
	model_pos = VERTEX;

	// oT4 of the vertex shader, already taken against the light as the pixel shader does (t4 is
	// not normalized again there, so the product can be interpolated instead of the vector).
	vertex_light = dot(normalize(mat3(MODEL_MATRIX) * NORMAL), normal_light);

	// CTerrain::CheckMovePatch: the patch the camera is over.
	vec3 eye = (inverse(MODEL_MATRIX) * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz;
	camera_patch = floor(vec2(eye.x, -eye.z) / (cell_size * PATCH_CELLS));
}

// One layer of the pixel shader. The layer's texture stands in for a height map: a Sobel filter
// over its eight neighbours, one SOBEL_STEP apart, gives a slope, (slope, 1) is taken as a normal,
// packed as n * 0.75 + 0.25 and lit by the sun direction as it is, without regard to how the
// surface or the texture is turned. The vertex-normal term is added at half strength by day.
vec3 lit_layer(vec3 tex, vec3 sobel_u, vec3 sobel_v, float from_vertex) {
	vec3 n = normalize(vec3(dot(sobel_u, LUMINANCE), dot(sobel_v, LUMINANCE), 1.0));
	n = normalize(n * 0.75 + 0.25);
	vec3 bump = tex * light_diffuse * dot(n, bump_light);
	vec3 base = tex * light_diffuse * from_vertex;
	return clamp(bump + (night ? base : base * 0.5), 0.0, 1.0);
}

// A tile in one of its eight orientations. f = position inside the cell, dx / dy = screen
// derivatives of the position in cells.
vec4 tile_layer(float index, int dir, vec2 f, vec2 dx, vec2 dy, float from_vertex) {
	vec4 u = TILE_DIR_U[dir];
	vec4 v = TILE_DIR_V[dir];
	vec2 uv = vec2(mix(mix(u.z, u.w, f.x), mix(u.x, u.y, f.x), f.y), mix(mix(v.z, v.w, f.x), mix(v.x, v.y, f.x), f.y));
	// The mapping is a rotation or a mirror, so its derivatives follow from the corner values.
	// Taking them from the cell position keeps the mip level steady across cell borders.
	vec2 along_x = vec2(u.w - u.z, v.w - v.z);
	vec2 along_z = vec2(u.x - u.z, v.x - v.z);
	vec2 gx = along_x * dx.x + along_z * dx.y;
	vec2 gy = along_x * dy.x + along_z * dy.y;
	vec3 uvw = vec3(uv, index);
	vec4 tex = textureGrad(tile_textures, uvw, gx, gy);
	if (!bump_enabled) {
		return tex;
	}
	float e = SOBEL_STEP;
	vec3 lt = textureGrad(tile_textures, uvw + vec3(-e, -e, 0.0), gx, gy).rgb;
	vec3 ct = textureGrad(tile_textures, uvw + vec3(0.0, -e, 0.0), gx, gy).rgb;
	vec3 rt = textureGrad(tile_textures, uvw + vec3(e, -e, 0.0), gx, gy).rgb;
	vec3 lm = textureGrad(tile_textures, uvw + vec3(-e, 0.0, 0.0), gx, gy).rgb;
	vec3 rm = textureGrad(tile_textures, uvw + vec3(e, 0.0, 0.0), gx, gy).rgb;
	vec3 lb = textureGrad(tile_textures, uvw + vec3(-e, e, 0.0), gx, gy).rgb;
	vec3 cb = textureGrad(tile_textures, uvw + vec3(0.0, e, 0.0), gx, gy).rgb;
	vec3 rb = textureGrad(tile_textures, uvw + vec3(e, e, 0.0), gx, gy).rgb;
	return vec4(lit_layer(tex.rgb, lt + 2.0 * lm + lb - rt - 2.0 * rm - rb, lt + 2.0 * ct + rt - lb - 2.0 * cb - rb, from_vertex), tex.a);
}

// The colour map atlas at uv inside one of its textures. The effect clamps at the texture's edge.
vec3 color_map_tap(vec2 origin, vec2 uv, float blocks, vec2 gx, vec2 gy) {
	float half_texel = 0.5 / color_map_texture_size;
	return textureGrad(color_map, (origin + clamp(uv, vec2(half_texel), vec2(1.0 - half_texel))) / blocks, gx, gy).rgb;
}

// The colour map as the only layer, which is what the patches away from the camera draw.
vec3 color_map_layer(vec2 p, vec2 dx, vec2 dy, float from_vertex) {
	float blocks = map_cells / color_map_cells;
	vec2 block = floor(p / color_map_cells);
	vec2 uv = p / color_map_cells - block;
	uv.y = 1.0 - uv.y; // v runs against z
	// CTerrainPatch::UVConvert keeps the lookups one texel inside each texture.
	float inner = (color_map_texture_size - 2.0) / color_map_texture_size;
	uv = uv * inner + 1.0 / color_map_texture_size;
	vec2 origin = vec2(block.x, blocks - 1.0 - block.y);
	vec2 scale = vec2(inner, -inner) / (color_map_cells * blocks);
	vec2 gx = dx * scale;
	vec2 gy = dy * scale;
	vec3 tex = color_map_tap(origin, uv, blocks, gx, gy);
	if (!bump_enabled) {
		return tex;
	}
	float e = SOBEL_STEP;
	vec3 lt = color_map_tap(origin, uv + vec2(-e, -e), blocks, gx, gy);
	vec3 ct = color_map_tap(origin, uv + vec2(0.0, -e), blocks, gx, gy);
	vec3 rt = color_map_tap(origin, uv + vec2(e, -e), blocks, gx, gy);
	vec3 lm = color_map_tap(origin, uv + vec2(-e, 0.0), blocks, gx, gy);
	vec3 rm = color_map_tap(origin, uv + vec2(e, 0.0), blocks, gx, gy);
	vec3 lb = color_map_tap(origin, uv + vec2(-e, e), blocks, gx, gy);
	vec3 cb = color_map_tap(origin, uv + vec2(0.0, e), blocks, gx, gy);
	vec3 rb = color_map_tap(origin, uv + vec2(e, e), blocks, gx, gy);
	return lit_layer(tex, lt + 2.0 * lm + lb - rt - 2.0 * rm - rb, lt + 2.0 * ct + rt - lb - 2.0 * cb - rb, from_vertex);
}

// The end of the pixel shader and what the client does to its output: the shadow term, the light
// map drawn over it, and the fog. attenuation is Godot's directional shadow, 1 where lit.
vec3 shaded(vec3 color, float attenuation, vec4 over, float fog) {
	if (shadow_enabled) {
		float lit = effect_shadow_factor(attenuation);
		color = night ? color * (lit * 0.5 + 0.5) * light_specular : color * (lit * 0.8 + 0.2);
		color = clamp(color, 0.1, 1.0);
	}
	color = min(color, vec3(1.0)); // the back buffer does not hold more
	color = mix(color, over.rgb, over.a); // D3DBLEND_SRCALPHA / D3DBLEND_INVSRCALPHA
	color = mix(fog_color, color, fog);
	return effect_to_linear(color);
}

void fragment() {
	vec2 p = vec2(model_pos.x, -model_pos.z) / cell_size; // position in cells
	vec2 dx = dFdx(p);
	vec2 dy = dFdy(p);
	p = clamp(p, vec2(0.0), vec2(map_cells - 0.001));
	vec2 cell = floor(p);
	vec2 f = p - cell;
	ivec4 info = ivec4(texelFetch(cell_map, ivec2(cell), 0) * 255.0 + 0.5);

	// CTerrain::SetLODLevel: a patch shows its tiles when its distance from the camera's patch,
	// counted in patches as in MakeDistanceTable, is within the level; beyond that it shows the
	// colour map. A cell without a tile shows the colour map at any distance.
	bool tiles = (info.a & 16) != 0;
	if (tiles && has_color_map) {
		vec2 away = abs(floor(cell / PATCH_CELLS) - camera_patch);
		tiles = floor(length(away) + 0.6) <= tile_patches;
	}

	vec3 color = vec3(0.0);
	if (tiles) {
		vec4 first = tile_layer(float(info.r | ((info.a & 3) << 8)), info.b & 7, f, dx, dy, vertex_light);
		// A cell with one tile has no second texture bound, which samples as black.
		vec4 second = vec4(0.0, 0.0, 0.0, 1.0);
		if ((info.a & 32) != 0) {
			second = tile_layer(float(info.g | ((info.a & 12) << 6)), (info.b >> 3) & 7, f, dx, dy, vertex_light);
		}
		TILE_ALPHA_TEST
		// The tile set holds pre-masked edge pieces, so the sum of the two layers is a blend.
		color = first.rgb + second.rgb;
	} else if (has_color_map) {
		color = color_map_layer(p, dx, dy, vertex_light);
	}

	vec4 over = vec4(0.0);
	if (light_map_enabled) {
		// CTerrainPatch::Tick: each cell uses the inner 6 of its 8 texels.
		float per_cell = light_map_texture_size / light_map_cells;
		vec2 lm_block = floor(p / light_map_cells);
		vec2 in_lm = cell - lm_block * light_map_cells;
		vec2 texel = vec2(in_lm.x * per_cell + 1.0 + f.x * (per_cell - 2.0), light_map_texture_size - 1.0 - in_lm.y * per_cell - f.y * (per_cell - 2.0));
		float lm_blocks = map_cells / light_map_cells;
		over = texture(light_map, (vec2(lm_block.x, lm_blocks - 1.0 - lm_block.y) * light_map_texture_size + texel) / (lm_blocks * light_map_texture_size));
	}

	effect_color = color;
	light_map_color = over;
	fog_factor = fog_enabled ? effect_fog_factor(-VERTEX.z, fog_range.x, fog_range.y) : 1.0;

	// The lit result goes out as emission; light() takes the shadow off it.
	ALBEDO = vec3(1.0);
	ROUGHNESS = 1.0;
	SPECULAR = 0.0;
	EMISSION = shaded(color, 1.0, over, fog_factor);
}

void light() {
	if (LIGHT_IS_DIRECTIONAL) {
		DIFFUSE_LIGHT += shaded(effect_color, ATTENUATION, light_map_color, fog_factor) - shaded(effect_color, 1.0, light_map_color, fog_factor);
	}
}
)";

	// The two texkill instructions; bBlend is always set. Left out when no tile can fail the test,
	// which keeps the terrain out of the passes that only need its depth.
	code = code.replace("TILE_ALPHA_TEST", tile_alpha_test ? "if (first.a < 0.3 || second.a < 0.3) {\n\t\t\tdiscard;\n\t\t}" : "");

	shaders[variant] = RS::get_singleton()->shader_create();
	RS::get_singleton()->shader_set_code(shaders[variant], code);
	RS::get_singleton()->material_set_shader(_get_material(), shaders[variant]);
}

void TerrainMaterial::_update_light() {
	_apply_world(effect_world_get());
}

// Everything the sky decides. While a sky publishes its state the material follows it; its own
// properties are what it shows without one.
void TerrainMaterial::_apply_world(const EffectWorld &p_world) {
	const bool world_night = p_world.active ? p_world.night : night;
	// The client normalizes the direction before it uploads it.
	const Vector3 direction = (p_world.active ? p_world.light_direction : light_direction).normalized();
	const Vector3 color = p_world.active ? p_world.light_color : Vector3(light_specular.r, light_specular.g, light_specular.b);
	const Vector3 fog = p_world.active ? p_world.fog_color : Vector3(fog_color.r, fog_color.g, fog_color.b);
	RS::get_singleton()->material_set_param(_get_material(), "night", world_night);
	RS::get_singleton()->material_set_param(_get_material(), "normal_light", effect_normal_light(direction, shadow_distance, world_night));
	RS::get_singleton()->material_set_param(_get_material(), "bump_light", Vector3(direction.x, direction.y, -direction.z));
	RS::get_singleton()->material_set_param(_get_material(), "light_specular", Vector3(color.x, color.y, color.z));
	RS::get_singleton()->material_set_param(_get_material(), "fog_color", fog);
	RS::get_singleton()->material_set_param(_get_material(), "fog_range", Vector2(fog_near, p_world.active ? p_world.fog_far : fog_far));
}

void TerrainMaterial::_effect_world_changed(const EffectWorld &p_world) {
	_apply_world(p_world);
}

void TerrainMaterial::set_tile_textures(const Ref<TextureLayered> &p_texture) {
	tile_textures = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "tile_textures", tex_rid);
	emit_changed();
}

Ref<TextureLayered> TerrainMaterial::get_tile_textures() const {
	return tile_textures;
}

void TerrainMaterial::set_cell_map(const Ref<Texture2D> &p_texture) {
	cell_map = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "cell_map", tex_rid);
}

Ref<Texture2D> TerrainMaterial::get_cell_map() const {
	return cell_map;
}

void TerrainMaterial::set_color_map(const Ref<Texture2D> &p_texture) {
	color_map = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "color_map", tex_rid);
	RS::get_singleton()->material_set_param(_get_material(), "has_color_map", p_texture.is_valid());
}

Ref<Texture2D> TerrainMaterial::get_color_map() const {
	return color_map;
}

void TerrainMaterial::set_light_map(const Ref<Texture2D> &p_texture) {
	light_map = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "light_map", tex_rid);
	RS::get_singleton()->material_set_param(_get_material(), "light_map_enabled", light_map_enabled && light_map.is_valid());
}

Ref<Texture2D> TerrainMaterial::get_light_map() const {
	return light_map;
}

void TerrainMaterial::set_light_map_enabled(bool p_enable) {
	light_map_enabled = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "light_map_enabled", light_map_enabled && light_map.is_valid());
}

bool TerrainMaterial::is_light_map_enabled() const {
	return light_map_enabled;
}

void TerrainMaterial::set_cell_size(float p_size) {
	cell_size = MAX(p_size, 0.001f);
	RS::get_singleton()->material_set_param(_get_material(), "cell_size", cell_size);
}

float TerrainMaterial::get_cell_size() const {
	return cell_size;
}

void TerrainMaterial::set_map_cells(int p_cells) {
	map_cells = MAX(p_cells, 1);
	RS::get_singleton()->material_set_param(_get_material(), "map_cells", float(map_cells));
}

int TerrainMaterial::get_map_cells() const {
	return map_cells;
}

void TerrainMaterial::set_color_map_cells(int p_cells) {
	color_map_cells = MAX(p_cells, 1);
	RS::get_singleton()->material_set_param(_get_material(), "color_map_cells", float(color_map_cells));
}

int TerrainMaterial::get_color_map_cells() const {
	return color_map_cells;
}

void TerrainMaterial::set_color_map_texture_size(int p_size) {
	color_map_texture_size = MAX(p_size, 4);
	RS::get_singleton()->material_set_param(_get_material(), "color_map_texture_size", float(color_map_texture_size));
}

int TerrainMaterial::get_color_map_texture_size() const {
	return color_map_texture_size;
}

void TerrainMaterial::set_light_map_cells(int p_cells) {
	light_map_cells = MAX(p_cells, 1);
	RS::get_singleton()->material_set_param(_get_material(), "light_map_cells", float(light_map_cells));
}

int TerrainMaterial::get_light_map_cells() const {
	return light_map_cells;
}

void TerrainMaterial::set_light_map_texture_size(int p_size) {
	light_map_texture_size = MAX(p_size, 4);
	RS::get_singleton()->material_set_param(_get_material(), "light_map_texture_size", float(light_map_texture_size));
}

int TerrainMaterial::get_light_map_texture_size() const {
	return light_map_texture_size;
}

void TerrainMaterial::set_tile_patches(int p_patches) {
	// CTerrain::Tick caps the level at MAX_LOD_LEVEL and SetLODLevel raises it to 2.
	tile_patches = CLAMP(p_patches, 2, 10);
	RS::get_singleton()->material_set_param(_get_material(), "tile_patches", float(tile_patches));
}

int TerrainMaterial::get_tile_patches() const {
	return tile_patches;
}

void TerrainMaterial::set_tile_alpha_test(bool p_enable) {
	tile_alpha_test = p_enable;
	_update_shader();
}

bool TerrainMaterial::is_tile_alpha_test() const {
	return tile_alpha_test;
}

void TerrainMaterial::set_bump_enabled(bool p_enable) {
	bump_enabled = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "bump_enabled", bump_enabled);
}

bool TerrainMaterial::is_bump_enabled() const {
	return bump_enabled;
}

void TerrainMaterial::set_night(bool p_night) {
	night = p_night;
	_update_light();
	notify_property_list_changed();
}

bool TerrainMaterial::is_night() const {
	return night;
}

void TerrainMaterial::set_light_direction(const Vector3 &p_direction) {
	light_direction = p_direction;
	_update_light();
}

Vector3 TerrainMaterial::get_light_direction() const {
	return light_direction;
}

void TerrainMaterial::set_light_diffuse(const Color &p_color) {
	light_diffuse = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "light_diffuse", Vector3(light_diffuse.r, light_diffuse.g, light_diffuse.b));
}

Color TerrainMaterial::get_light_diffuse() const {
	return light_diffuse;
}

void TerrainMaterial::set_light_specular(const Color &p_color) {
	light_specular = p_color;
	_update_light();
}

Color TerrainMaterial::get_light_specular() const {
	return light_specular;
}

void TerrainMaterial::set_shadow_enabled(bool p_enable) {
	shadow_enabled = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "shadow_enabled", shadow_enabled);
}

bool TerrainMaterial::is_shadow_enabled() const {
	return shadow_enabled;
}

void TerrainMaterial::set_shadow_distance(float p_distance) {
	shadow_distance = p_distance;
	_update_light();
}

float TerrainMaterial::get_shadow_distance() const {
	return shadow_distance;
}

void TerrainMaterial::set_fog_enabled(bool p_enable) {
	fog_enabled = p_enable;
	RS::get_singleton()->material_set_param(_get_material(), "fog_enabled", fog_enabled);
	notify_property_list_changed();
}

bool TerrainMaterial::is_fog_enabled() const {
	return fog_enabled;
}

void TerrainMaterial::set_fog_color(const Color &p_color) {
	fog_color = p_color;
	_update_light();
}

Color TerrainMaterial::get_fog_color() const {
	return fog_color;
}

void TerrainMaterial::set_fog_near(float p_near) {
	fog_near = p_near;
	_update_light();
}

float TerrainMaterial::get_fog_near() const {
	return fog_near;
}

void TerrainMaterial::set_fog_far(float p_far) {
	fog_far = p_far;
	_update_light();
}

float TerrainMaterial::get_fog_far() const {
	return fog_far;
}

Shader::Mode TerrainMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID TerrainMaterial::get_shader_rid() const {
	MutexLock shader_lock(shader_mutex);
	return shaders[tile_alpha_test ? 1 : 0];
}

void TerrainMaterial::_validate_property(PropertyInfo &p_property) const {
	// The effect only uses fvSpecular in its night branch.
	if (p_property.name == "light_specular" && !night) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if ((p_property.name == "fog_color" || p_property.name == "fog_near" || p_property.name == "fog_far") && !fog_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void TerrainMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tile_textures", "texture"), &TerrainMaterial::set_tile_textures);
	ClassDB::bind_method(D_METHOD("get_tile_textures"), &TerrainMaterial::get_tile_textures);
	ClassDB::bind_method(D_METHOD("set_cell_map", "texture"), &TerrainMaterial::set_cell_map);
	ClassDB::bind_method(D_METHOD("get_cell_map"), &TerrainMaterial::get_cell_map);
	ClassDB::bind_method(D_METHOD("set_color_map", "texture"), &TerrainMaterial::set_color_map);
	ClassDB::bind_method(D_METHOD("get_color_map"), &TerrainMaterial::get_color_map);
	ClassDB::bind_method(D_METHOD("set_light_map", "texture"), &TerrainMaterial::set_light_map);
	ClassDB::bind_method(D_METHOD("get_light_map"), &TerrainMaterial::get_light_map);
	ClassDB::bind_method(D_METHOD("set_light_map_enabled", "enable"), &TerrainMaterial::set_light_map_enabled);
	ClassDB::bind_method(D_METHOD("is_light_map_enabled"), &TerrainMaterial::is_light_map_enabled);
	ClassDB::bind_method(D_METHOD("set_cell_size", "size"), &TerrainMaterial::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &TerrainMaterial::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_map_cells", "cells"), &TerrainMaterial::set_map_cells);
	ClassDB::bind_method(D_METHOD("get_map_cells"), &TerrainMaterial::get_map_cells);
	ClassDB::bind_method(D_METHOD("set_color_map_cells", "cells"), &TerrainMaterial::set_color_map_cells);
	ClassDB::bind_method(D_METHOD("get_color_map_cells"), &TerrainMaterial::get_color_map_cells);
	ClassDB::bind_method(D_METHOD("set_color_map_texture_size", "size"), &TerrainMaterial::set_color_map_texture_size);
	ClassDB::bind_method(D_METHOD("get_color_map_texture_size"), &TerrainMaterial::get_color_map_texture_size);
	ClassDB::bind_method(D_METHOD("set_light_map_cells", "cells"), &TerrainMaterial::set_light_map_cells);
	ClassDB::bind_method(D_METHOD("get_light_map_cells"), &TerrainMaterial::get_light_map_cells);
	ClassDB::bind_method(D_METHOD("set_light_map_texture_size", "size"), &TerrainMaterial::set_light_map_texture_size);
	ClassDB::bind_method(D_METHOD("get_light_map_texture_size"), &TerrainMaterial::get_light_map_texture_size);
	ClassDB::bind_method(D_METHOD("set_tile_patches", "patches"), &TerrainMaterial::set_tile_patches);
	ClassDB::bind_method(D_METHOD("get_tile_patches"), &TerrainMaterial::get_tile_patches);
	ClassDB::bind_method(D_METHOD("set_tile_alpha_test", "enable"), &TerrainMaterial::set_tile_alpha_test);
	ClassDB::bind_method(D_METHOD("is_tile_alpha_test"), &TerrainMaterial::is_tile_alpha_test);
	ClassDB::bind_method(D_METHOD("set_bump_enabled", "enable"), &TerrainMaterial::set_bump_enabled);
	ClassDB::bind_method(D_METHOD("is_bump_enabled"), &TerrainMaterial::is_bump_enabled);
	ClassDB::bind_method(D_METHOD("set_night", "night"), &TerrainMaterial::set_night);
	ClassDB::bind_method(D_METHOD("is_night"), &TerrainMaterial::is_night);
	ClassDB::bind_method(D_METHOD("set_light_direction", "direction"), &TerrainMaterial::set_light_direction);
	ClassDB::bind_method(D_METHOD("get_light_direction"), &TerrainMaterial::get_light_direction);
	ClassDB::bind_method(D_METHOD("set_light_diffuse", "color"), &TerrainMaterial::set_light_diffuse);
	ClassDB::bind_method(D_METHOD("get_light_diffuse"), &TerrainMaterial::get_light_diffuse);
	ClassDB::bind_method(D_METHOD("set_light_specular", "color"), &TerrainMaterial::set_light_specular);
	ClassDB::bind_method(D_METHOD("get_light_specular"), &TerrainMaterial::get_light_specular);
	ClassDB::bind_method(D_METHOD("set_shadow_enabled", "enable"), &TerrainMaterial::set_shadow_enabled);
	ClassDB::bind_method(D_METHOD("is_shadow_enabled"), &TerrainMaterial::is_shadow_enabled);
	ClassDB::bind_method(D_METHOD("set_shadow_distance", "distance"), &TerrainMaterial::set_shadow_distance);
	ClassDB::bind_method(D_METHOD("get_shadow_distance"), &TerrainMaterial::get_shadow_distance);
	ClassDB::bind_method(D_METHOD("set_fog_enabled", "enable"), &TerrainMaterial::set_fog_enabled);
	ClassDB::bind_method(D_METHOD("is_fog_enabled"), &TerrainMaterial::is_fog_enabled);
	ClassDB::bind_method(D_METHOD("set_fog_color", "color"), &TerrainMaterial::set_fog_color);
	ClassDB::bind_method(D_METHOD("get_fog_color"), &TerrainMaterial::get_fog_color);
	ClassDB::bind_method(D_METHOD("set_fog_near", "distance"), &TerrainMaterial::set_fog_near);
	ClassDB::bind_method(D_METHOD("get_fog_near"), &TerrainMaterial::get_fog_near);
	ClassDB::bind_method(D_METHOD("set_fog_far", "distance"), &TerrainMaterial::set_fog_far);
	ClassDB::bind_method(D_METHOD("get_fog_far"), &TerrainMaterial::get_fog_far);

	ADD_GROUP("Layers", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tile_textures", PROPERTY_HINT_RESOURCE_TYPE, TextureLayered::get_class_static()), "set_tile_textures", "get_tile_textures");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cell_map", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_cell_map", "get_cell_map");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "color_map", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_color_map", "get_color_map");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "light_map", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_light_map", "get_light_map");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "light_map_enabled"), "set_light_map_enabled", "is_light_map_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cell_size", PROPERTY_HINT_RANGE, "0.25,64,0.25,or_greater,suffix:m"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "map_cells", PROPERTY_HINT_RANGE, "1,4096,1"), "set_map_cells", "get_map_cells");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "color_map_cells", PROPERTY_HINT_RANGE, "1,4096,1"), "set_color_map_cells", "get_color_map_cells");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "color_map_texture_size", PROPERTY_HINT_RANGE, "4,4096,1,suffix:px"), "set_color_map_texture_size", "get_color_map_texture_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "light_map_cells", PROPERTY_HINT_RANGE, "1,4096,1"), "set_light_map_cells", "get_light_map_cells");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "light_map_texture_size", PROPERTY_HINT_RANGE, "4,4096,1,suffix:px"), "set_light_map_texture_size", "get_light_map_texture_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tile_patches", PROPERTY_HINT_RANGE, "2,10,1"), "set_tile_patches", "get_tile_patches");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tile_alpha_test"), "set_tile_alpha_test", "is_tile_alpha_test");

	ADD_GROUP("Light", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bump_enabled"), "set_bump_enabled", "is_bump_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "night"), "set_night", "is_night");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "light_direction"), "set_light_direction", "get_light_direction");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "light_diffuse", PROPERTY_HINT_COLOR_NO_ALPHA), "set_light_diffuse", "get_light_diffuse");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "light_specular", PROPERTY_HINT_COLOR_NO_ALPHA), "set_light_specular", "get_light_specular");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shadow_enabled"), "set_shadow_enabled", "is_shadow_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shadow_distance", PROPERTY_HINT_RANGE, "1,1024,1,or_greater,suffix:m"), "set_shadow_distance", "get_shadow_distance");

	ADD_GROUP("Fog", "fog_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fog_enabled"), "set_fog_enabled", "is_fog_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fog_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_fog_color", "get_fog_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_near", PROPERTY_HINT_RANGE, "0.01,16,0.01,or_greater,suffix:m"), "set_fog_near", "get_fog_near");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_far", PROPERTY_HINT_RANGE, "1,4096,1,or_greater,suffix:m"), "set_fog_far", "get_fog_far");
}

void TerrainMaterial::cleanup_shader() {
	MutexLock shader_lock(shader_mutex);
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	for (RID &shader : shaders) {
		if (shader.is_valid()) {
			RS::get_singleton()->free_rid(shader);
			shader = RID();
		}
	}
}

TerrainMaterial::TerrainMaterial() {
	_set_material(RS::get_singleton()->material_create());

	_update_shader();
	set_light_map_enabled(false);
	set_cell_size(4.0);
	set_map_cells(256);
	set_color_map_cells(32);
	set_color_map_texture_size(128);
	set_light_map_cells(4);
	set_light_map_texture_size(32);
	set_tile_patches(8);
	set_bump_enabled(true);
	set_night(false);
	set_light_diffuse(Color(1, 1, 1, 1));
	set_light_specular(Color(1, 1, 1, 1));
	set_shadow_enabled(true);
	set_fog_enabled(true);
	set_fog_color(fog_color);
	set_fog_far(512.0);
	_update_light();
}

TerrainMaterial::~TerrainMaterial() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
