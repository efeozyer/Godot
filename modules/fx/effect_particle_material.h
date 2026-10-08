#pragma once

#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Process material for GPUParticles3D that reproduces the client engine's particle emitter
// (CFXPartParticles / CFXParticle).
//
// Particles spawn in a box around the emitter. A spread emitter throws them in a cone around the
// emit direction; a gather emitter sends them straight towards a point. Each particle then moves
// at a fixed velocity in its own local frame, that frame turns around the emit direction, and the
// particle rises or falls with its own gravity. Width and height grow independently over its life.
//
// All positions and directions are given in the client's left-handed space; the shader mirrors Z.
// Draw the particles with EffectQuadMaterial, which reads the per-particle size, age and frame.
class EffectParticleMaterial : public Material {
	GDCLASS(EffectParticleMaterial, Material);

public:
	enum EmitMode {
		EMIT_MODE_SPREAD = 1,
		EMIT_MODE_GATHER = 2,
	};

private:
	EmitMode emit_mode = EMIT_MODE_SPREAD;
	Vector3 emit_direction = Vector3(0, 0, 1);
	int flipbook_frames = 1;
	Ref<Texture2D> color_table;
	Vector3 box_min = Vector3();
	Vector3 box_max = Vector3();
	Vector3 gather_point = Vector3();
	float emit_angle = 0.0;
	float orbit_speed = 0.0;
	float speed = 0.0;
	float gravity = 0.0;
	Vector2 size_range = Vector2(1, 1);
	float size_add = 0.0;
	Vector2 scale_velocity = Vector2();
	Vector2 life_range = Vector2(1, 1);
	float life_extra = 0.0;
	float flipbook_fps = 0.0;

	static Mutex shader_mutex;
	static RID shader;
	static void _update_shader();
	mutable bool shader_set = false;

protected:
	static void _bind_methods();

public:
	void set_emit_mode(EmitMode p_mode);
	EmitMode get_emit_mode() const;

	void set_emit_direction(const Vector3 &p_direction);
	Vector3 get_emit_direction() const;

	void set_flipbook_frames(int p_frames);
	int get_flipbook_frames() const;

	void set_color_table(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_color_table() const;

	void set_box_min(const Vector3 &p_box_min);
	Vector3 get_box_min() const;

	void set_box_max(const Vector3 &p_box_max);
	Vector3 get_box_max() const;

	void set_gather_point(const Vector3 &p_gather_point);
	Vector3 get_gather_point() const;

	void set_emit_angle(float p_emit_angle);
	float get_emit_angle() const;

	void set_orbit_speed(float p_orbit_speed);
	float get_orbit_speed() const;

	void set_speed(float p_speed);
	float get_speed() const;

	void set_gravity(float p_gravity);
	float get_gravity() const;

	void set_size_range(const Vector2 &p_size_range);
	Vector2 get_size_range() const;

	void set_size_add(float p_size_add);
	float get_size_add() const;

	void set_scale_velocity(const Vector2 &p_scale_velocity);
	Vector2 get_scale_velocity() const;

	void set_life_range(const Vector2 &p_life_range);
	Vector2 get_life_range() const;

	void set_life_extra(float p_life_extra);
	float get_life_extra() const;

	void set_flipbook_fps(float p_flipbook_fps);
	float get_flipbook_fps() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();

	EffectParticleMaterial();
	virtual ~EffectParticleMaterial();
};

VARIANT_ENUM_CAST(EffectParticleMaterial::EmitMode)
