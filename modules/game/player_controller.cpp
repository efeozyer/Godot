#include "player_controller.h"

#include "camera_controller.h"

#include "core/config/engine.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "scene/3d/camera_3d.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/control.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

bool PlayerController::_is_text_field_focused() const {
	// Stands in for CUIBase::GetFocusedEdit(): movement keys are ignored while typing.
	const Control *focus = get_viewport()->gui_get_focus_owner();
	return focus && focus->is_text_field();
}

bool PlayerController::_is_third_person() const {
	const CameraController *camera = ObjectDB::get_instance<CameraController>(view_camera_id);
	return !camera || camera->get_view_point() == CameraController::VIEW_POINT_THIRD_PERSON;
}

Camera3D *PlayerController::_get_camera() const {
	Camera3D *camera = ObjectDB::get_instance<CameraController>(view_camera_id);
	return camera ? camera : get_viewport()->get_camera_3d();
}

Vector3 PlayerController::_get_forward() const {
	return Vector3(Math::sin(yaw), 0, -Math::cos(yaw));
}

void PlayerController::_apply_yaw() {
	set_rotation(Vector3(0, -yaw, 0));
}

// CPlayerMySelf::RotAdd, with the per-frame amount already multiplied out.
void PlayerController::rot_add(real_t p_radians) {
	yaw += p_radians;
	if (yaw >= Math::TAU) {
		yaw -= Math::TAU;
	}
	if (yaw <= -Math::TAU) {
		yaw += Math::TAU;
	}
	yaw_to_reach = yaw;
	_apply_yaw();
}

// CPlayerBase::RotateTo.
void PlayerController::rotate_to(real_t p_yaw, bool p_immediately) {
	int lot = (int)(p_yaw / Math::TAU);
	if (lot) {
		p_yaw -= lot * Math::TAU;
	}
	lot = (int)(p_yaw / Math::PI);
	if (lot) {
		p_yaw -= lot * Math::TAU;
	}

	yaw_to_reach = p_yaw;
	if (p_immediately) {
		yaw = p_yaw;
		_apply_yaw();
	}
}

// CPlayerBase::TickYaw.
void PlayerController::_tick_yaw(double p_delta) {
	if (yaw == yaw_to_reach) {
		return;
	}

	const real_t yaw_diff = yaw_to_reach - yaw;
	real_t yaw_delta = turn_speed * p_delta;
	if (Math::abs(yaw_diff) <= yaw_delta) {
		yaw = yaw_to_reach;
	} else {
		// Turn the short way round.
		if (yaw_diff > 0) {
			if (yaw_diff >= Math::PI) {
				yaw_delta = -yaw_delta;
			}
		} else if (yaw_diff > -Math::PI) {
			yaw_delta = -yaw_delta;
		}

		yaw += yaw_delta;
		if (Math::abs(yaw) > Math::PI) {
			const int lot = (int)(yaw / Math::PI);
			if (lot) {
				yaw -= lot * Math::TAU;
			}
		}
	}
	_apply_yaw();
}

// CPlayerMySelf::UpdateMoveSpeed.
real_t PlayerController::_get_state_speed() const {
	real_t speed = walk_speed;
	switch (move_state) {
		case MOVE_STATE_STOP: {
			speed = 0.0;
		} break;
		case MOVE_STATE_WALK: {
		} break;
		case MOVE_STATE_RUN: {
			speed *= run_speed_scale;
		} break;
		case MOVE_STATE_WALK_BACKWARD: {
			speed = -speed;
		} break;
	}
	return speed * move_speed_scale;
}

// Stands in for CPlayerMySelf::CheckCollision. The client never slides along an obstacle: a hit on
// anything steeper than the climbable slope refuses the whole step. A ledge up to
// step_height is climbed, as CheckCollision does for shapes within 0.6 of the feet.
bool PlayerController::_test_blocked(const Vector3 &p_motion, bool &r_step_up) {
	r_step_up = false;
	if (p_motion.is_zero_approx()) {
		return false;
	}

	const Transform3D from = get_global_transform();
	Ref<KinematicCollision3D> collision;
	collision.instantiate();
	if (!test_move(from, p_motion, collision)) {
		return false;
	}
	if (collision->get_normal().angle_to(Vector3(0, 1, 0)) <= get_floor_max_angle() + (real_t)0.01) {
		return false;
	}

	if (step_height > 0.0) {
		const Vector3 rise = Vector3(0, 1, 0) * step_height;
		Transform3D raised = from;
		raised.origin += rise;
		if (!test_move(from, rise) && !test_move(raised, p_motion)) {
			r_step_up = true;
			return false;
		}
	}
	return true;
}

bool PlayerController::_is_blocked_now() {
	bool step_up = false;
	return _test_blocked(_get_forward() * _get_state_speed() * get_physics_process_delta_time(), step_up);
}

void PlayerController::_step_up(const Vector3 &p_motion) {
	const Vector3 rise = Vector3(0, 1, 0) * step_height;
	Transform3D xform = get_global_transform();
	xform.origin += rise + p_motion;

	Ref<KinematicCollision3D> collision;
	collision.instantiate();
	if (test_move(xform, -rise, collision)) {
		xform.origin += collision->get_travel();
	} else {
		xform.origin -= rise;
	}
	set_global_transform(xform);
}

// CPlayerMySelf::ActionMove. The animation follows the state in _update_animation().
void PlayerController::_action_move(MoveState p_state) {
	move_state = p_state;
}

// CGameProcMain::CommandMove. The stop branch includes what MsgSend_Move(false, false)
// does to the player: it ends a click move.
void PlayerController::command_move(MoveDirection p_direction, bool p_start_or_end) {
	if (p_direction == MOVE_DIRECTION_STOP) {
		move_continuous = false;
		_action_move(MOVE_STATE_STOP);
		target_move = false;
		return;
	}

	if (p_direction == MOVE_DIRECTION_FORWARD) {
		_action_move(running ? MOVE_STATE_RUN : MOVE_STATE_WALK);
	} else {
		_action_move(MOVE_STATE_WALK_BACKWARD);
	}

	if (p_start_or_end && _is_blocked_now()) {
		_action_move(MOVE_STATE_STOP);
	}
}

// CGameProcMain::CommandToggleMoveContinous and CPlayerMySelf::ToggleMoveMode.
bool PlayerController::toggle_move_continuous() {
	move_continuous = !move_continuous;
	if (move_continuous) {
		move_state = running ? MOVE_STATE_RUN : MOVE_STATE_WALK;
		if (_is_blocked_now()) {
			move_continuous = false;
			move_state = MOVE_STATE_STOP;
		}
	} else {
		move_state = MOVE_STATE_STOP;
	}

	command_move(move_continuous ? MOVE_DIRECTION_FORWARD : MOVE_DIRECTION_STOP, true);
	return move_continuous;
}

// CPlayerMySelf::ToggleRunMode.
bool PlayerController::toggle_run_mode() {
	running = !running;
	if (running) {
		if (move_state == MOVE_STATE_WALK) {
			move_state = MOVE_STATE_RUN;
		}
	} else if (move_state == MOVE_STATE_RUN) {
		move_state = MOVE_STATE_WALK;
	}
	return running;
}

// CPlayerMySelf::SetMoveTargetPos.
void PlayerController::set_move_target_position(const Vector3 &p_position) {
	target_position = p_position;
	target_move = true;
}

void PlayerController::set_view_camera(CameraController *p_camera) {
	view_camera_id = p_camera ? p_camera->get_instance_id() : ObjectID();
}

// The player half of CGameProcMain::CommandCameraChange: leaving the third person view
// during a click move turns that move into continuous forward movement.
void PlayerController::view_point_changing() {
	if (_is_third_person() && target_move) {
		move_continuous = false;
		target_move = false;
		toggle_move_continuous();
	}
}

// The facing a click asks for: the screen direction from the character to the cursor,
// turned by the camera yaw (OnMouseLBtnPress / OnMouseLbtnDown).
bool PlayerController::_get_cursor_yaw(const Camera3D *p_camera, const Point2 &p_cursor, real_t &r_yaw) const {
	const Vector3 position = get_global_position();
	if (p_camera->is_position_behind(position)) {
		return false;
	}

	const Point2 screen = p_camera->unproject_position(position);
	const Vector3 view = -p_camera->get_global_basis().get_column(2);
	const real_t view_yaw = Math::atan2(view.x, -view.z);
	r_yaw = view_yaw + Math::atan2(p_cursor.x - screen.x, screen.y - p_cursor.y);
	return true;
}

// Stands in for PickWideWithTerrain + ClipPickPosWithShape with one physics ray.
bool PlayerController::_pick_ground(const Camera3D *p_camera, const Point2 &p_cursor, Vector3 &r_point) {
	Ref<World3D> world = get_world_3d();
	ERR_FAIL_COND_V(world.is_null(), false);
	PhysicsDirectSpaceState3D *space_state = PhysicsServer3D::get_singleton()->space_get_direct_state(world->get_space());
	ERR_FAIL_NULL_V(space_state, false);

	PS3DT::RayParameters ray_params;
	ray_params.from = p_camera->project_ray_origin(p_cursor);
	ray_params.to = ray_params.from + p_camera->project_ray_normal(p_cursor) * p_camera->get_far();
	ray_params.exclude.insert(get_rid());
	ray_params.collision_mask = click_collision_mask;

	PS3DT::RayResult result;
	if (!space_state->intersect_ray(ray_params, result)) {
		return false;
	}
	r_point = result.position;
	return true;
}

// CGameProcMain::OnMouseLBtnPress, the branch with no character under the cursor.
void PlayerController::_left_button_press(const Point2 &p_cursor) {
	const Input *input = Input::get_singleton();
	if (input->is_physical_key_pressed(Key::W) || input->is_physical_key_pressed(Key::E) || input->is_physical_key_pressed(Key::UP)) {
		return;
	}

	const Camera3D *camera = _get_camera();
	real_t cursor_yaw = 0.0;
	if (!camera || !_is_third_person() || !_get_cursor_yaw(camera, p_cursor, cursor_yaw)) {
		return;
	}

	left_click_time = time;
	rotate_to(cursor_yaw, true);

	Vector3 move_point;
	if (!_pick_ground(camera, p_cursor, move_point)) {
		return;
	}
	const Vector3 position = get_global_position();
	move_point.y = position.y;
	const real_t distance = move_point.distance_to(position);

	if (!target_move) {
		if (distance <= click_move_min_distance) {
			return;
		}
		move_click_started = true;
		command_move(MOVE_DIRECTION_FORWARD, true);
	}

	if (distance > click_move_min_distance) {
		set_move_target_position(move_point);
		return;
	}

	if (target_move) {
		command_move(MOVE_DIRECTION_STOP, true);
	}
}

// CGameProcMain::OnMouseLbtnDown: holding the button keeps steering towards the cursor.
void PlayerController::_left_button_hold(const Point2 &p_cursor) {
	const Input *input = Input::get_singleton();
	const Camera3D *camera = _get_camera();
	real_t cursor_yaw = 0.0;
	if (!camera || !_is_third_person() || !_get_cursor_yaw(camera, p_cursor, cursor_yaw)) {
		return;
	}
	if (input->is_physical_key_pressed(Key::W) || input->is_physical_key_pressed(Key::UP)) {
		return;
	}
	if (time - left_click_time <= click_hold_delay) {
		return;
	}

	rotate_to(cursor_yaw, true);

	Vector3 move_point;
	if (target_move) {
		command_move(MOVE_DIRECTION_FORWARD, false);
		if (_pick_ground(camera, p_cursor, move_point)) {
			set_move_target_position(move_point);
		}
	} else if (_pick_ground(camera, p_cursor, move_point) && move_point.distance_to(get_global_position()) > click_move_min_distance) {
		if (move_click_started) {
			command_move(MOVE_DIRECTION_FORWARD, false);
		} else {
			move_click_started = true;
			command_move(MOVE_DIRECTION_FORWARD, true);
		}
		set_move_target_position(move_point);
	}
}

// CGameProcMain::OnMouseLBtnPressd.
void PlayerController::_left_button_release() {
	move_click_started = false;
}

void PlayerController::_process_mouse() {
	if (left_click_pending) {
		left_click_pending = false;
		_left_button_press(get_viewport()->get_mouse_position());
	}

	if (left_button_down) {
		if (Input::get_singleton()->is_mouse_button_pressed(MouseButton::LEFT)) {
			_left_button_hold(get_viewport()->get_mouse_position());
		} else {
			left_button_down = false;
			_left_button_release();
		}
	}
}

// The movement keys of CGameProcMain::ProcessLocalInput, with the default eKeyMap.
void PlayerController::_process_keys(double p_delta) {
	const Input *input = Input::get_singleton();
	const bool enabled = !_is_text_field_focused();

	key_forward.update(enabled && input->is_physical_key_pressed(Key::W));
	key_forward_alt.update(enabled && input->is_physical_key_pressed(Key::UP));
	key_backward.update(enabled && input->is_physical_key_pressed(Key::S));
	key_backward_alt.update(enabled && input->is_physical_key_pressed(Key::DOWN));
	key_move_continuous.update(enabled && input->is_physical_key_pressed(Key::E));
	key_toggle_run.update(enabled && input->is_physical_key_pressed(Key::Y));

	// Shift+Y is a different shortcut in the client.
	if (key_toggle_run.press && !input->is_physical_key_pressed(Key::SHIFT)) {
		toggle_run_mode();
	}

	if (enabled) {
		if (input->is_physical_key_pressed(Key::A) || input->is_physical_key_pressed(Key::LEFT)) {
			rot_add(-key_turn_speed * p_delta);
		}
		if (input->is_physical_key_pressed(Key::D) || input->is_physical_key_pressed(Key::RIGHT)) {
			rot_add(key_turn_speed * p_delta);
		}
	}

	if (key_forward.down || key_forward_alt.down) {
		bool start = false;
		if (key_forward.press || key_forward_alt.press) {
			if (_is_third_person()) {
				// Taking over from a click move does not count as a fresh start.
				start = !target_move;
				target_move = false;
			} else {
				start = true;
			}
		}
		command_move(MOVE_DIRECTION_FORWARD, start);
	} else if (key_backward.down || key_backward_alt.down) {
		bool start = false;
		if (key_backward.press || key_backward_alt.press) {
			if (_is_third_person()) {
				start = !target_move;
				target_move = false;
			} else {
				start = true;
			}
		}
		command_move(MOVE_DIRECTION_BACKWARD, start);
	} else if (key_move_continuous.press) {
		toggle_move_continuous();
	}

	if (key_forward.released || key_forward_alt.released || key_backward.released || key_backward_alt.released) {
		command_move(MOVE_DIRECTION_STOP, true);
	}
}

// CPlayerMySelf::TargetOrPosMove, the branch that walks to a position.
void PlayerController::_target_or_pos_move() {
	if (!target_move) {
		return;
	}

	const Vector3 position = get_global_position();
	target_position.y = position.y;

	const Vector3 offset = target_position - position;
	if (!offset.is_zero_approx()) {
		rotate_to(Math::atan2(offset.x, -offset.z), true);
	}

	if (offset.length() < click_move_arrive_distance) {
		command_move(MOVE_DIRECTION_STOP, true);
	}
}

// The movement block of CPlayerMySelf::Tick. The client places the character on the terrain
// height itself; here the body's gravity and floor handling do that.
void PlayerController::_tick_move(double p_delta) {
	Vector3 horizontal;
	if (move_state != MOVE_STATE_STOP) {
		move_speed = _get_state_speed();
		const Vector3 direction = _get_forward();
		const Vector3 motion = direction * move_speed * p_delta;

		bool step_up = false;
		if (_test_blocked(motion, step_up)) {
			move_speed = 0.0;
		} else if (step_up) {
			_step_up(motion);
		} else {
			horizontal = direction * move_speed;
		}

		_target_or_pos_move();
	} else {
		move_speed = 0.0;
	}

	// Standing still, or refused by a collision: back to the stop state, which also ends
	// a click move (the stop packet path of MsgSend_Move).
	if (move_speed == 0.0) {
		_action_move(MOVE_STATE_STOP);
		target_move = false;
	}

	Vector3 linear_velocity = get_velocity();
	linear_velocity.x = horizontal.x;
	linear_velocity.z = horizontal.z;
	if (!is_on_floor()) {
		linear_velocity += get_gravity() * p_delta;
	}
	set_velocity(linear_velocity);
	move_and_slide();
}

void PlayerController::_update_animation() {
	if (animation_state == move_state) {
		return;
	}
	animation_state = move_state;

	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(get_node_or_null(animation_player_path));
	if (!player) {
		return;
	}

	StringName clip;
	switch (move_state) {
		case MOVE_STATE_STOP: {
			clip = animation_idle;
		} break;
		case MOVE_STATE_WALK: {
			clip = animation_walk;
		} break;
		case MOVE_STATE_RUN: {
			clip = animation_run;
		} break;
		case MOVE_STATE_WALK_BACKWARD: {
			clip = animation_walk_backward;
		} break;
	}
	if (!clip.is_empty() && player->has_animation(clip)) {
		player->play(clip);
	}
}

void PlayerController::_notification(int p_what) {
	// Gameplay must not run while the scene is open in the editor.
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	switch (p_what) {
		case NOTIFICATION_READY: {
			yaw = -get_rotation().y;
			yaw_to_reach = yaw;
			set_physics_process(true);
			set_process_unhandled_input(true);
		} break;

		case NOTIFICATION_PHYSICS_PROCESS: {
			const double delta = get_physics_process_delta_time();
			time += delta;

			// Same order as a client frame: mouse, keys, then the player tick.
			if (click_to_move) {
				_process_mouse();
			}
			_process_keys(delta);
			_tick_move(delta);
			_tick_yaw(delta);
			_update_animation();
		} break;
	}
}

void PlayerController::unhandled_input(const Ref<InputEvent> &p_event) {
	// Only clicks the GUI did not take reach here, like s_pUIMgr->m_bDoneSomething.
	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->is_pressed() && button->get_button_index() == MouseButton::LEFT) {
		left_click_pending = true;
		left_button_down = true;
	}
}

PlayerController::MoveState PlayerController::get_move_state() const {
	return move_state;
}

real_t PlayerController::get_yaw() const {
	return yaw;
}

void PlayerController::set_walk_speed(real_t p_speed) {
	walk_speed = p_speed;
}

real_t PlayerController::get_walk_speed() const {
	return walk_speed;
}

void PlayerController::set_run_speed_scale(real_t p_scale) {
	run_speed_scale = p_scale;
}

real_t PlayerController::get_run_speed_scale() const {
	return run_speed_scale;
}

void PlayerController::set_move_speed_scale(real_t p_scale) {
	move_speed_scale = p_scale;
}

real_t PlayerController::get_move_speed_scale() const {
	return move_speed_scale;
}

void PlayerController::set_running(bool p_running) {
	if (running != p_running) {
		toggle_run_mode();
	}
}

bool PlayerController::is_running() const {
	return running;
}

void PlayerController::set_key_turn_speed(real_t p_speed) {
	key_turn_speed = p_speed;
}

real_t PlayerController::get_key_turn_speed() const {
	return key_turn_speed;
}

void PlayerController::set_turn_speed(real_t p_speed) {
	turn_speed = p_speed;
}

real_t PlayerController::get_turn_speed() const {
	return turn_speed;
}

void PlayerController::set_step_height(real_t p_height) {
	step_height = p_height;
}

real_t PlayerController::get_step_height() const {
	return step_height;
}

void PlayerController::set_click_to_move(bool p_enabled) {
	click_to_move = p_enabled;
}

bool PlayerController::is_click_to_move() const {
	return click_to_move;
}

void PlayerController::set_click_move_min_distance(real_t p_distance) {
	click_move_min_distance = p_distance;
}

real_t PlayerController::get_click_move_min_distance() const {
	return click_move_min_distance;
}

void PlayerController::set_click_move_arrive_distance(real_t p_distance) {
	click_move_arrive_distance = p_distance;
}

real_t PlayerController::get_click_move_arrive_distance() const {
	return click_move_arrive_distance;
}

void PlayerController::set_click_hold_delay(real_t p_delay) {
	click_hold_delay = p_delay;
}

real_t PlayerController::get_click_hold_delay() const {
	return click_hold_delay;
}

void PlayerController::set_click_collision_mask(uint32_t p_mask) {
	click_collision_mask = p_mask;
}

uint32_t PlayerController::get_click_collision_mask() const {
	return click_collision_mask;
}

void PlayerController::set_animation_player(const NodePath &p_path) {
	animation_player_path = p_path;
	animation_state = -1;
}

NodePath PlayerController::get_animation_player() const {
	return animation_player_path;
}

void PlayerController::set_animation_idle(const StringName &p_name) {
	animation_idle = p_name;
	animation_state = -1;
}

StringName PlayerController::get_animation_idle() const {
	return animation_idle;
}

void PlayerController::set_animation_walk(const StringName &p_name) {
	animation_walk = p_name;
	animation_state = -1;
}

StringName PlayerController::get_animation_walk() const {
	return animation_walk;
}

void PlayerController::set_animation_run(const StringName &p_name) {
	animation_run = p_name;
	animation_state = -1;
}

StringName PlayerController::get_animation_run() const {
	return animation_run;
}

void PlayerController::set_animation_walk_backward(const StringName &p_name) {
	animation_walk_backward = p_name;
	animation_state = -1;
}

StringName PlayerController::get_animation_walk_backward() const {
	return animation_walk_backward;
}

void PlayerController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_walk_speed", "speed"), &PlayerController::set_walk_speed);
	ClassDB::bind_method(D_METHOD("get_walk_speed"), &PlayerController::get_walk_speed);
	ClassDB::bind_method(D_METHOD("set_run_speed_scale", "scale"), &PlayerController::set_run_speed_scale);
	ClassDB::bind_method(D_METHOD("get_run_speed_scale"), &PlayerController::get_run_speed_scale);
	ClassDB::bind_method(D_METHOD("set_move_speed_scale", "scale"), &PlayerController::set_move_speed_scale);
	ClassDB::bind_method(D_METHOD("get_move_speed_scale"), &PlayerController::get_move_speed_scale);
	ClassDB::bind_method(D_METHOD("set_running", "running"), &PlayerController::set_running);
	ClassDB::bind_method(D_METHOD("is_running"), &PlayerController::is_running);
	ClassDB::bind_method(D_METHOD("set_key_turn_speed", "speed"), &PlayerController::set_key_turn_speed);
	ClassDB::bind_method(D_METHOD("get_key_turn_speed"), &PlayerController::get_key_turn_speed);
	ClassDB::bind_method(D_METHOD("set_turn_speed", "speed"), &PlayerController::set_turn_speed);
	ClassDB::bind_method(D_METHOD("get_turn_speed"), &PlayerController::get_turn_speed);
	ClassDB::bind_method(D_METHOD("set_step_height", "height"), &PlayerController::set_step_height);
	ClassDB::bind_method(D_METHOD("get_step_height"), &PlayerController::get_step_height);
	ClassDB::bind_method(D_METHOD("set_click_to_move", "enabled"), &PlayerController::set_click_to_move);
	ClassDB::bind_method(D_METHOD("is_click_to_move"), &PlayerController::is_click_to_move);
	ClassDB::bind_method(D_METHOD("set_click_move_min_distance", "distance"), &PlayerController::set_click_move_min_distance);
	ClassDB::bind_method(D_METHOD("get_click_move_min_distance"), &PlayerController::get_click_move_min_distance);
	ClassDB::bind_method(D_METHOD("set_click_move_arrive_distance", "distance"), &PlayerController::set_click_move_arrive_distance);
	ClassDB::bind_method(D_METHOD("get_click_move_arrive_distance"), &PlayerController::get_click_move_arrive_distance);
	ClassDB::bind_method(D_METHOD("set_click_hold_delay", "delay"), &PlayerController::set_click_hold_delay);
	ClassDB::bind_method(D_METHOD("get_click_hold_delay"), &PlayerController::get_click_hold_delay);
	ClassDB::bind_method(D_METHOD("set_click_collision_mask", "mask"), &PlayerController::set_click_collision_mask);
	ClassDB::bind_method(D_METHOD("get_click_collision_mask"), &PlayerController::get_click_collision_mask);
	ClassDB::bind_method(D_METHOD("set_animation_player", "path"), &PlayerController::set_animation_player);
	ClassDB::bind_method(D_METHOD("get_animation_player"), &PlayerController::get_animation_player);
	ClassDB::bind_method(D_METHOD("set_animation_idle", "name"), &PlayerController::set_animation_idle);
	ClassDB::bind_method(D_METHOD("get_animation_idle"), &PlayerController::get_animation_idle);
	ClassDB::bind_method(D_METHOD("set_animation_walk", "name"), &PlayerController::set_animation_walk);
	ClassDB::bind_method(D_METHOD("get_animation_walk"), &PlayerController::get_animation_walk);
	ClassDB::bind_method(D_METHOD("set_animation_run", "name"), &PlayerController::set_animation_run);
	ClassDB::bind_method(D_METHOD("get_animation_run"), &PlayerController::get_animation_run);
	ClassDB::bind_method(D_METHOD("set_animation_walk_backward", "name"), &PlayerController::set_animation_walk_backward);
	ClassDB::bind_method(D_METHOD("get_animation_walk_backward"), &PlayerController::get_animation_walk_backward);
	ClassDB::bind_method(D_METHOD("get_move_state"), &PlayerController::get_move_state);

	// Defaults are the client's constants: MOVE_SPEED_WHEN_WALK, MOVE_DELTA_WHEN_RUNNING,
	// RotKeyDelta, m_fRotRadianPerSec and the click-move distances in GameProcMain.cpp.
	ADD_GROUP("Movement", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walk_speed", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater,suffix:m/s"), "set_walk_speed", "get_walk_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "run_speed_scale", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_run_speed_scale", "get_run_speed_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "move_speed_scale", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_move_speed_scale", "get_move_speed_scale");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "running"), "set_running", "is_running");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "key_turn_speed", PROPERTY_HINT_RANGE, "0,720,0.1,or_greater,radians_as_degrees"), "set_key_turn_speed", "get_key_turn_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turn_speed", PROPERTY_HINT_RANGE, "0,720,0.1,or_greater,radians_as_degrees"), "set_turn_speed", "get_turn_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "step_height", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:m"), "set_step_height", "get_step_height");

	ADD_GROUP("Click To Move", "click_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "click_to_move"), "set_click_to_move", "is_click_to_move");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "click_move_min_distance", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_click_move_min_distance", "get_click_move_min_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "click_move_arrive_distance", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_click_move_arrive_distance", "get_click_move_arrive_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "click_hold_delay", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:s"), "set_click_hold_delay", "get_click_hold_delay");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "click_collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_click_collision_mask", "get_click_collision_mask");

	ADD_GROUP("Animation", "animation_");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "animation_player", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AnimationPlayer"), "set_animation_player", "get_animation_player");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation_idle"), "set_animation_idle", "get_animation_idle");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation_walk"), "set_animation_walk", "get_animation_walk");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation_run"), "set_animation_run", "get_animation_run");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation_walk_backward"), "set_animation_walk_backward", "get_animation_walk_backward");

	BIND_ENUM_CONSTANT(MOVE_STATE_STOP);
	BIND_ENUM_CONSTANT(MOVE_STATE_WALK);
	BIND_ENUM_CONSTANT(MOVE_STATE_RUN);
	BIND_ENUM_CONSTANT(MOVE_STATE_WALK_BACKWARD);
}

PlayerController::PlayerController() {
	// MAX_INCLINE_CLIMB: slopes steeper than 40 degrees cannot be climbed.
	set_floor_max_angle(Math::deg_to_rad((real_t)40.0));
}
