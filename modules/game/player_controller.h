#pragma once

#include "scene/3d/physics/character_body_3d.h"

class Camera3D;
class CameraController;

// Player movement of the Knight Online client: CGameProcMain::ProcessLocalInput and
// CPlayerMySelf. Add it to a scene with a CollisionShape3D child; the node origin is the
// character's feet. An CameraController that targets this node registers itself here.
//
// W/Up walks or runs forward, S/Down walks backward, A/D/Left/Right turn the character,
// E toggles continuous forward movement, Y toggles walk/run, and a left click on the
// ground moves the character there while the camera is in the third person view.
//
// Yaw is kept in the client's convention: 0 faces the client's +Z (Godot -Z) and a positive yaw turns the
// character to its right, so the node's Y rotation is -yaw.
class PlayerController : public CharacterBody3D {
	GDCLASS(PlayerController, CharacterBody3D);

public:
	// e_StateMove.
	enum MoveState {
		MOVE_STATE_STOP,
		MOVE_STATE_WALK,
		MOVE_STATE_RUN,
		MOVE_STATE_WALK_BACKWARD,
	};

	// e_MoveDirection.
	enum MoveDirection {
		MOVE_DIRECTION_STOP,
		MOVE_DIRECTION_FORWARD,
		MOVE_DIRECTION_BACKWARD,
	};

private:
	// Mirrors CLocalInput's IsKeyDown / IsKeyPress / IsKeyPressed for one key.
	struct KeyState {
		bool down = false;
		bool press = false;
		bool released = false;

		void update(bool p_down) {
			press = p_down && !down;
			released = !p_down && down;
			down = p_down;
		}
	};

	real_t walk_speed = 1.5;
	real_t run_speed_scale = 3.0;
	// CPlayerMySelf::m_bTempMoveTurbo: the client's speed-up for game masters, ten times the
	// speed while the key is held. Here it is Q, for anyone.
	real_t turbo_speed_scale = 10.0;
	bool turbo = false;
	real_t move_speed_scale = 1.0;
	bool running = true;
	real_t key_turn_speed = Math::deg_to_rad((real_t)60.0);
	real_t turn_speed = Math::deg_to_rad((real_t)270.0);
	real_t step_height = 0.6;

	bool click_to_move = true;
	real_t click_move_min_distance = 1.5;
	real_t click_move_arrive_distance = 0.5;
	real_t click_hold_delay = 0.1;
	uint32_t click_collision_mask = 1;

	NodePath animation_player_path;
	StringName animation_idle;
	StringName animation_walk;
	StringName animation_run;
	StringName animation_walk_backward;
	// The client's ANI_SITDOWN, ANI_SITDOWN_BREATH and ANI_STANDUP, by their clip names.
	StringName animation_sit_down = "SitDown";
	StringName animation_sit_idle = "SitDown_Breath";
	StringName animation_stand_up = "StandUp";
	// CPlayerMySelf::m_bSitDown.
	bool sitting = false;

	MoveState move_state = MOVE_STATE_STOP;
	int animation_state = -1;
	real_t move_speed = 0.0;
	real_t yaw = 0.0;
	real_t yaw_to_reach = 0.0;
	bool move_continuous = false;
	bool target_move = false;
	Vector3 target_position;

	KeyState key_forward;
	KeyState key_forward_alt;
	KeyState key_backward;
	KeyState key_backward_alt;
	KeyState key_move_continuous;
	KeyState key_toggle_run;
	KeyState key_sit;

	bool left_click_pending = false;
	bool left_button_down = false;
	bool move_click_started = false;
	double time = 0.0;
	double left_click_time = 0.0;

	ObjectID view_camera_id;

	bool _is_text_field_focused() const;
	bool _is_third_person() const;
	Camera3D *_get_camera() const;
	Vector3 _get_forward() const;
	void _apply_yaw();

	real_t _get_state_speed() const;
	bool _test_blocked(const Vector3 &p_motion, bool &r_step_up);
	bool _is_blocked_now();
	void _step_up(const Vector3 &p_motion);
	void _action_move(MoveState p_state);

	bool _get_cursor_yaw(const Camera3D *p_camera, const Point2 &p_cursor, real_t &r_yaw) const;
	bool _pick_ground(const Camera3D *p_camera, const Point2 &p_cursor, Vector3 &r_point);
	void _left_button_press(const Point2 &p_cursor);
	void _left_button_hold(const Point2 &p_cursor);
	void _left_button_release();

	void _process_mouse();
	void _process_keys(double p_delta);
	void _target_or_pos_move();
	void _tick_move(double p_delta);
	void _tick_yaw(double p_delta);
	void _update_animation();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual void unhandled_input(const Ref<InputEvent> &p_event) override;

	void set_view_camera(CameraController *p_camera);
	void view_point_changing();

	void rot_add(real_t p_radians);
	void rotate_to(real_t p_yaw, bool p_immediately);
	void command_move(MoveDirection p_direction, bool p_start_or_end);
	bool toggle_move_continuous();
	bool toggle_run_mode();
	void set_move_target_position(const Vector3 &p_position);

	MoveState get_move_state() const;
	real_t get_yaw() const;

	void set_walk_speed(real_t p_speed);
	real_t get_walk_speed() const;

	void set_turbo_speed_scale(real_t p_scale);
	real_t get_turbo_speed_scale() const;

	void set_run_speed_scale(real_t p_scale);
	real_t get_run_speed_scale() const;

	void set_move_speed_scale(real_t p_scale);
	real_t get_move_speed_scale() const;

	void set_running(bool p_running);
	bool is_running() const;

	void set_key_turn_speed(real_t p_speed);
	real_t get_key_turn_speed() const;

	void set_turn_speed(real_t p_speed);
	real_t get_turn_speed() const;

	void set_step_height(real_t p_height);
	real_t get_step_height() const;

	void set_click_to_move(bool p_enabled);
	bool is_click_to_move() const;

	void set_click_move_min_distance(real_t p_distance);
	real_t get_click_move_min_distance() const;

	void set_click_move_arrive_distance(real_t p_distance);
	real_t get_click_move_arrive_distance() const;

	void set_click_hold_delay(real_t p_delay);
	real_t get_click_hold_delay() const;

	void set_click_collision_mask(uint32_t p_mask);
	uint32_t get_click_collision_mask() const;

	void set_animation_player(const NodePath &p_path);
	NodePath get_animation_player() const;

	void set_animation_idle(const StringName &p_name);
	StringName get_animation_idle() const;

	void set_animation_walk(const StringName &p_name);
	StringName get_animation_walk() const;

	void set_animation_run(const StringName &p_name);
	StringName get_animation_run() const;

	void set_animation_sit_down(const StringName &p_name);
	StringName get_animation_sit_down() const;

	void set_animation_sit_idle(const StringName &p_name);
	StringName get_animation_sit_idle() const;

	void set_animation_stand_up(const StringName &p_name);
	StringName get_animation_stand_up() const;

	// CGameProcMain::CommandSitDown. The character only sits down from a standstill. Standing up
	// plays its animation unless p_immediately, which is how a move command gets it going.
	void command_sit_down(bool p_sit, bool p_immediately);
	bool is_sitting() const;

	void set_animation_walk_backward(const StringName &p_name);
	StringName get_animation_walk_backward() const;

	PlayerController();
};

VARIANT_ENUM_CAST(PlayerController::MoveState);
