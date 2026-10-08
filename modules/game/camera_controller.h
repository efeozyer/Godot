#pragma once

#include "scene/3d/camera_3d.h"

class PlayerController;

// Camera of the Knight Online client: CGameEng's view points and the camera half of
// CGameProcMain::ProcessLocalInput. It follows the node at target_path (its parent by
// default), whose origin must be the character's feet, and detaches from the parent's
// transform at runtime. If the target is an PlayerController the two cooperate: the
// camera turns the player in the views that sit behind it, and the player reads the view
// point for click-to-move.
//
// F9 cycles the view points. In the third person view the camera yaw is independent of
// the character: dragging with the right button turns the camera (horizontal) and zooms
// (vertical), Home/End tilt it, the wheel zooms, a middle click spins it by 180 degrees
// and the screen edges turn it. In the other views the same inputs turn the character and
// pitch the camera instead.
//
// Angles use the client's convention: yaw 0 looks along the client's +Z (Godot -Z), a positive yaw turns
// right and a positive pitch looks down.
class CameraController : public Camera3D {
	GDCLASS(CameraController, Camera3D);

public:
	// e_ViewPoint, in the order KM_CAMERA_CHANGE cycles through them.
	enum ViewPoint {
		VIEW_POINT_BACKWARD,
		VIEW_POINT_FIRST_PERSON,
		VIEW_POINT_FORWARD,
		VIEW_POINT_THIRD_PERSON,
	};

private:
	NodePath target_path = NodePath("..");
	ViewPoint view_point = VIEW_POINT_THIRD_PERSON;
	real_t character_height = 1.8;

	real_t third_person_distance = 7.0;
	real_t third_person_distance_min = 2.0;
	real_t third_person_distance_max = 10.0;
	real_t third_person_pitch = Math::deg_to_rad((real_t)25.0);
	real_t third_person_pitch_min = Math::deg_to_rad((real_t)10.0);
	real_t camera_yaw = 0.0;

	real_t zoom = 1.0;
	real_t zoom_min = 0.4;
	real_t zoom_max = 1.6;
	real_t pitch_min = Math::deg_to_rad((real_t)-50.0);
	real_t pitch_max = Math::deg_to_rad((real_t)70.0);

	real_t key_pitch_speed = Math::deg_to_rad((real_t)45.0);
	int mouse_rotate_speed = 5;
	bool drag_zoom = true;
	real_t wheel_zoom_step = 120.0 * 0.05 / 90.0;
	bool screen_edge_rotation = true;
	real_t edge_yaw_speed = 2.0;
	real_t edge_pitch_speed = 1.0;
	real_t middle_click_spin_angle = Math::PI;
	real_t middle_click_spin_speed = Math::TAU;

	bool collision_enabled = true;
	uint32_t collision_mask = 1;
	uint32_t ground_collision_mask = 1;

	real_t pitch_backward = 0.0;
	real_t pitch_first_person = 0.0;
	real_t pitch_forward = 0.0;

	ObjectID target_id;
	Vector2 drag_motion;
	bool right_button_down = false;
	bool dragging = false;
	Point2 drag_origin;
	real_t middle_rotate_time = -1.0;
	bool key_change_down = false;

	Node3D *_get_target() const;
	PlayerController *_get_player() const;
	void _update_target();

	void _camera_pitch_add(real_t p_radians);
	void _camera_yaw_add(real_t p_radians);
	void _camera_zoom(real_t p_amount);
	void _turn(real_t p_radians);
	void _end_drag();

	void _process_local_input(double p_delta);
	void _check_collision(Node3D *p_target, Vector3 &r_eye, const Vector3 &p_at);
	void _update_view();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual void unhandled_input(const Ref<InputEvent> &p_event) override;

	void view_point_change();

	void set_target_path(const NodePath &p_path);
	NodePath get_target_path() const;

	void set_view_point(ViewPoint p_view_point);
	ViewPoint get_view_point() const;

	void set_character_height(real_t p_height);
	real_t get_character_height() const;

	void set_third_person_distance(real_t p_distance);
	real_t get_third_person_distance() const;

	void set_third_person_distance_min(real_t p_distance);
	real_t get_third_person_distance_min() const;

	void set_third_person_distance_max(real_t p_distance);
	real_t get_third_person_distance_max() const;

	void set_third_person_pitch(real_t p_pitch);
	real_t get_third_person_pitch() const;

	void set_third_person_pitch_min(real_t p_pitch);
	real_t get_third_person_pitch_min() const;

	void set_camera_yaw(real_t p_yaw);
	real_t get_camera_yaw() const;

	void set_zoom(real_t p_zoom);
	real_t get_zoom() const;

	void set_zoom_min(real_t p_zoom);
	real_t get_zoom_min() const;

	void set_zoom_max(real_t p_zoom);
	real_t get_zoom_max() const;

	void set_pitch_min(real_t p_pitch);
	real_t get_pitch_min() const;

	void set_pitch_max(real_t p_pitch);
	real_t get_pitch_max() const;

	void set_key_pitch_speed(real_t p_speed);
	real_t get_key_pitch_speed() const;

	void set_mouse_rotate_speed(int p_speed);
	int get_mouse_rotate_speed() const;

	void set_drag_zoom(bool p_enabled);
	bool is_drag_zoom() const;

	void set_wheel_zoom_step(real_t p_step);
	real_t get_wheel_zoom_step() const;

	void set_screen_edge_rotation(bool p_enabled);
	bool is_screen_edge_rotation() const;

	void set_edge_yaw_speed(real_t p_speed);
	real_t get_edge_yaw_speed() const;

	void set_edge_pitch_speed(real_t p_speed);
	real_t get_edge_pitch_speed() const;

	void set_middle_click_spin_angle(real_t p_angle);
	real_t get_middle_click_spin_angle() const;

	void set_middle_click_spin_speed(real_t p_speed);
	real_t get_middle_click_spin_speed() const;

	void set_collision_enabled(bool p_enabled);
	bool is_collision_enabled() const;

	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;

	void set_ground_collision_mask(uint32_t p_mask);
	uint32_t get_ground_collision_mask() const;

	CameraController();
};

VARIANT_ENUM_CAST(CameraController::ViewPoint);
