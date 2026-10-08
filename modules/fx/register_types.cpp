#include "register_types.h"

#include "flag_3d.h"
#include "flag_material.h"
#include "normal_map_material.h"
#include "effect_particle_material.h"
#include "post_process_effect.h"
#include "effect_quad_material.h"
#include "sky_3d.h"
#include "sky_material.h"
#include "terrain_material.h"
#include "view_yaw_3d.h"

#include "core/object/class_db.h"

void initialize_fx_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(EffectParticleMaterial);
	GDREGISTER_CLASS(PostProcessEffect);
	GDREGISTER_CLASS(EffectQuadMaterial);
	GDREGISTER_CLASS(TerrainMaterial);
	GDREGISTER_CLASS(NormalMapMaterial);
	GDREGISTER_CLASS(ViewYaw3D);
	GDREGISTER_CLASS(FlagMaterial);
	GDREGISTER_CLASS(Flag3D);
	GDREGISTER_CLASS(SkyMaterial);
	GDREGISTER_CLASS(Sky3D);
}

void uninitialize_fx_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	EffectParticleMaterial::cleanup_shader();
	EffectQuadMaterial::cleanup_shader();
	TerrainMaterial::cleanup_shader();
	NormalMapMaterial::cleanup_shader();
	FlagMaterial::cleanup_shader();
	SkyMaterial::cleanup_shader();
}
