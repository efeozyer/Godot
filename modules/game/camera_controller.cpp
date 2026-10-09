#include "camera_controller.h"

#include "player_controller.h"

#include "core/config/engine.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/gui/control.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

// GameBase.h: the client's mouse deltas are normalized to a 90 FPS frame.
static constexpr real_t REFERENCE_FRAME_SEC = 0.011111;

// The client's vector (0, 0, 1) turned by RotationYawPitchRoll(yaw, pitch, 0), with Z mirrored
// for Godot.
static Vector3 _view_direction(real_t p_yaw, real_t p_pitch) {
	const real_t cos_pitch = Math::cos(p_pitch);
	return Vector3(cos_pitch * Math::sin(p_yaw), -Math::sin(p_pitch), -cos_pitch * Math::cos(p_yaw));
}

Node3D *CameraController::_get_target() const {
	return ObjectDB::get_instance<Node3D>(target_id);
}

PlayerController *CameraController::_get_player() const {
	return ObjectDB::get_instance<PlayerController>(target_id);
}

void CameraController::_update_target() {
	PlayerController *player = _get_player();
	if (player) {
		player->set_view_camera(nullptr);
	}

	Node3D *target = Object::cast_to<Node3D>(get_node_or_null(target_path));
	target_id = target ? target->get_instance_id() : ObjectID();

	player = _get_player();
	if (player) {
		player->set_view_camera(this);
	}
}

// CGameEng::CameraPitchAdd, with the per-frame amount already multiplied out.
void CameraController::_camera_pitch_add(real_t p_radians) {
	switch (view_point) {
		case VIEW_POINT_BACKWARD: {
			pitch_backward = CLAMP(pitch_backward + p_radians, pitch_min, pitch_max);
		} break;
		case VIEW_POINT_FIRST_PERSON: {
			pitch_first_person = CLAMP(pitch_first_person + p_radians, pitch_min, pitch_max);
		} break;
		case VIEW_POINT_FORWARD: {
			pitch_forward = CLAMP(pitch_forward + p_radians, pitch_min, pitch_max);
		} break;
		case VIEW_POINT_THIRD_PERSON: {
			third_person_pitch = CLAMP(third_person_pitch + p_radians, third_person_pitch_min, pitch_max);
		} break;
	}
}

// CGameEng::CameraYawAdd: only the third person view has a yaw of its own.
void CameraController::_camera_yaw_add(real_t p_radians) {
	if (view_point == VIEW_POINT_THIRD_PERSON) {
		camera_yaw += p_radians;
	}
}

// CGameEng::CameraZoom. The first person view has no zoom.
void CameraController::_camera_zoom(real_t p_amount) {
	if (view_point == VIEW_POINT_BACKWARD || view_point == VIEW_POINT_FORWARD) {
		zoom = MAX(zoom - p_amount, zoom_min);
		if (zoom_max > 0.0) {
			zoom = MIN(zoom, zoom_max);
		}
	} else if (view_point == VIEW_POINT_THIRD_PERSON) {
		third_person_distance = MAX(third_person_distance - (real_t)4.0 * p_amount, third_person_distance_min);
		if (third_person_distance_max > 0.0) {
			third_person_distance = MIN(third_person_distance, third_person_distance_max);
		}
	}
}

// Every horizontal camera input in the client makes this choice: the third person view
// turns the camera, the other views turn the character the camera is bound to.
void CameraController::_turn(real_t p_radians) {
	if (view_point == VIEW_POINT_THIRD_PERSON) {
		_camera_yaw_add(p_radians);
	} else {
		PlayerController *player = _get_player();
		if (player) {
			player->rot_add(p_radians);
		}
	}
}

// CGameEng::ViewPointChange(VP_UNKNOWN), reached through CGameProcMain::CommandCameraChange.
void CameraController::view_point_change() {
	PlayerController *player = _get_player();
	if (player) {
		player->view_point_changing();
	}
	view_point = ViewPoint((view_point + 1) % (VIEW_POINT_THIRD_PERSON + 1));
}

void CameraController::_end_drag() {
	right_button_down = false;
	drag_motion = Vector2();
	if (dragging) {
		dragging = false;
		// The client hides the cursor and pins it where the drag began.
		Input::get_singleton()->set_mouse_mode(Input::MouseMode::MOUSE_MODE_VISIBLE);
		get_viewport()->warp_mouse(drag_origin);
	}
}

// The camera inputs of CGameProcMain::ProcessLocalInput and the middle button spin of
// CGameProcMain::Tick.
void CameraController::_process_local_input(double p_delta) {
	const Input *input = Input::get_singleton();
	const bool third_person = view_point == VIEW_POINT_THIRD_PERSON;

	if (right_button_down && !input->is_mouse_button_pressed(MouseButton::RIGHT)) {
		_end_drag();
	}

	// OnMouseRbtnDown: pi * pixels * sensitivity radians per second, scaled to the 90 FPS
	// reference frame; integrated over the frame that leaves a fixed angle per pixel.
	if (drag_motion != Vector2()) {
		const real_t scale = Math::PI * (mouse_rotate_speed + 1) / 100.0 * REFERENCE_FRAME_SEC;
		const real_t rot_y = drag_motion.x * scale;
		const real_t rot_x = drag_motion.y * scale;
		drag_motion = Vector2();

		if (rot_y != 0.0) {
			_turn(rot_y);
		}
		if (drag_zoom && rot_x != 0.0) {
			if (third_person) {
				_camera_zoom(-rot_x);
			} else {
				_camera_pitch_add(rot_x);
			}
		}
	}

	// m_fMBRotateTime: releasing the middle button turns the view around.
	if (middle_rotate_time >= 0.0) {
		real_t radians = middle_click_spin_speed * p_delta;
		middle_rotate_time += radians;
		if (middle_rotate_time > middle_click_spin_angle) {
			radians -= middle_rotate_time - middle_click_spin_angle;
			middle_rotate_time = -1.0;
		}
		if (radians != 0.0) {
			_turn(radians);
		}
	}

	// The cursor resting on the outermost pixel of the view turns and tilts it.
	if (screen_edge_rotation && input->get_mouse_mode() == Input::MouseMode::MOUSE_MODE_VISIBLE) {
		const Point2i cursor = get_viewport()->get_mouse_position();
		const Size2i view_size = get_viewport()->get_visible_rect().size;

		real_t rot_y = 0.0;
		real_t rot_x = 0.0;
		if (cursor.x == 0) {
			rot_y = -edge_yaw_speed;
		} else if (cursor.x == view_size.x - 1) {
			rot_y = edge_yaw_speed;
		}
		if (cursor.y == 0) {
			rot_x = -edge_pitch_speed;
		} else if (cursor.y == view_size.y - 1) {
			rot_x = edge_pitch_speed;
		}

		if (rot_y != 0.0) {
			_turn(rot_y * p_delta);
		}
		if (rot_x != 0.0 && !third_person) {
			_camera_pitch_add(rot_x * p_delta);
		}
	}

	// KM_CAMERA_CHANGE.
	const bool change_down = input->is_physical_key_pressed(Key::F9);
	if (change_down && !key_change_down) {
		view_point_change();
	}
	key_change_down = change_down;

	// Home raises the third person camera and End lowers it, unless a text field has focus.
	if (view_point == VIEW_POINT_THIRD_PERSON) {
		const Control *focus = get_viewport()->gui_get_focus_owner();
		if (!focus || !focus->is_text_field()) {
			if (input->is_physical_key_pressed(Key::HOME)) {
				_camera_pitch_add(key_pitch_speed * p_delta);
			} else if (input->is_physical_key_pressed(Key::END)) {
				_camera_pitch_add(-key_pitch_speed * p_delta);
			}
		}
	}
}

// Stands in for CheckCollisionCameraWithShape and CheckCollisionCameraWithTerrain with
// physics rays. The client tests the terrain first; here the terrain is ordinary
// collision geometry too, so the eye is pulled in first and then kept clear of the ground.
void CameraController::_check_collision(Node3D *p_target, Vector3 &r_eye, const Vector3 &p_at) {
	Ref<World3D> world = get_world_3d();
	ERR_FAIL_COND(world.is_null());
	PhysicsDirectSpaceState3D *space_state = PhysicsServer3D::get_singleton()->space_get_direct_state(world->get_space());
	ERR_FAIL_NULL(space_state);

	const real_t near_plane = get_near();

	PS3DT::RayParameters ray_params;
	const CollisionObject3D *body = Object::cast_to<CollisionObject3D>(p_target);
	if (body) {
		ray_params.exclude.insert(body->get_rid());
	}

	// CShapeMgr::CheckCollisionCamera: move the eye to the first obstacle on the way from
	// the look-at point, unless that obstacle is closer to the eye than two near planes.
	ray_params.from = p_at;
	ray_params.to = r_eye;
	ray_params.collision_mask = collision_mask;
	PS3DT::RayResult result;
	if (space_state->intersect_ray(ray_params, result) && r_eye.distance_to(result.position) >= near_plane * 2.0) {
		r_eye = result.position;
	}

	// CTerrain::CheckCollisionCamera: keep the eye one near plane above the ground.
	ray_params.from = r_eye + Vector3(0, near_plane * 2.0, 0);
	ray_params.to = r_eye - Vector3(0, near_plane, 0);
	ray_params.collision_mask = ground_collision_mask;
	if (space_state->intersect_ray(ray_params, result)) {
		r_eye.y = result.position.y + near_plane;
	}
}

// The view point block of CGameEng::Tick. The client applies the result at once; there is
// no follow lag.
void CameraController::_update_view() {
	Node3D *target = _get_target();
	if (!target) {
		return;
	}

	const Vector3 position = target->get_global_position();
	const Vector3 facing = -target->get_global_basis().get_column(2);
	const real_t yaw = Math::atan2(facing.x, -facing.z);
	const real_t radius = character_height * 2.0;

	Vector3 eye;
	Vector3 at = position + Vector3(0, character_height * 0.8, 0);
	switch (view_point) {
		case VIEW_POINT_BACKWARD: {
			eye = at - _view_direction(yaw, pitch_backward) * (radius / Math::deg_to_rad(get_fov())) * zoom;
		} break;
		case VIEW_POINT_FIRST_PERSON: {
			eye = position + Vector3(0, character_height - 0.1, 0);
			at = eye + _view_direction(yaw, pitch_first_person);
		} break;
		case VIEW_POINT_FORWARD: {
			eye = at + _view_direction(yaw, -pitch_forward) * radius * zoom;
		} break;
		case VIEW_POINT_THIRD_PERSON: {
			eye = at - _view_direction(camera_yaw, third_person_pitch) * third_person_distance;
		} break;
	}

	// The first person view is not collision checked.
	if (collision_enabled && view_point != VIEW_POINT_FIRST_PERSON) {
		_check_collision(target, eye, at);
	}

	if ((at - eye).cross(Vector3(0, 1, 0)).is_zero_approx()) {
		set_global_position(eye);
	} else {
		look_at_from_position(eye, at);
	}
}

void CameraController::_notification(int p_what) {
	// Gameplay must not run while the scene is open in the editor.
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	switch (p_what) {
		case NOTIFICATION_READY: {
			// The camera is placed in world space every frame, whatever its parent does.
			set_as_top_level(true);
			_update_target();
			set_process(true);
			set_process_unhandled_input(true);
			_update_view();
		} break;

		case NOTIFICATION_PROCESS: {
			_process_local_input(get_process_delta_time());
			_update_view();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_end_drag();
			PlayerController *player = _get_player();
			if (player) {
				player->set_view_camera(nullptr);
			}
		} break;
	}
}

void CameraController::unhandled_input(const Ref<InputEvent> &p_event) {
	// Only events the GUI did not take reach here, like s_pUIMgr->m_bDoneSomething and the
	// focused window's OnMouseWheelEvent.
	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid()) {
		switch (button->get_button_index()) {
			case MouseButton::RIGHT: {
				if (button->is_pressed()) {
					right_button_down = true;
				} else {
					_end_drag();
				}
			} break;
			case MouseButton::MIDDLE: {
				// MOUSE_MBCLICKED is the release edge.
				if (!button->is_pressed()) {
					middle_rotate_time = 0.0;
				}
			} break;
			case MouseButton::WHEEL_UP:
			case MouseButton::WHEEL_DOWN: {
				if (button->is_pressed()) {
					const real_t factor = button->get_factor() > 0.0 ? button->get_factor() : 1.0;
					const real_t direction = button->get_button_index() == MouseButton::WHEEL_UP ? 1.0 : -1.0;
					_camera_zoom(direction * factor * wheel_zoom_step);
				}
			} break;
			default: {
			} break;
		}
		return;
	}

	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid() && right_button_down) {
		if (!dragging) {
			dragging = true;
			drag_origin = motion->get_position() - motion->get_relative();
			Input::get_singleton()->set_mouse_mode(Input::MouseMode::MOUSE_MODE_CAPTURED);
		}
		drag_motion += motion->get_relative();
	}
}

void CameraController::set_target_path(const NodePath &p_path) {
	target_path = p_path;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		_update_target();
	}
}

NodePath CameraController::get_target_path() const {
	return target_path;
}

void CameraController::set_view_point(ViewPoint p_view_point) {
	ERR_FAIL_INDEX((int)p_view_point, VIEW_POINT_THIRD_PERSON + 1);
	view_point = p_view_point;
}

CameraController::ViewPoint CameraController::get_view_point() const {
	return view_point;
}

void CameraController::set_character_height(real_t p_height) {
	character_height = p_height;
}

real_t CameraController::get_character_height() const {
	return character_height;
}

void CameraController::set_third_person_distance(real_t p_distance) {
	third_person_distance = p_distance;
}

real_t CameraController::get_third_person_distance() const {
	return third_person_distance;
}

void CameraController::set_third_person_distance_min(real_t p_distance) {
	third_person_distance_min = p_distance;
}

real_t CameraController::get_third_person_distance_min() const {
	return third_person_distance_min;
}

void CameraController::set_third_person_distance_max(real_t p_distance) {
	third_person_distance_max = p_distance;
}

real_t CameraController::get_third_person_distance_max() const {
	return third_person_distance_max;
}

void CameraController::set_third_person_pitch(real_t p_pitch) {
	third_person_pitch = p_pitch;
}

real_t CameraController::get_third_person_pitch() const {
	return third_person_pitch;
}

void CameraController::set_third_person_pitch_min(real_t p_pitch) {
	third_person_pitch_min = p_pitch;
}

real_t CameraController::get_third_person_pitch_min() const {
	return third_person_pitch_min;
}

void CameraController::set_camera_yaw(real_t p_yaw) {
	camera_yaw = p_yaw;
}

real_t CameraController::get_camera_yaw() const {
	return camera_yaw;
}

void CameraController::set_zoom(real_t p_zoom) {
	zoom = p_zoom;
}

real_t CameraController::get_zoom() const {
	return zoom;
}

void CameraController::set_zoom_min(real_t p_zoom) {
	zoom_min = p_zoom;
}

real_t CameraController::get_zoom_min() const {
	return zoom_min;
}

void CameraController::set_zoom_max(real_t p_zoom) {
	zoom_max = p_zoom;
}

real_t CameraController::get_zoom_max() const {
	return zoom_max;
}

void CameraController::set_pitch_min(real_t p_pitch) {
	pitch_min = p_pitch;
}

real_t CameraController::get_pitch_min() const {
	return pitch_min;
}

void CameraController::set_pitch_max(real_t p_pitch) {
	pitch_max = p_pitch;
}

real_t CameraController::get_pitch_max() const {
	return pitch_max;
}

void CameraController::set_key_pitch_speed(real_t p_speed) {
	key_pitch_speed = p_speed;
}

real_t CameraController::get_key_pitch_speed() const {
	return key_pitch_speed;
}

void CameraController::set_mouse_rotate_speed(int p_speed) {
	mouse_rotate_speed = p_speed;
}

int CameraController::get_mouse_rotate_speed() const {
	return mouse_rotate_speed;
}

void CameraController::set_drag_zoom(bool p_enabled) {
	drag_zoom = p_enabled;
}

bool CameraController::is_drag_zoom() const {
	return drag_zoom;
}

void CameraController::set_wheel_zoom_step(real_t p_step) {
	wheel_zoom_step = p_step;
}

real_t CameraController::get_wheel_zoom_step() const {
	return wheel_zoom_step;
}

void CameraController::set_screen_edge_rotation(bool p_enabled) {
	screen_edge_rotation = p_enabled;
}

bool CameraController::is_screen_edge_rotation() const {
	return screen_edge_rotation;
}

void CameraController::set_edge_yaw_speed(real_t p_speed) {
	edge_yaw_speed = p_speed;
}

real_t CameraController::get_edge_yaw_speed() const {
	return edge_yaw_speed;
}

void CameraController::set_edge_pitch_speed(real_t p_speed) {
	edge_pitch_speed = p_speed;
}

real_t CameraController::get_edge_pitch_speed() const {
	return edge_pitch_speed;
}

void CameraController::set_middle_click_spin_angle(real_t p_angle) {
	middle_click_spin_angle = p_angle;
}

real_t CameraController::get_middle_click_spin_angle() const {
	return middle_click_spin_angle;
}

void CameraController::set_middle_click_spin_speed(real_t p_speed) {
	middle_click_spin_speed = p_speed;
}

real_t CameraController::get_middle_click_spin_speed() const {
	return middle_click_spin_speed;
}

void CameraController::set_collision_enabled(bool p_enabled) {
	collision_enabled = p_enabled;
}

bool CameraController::is_collision_enabled() const {
	return collision_enabled;
}

void CameraController::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
}

uint32_t CameraController::get_collision_mask() const {
	return collision_mask;
}

void CameraController::set_ground_collision_mask(uint32_t p_mask) {
	ground_collision_mask = p_mask;
}

uint32_t CameraController::get_ground_collision_mask() const {
	return ground_collision_mask;
}

void CameraController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_target_path", "path"), &CameraController::set_target_path);
	ClassDB::bind_method(D_METHOD("get_target_path"), &CameraController::get_target_path);
	ClassDB::bind_method(D_METHOD("set_view_point", "view_point"), &CameraController::set_view_point);
	ClassDB::bind_method(D_METHOD("get_view_point"), &CameraController::get_view_point);
	ClassDB::bind_method(D_METHOD("set_character_height", "height"), &CameraController::set_character_height);
	ClassDB::bind_method(D_METHOD("get_character_height"), &CameraController::get_character_height);
	ClassDB::bind_method(D_METHOD("set_third_person_distance", "distance"), &CameraController::set_third_person_distance);
	ClassDB::bind_method(D_METHOD("get_third_person_distance"), &CameraController::get_third_person_distance);
	ClassDB::bind_method(D_METHOD("set_third_person_distance_min", "distance"), &CameraController::set_third_person_distance_min);
	ClassDB::bind_method(D_METHOD("get_third_person_distance_min"), &CameraController::get_third_person_distance_min);
	ClassDB::bind_method(D_METHOD("set_third_person_distance_max", "distance"), &CameraController::set_third_person_distance_max);
	ClassDB::bind_method(D_METHOD("get_third_person_distance_max"), &CameraController::get_third_person_distance_max);
	ClassDB::bind_method(D_METHOD("set_third_person_pitch", "pitch"), &CameraController::set_third_person_pitch);
	ClassDB::bind_method(D_METHOD("get_third_person_pitch"), &CameraController::get_third_person_pitch);
	ClassDB::bind_method(D_METHOD("set_third_person_pitch_min", "pitch"), &CameraController::set_third_person_pitch_min);
	ClassDB::bind_method(D_METHOD("get_third_person_pitch_min"), &CameraController::get_third_person_pitch_min);
	ClassDB::bind_method(D_METHOD("set_camera_yaw", "yaw"), &CameraController::set_camera_yaw);
	ClassDB::bind_method(D_METHOD("get_camera_yaw"), &CameraController::get_camera_yaw);
	ClassDB::bind_method(D_METHOD("set_zoom", "zoom"), &CameraController::set_zoom);
	ClassDB::bind_method(D_METHOD("get_zoom"), &CameraController::get_zoom);
	ClassDB::bind_method(D_METHOD("set_zoom_min", "zoom"), &CameraController::set_zoom_min);
	ClassDB::bind_method(D_METHOD("get_zoom_min"), &CameraController::get_zoom_min);
	ClassDB::bind_method(D_METHOD("set_zoom_max", "zoom"), &CameraController::set_zoom_max);
	ClassDB::bind_method(D_METHOD("get_zoom_max"), &CameraController::get_zoom_max);
	ClassDB::bind_method(D_METHOD("set_pitch_min", "pitch"), &CameraController::set_pitch_min);
	ClassDB::bind_method(D_METHOD("get_pitch_min"), &CameraController::get_pitch_min);
	ClassDB::bind_method(D_METHOD("set_pitch_max", "pitch"), &CameraController::set_pitch_max);
	ClassDB::bind_method(D_METHOD("get_pitch_max"), &CameraController::get_pitch_max);
	ClassDB::bind_method(D_METHOD("set_key_pitch_speed", "speed"), &CameraController::set_key_pitch_speed);
	ClassDB::bind_method(D_METHOD("get_key_pitch_speed"), &CameraController::get_key_pitch_speed);
	ClassDB::bind_method(D_METHOD("set_mouse_rotate_speed", "speed"), &CameraController::set_mouse_rotate_speed);
	ClassDB::bind_method(D_METHOD("get_mouse_rotate_speed"), &CameraController::get_mouse_rotate_speed);
	ClassDB::bind_method(D_METHOD("set_drag_zoom", "enabled"), &CameraController::set_drag_zoom);
	ClassDB::bind_method(D_METHOD("is_drag_zoom"), &CameraController::is_drag_zoom);
	ClassDB::bind_method(D_METHOD("set_wheel_zoom_step", "step"), &CameraController::set_wheel_zoom_step);
	ClassDB::bind_method(D_METHOD("get_wheel_zoom_step"), &CameraController::get_wheel_zoom_step);
	ClassDB::bind_method(D_METHOD("set_screen_edge_rotation", "enabled"), &CameraController::set_screen_edge_rotation);
	ClassDB::bind_method(D_METHOD("is_screen_edge_rotation"), &CameraController::is_screen_edge_rotation);
	ClassDB::bind_method(D_METHOD("set_edge_yaw_speed", "speed"), &CameraController::set_edge_yaw_speed);
	ClassDB::bind_method(D_METHOD("get_edge_yaw_speed"), &CameraController::get_edge_yaw_speed);
	ClassDB::bind_method(D_METHOD("set_edge_pitch_speed", "speed"), &CameraController::set_edge_pitch_speed);
	ClassDB::bind_method(D_METHOD("get_edge_pitch_speed"), &CameraController::get_edge_pitch_speed);
	ClassDB::bind_method(D_METHOD("set_middle_click_spin_angle", "angle"), &CameraController::set_middle_click_spin_angle);
	ClassDB::bind_method(D_METHOD("get_middle_click_spin_angle"), &CameraController::get_middle_click_spin_angle);
	ClassDB::bind_method(D_METHOD("set_middle_click_spin_speed", "speed"), &CameraController::set_middle_click_spin_speed);
	ClassDB::bind_method(D_METHOD("get_middle_click_spin_speed"), &CameraController::get_middle_click_spin_speed);
	ClassDB::bind_method(D_METHOD("set_collision_enabled", "enabled"), &CameraController::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("is_collision_enabled"), &CameraController::is_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &CameraController::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &CameraController::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_ground_collision_mask", "mask"), &CameraController::set_ground_collision_mask);
	ClassDB::bind_method(D_METHOD("get_ground_collision_mask"), &CameraController::get_ground_collision_mask);

	// Defaults are the client's constants, from CGameEng's constructor, CameraPitchAdd and
	// CameraZoom, and from the input handlers in GameProcMain.cpp and WarFareMain.cpp.
	ADD_GROUP("Follow", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_target_path", "get_target_path");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "view_point", PROPERTY_HINT_ENUM, "Backward,First Person,Forward,Third Person"), "set_view_point", "get_view_point");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "character_height", PROPERTY_HINT_RANGE, "0.1,10,0.01,or_greater,suffix:m"), "set_character_height", "get_character_height");

	ADD_GROUP("Third Person", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "third_person_distance", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m"), "set_third_person_distance", "get_third_person_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "third_person_distance_min", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m"), "set_third_person_distance_min", "get_third_person_distance_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "third_person_distance_max", PROPERTY_HINT_RANGE, "0,50,0.01,or_greater,suffix:m"), "set_third_person_distance_max", "get_third_person_distance_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "third_person_pitch", PROPERTY_HINT_RANGE, "-90,90,0.1,radians_as_degrees"), "set_third_person_pitch", "get_third_person_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "third_person_pitch_min", PROPERTY_HINT_RANGE, "-90,90,0.1,radians_as_degrees"), "set_third_person_pitch_min", "get_third_person_pitch_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "camera_yaw", PROPERTY_HINT_RANGE, "-360,360,0.1,or_less,or_greater,radians_as_degrees"), "set_camera_yaw", "get_camera_yaw");

	ADD_GROUP("Other Views", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom", PROPERTY_HINT_RANGE, "0.1,4,0.01,or_greater"), "set_zoom", "get_zoom");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_min", PROPERTY_HINT_RANGE, "0.1,4,0.01,or_greater"), "set_zoom_min", "get_zoom_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_max", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_zoom_max", "get_zoom_max");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_min", PROPERTY_HINT_RANGE, "-90,90,0.1,radians_as_degrees"), "set_pitch_min", "get_pitch_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_max", PROPERTY_HINT_RANGE, "-90,90,0.1,radians_as_degrees"), "set_pitch_max", "get_pitch_max");

	ADD_GROUP("Controls", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "key_pitch_speed", PROPERTY_HINT_RANGE, "0,360,0.1,or_greater,radians_as_degrees"), "set_key_pitch_speed", "get_key_pitch_speed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mouse_rotate_speed", PROPERTY_HINT_RANGE, "0,20,1,or_greater"), "set_mouse_rotate_speed", "get_mouse_rotate_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "drag_zoom"), "set_drag_zoom", "is_drag_zoom");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wheel_zoom_step", PROPERTY_HINT_RANGE, "0,1,0.0001,or_greater"), "set_wheel_zoom_step", "get_wheel_zoom_step");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "screen_edge_rotation"), "set_screen_edge_rotation", "is_screen_edge_rotation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_yaw_speed", PROPERTY_HINT_RANGE, "0,720,0.1,or_greater,radians_as_degrees"), "set_edge_yaw_speed", "get_edge_yaw_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_pitch_speed", PROPERTY_HINT_RANGE, "0,720,0.1,or_greater,radians_as_degrees"), "set_edge_pitch_speed", "get_edge_pitch_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "middle_click_spin_angle", PROPERTY_HINT_RANGE, "0,360,0.1,or_greater,radians_as_degrees"), "set_middle_click_spin_angle", "get_middle_click_spin_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "middle_click_spin_speed", PROPERTY_HINT_RANGE, "0,1440,0.1,or_greater,radians_as_degrees"), "set_middle_click_spin_speed", "get_middle_click_spin_speed");

	ADD_GROUP("Collision", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"), "set_collision_enabled", "is_collision_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ground_collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_ground_collision_mask", "get_ground_collision_mask");

	BIND_ENUM_CONSTANT(VIEW_POINT_BACKWARD);
	BIND_ENUM_CONSTANT(VIEW_POINT_FIRST_PERSON);
	BIND_ENUM_CONSTANT(VIEW_POINT_FORWARD);
	BIND_ENUM_CONSTANT(VIEW_POINT_THIRD_PERSON);
}

CameraController::CameraController() {
	// CGameEng's default camera: 70 degree field of view, 0.7 near and 512 far planes.
	set_fov(70.0);
	set_near(0.7);
	set_far(512.0);
}
