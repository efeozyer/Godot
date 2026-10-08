#include "register_types.h"

#include "terrain_3d.h"
#include "terrain_data.h"
#include "terrain_layer_material.h"

#ifdef TOOLS_ENABLED
#include "editor/terrain_editor_plugin.h"
#endif

#include "core/object/class_db.h"

void initialize_terrain_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(TerrainData);
		GDREGISTER_CLASS(TerrainLayerMaterial);
		GDREGISTER_CLASS(Terrain3D);
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_VIRTUAL_CLASS(TerrainEditorPlugin);
		EditorPlugins::add_by_type<TerrainEditorPlugin>();
	}
#endif
}

void uninitialize_terrain_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	TerrainLayerMaterial::cleanup_shader();
}
