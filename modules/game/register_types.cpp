#include "register_types.h"

#include "game_root.h"
#include "camera_controller.h"
#include "player_controller.h"

#include "core/object/class_db.h"

void initialize_game_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	// Every game class must be registered here to be usable in scenes and the editor.
	GDREGISTER_CLASS(GameRoot);
	GDREGISTER_CLASS(CameraController);
	GDREGISTER_CLASS(PlayerController);
}

void uninitialize_game_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
