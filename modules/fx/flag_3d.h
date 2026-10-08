#pragma once

#include "flag_material.h"

#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/mesh.h"

// One banner of a zone's .flag file: a port of CFlag and of the part of CFlagMgr::Tick
// that animates it.
//
// The banner is a square grid 3.31 wide and 1.82 high, standing in the node's XY plane with its
// pole edge on the Y axis and its lower pole corner at the origin; the node's transform holds the
// position, rotation and scale stored in the file. flag_kind picks the tessellation and the wave:
//
//   0      6 x 6 quads, amplitude 0.5, phase advancing by wave_speed * 0.02 a second
//   1      12 x 12 quads, amplitude 0.5, phase advancing by wave_speed * 0.034 a second
//   2      a single quad that does not move
//   other  (kind / 4) x (kind / 4) quads, amplitude kind / 100, phase as for kind 0
//
// A banner without a texture is not drawn, as in the client, where the banners that belong to
// clans stay hidden until the server names the clans.
class Flag3D : public GeometryInstance3D {
	GDCLASS(Flag3D, GeometryInstance3D);

public:
	// CFlag::FLAG_WIDTH, FLAG_HEIGHT.
	static constexpr float FLAG_WIDTH = 3.31f;
	static constexpr float FLAG_HEIGHT = 1.82f;

private:
	String flag_id;
	int flag_kind = 0;
	Ref<Texture2D> texture;
	FlagMaterial::Transparency transparency = FlagMaterial::TRANSPARENCY_DISABLED;
	float wave_speed = 30.0;
	float wave_phase = 0.0;

	Ref<ArrayMesh> mesh;
	Ref<FlagMaterial> material;
	AABB aabb;

	int _get_segments() const;
	float _get_amplitude() const;
	void _rebuild();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// The name the zone file gives the banner. The client addresses banners by the number this
	// starts with.
	void set_flag_id(const String &p_id);
	String get_flag_id() const;

	void set_flag_kind(int p_kind);
	int get_flag_kind() const;

	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;

	void set_flag_transparency(FlagMaterial::Transparency p_transparency);
	FlagMaterial::Transparency get_flag_transparency() const;

	void set_wave_speed(float p_speed);
	float get_wave_speed() const;

	void set_wave_phase(float p_phase);
	float get_wave_phase() const;

	virtual AABB get_aabb() const override;

	Flag3D();
	~Flag3D();
};
