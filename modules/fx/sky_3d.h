#pragma once

#include "sky_material.h"

#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/environment.h"
#include "scene/resources/mesh.h"

// The sky of a zone: a port of CSkyMng and of the classes it drives (CSky, CStar,
// CMoon, CSun, CCloud, CColorChange).
//
// A zone's sky file is a list of "day changes": at a game time of day, one element of the sky
// (sky colour, fog colour, star count, moon phase, the sun's three sprites, the two cloud layer
// colours, the cloud textures, the three world lights) starts fading to a new value over a number
// of real seconds. This node keeps the game clock, runs that list exactly as the client does and
// applies the result every frame:
//
//  - it draws the sky layers itself (see SkyMaterial), around whichever camera is rendering;
//  - environment: background colour = sky colour (the client clears the screen with it), fog
//    colour and range (the client's linear fog starts at a tenth of the view distance and ends
//    at it), ambient light = the ambient terms of the two directional lights;
//  - sun_light: colour of light 0 and the direction of the client's shadow-map light
//    (CShadow::BuildLightMatrices), which follows the sun, (-1, 0, 1) turned about the Z axis by
//    the sun's angle, from higher up and is turned around at night;
//  - every TerrainMaterial and NormalMapMaterial: sun direction, light 0's colour, the night
//    flag, fog colour and far plane (see EffectWorld), as CGameProcMain::Render hands them to
//    the two effects each frame;
//  - support_light: colour of light 1, direction (2, -3, 2);
//  - player_light: colour of light 2, the point light the client keeps near the player.
//
// Game time runs time_scale times faster than real time (10 in the client, so a day lasts 2.4
// hours); the client takes the time of day from the server and starts at 10:00 until it has one.
// The clock does not advance in the editor.
class Sky3D : public Node3D {
	GDCLASS(Sky3D, Node3D);

public:
	// eSKY_DAYCHANGE
	enum ChangeType {
		CHANGE_SKY_COLOR,
		CHANGE_FOG_COLOR,
		CHANGE_STAR_COUNT,
		CHANGE_MOON_PHASE,
		CHANGE_SUN_COLOR,
		CHANGE_GLOW_COLOR,
		CHANGE_FLARE_COLOR,
		CHANGE_CLOUD1_COLOR,
		CHANGE_CLOUD2_COLOR,
		CHANGE_CLOUD_TEXTURE,
		CHANGE_LIGHT0_COLOR,
		CHANGE_LIGHT1_COLOR,
		CHANGE_LIGHT2_COLOR,
		CHANGE_MAX
	};

	static constexpr int SUN_PART_COUNT = 3; // Disc, glow, flare.
	static constexpr int CLOUD_TEXTURE_COUNT = 6; // Wisps, puffs, tatters, streaks, dense, overcast.
	static constexpr int CLOUD_LAYER_COUNT = 2;
	static constexpr int LIGHT_COUNT = 3;
	static constexpr int STAR_COUNT = 200;

private:
	// CColorChange: a D3DCOLOR fading to another, advanced in steps of at least 0.1 s.
	struct ColorFade {
		uint32_t current = 0xffffffff;
		uint32_t previous = 0xffffffff;
		uint32_t next = 0xffffffff;
		float percentage = 0.0;
		float rate = 0.0;
		float pending = 0.0;

		void change(uint32_t p_color, float p_seconds = 0.0);
		void set_percentage(float p_percentage);
		void tick(float p_delta);
	};

	// CDeltaChange: the same for a number.
	struct DeltaFade {
		float current = 0.0;
		float previous = 0.0;
		float next = 0.0;
		float percentage = 0.0;
		float rate = 0.0;
		float pending = 0.0;

		void start(float p_value, float p_seconds = 0.0);
		void set_percentage(float p_percentage);
		void tick(float p_delta);
	};

	// __SKY_DAYCHANGE
	struct DayChange {
		int type = 0;
		uint32_t when = 0; // Game seconds from 00:00.
		uint32_t param1 = 0;
		uint32_t param2 = 0;
		float how_long = 0.0; // Real seconds at the client's clock rate.
	};

	struct Layer {
		RID instance;
		Ref<SkyMaterial> material;
		bool drawn = true;
	};

	// In the order the client draws them.
	static constexpr int LAYER_HORIZON = 0;
	static constexpr int LAYER_STARS = 1;
	static constexpr int LAYER_MOON = 2;
	static constexpr int LAYER_SUN = 3;
	static constexpr int LAYER_CLOUD = LAYER_SUN + SUN_PART_COUNT;
	static constexpr int LAYER_MAX = LAYER_CLOUD + CLOUD_LAYER_COUNT;

	// Exported data.
	Ref<Texture2D> sun_textures[SUN_PART_COUNT];
	Ref<Texture2D> cloud_textures[CLOUD_TEXTURE_COUNT];
	Ref<Texture2D> moon_texture;
	PackedStringArray change_names;
	PackedInt32Array change_types;
	PackedInt32Array change_times;
	PackedInt32Array change_params1;
	PackedInt32Array change_params2;
	PackedFloat32Array change_durations;
	bool clock_running = true;
	float time_scale = 10.0;
	int fixed_sun_hour = 0;
	float view_distance = 512.0;
	float shadow_distance = 150.0;
	Ref<Environment> environment;
	NodePath sun_light;
	NodePath support_light;
	NodePath player_light;

	// CSkyMng state.
	LocalVector<DayChange> changes; // Sorted by time.
	int change_position = 0;
	double game_time = 10.0 * 3600.0; // Seconds from 00:00.
	int game_year = 0;
	int game_month = 0;
	int game_day = 0;
	bool state_dirty = true;

	ColorFade sky_color;
	ColorFade fog_color;
	ColorFade light_diffuse[LIGHT_COUNT];
	ColorFade light_ambient[LIGHT_COUNT];

	// CSun, CMoon.
	ColorFade sun_color[SUN_PART_COUNT];
	DeltaFade sun_size[SUN_PART_COUNT];
	float sun_radian = 0.0;
	float moon_radian = Math::PI;
	int moon_phase = 10;

	// CStar.
	int star_current = 0;
	int star_next = 0;
	float star_add_time = 0.0;
	bool star_increase = false;
	float star_pending = 0.0;

	// CCloud.
	ColorFade cloud_color[CLOUD_LAYER_COUNT];
	ColorFade cloud_alpha;
	int cloud_type[3] = { 0, 1, -1 };
	float cloud_texture_time = 0.0;
	int cloud_backup = -1;
	float cloud_backup_time = 0.0;
	float cloud_scroll_pending = 0.0;
	// tu, tv, tu2, tv2 of each layer's vertices, as an offset from where they started.
	float cloud_uv[CLOUD_LAYER_COUNT][4] = {};
	uint32_t cloud_vertex_color[CLOUD_LAYER_COUNT] = { 0xffffffff, 0xffffffff };

	// Drawing.
	Ref<ArrayMesh> horizon_mesh;
	Ref<ArrayMesh> star_mesh;
	Ref<ArrayMesh> disc_mesh;
	Ref<ArrayMesh> cloud_mesh;
	Layer layers[LAYER_MAX];

	void _create_layers();
	void _update_layer_visibility();

	void _reset_state();
	void _rebuild_changes();
	void _update_state_if_dirty();
	int _get_latest_change(int p_type, int p_position) const;
	void _apply_change(const DayChange &p_change, float p_take_time);
	void _set_check_game_time(uint32_t p_game_time);
	void _set_star(int p_count, float p_seconds);
	void _set_cloud(int p_cloud1, int p_cloud2, float p_seconds);
	void _tick_clouds(float p_frame_delta, float p_fade_delta);
	void _tick(double p_delta);
	void _apply();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_sun_texture(int p_part, const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_sun_texture(int p_part) const;

	void set_cloud_texture(int p_cloud, const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_cloud_texture(int p_cloud) const;

	void set_moon_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_moon_texture() const;

	void set_change_names(const PackedStringArray &p_names);
	PackedStringArray get_change_names() const;

	void set_change_types(const PackedInt32Array &p_types);
	PackedInt32Array get_change_types() const;

	void set_change_times(const PackedInt32Array &p_times);
	PackedInt32Array get_change_times() const;

	void set_change_params1(const PackedInt32Array &p_params);
	PackedInt32Array get_change_params1() const;

	void set_change_params2(const PackedInt32Array &p_params);
	PackedInt32Array get_change_params2() const;

	void set_change_durations(const PackedFloat32Array &p_durations);
	PackedFloat32Array get_change_durations() const;

	// Hours from midnight. Setting it does what the client does when the server sends the time.
	void set_time_of_day(float p_hours);
	float get_time_of_day() const;

	void set_clock_running(bool p_running);
	bool is_clock_running() const;

	void set_time_scale(float p_scale);
	float get_time_scale() const;

	// Zones.tbl iFixedSundDirection: above zero, the sun and moon stay where they are at that hour
	// and the schedule is read at that hour too.
	void set_fixed_sun_hour(int p_hour);
	int get_fixed_sun_hour() const;

	void set_moon_phase(int p_phase);
	int get_moon_phase() const;

	// CSkyMng::SetGameTime's date: the moon phase becomes month * 30 + day.
	void set_game_date(int p_year, int p_month, int p_day);

	void set_view_distance(float p_distance);
	float get_view_distance() const;

	// The zone's shadow range: the client's shadow light, which sun_light is given the direction
	// of, stands this far back along the sun's direction and 100 higher.
	void set_shadow_distance(float p_distance);
	float get_shadow_distance() const;

	void set_environment(const Ref<Environment> &p_environment);
	Ref<Environment> get_environment() const;

	void set_sun_light(const NodePath &p_path);
	NodePath get_sun_light() const;

	void set_support_light(const NodePath &p_path);
	NodePath get_support_light() const;

	void set_player_light(const NodePath &p_path);
	NodePath get_player_light() const;

	// What the schedule currently gives.
	Color get_sky_color() const;
	Color get_fog_color() const;
	Color get_light_diffuse_color(int p_light) const;
	Color get_light_ambient_color(int p_light) const;
	float get_sun_angle() const;
	bool is_night_time() const;

	Sky3D();
	~Sky3D();
};

VARIANT_ENUM_CAST(Sky3D::ChangeType)
