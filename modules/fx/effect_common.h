#pragma once

#include "core/math/vector3.h"

// What the client's two world effects, SHC/TerranBlend_Shader.fx and SHC/NormalMap_shader.fx,
// have in common: the sun they are lit by, the shadow they receive and the fog they fade into.
// TerrainMaterial and NormalMapMaterial both build on this.

// The vector that the effects' vertex-normal light term is taken against.
//
// Neither effect lights the vertex normal with the sun direction itself. Their vertex shaders turn
// the normal into the view space of the shadow-map light (g_matWorld * g_matLightView), negate it,
// and the pixel shaders then take its dot product with the world-space LightDirection. With the
// axes of the light's view as x, y and z, that equals dot(world normal, -(L.x * x + L.y * y + L.z * z)),
// so the whole term is one dot product with the vector returned here.
//
// The light's view is built as CShadow::BuildLightMatrices does: the eye sits p_shadow_distance
// (the zone's shadow range) back along the light direction, rounded to hundredths, and
// SHADOW_EYE_LIFT higher, looking at the player; CShadow::BeginRender turns the direction around
// at night. p_light_direction and the result are in Godot's space (the client's with Z mirrored).
Vector3 effect_normal_light(const Vector3 &p_light_direction, float p_shadow_distance, bool p_night);

// GLSL shared by the two effects' fragment and light functions.
//
// effect_shadow_factor: the 3x3 shadow-map filter of both pixel shaders adds 1 for a lit tap and 0.5
// for a shadowed one and divides by nine. Godot's own directional shadow stands in for the
// client's shadow map, so the filtered attenuation (1 lit, 0 shadowed) is mapped onto that range.
//
// effect_fog_factor: both vertex shaders output oFog = (g_Fog.y - z) / (g_Fog.y - g_Fog.x), with z the
// projected depth and g_Fog the camera's near and far planes; BeginPass switches fog on, and the
// fixed-function fog stage then blends towards D3DRS_FOGCOLOR by that factor.
//
// effect_to_linear: the client works on the texture values as stored and writes them straight to the
// back buffer, so everything up to here is done on those values and only the result is linearised.
extern const char *effect_shader_functions;

// The time of day, as the sky hands it to the two effects.
//
// In the client CGameProcMain::Render gives both effects the sun's direction and colour, the
// night flag and the fog colour every frame, taken from the sky manager. Here Sky3D publishes
// that state and every TerrainMaterial and NormalMapMaterial follows it while one is
// published; without a sky they use their own properties, which are left untouched either way.
struct EffectWorld {
	bool active = false;
	Vector3 light_direction; // The way the sunlight travels, Godot's space.
	Vector3 light_color = Vector3(1, 1, 1); // Light 0's diffuse colour, as stored (not linearised).
	bool night = false; // CSkyMng::IsNightTime.
	Vector3 fog_color = Vector3(1, 1, 1); // As stored.
	float fog_far = 512.0; // The camera's far plane.
};

class EffectWorldListener {
public:
	// Called on the thread that publishes, with the new state.
	virtual void _effect_world_changed(const EffectWorld &p_world) = 0;

	EffectWorldListener();
	virtual ~EffectWorldListener();
};

EffectWorld effect_world_get();
// p_owner identifies the publisher, so that only the one that published last can withdraw.
void effect_world_set(const void *p_owner, const EffectWorld &p_world);
void effect_world_clear(const void *p_owner);
