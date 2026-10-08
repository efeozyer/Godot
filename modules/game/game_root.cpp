#include "game_root.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"

void GameRoot::_notification(int p_what) {
	// Gameplay must not run while the scene is open in the editor.
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	switch (p_what) {
		case NOTIFICATION_READY: {
			child = memnew(Node2D);
			child->set_name("Child");
			child->set_position(Point2(100, 0));
			add_child(child);

			print_line(vformat("GameRoot ready, children: %d", get_child_count()));
			set_process(true);
		} break;

		case NOTIFICATION_PROCESS: {
			rotate(spin_speed * get_process_delta_time());
		} break;
	}
}

void GameRoot::set_spin_speed(real_t p_speed) {
	spin_speed = p_speed;
}

real_t GameRoot::get_spin_speed() const {
	return spin_speed;
}

void GameRoot::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_spin_speed", "speed"), &GameRoot::set_spin_speed);
	ClassDB::bind_method(D_METHOD("get_spin_speed"), &GameRoot::get_spin_speed);

	// Exposes the value in the editor inspector and saves it in scene files.
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_speed"), "set_spin_speed", "get_spin_speed");
}
