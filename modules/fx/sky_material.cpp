#include "sky_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex SkyMaterial::shader_mutex;
RID SkyMaterial::shaders[SkyMaterial::PART_MAX];

// Every layer ends up at the far plane (depth 0 with the reversed depth buffer), behind anything
// the scene has drawn, and takes no fog: the client draws its sky with fog and depth switched off.

// CSky::Render. The mesh holds the two quads in the client's local space (X to the right,
// Y up, Z the distance in front of the camera); UV.x is the vertex alpha.
static const char *shader_horizon = R"(shader_type spatial;
render_mode blend_mix, unshaded, cull_disabled, depth_draw_never, fog_disabled, shadows_disabled;

uniform vec4 color : source_color = vec4(1.0);

varying float fade;

void vertex() {
	// The quads turn about the Y axis to stay in front of the camera, whatever its pitch.
	vec3 back = INV_VIEW_MATRIX[2].xyz;
	vec3 forward = vec3(-back.x, 0.0, -back.z);
	float forward_length = length(forward);
	forward = forward_length > 0.00001 ? forward / forward_length : vec3(0.0, 0.0, -1.0);
	vec3 right = vec3(-forward.z, 0.0, forward.x);
	vec3 direction = right * VERTEX.x + vec3(0.0, VERTEX.y, 0.0) + forward * VERTEX.z;
	vec4 clip = PROJECTION_MATRIX * vec4(mat3(VIEW_MATRIX) * direction, 1.0);
	POSITION = vec4(clip.xy, 0.0, clip.w);
	fade = UV.x;
}

void fragment() {
	ALBEDO = color.rgb;
	ALPHA = color.a * fade;
}
)";

// CStar::Render. Positions are sky-space directions; UV.x is the star's alpha and UV.y its index.
static const char *shader_stars = R"(shader_type spatial;
render_mode blend_mix, unshaded, cull_disabled, depth_draw_never, fog_disabled, shadows_disabled;

uniform int star_count = 0;

varying float fade;

void vertex() {
	vec4 clip = PROJECTION_MATRIX * vec4(mat3(VIEW_MATRIX) * VERTEX, 1.0);
	if (int(UV.y + 0.5) < star_count) {
		POSITION = vec4(clip.xy, 0.0, clip.w);
	} else {
		POSITION = vec4(2.0, 2.0, 0.0, 1.0);
	}
	POINT_SIZE = 1.0;
	fade = UV.x;
}

void fragment() {
	ALBEDO = vec3(1.0);
	ALPHA = fade;
}
)";

// CMoon::Render / CSun::Render. The mesh is a square with corners at +-1. The client projects
// the point (5, 0, 0), turned about the Z axis, with the view rotation and the projection, and then
// draws a screen-space square around it whose side is a fraction of the viewport width; it skips
// the square when the point is nearer than the near plane (0.7).
static const char *shader_disc_vertex = R"(
uniform sampler2D disc_texture : source_color, filter_linear, repeat_disable;
uniform float angle = 0.0;
uniform float size = 0.1;
uniform vec4 uv_rect = vec4(0.0, 0.0, 1.0, 1.0);

void vertex() {
	vec3 view_position = mat3(VIEW_MATRIX) * vec3(5.0 * cos(angle), 5.0 * sin(angle), 0.0);
	if (-view_position.z < 0.7) {
		POSITION = vec4(2.0, 2.0, 0.0, 1.0);
	} else {
		vec4 clip = PROJECTION_MATRIX * vec4(view_position, 1.0);
		// Half the side is size in normalized device X; the same number of pixels in Y. The
		// projection's Y scale is negative when clip space points down.
		vec2 half_size = vec2(size, size * VIEWPORT_SIZE.x / VIEWPORT_SIZE.y * sign(PROJECTION_MATRIX[1][1]));
		POSITION = vec4(clip.xy / clip.w + VERTEX.xy * half_size, 0.0, 1.0);
	}
	UV = uv_rect.xy + UV * uv_rect.zw;
}
)";

static const char *shader_moon_fragment = R"(
void fragment() {
	vec4 tex = texture(disc_texture, UV);
	ALBEDO = tex.rgb;
	ALPHA = tex.a;
}
)";

// D3DBLEND_ONE / D3DBLEND_ONE: the texture's colour is added whatever its alpha.
static const char *shader_sun_fragment = R"(
void fragment() {
	ALBEDO = texture(disc_texture, UV).rgb;
	ALPHA = 1.0;
}
)";

// CCloud::Render. Positions are sky-space directions (already turned by the client's fixed
// world rotation); UV2.x is the vertex alpha: 0 on the outer ring, 1 on the inner one.
static const char *shader_cloud = R"(shader_type spatial;
render_mode blend_mix, unshaded, cull_disabled, depth_draw_never, fog_disabled, shadows_disabled;

uniform sampler2D cloud_texture : source_color, filter_linear, repeat_enable;
uniform vec4 color : source_color = vec4(1.0);
uniform vec2 uv_offset = vec2(0.0);

varying float fade;

void vertex() {
	vec4 clip = PROJECTION_MATRIX * vec4(mat3(VIEW_MATRIX) * VERTEX, 1.0);
	POSITION = vec4(clip.xy, 0.0, clip.w);
	UV = UV + uv_offset;
	fade = UV2.x;
}

void fragment() {
	vec4 tex = texture(cloud_texture, UV);
	ALBEDO = tex.rgb * color.rgb;
	ALPHA = tex.a * color.a * fade;
}
)";

void SkyMaterial::_update_shader() {
	MutexLock shader_lock(shader_mutex);

	if (shaders[part].is_null()) {
		String code;
		switch (part) {
			case PART_HORIZON: {
				code = shader_horizon;
			} break;
			case PART_STARS: {
				code = shader_stars;
			} break;
			case PART_MOON: {
				code = String("shader_type spatial;\nrender_mode blend_mix, unshaded, cull_disabled, depth_draw_never, fog_disabled, shadows_disabled;\n") + String(shader_disc_vertex) + String(shader_moon_fragment);
			} break;
			case PART_SUN: {
				code = String("shader_type spatial;\nrender_mode blend_add, unshaded, cull_disabled, depth_draw_never, fog_disabled, shadows_disabled;\n") + String(shader_disc_vertex) + String(shader_sun_fragment);
			} break;
			case PART_CLOUD: {
				code = shader_cloud;
			} break;
			case PART_MAX: {
			} break;
		}
		shaders[part] = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shaders[part], code);
	}
	RS::get_singleton()->material_set_shader(_get_material(), shaders[part]);
}

void SkyMaterial::set_part(Part p_part) {
	ERR_FAIL_INDEX(p_part, PART_MAX);
	part = p_part;
	_update_shader();
	// A parameter only reaches the shader that declares it, so hand them all over again.
	set_color(color);
	set_texture(texture);
	set_angle(angle);
	set_size(size);
	set_uv_rect(uv_rect);
	set_uv_offset(uv_offset);
	set_star_count(star_count);
}

SkyMaterial::Part SkyMaterial::get_part() const {
	return part;
}

void SkyMaterial::set_color(const Color &p_color) {
	color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "color", color);
}

Color SkyMaterial::get_color() const {
	return color;
}

void SkyMaterial::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "disc_texture", tex_rid);
	RS::get_singleton()->material_set_param(_get_material(), "cloud_texture", tex_rid);
}

Ref<Texture2D> SkyMaterial::get_texture() const {
	return texture;
}

void SkyMaterial::set_angle(float p_angle) {
	angle = p_angle;
	RS::get_singleton()->material_set_param(_get_material(), "angle", angle);
}

float SkyMaterial::get_angle() const {
	return angle;
}

void SkyMaterial::set_size(float p_size) {
	size = p_size;
	RS::get_singleton()->material_set_param(_get_material(), "size", size);
}

float SkyMaterial::get_size() const {
	return size;
}

void SkyMaterial::set_uv_rect(const Rect2 &p_rect) {
	uv_rect = p_rect;
	RS::get_singleton()->material_set_param(_get_material(), "uv_rect", Vector4(uv_rect.position.x, uv_rect.position.y, uv_rect.size.x, uv_rect.size.y));
}

Rect2 SkyMaterial::get_uv_rect() const {
	return uv_rect;
}

void SkyMaterial::set_uv_offset(const Vector2 &p_offset) {
	uv_offset = p_offset;
	RS::get_singleton()->material_set_param(_get_material(), "uv_offset", uv_offset);
}

Vector2 SkyMaterial::get_uv_offset() const {
	return uv_offset;
}

void SkyMaterial::set_star_count(int p_count) {
	star_count = MAX(p_count, 0);
	RS::get_singleton()->material_set_param(_get_material(), "star_count", star_count);
}

int SkyMaterial::get_star_count() const {
	return star_count;
}

Shader::Mode SkyMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID SkyMaterial::get_shader_rid() const {
	MutexLock shader_lock(shader_mutex);
	return shaders[part];
}

void SkyMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_part", "part"), &SkyMaterial::set_part);
	ClassDB::bind_method(D_METHOD("get_part"), &SkyMaterial::get_part);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &SkyMaterial::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &SkyMaterial::get_color);
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &SkyMaterial::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &SkyMaterial::get_texture);
	ClassDB::bind_method(D_METHOD("set_angle", "angle"), &SkyMaterial::set_angle);
	ClassDB::bind_method(D_METHOD("get_angle"), &SkyMaterial::get_angle);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &SkyMaterial::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &SkyMaterial::get_size);
	ClassDB::bind_method(D_METHOD("set_uv_rect", "rect"), &SkyMaterial::set_uv_rect);
	ClassDB::bind_method(D_METHOD("get_uv_rect"), &SkyMaterial::get_uv_rect);
	ClassDB::bind_method(D_METHOD("set_uv_offset", "offset"), &SkyMaterial::set_uv_offset);
	ClassDB::bind_method(D_METHOD("get_uv_offset"), &SkyMaterial::get_uv_offset);
	ClassDB::bind_method(D_METHOD("set_star_count", "count"), &SkyMaterial::set_star_count);
	ClassDB::bind_method(D_METHOD("get_star_count"), &SkyMaterial::get_star_count);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "part", PROPERTY_HINT_ENUM, "Horizon,Stars,Moon,Sun,Cloud"), "set_part", "get_part");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle", PROPERTY_HINT_NONE, "radians_as_degrees"), "set_angle", "get_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "size", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::RECT2, "uv_rect"), "set_uv_rect", "get_uv_rect");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "uv_offset"), "set_uv_offset", "get_uv_offset");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "star_count", PROPERTY_HINT_RANGE, "0,200,1"), "set_star_count", "get_star_count");

	BIND_ENUM_CONSTANT(PART_HORIZON);
	BIND_ENUM_CONSTANT(PART_STARS);
	BIND_ENUM_CONSTANT(PART_MOON);
	BIND_ENUM_CONSTANT(PART_SUN);
	BIND_ENUM_CONSTANT(PART_CLOUD);
}

void SkyMaterial::cleanup_shader() {
	MutexLock shader_lock(shader_mutex);
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	for (int i = 0; i < PART_MAX; i++) {
		if (shaders[i].is_valid()) {
			RS::get_singleton()->free_rid(shaders[i]);
			shaders[i] = RID();
		}
	}
}

SkyMaterial::SkyMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_part(PART_HORIZON);
}

SkyMaterial::~SkyMaterial() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
