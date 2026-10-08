#include "effect_common.h"

#include "core/math/math_funcs.h"
#include "core/os/mutex.h"
#include "core/templates/hash_set.h"

Vector3 effect_normal_light(const Vector3 &p_light_direction, float p_shadow_distance, bool p_night) {
	// CShadow::BuildLightMatrices.
	const double eye_lift = 100.0; // SHADOW_EYE_LIFT

	// Back to the client's left-handed space.
	const Vector3 light = Vector3(p_light_direction.x, p_light_direction.y, -p_light_direction.z);
	const Vector3 shadow_light = p_night ? -light : light;

	Vector3 forward;
	forward.x = Math::floor(double(shadow_light.x * 100.0f) + 0.5) * 0.01 * p_shadow_distance;
	forward.y = Math::floor(double(shadow_light.y * 100.0f) + 0.5) * 0.01 * p_shadow_distance - eye_lift;
	forward.z = Math::floor(double(shadow_light.z * 100.0f) + 0.5) * 0.01 * p_shadow_distance;
	forward.normalize();

	// D3DXMatrixLookAtLH with the camera's up vector, which the client keeps at +Y.
	Vector3 right = Vector3(0, 1, 0).cross(forward);
	if (right.is_zero_approx()) {
		// A light pointing straight down has no right axis. D3DXVec3Normalize leaves the zero vector
		// as it is, the up axis built on it is zero as well, and only the forward axis remains.
		return Vector3(-light.z * forward.x, -light.z * forward.y, light.z * forward.z);
	}
	right.normalize();
	const Vector3 up = forward.cross(right);

	const Vector3 normal_light = -(right * light.x + up * light.y + forward * light.z);
	return Vector3(normal_light.x, normal_light.y, -normal_light.z);
}

const char *effect_shader_functions = R"(
float effect_shadow_factor(float attenuation) {
	return 0.5 + 0.5 * attenuation;
}

float effect_fog_factor(float depth, float z_near, float z_far) {
	float projected = (depth - z_near) * z_far / (z_far - z_near);
	return clamp((z_far - projected) / (z_far - z_near), 0.0, 1.0);
}

vec3 effect_to_linear(vec3 color) {
	return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), color));
}
)";

static Mutex effect_world_mutex;
static EffectWorld effect_world;
static const void *effect_world_owner = nullptr;

static HashSet<EffectWorldListener *> &effect_world_listeners() {
	// Built on first use: materials can be made before this file's statics would be.
	static HashSet<EffectWorldListener *> listeners;
	return listeners;
}

EffectWorldListener::EffectWorldListener() {
	MutexLock lock(effect_world_mutex);
	effect_world_listeners().insert(this);
}

EffectWorldListener::~EffectWorldListener() {
	MutexLock lock(effect_world_mutex);
	effect_world_listeners().erase(this);
}

EffectWorld effect_world_get() {
	MutexLock lock(effect_world_mutex);
	return effect_world;
}

static void effect_world_publish(const EffectWorld &p_world) {
	const bool same = effect_world.active == p_world.active && effect_world.light_direction == p_world.light_direction && effect_world.light_color == p_world.light_color && effect_world.night == p_world.night && effect_world.fog_color == p_world.fog_color && effect_world.fog_far == p_world.fog_far;
	if (same) {
		return;
	}
	effect_world = p_world;
	for (EffectWorldListener *listener : effect_world_listeners()) {
		listener->_effect_world_changed(effect_world);
	}
}

void effect_world_set(const void *p_owner, const EffectWorld &p_world) {
	MutexLock lock(effect_world_mutex);
	effect_world_owner = p_owner;
	effect_world_publish(p_world);
}

void effect_world_clear(const void *p_owner) {
	MutexLock lock(effect_world_mutex);
	if (effect_world_owner != p_owner) {
		return;
	}
	effect_world_owner = nullptr;
	effect_world_publish(EffectWorld());
}
