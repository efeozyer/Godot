#include "effect_particle_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex EffectParticleMaterial::shader_mutex;
RID EffectParticleMaterial::shader;

void EffectParticleMaterial::set_emit_mode(EmitMode p_mode) {
	ERR_FAIL_COND(p_mode != EMIT_MODE_SPREAD && p_mode != EMIT_MODE_GATHER);
	emit_mode = p_mode;
	RS::get_singleton()->material_set_param(_get_material(), "emit_mode", int(emit_mode));
}

EffectParticleMaterial::EmitMode EffectParticleMaterial::get_emit_mode() const {
	return emit_mode;
}

void EffectParticleMaterial::set_emit_direction(const Vector3 &p_direction) {
	emit_direction = p_direction;

	// Rotation taking +Z onto the emit direction (CFXPartParticles::RotateQuaternion). The client falls
	// back to negating the vector when the two directions are parallel, which leaves it unchanged
	// for +Z and flips it for -Z.
	Basis basis;
	Vector3 axis = Vector3(0, 1, 0);
	if (!p_direction.is_zero_approx()) {
		axis = p_direction.normalized();
		const Vector3 turn_axis = Vector3(0, 0, 1).cross(axis);
		if (turn_axis.is_zero_approx()) {
			if (axis.z < 0) {
				basis = Basis(Vector3(-1, 0, 0), Vector3(0, -1, 0), Vector3(0, 0, -1));
			}
		} else {
			basis = Basis(turn_axis.normalized(), Math::acos(CLAMP(axis.z, -1.0f, 1.0f)));
		}
	}
	RS::get_singleton()->material_set_param(_get_material(), "axis_basis", basis);
	RS::get_singleton()->material_set_param(_get_material(), "orbit_axis", axis);
}

Vector3 EffectParticleMaterial::get_emit_direction() const {
	return emit_direction;
}

void EffectParticleMaterial::set_flipbook_frames(int p_frames) {
	flipbook_frames = MAX(p_frames, 1);
	RS::get_singleton()->material_set_param(_get_material(), "frame_count", float(flipbook_frames));
}

int EffectParticleMaterial::get_flipbook_frames() const {
	return flipbook_frames;
}

void EffectParticleMaterial::set_color_table(const Ref<Texture2D> &p_texture) {
	color_table = p_texture;
	Variant tex_rid = p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant();
	RS::get_singleton()->material_set_param(_get_material(), "color_table", tex_rid);
}

Ref<Texture2D> EffectParticleMaterial::get_color_table() const {
	return color_table;
}

void EffectParticleMaterial::set_box_min(const Vector3 &p_box_min) {
	box_min = p_box_min;
	RS::get_singleton()->material_set_param(_get_material(), "box_min", p_box_min);
}

Vector3 EffectParticleMaterial::get_box_min() const {
	return box_min;
}

void EffectParticleMaterial::set_box_max(const Vector3 &p_box_max) {
	box_max = p_box_max;
	RS::get_singleton()->material_set_param(_get_material(), "box_max", p_box_max);
}

Vector3 EffectParticleMaterial::get_box_max() const {
	return box_max;
}

void EffectParticleMaterial::set_gather_point(const Vector3 &p_gather_point) {
	gather_point = p_gather_point;
	RS::get_singleton()->material_set_param(_get_material(), "gather_point", p_gather_point);
}

Vector3 EffectParticleMaterial::get_gather_point() const {
	return gather_point;
}

void EffectParticleMaterial::set_emit_angle(float p_emit_angle) {
	emit_angle = p_emit_angle;
	RS::get_singleton()->material_set_param(_get_material(), "emit_angle", p_emit_angle);
}

float EffectParticleMaterial::get_emit_angle() const {
	return emit_angle;
}

void EffectParticleMaterial::set_orbit_speed(float p_orbit_speed) {
	orbit_speed = p_orbit_speed;
	RS::get_singleton()->material_set_param(_get_material(), "orbit_speed", p_orbit_speed);
}

float EffectParticleMaterial::get_orbit_speed() const {
	return orbit_speed;
}

void EffectParticleMaterial::set_speed(float p_speed) {
	speed = p_speed;
	RS::get_singleton()->material_set_param(_get_material(), "speed", p_speed);
}

float EffectParticleMaterial::get_speed() const {
	return speed;
}

void EffectParticleMaterial::set_gravity(float p_gravity) {
	gravity = p_gravity;
	RS::get_singleton()->material_set_param(_get_material(), "gravity", p_gravity);
}

float EffectParticleMaterial::get_gravity() const {
	return gravity;
}

void EffectParticleMaterial::set_size_range(const Vector2 &p_size_range) {
	size_range = p_size_range;
	RS::get_singleton()->material_set_param(_get_material(), "size_range", p_size_range);
}

Vector2 EffectParticleMaterial::get_size_range() const {
	return size_range;
}

void EffectParticleMaterial::set_size_add(float p_size_add) {
	size_add = p_size_add;
	RS::get_singleton()->material_set_param(_get_material(), "size_add", p_size_add);
}

float EffectParticleMaterial::get_size_add() const {
	return size_add;
}

void EffectParticleMaterial::set_scale_velocity(const Vector2 &p_scale_velocity) {
	scale_velocity = p_scale_velocity;
	RS::get_singleton()->material_set_param(_get_material(), "scale_velocity", p_scale_velocity);
}

Vector2 EffectParticleMaterial::get_scale_velocity() const {
	return scale_velocity;
}

void EffectParticleMaterial::set_life_range(const Vector2 &p_life_range) {
	life_range = p_life_range;
	RS::get_singleton()->material_set_param(_get_material(), "life_range", p_life_range);
}

Vector2 EffectParticleMaterial::get_life_range() const {
	return life_range;
}

void EffectParticleMaterial::set_life_extra(float p_life_extra) {
	life_extra = p_life_extra;
	RS::get_singleton()->material_set_param(_get_material(), "life_extra", p_life_extra);
}

float EffectParticleMaterial::get_life_extra() const {
	return life_extra;
}

void EffectParticleMaterial::set_flipbook_fps(float p_flipbook_fps) {
	flipbook_fps = p_flipbook_fps;
	RS::get_singleton()->material_set_param(_get_material(), "tex_fps", p_flipbook_fps);
}

float EffectParticleMaterial::get_flipbook_fps() const {
	return flipbook_fps;
}

Shader::Mode EffectParticleMaterial::get_shader_mode() const {
	return Shader::MODE_PARTICLES;
}

RID EffectParticleMaterial::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID EffectParticleMaterial::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		shader_set = true;
	}
	return _get_material();
}

void EffectParticleMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_emit_mode", "mode"), &EffectParticleMaterial::set_emit_mode);
	ClassDB::bind_method(D_METHOD("get_emit_mode"), &EffectParticleMaterial::get_emit_mode);
	ClassDB::bind_method(D_METHOD("set_emit_direction", "direction"), &EffectParticleMaterial::set_emit_direction);
	ClassDB::bind_method(D_METHOD("get_emit_direction"), &EffectParticleMaterial::get_emit_direction);
	ClassDB::bind_method(D_METHOD("set_flipbook_frames", "frames"), &EffectParticleMaterial::set_flipbook_frames);
	ClassDB::bind_method(D_METHOD("get_flipbook_frames"), &EffectParticleMaterial::get_flipbook_frames);
	ClassDB::bind_method(D_METHOD("set_color_table", "texture"), &EffectParticleMaterial::set_color_table);
	ClassDB::bind_method(D_METHOD("get_color_table"), &EffectParticleMaterial::get_color_table);
	ClassDB::bind_method(D_METHOD("set_box_min", "box_min"), &EffectParticleMaterial::set_box_min);
	ClassDB::bind_method(D_METHOD("get_box_min"), &EffectParticleMaterial::get_box_min);
	ClassDB::bind_method(D_METHOD("set_box_max", "box_max"), &EffectParticleMaterial::set_box_max);
	ClassDB::bind_method(D_METHOD("get_box_max"), &EffectParticleMaterial::get_box_max);
	ClassDB::bind_method(D_METHOD("set_gather_point", "gather_point"), &EffectParticleMaterial::set_gather_point);
	ClassDB::bind_method(D_METHOD("get_gather_point"), &EffectParticleMaterial::get_gather_point);
	ClassDB::bind_method(D_METHOD("set_emit_angle", "emit_angle"), &EffectParticleMaterial::set_emit_angle);
	ClassDB::bind_method(D_METHOD("get_emit_angle"), &EffectParticleMaterial::get_emit_angle);
	ClassDB::bind_method(D_METHOD("set_orbit_speed", "orbit_speed"), &EffectParticleMaterial::set_orbit_speed);
	ClassDB::bind_method(D_METHOD("get_orbit_speed"), &EffectParticleMaterial::get_orbit_speed);
	ClassDB::bind_method(D_METHOD("set_speed", "speed"), &EffectParticleMaterial::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &EffectParticleMaterial::get_speed);
	ClassDB::bind_method(D_METHOD("set_gravity", "gravity"), &EffectParticleMaterial::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &EffectParticleMaterial::get_gravity);
	ClassDB::bind_method(D_METHOD("set_size_range", "size_range"), &EffectParticleMaterial::set_size_range);
	ClassDB::bind_method(D_METHOD("get_size_range"), &EffectParticleMaterial::get_size_range);
	ClassDB::bind_method(D_METHOD("set_size_add", "size_add"), &EffectParticleMaterial::set_size_add);
	ClassDB::bind_method(D_METHOD("get_size_add"), &EffectParticleMaterial::get_size_add);
	ClassDB::bind_method(D_METHOD("set_scale_velocity", "scale_velocity"), &EffectParticleMaterial::set_scale_velocity);
	ClassDB::bind_method(D_METHOD("get_scale_velocity"), &EffectParticleMaterial::get_scale_velocity);
	ClassDB::bind_method(D_METHOD("set_life_range", "life_range"), &EffectParticleMaterial::set_life_range);
	ClassDB::bind_method(D_METHOD("get_life_range"), &EffectParticleMaterial::get_life_range);
	ClassDB::bind_method(D_METHOD("set_life_extra", "life_extra"), &EffectParticleMaterial::set_life_extra);
	ClassDB::bind_method(D_METHOD("get_life_extra"), &EffectParticleMaterial::get_life_extra);
	ClassDB::bind_method(D_METHOD("set_flipbook_fps", "flipbook_fps"), &EffectParticleMaterial::set_flipbook_fps);
	ClassDB::bind_method(D_METHOD("get_flipbook_fps"), &EffectParticleMaterial::get_flipbook_fps);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "emit_mode", PROPERTY_HINT_ENUM, "Spread:1,Gather:2"), "set_emit_mode", "get_emit_mode");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "emit_direction"), "set_emit_direction", "get_emit_direction");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "box_min"), "set_box_min", "get_box_min");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "box_max"), "set_box_max", "get_box_max");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "gather_point"), "set_gather_point", "get_gather_point");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "emit_angle", PROPERTY_HINT_RANGE, "0,360,0.01,or_greater,degrees"), "set_emit_angle", "get_emit_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbit_speed", PROPERTY_HINT_NONE, "suffix:rad/s"), "set_orbit_speed", "get_orbit_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_NONE, "suffix:m/s"), "set_speed", "get_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity", PROPERTY_HINT_NONE, "suffix:m/s^2"), "set_gravity", "get_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size_range", PROPERTY_HINT_NONE, "suffix:m"), "set_size_range", "get_size_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "size_add", PROPERTY_HINT_NONE, "suffix:m"), "set_size_add", "get_size_add");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "scale_velocity", PROPERTY_HINT_NONE, "suffix:m/s"), "set_scale_velocity", "get_scale_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "life_range", PROPERTY_HINT_NONE, "suffix:s"), "set_life_range", "get_life_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "life_extra", PROPERTY_HINT_NONE, "suffix:s"), "set_life_extra", "get_life_extra");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flipbook_fps", PROPERTY_HINT_RANGE, "0,120,0.01,or_greater,suffix:FPS"), "set_flipbook_fps", "get_flipbook_fps");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flipbook_frames", PROPERTY_HINT_RANGE, "1,256,1,or_greater"), "set_flipbook_frames", "get_flipbook_frames");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "color_table", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_color_table", "get_color_table");

	BIND_ENUM_CONSTANT(EMIT_MODE_SPREAD);
	BIND_ENUM_CONSTANT(EMIT_MODE_GATHER);
}

void EffectParticleMaterial::cleanup_shader() {
	if (shader.is_valid()) {
		ERR_FAIL_NULL(RenderingServer::get_singleton());
		RS::get_singleton()->free_rid(shader);
	}
}

void EffectParticleMaterial::_update_shader() {
	MutexLock shader_lock(shader_mutex);
	if (shader.is_null()) {
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, R"(
shader_type particles;
render_mode disable_velocity;

uniform int emit_mode = 1; // 1 = spread, 2 = gather.
uniform vec3 box_min;
uniform vec3 box_max;
uniform vec3 gather_point;
uniform float emit_angle; // Full cone angle of a spread emitter, in degrees.
uniform mat3 axis_basis = mat3(1.0); // Turns +Z onto the emit axis.
uniform vec3 orbit_axis = vec3(0.0, 1.0, 0.0);
uniform float orbit_speed; // Radians per second.
uniform float speed;
uniform float gravity;
uniform vec2 size_range;
uniform float size_add;
uniform vec2 life_range;
uniform float life_extra;
uniform vec2 scale_velocity;
uniform float tex_fps;
uniform float frame_count = 1.0;
uniform sampler2D color_table : repeat_disable;

uint hash(uint x) {
	x = ((x >> uint(16)) ^ x) * uint(73244475);
	x = ((x >> uint(16)) ^ x) * uint(73244475);
	x = (x >> uint(16)) ^ x;
	return x;
}

float rand_from_seed(inout uint seed) {
	int k;
	int s = int(seed);
	if (s == 0) {
		s = 305420679;
	}
	k = s / 127773;
	s = 16807 * (s - k * 127773) - 2836 * k;
	if (s < 0) {
		s += 2147483647;
	}
	seed = uint(s);
	return float(seed % uint(65536)) / 65535.0;
}

vec3 rotate_around(vec3 v, vec3 axis, float angle) {
	float c = cos(angle);
	float s = sin(angle);
	return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

void start() {
	uint seed = hash(NUMBER + uint(1) + RANDOM_SEED);
	vec3 local = mix(box_min, box_max, vec3(rand_from_seed(seed), rand_from_seed(seed), rand_from_seed(seed)));
	float life = mix(life_range.x, life_range.y, rand_from_seed(seed)) + life_extra;
	float size = mix(size_range.x, size_range.y, rand_from_seed(seed)) + size_add;

	vec3 direction = gather_point;
	if (emit_mode == 1) {
		float tilt = radians((rand_from_seed(seed) - 0.5) * emit_angle);
		float turn = rand_from_seed(seed) * TAU;
		direction = vec3(sin(tilt) * cos(turn), sin(tilt) * sin(turn), cos(tilt));
	}
	// An emitter bound to an animated shape scales its spawn box with the shape.
	local *= vec3(length(EMISSION_TRANSFORM[0].xyz), length(EMISSION_TRANSFORM[1].xyz), length(EMISSION_TRANSFORM[2].xyz));
	direction = axis_basis * direction;
	local = axis_basis * local;
	if (emit_mode == 2) {
		direction -= local;
	}
	if (length(direction) > 0.0) {
		direction = normalize(direction);
	}

	// CUSTOM: x = life in seconds, y = base size, z = flipbook frame, w = age in seconds.
	CUSTOM = vec4(life, size, 0.0, 0.0);
	USERDATA1.xyz = local;
	USERDATA2.xyz = direction * speed;
	USERDATA3.xyz = EMISSION_TRANSFORM[3].xyz;
	VELOCITY = vec3(0.0);
	TRANSFORM = mat4(1.0);
	TRANSFORM[3].xyz = USERDATA3.xyz + vec3(local.x, local.y, -local.z);
	COLOR = texture(color_table, vec2(0.0));
}

void process() {
	CUSTOM.w += DELTA;
	float age = CUSTOM.w;
	if (age >= CUSTOM.x) {
		ACTIVE = false;
	}

	vec3 position = USERDATA1.xyz + USERDATA2.xyz * age;
	if (orbit_speed != 0.0) {
		position = rotate_around(position, orbit_axis, orbit_speed * age);
	}
	position.y -= 0.5 * gravity * age * age;

	vec2 size = max(vec2(CUSTOM.y) + scale_velocity * age, vec2(0.0));
	TRANSFORM[0].xyz = vec3(size.x, 0.0, 0.0);
	TRANSFORM[1].xyz = vec3(0.0, size.y, 0.0);
	TRANSFORM[2].xyz = vec3(0.0, 0.0, CUSTOM.y);
	TRANSFORM[3].xyz = USERDATA3.xyz + vec3(position.x, position.y, -position.z);

	COLOR = texture(color_table, vec2(clamp(age / max(CUSTOM.x, 0.0001), 0.0, 1.0), 0.0));
	CUSTOM.z = mod(floor(age * tex_fps), frame_count);
}
)");
	}
}

EffectParticleMaterial::EffectParticleMaterial() {
	_set_material(RS::get_singleton()->material_create());

	set_emit_mode(EMIT_MODE_SPREAD);
	set_emit_direction(Vector3(0, 0, 1));
	set_flipbook_frames(1);
	set_box_min(Vector3());
	set_box_max(Vector3());
	set_gather_point(Vector3());
	set_emit_angle(0.0);
	set_orbit_speed(0.0);
	set_speed(0.0);
	set_gravity(0.0);
	set_size_range(Vector2(1, 1));
	set_size_add(0.0);
	set_scale_velocity(Vector2());
	set_life_range(Vector2(1, 1));
	set_life_extra(0.0);
	set_flipbook_fps(0.0);
}

EffectParticleMaterial::~EffectParticleMaterial() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
