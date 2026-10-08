#pragma once

#include "core/templates/hash_map.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Draws a camera-facing quad the way the client engine builds one, for both its particles
// (CFXParticle::Tick) and its billboard parts (CFXPartBillBoard::BuildVB).
//
// The quad's corners are scaled by the node's (or particle's) separate X and Y scale, turned by
// corner_basis, spun in the quad's own plane, and then oriented: towards the camera, towards the
// camera around the Y axis only, or by a fixed rotation. The quad can be pulled towards the camera
// so that it draws in front of whatever it is attached to. The texture can be a flipbook atlas.
//
// With per_particle enabled the spin angle and flipbook frame come from the particle data written
// by EffectParticleMaterial; otherwise they come from the angle and frame properties.
class EffectQuadMaterial : public Material {
	GDCLASS(EffectQuadMaterial, Material);

public:
	enum BlendMode {
		BLEND_MODE_ADD,
		BLEND_MODE_MIX,
		BLEND_MODE_CUSTOM,
	};

	enum BlendFactor {
		BLEND_FACTOR_ZERO,
		BLEND_FACTOR_ONE,
		BLEND_FACTOR_SRC_COLOR,
		BLEND_FACTOR_INV_SRC_COLOR,
		BLEND_FACTOR_SRC_ALPHA,
		BLEND_FACTOR_INV_SRC_ALPHA,
		BLEND_FACTOR_MAX
	};

	enum Facing {
		FACING_CAMERA,
		FACING_CAMERA_Y,
		FACING_FIXED,
	};

private:
	BlendMode blend_mode = BLEND_MODE_ADD;
	BlendFactor blend_src_factor = BLEND_FACTOR_SRC_ALPHA;
	BlendFactor blend_dst_factor = BLEND_FACTOR_ONE;
	Facing facing = FACING_CAMERA;
	Basis fixed_basis;
	Basis corner_basis;
	float camera_offset = 0.0;
	Color color = Color(1, 1, 1, 1);
	Ref<Texture2D> texture;
	int h_frames = 1;
	int v_frames = 1;
	bool per_particle = false;
	float spin_speed = 0.0;
	float angle = 0.0;
	int frame = 0;

	static Mutex shader_mutex;
	// One shader per blend state, keyed by _get_shader_key().
	static HashMap<int, RID> shaders;

	int _get_shader_key() const;
	void _update_shader();

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_blend_mode(BlendMode p_mode);
	BlendMode get_blend_mode() const;

	void set_blend_src_factor(BlendFactor p_factor);
	BlendFactor get_blend_src_factor() const;

	void set_blend_dst_factor(BlendFactor p_factor);
	BlendFactor get_blend_dst_factor() const;

	void set_facing(Facing p_facing);
	Facing get_facing() const;

	void set_fixed_basis(const Basis &p_basis);
	Basis get_fixed_basis() const;

	void set_corner_basis(const Basis &p_basis);
	Basis get_corner_basis() const;

	void set_camera_offset(float p_offset);
	float get_camera_offset() const;

	void set_color(const Color &p_color);
	Color get_color() const;

	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;

	void set_h_frames(int p_frames);
	int get_h_frames() const;

	void set_v_frames(int p_frames);
	int get_v_frames() const;

	void set_per_particle(bool p_enable);
	bool is_per_particle() const;

	void set_spin_speed(float p_speed);
	float get_spin_speed() const;

	void set_angle(float p_angle);
	float get_angle() const;

	void set_frame(int p_frame);
	int get_frame() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;

	static void cleanup_shader();

	EffectQuadMaterial();
	virtual ~EffectQuadMaterial();
};

VARIANT_ENUM_CAST(EffectQuadMaterial::BlendMode)
VARIANT_ENUM_CAST(EffectQuadMaterial::BlendFactor)
VARIANT_ENUM_CAST(EffectQuadMaterial::Facing)
