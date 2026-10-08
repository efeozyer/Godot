#include "sky_3d.h"

#include "effect_common.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/light_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_server.h"

// TIME_REAL_PER_GAME: game seconds per real second in the client. Fade durations are stored in
// real seconds at this rate.
static const float CLIENT_TIME_SCALE = 10.0f;
static const uint32_t SECONDS_PER_DAY = 86400;
static const int CLOUD_NONE = -1;
// CMoon::Init: side of the moon as a fraction of the viewport width.
static const float MOON_RATIO = 0.2f;

static Color color_from_argb(uint32_t p_color) {
	return Color(((p_color >> 16) & 0xff) / 255.0f, ((p_color >> 8) & 0xff) / 255.0f, (p_color & 0xff) / 255.0f, ((p_color >> 24) & 0xff) / 255.0f);
}

// --- CColorChange / CDeltaChange -----------------------------------------------------------

void Sky3D::ColorFade::change(uint32_t p_color, float p_seconds) {
	if (p_color == current || p_seconds <= 0.0f) {
		current = previous = next = p_color;
		rate = percentage = pending = 0.0f;
		return;
	}
	previous = current;
	next = p_color;
	percentage = 0.0f;
	rate = 1.0f / p_seconds;
}

void Sky3D::ColorFade::set_percentage(float p_percentage) {
	if (!(p_percentage >= 0.0f && p_percentage <= 1.0f)) {
		return;
	}
	if (p_percentage == 1.0f) {
		previous = current = next;
		percentage = rate = 0.0f;
		return;
	}
	percentage = p_percentage;
	pending = 0.0f;
}

void Sky3D::ColorFade::tick(float p_delta) {
	if (rate == 0.0f) {
		return;
	}

	pending += p_delta;
	if (pending > 0.1f) {
		percentage += rate * pending;
		pending = 0.0f;
	}

	if (percentage > 1.0f) {
		previous = current = next;
		percentage = rate = 0.0f;
		return;
	}

	// Each channel moves by a truncated share of the difference.
	uint32_t result = 0;
	for (int shift = 0; shift < 32; shift += 8) {
		const int from = (previous >> shift) & 0xff;
		const int to = (next >> shift) & 0xff;
		result |= uint32_t((from + int((to - from) * percentage)) & 0xff) << shift;
	}
	current = result;
}

void Sky3D::DeltaFade::start(float p_value, float p_seconds) {
	if (p_value == current || p_seconds <= 0.0f) {
		current = previous = next = p_value;
		rate = percentage = pending = 0.0f;
		return;
	}
	previous = current;
	next = p_value;
	percentage = 0.0f;
	rate = 1.0f / p_seconds;
}

void Sky3D::DeltaFade::set_percentage(float p_percentage) {
	if (!(p_percentage >= 0.0f && p_percentage <= 1.0f)) {
		return;
	}
	if (p_percentage == 1.0f) {
		previous = current = next;
		percentage = rate = 0.0f;
		return;
	}
	percentage = p_percentage;
	pending = 0.0f;
}

void Sky3D::DeltaFade::tick(float p_delta) {
	if (rate == 0.0f) {
		return;
	}

	pending += p_delta;
	if (pending > 0.1f) {
		percentage += rate * pending;
		pending = 0.0f;
	}

	if (percentage > 1.0f) {
		previous = current = next;
		percentage = rate = 0.0f;
		return;
	}

	current = previous + (next - previous) * percentage;
}

// --- CSkyMng -----------------------------------------------------------------------------------

void Sky3D::_reset_state() {
	// CSky::Release.
	sky_color = ColorFade();
	sky_color.change(0xff081021);
	fog_color = ColorFade();
	fog_color.change(0xff102942);

	for (int i = 0; i < LIGHT_COUNT; i++) {
		light_diffuse[i] = ColorFade();
		light_ambient[i] = ColorFade();
	}

	// CSun::Init.
	static const float sun_sizes[SUN_PART_COUNT] = { 0.1f, 0.25f, 0.13f };
	for (int i = 0; i < SUN_PART_COUNT; i++) {
		sun_color[i] = ColorFade();
		sun_size[i] = DeltaFade();
		sun_size[i].start(sun_sizes[i]);
	}

	// CStar::Release.
	star_current = 0;
	star_next = 0;
	star_add_time = 0.0f;
	star_increase = false;
	star_pending = 0.0f;

	// CCloud::Release. The texture coordinates and vertex colours are those of CCloud::Init.
	for (int i = 0; i < CLOUD_LAYER_COUNT; i++) {
		cloud_color[i] = ColorFade();
		cloud_vertex_color[i] = 0xffffffff;
		for (int j = 0; j < 4; j++) {
			cloud_uv[i][j] = 0.0f;
		}
	}
	cloud_alpha = ColorFade();
	cloud_type[0] = 0; // CLOUD_WISPS
	cloud_type[1] = 1; // CLOUD_PUFFS
	cloud_type[2] = CLOUD_NONE;
	cloud_texture_time = 0.0f;
	cloud_backup = CLOUD_NONE;
	cloud_backup_time = 0.0f;
	cloud_scroll_pending = 0.0f;

	change_position = 0;
}

void Sky3D::_rebuild_changes() {
	changes.clear();
	const int count = MIN(MIN(change_types.size(), change_times.size()), MIN(MIN(change_params1.size(), change_params2.size()), change_durations.size()));
	for (int i = 0; i < count; i++) {
		DayChange change;
		change.type = change_types[i];
		change.when = uint32_t(change_times[i]);
		change.param1 = uint32_t(change_params1[i]);
		change.param2 = uint32_t(change_params2[i]);
		change.how_long = change_durations[i];

		// CSkyMng::Load sorts by time. Entries that share a time keep the order they are given in.
		uint32_t at = changes.size();
		changes.push_back(change);
		while (at > 0 && changes[at - 1].when > change.when) {
			changes[at] = changes[at - 1];
			at--;
		}
		changes[at] = change;
	}
}

void Sky3D::_update_state_if_dirty() {
	if (!state_dirty) {
		return;
	}
	state_dirty = false;

	// CSkyMng::Load, followed by the time being set.
	_rebuild_changes();
	_reset_state();
	_set_check_game_time(fixed_sun_hour > 0 ? uint32_t(fixed_sun_hour * 3600) : uint32_t(game_time));
}

// CSkyMng::GetLatestChange: the last change of a type before a position in the list, going
// round to the end of the list (the evening before) when there is none.
int Sky3D::_get_latest_change(int p_type, int p_position) const {
	const int count = changes.size();
	int find = p_position - 1;
	if (find < 0 || find >= count) {
		find = count - 1;
	}

	while (find >= 0) {
		if (changes[find].type == p_type) {
			break;
		}
		find--;
	}

	if (find < 0) {
		find = count - 1;
		while (find >= 0) {
			if (changes[find].type == p_type) {
				break;
			}
			if (p_position > find) {
				find = -1;
				break;
			}
			find--;
		}
	}
	return find;
}

// CSkyMng::ApplyDayChange. p_take_time is what is left of the change's duration.
void Sky3D::_apply_change(const DayChange &p_change, float p_take_time) {
	// With no duration the client divides by zero; the fade is then already instant.
	const float percentage = p_change.how_long > 0.0f ? 1.0f - p_take_time / p_change.how_long : 1.0f;

	switch (p_change.type) {
		case CHANGE_SKY_COLOR: {
			sky_color.change(p_change.param1, p_change.how_long);
			sky_color.set_percentage(percentage);
		} break;
		case CHANGE_FOG_COLOR: {
			fog_color.change(p_change.param1, p_change.how_long);
			fog_color.set_percentage(percentage);
		} break;
		case CHANGE_STAR_COUNT: {
			_set_star(int(p_change.param1), p_take_time);
		} break;
		case CHANGE_MOON_PHASE: {
			moon_phase = (moon_phase + 1) % 24;
		} break;
		case CHANGE_SUN_COLOR:
		case CHANGE_GLOW_COLOR:
		case CHANGE_FLARE_COLOR: {
			const int part = p_change.type - CHANGE_SUN_COLOR;
			sun_color[part].change(p_change.param1, p_change.how_long);
			sun_color[part].set_percentage(percentage);
			// 1000 fills the viewport's width.
			sun_size[part].start(p_change.param2 / 1000.0f, p_change.how_long);
			sun_size[part].set_percentage(percentage);
		} break;
		case CHANGE_CLOUD1_COLOR:
		case CHANGE_CLOUD2_COLOR: {
			const int layer = p_change.type - CHANGE_CLOUD1_COLOR;
			cloud_color[layer].change(p_change.param1, p_change.how_long);
			cloud_color[layer].set_percentage(percentage);
		} break;
		case CHANGE_CLOUD_TEXTURE: {
			_set_cloud(int(p_change.param1), int(p_change.param2), p_take_time);
		} break;
		case CHANGE_LIGHT0_COLOR:
		case CHANGE_LIGHT1_COLOR:
		case CHANGE_LIGHT2_COLOR: {
			const int light = p_change.type - CHANGE_LIGHT0_COLOR;
			light_diffuse[light].change(p_change.param1, p_change.how_long);
			light_diffuse[light].set_percentage(percentage);
			light_ambient[light].change(p_change.param2, p_change.how_long);
			light_ambient[light].set_percentage(percentage);
		} break;
		default: {
		} break;
	}
}

// CSkyMng::SetCheckGameTime: puts every element in the state it has at a time of day, with
// the fades that are under way at that time part done.
void Sky3D::_set_check_game_time(uint32_t p_game_time) {
	uint32_t check_time = p_game_time % SECONDS_PER_DAY;
	game_time = check_time;

	float angle_time = float(check_time);
	if (fixed_sun_hour > 0) {
		check_time = fixed_sun_hour * 3600;
		angle_time = fixed_sun_hour * 3600.0f;
	}
	angle_time = angle_time / 86400.0f * 360.0f;
	sun_radian = Math::deg_to_rad(angle_time + 270.0f);
	moon_radian = Math::deg_to_rad(angle_time + 90.0f);

	const int count = changes.size();
	if (count == 0) {
		return;
	}

	change_position = 0;
	while (change_position < count && changes[change_position].when < check_time) {
		change_position++;
	}
	if (change_position >= count) {
		change_position = count - 1;
	}

	for (int type = 0; type < CHANGE_MAX; type++) {
		if (type == CHANGE_MOON_PHASE) {
			continue;
		}

		const int latest = _get_latest_change(type, change_position);
		if (latest < 0) {
			continue;
		}

		const DayChange &change = changes[latest];
		uint32_t end = change.when + uint32_t(CLIENT_TIME_SCALE * change.how_long);
		if (end > SECONDS_PER_DAY) {
			end -= SECONDS_PER_DAY;
		}

		if (end < check_time) {
			_apply_change(change, 0.0f);
		} else {
			const int before = _get_latest_change(type, latest);
			if (before >= 0) {
				_apply_change(changes[before], 0.0f);
			}

			// Unsigned as in the client: a change that belongs to the evening before gives a huge
			// difference and is applied in full.
			const uint32_t elapsed = check_time - change.when;
			float take_time = change.how_long - elapsed / CLIENT_TIME_SCALE;
			if (take_time < 0.0f) {
				take_time = 0.0f;
			}
			_apply_change(change, take_time);
		}
	}
}

// CStar::SetStar.
void Sky3D::_set_star(int p_count, float p_seconds) {
	if (p_count > STAR_COUNT) {
		p_count = STAR_COUNT;
	} else if (p_count < 0) {
		p_count = 0;
	}
	star_next = p_count;
	if (p_seconds <= 0.0f) {
		star_current = p_count;
		star_add_time = 0.0f;
		return;
	}

	const int difference = star_next - star_current;
	if (difference == 0) {
		return;
	}
	star_increase = difference > 0;
	star_add_time = p_seconds / Math::abs(difference);
}

// CCloud::SetCloud: which two of the six cloud textures are shown, with a cross-fade.
void Sky3D::_set_cloud(int p_cloud1, int p_cloud2, float p_seconds) {
	if (p_cloud1 < 0 || p_cloud1 >= CLOUD_TEXTURE_COUNT || p_cloud2 < 0 || p_cloud2 >= CLOUD_TEXTURE_COUNT) {
		return;
	}

	cloud_alpha.change(0xffffffff);
	cloud_texture_time = 0.0f;
	cloud_type[2] = CLOUD_NONE;
	cloud_backup = CLOUD_NONE;
	cloud_backup_time = 0.0f;

	int same_count = 0;
	if (p_cloud1 == cloud_type[0] || p_cloud1 == cloud_type[1]) {
		same_count++;
	}
	if (p_cloud2 == cloud_type[0] || p_cloud2 == cloud_type[1]) {
		same_count++;
	}

	if (same_count == 2) {
		return;
	}

	if (same_count == 1) {
		if (cloud_type[0] == p_cloud1) {
			cloud_type[2] = p_cloud2;
		} else if (cloud_type[0] == p_cloud2) {
			cloud_type[2] = p_cloud1;
		} else if (cloud_type[1] == p_cloud1) {
			cloud_type[1] = cloud_type[0];
			cloud_type[0] = p_cloud1;
			cloud_type[2] = p_cloud2;
		} else {
			cloud_type[1] = cloud_type[0];
			cloud_type[0] = p_cloud2;
			cloud_type[2] = p_cloud1;
		}

		if (p_seconds == 0.0f) {
			cloud_type[1] = cloud_type[2];
			cloud_type[2] = CLOUD_NONE;
		} else {
			cloud_alpha.change(0x00ffffff, p_seconds);
			cloud_texture_time = p_seconds;
		}
		return;
	}

	if (p_seconds == 0.0f) {
		cloud_type[0] = p_cloud1;
		cloud_type[1] = p_cloud2;
		cloud_type[2] = CLOUD_NONE;
		return;
	}
	cloud_type[2] = p_cloud1;
	cloud_backup = p_cloud2;
	cloud_texture_time = p_seconds * 0.5f;
	cloud_backup_time = cloud_texture_time;
	cloud_alpha.change(0x00ffffff, cloud_texture_time);
}

// CCloud::Tick, as its DirectX 11 path leaves the two layers.
void Sky3D::_tick_clouds(float p_frame_delta, float p_fade_delta) {
	// The layers scroll in steps of at least a twentieth of a second.
	cloud_scroll_pending += p_frame_delta;
	if (cloud_scroll_pending >= 0.05f) {
		const float du = cloud_scroll_pending * 0.005f;
		const float dv = cloud_scroll_pending * 0.015f;
		const float dv2 = cloud_scroll_pending * 0.025f;
		for (int i = 0; i < CLOUD_LAYER_COUNT; i++) {
			cloud_uv[i][0] += du;
			cloud_uv[i][1] += dv;
			cloud_uv[i][2] += dv;
			cloud_uv[i][3] += dv2;
			if (cloud_uv[i][0] > 10.0f) {
				cloud_uv[i][0] -= 10.0f;
			}
			if (cloud_uv[i][1] > 10.0f) {
				cloud_uv[i][1] -= 10.0f;
			}
		}
		cloud_scroll_pending = 0.0f;
	}

	cloud_color[0].tick(p_fade_delta);
	cloud_color[1].tick(p_fade_delta);

	if (cloud_type[2] != CLOUD_NONE) {
		cloud_alpha.tick(p_fade_delta);
		cloud_texture_time -= p_fade_delta;
		if (cloud_texture_time < 0.0f) {
			cloud_texture_time = 0.0f;
			cloud_type[1] = cloud_type[2];
			cloud_type[2] = CLOUD_NONE;
			cloud_alpha.change(0xffffffff);

			if (cloud_backup != CLOUD_NONE) {
				SWAP(cloud_type[0], cloud_type[1]);
				cloud_type[2] = cloud_backup;
				cloud_texture_time = cloud_backup_time;
				cloud_backup = CLOUD_NONE;
				cloud_backup_time = 0.0f;
				cloud_alpha.change(0x00ffffff, cloud_texture_time);

				for (int i = 0; i < CLOUD_LAYER_COUNT; i++) {
					SWAP(cloud_uv[i][0], cloud_uv[i][2]);
					SWAP(cloud_uv[i][1], cloud_uv[i][3]);
				}
			}
		}
	}

	// The colour of each layer's inner ring of vertices; the outer ring has the same colour with
	// no alpha.
	const uint32_t color1 = cloud_color[0].current;
	uint32_t color2 = cloud_color[1].current;
	cloud_vertex_color[0] = color1;
	if (cloud_type[2] == CLOUD_NONE) {
		cloud_vertex_color[1] = color2;
	} else {
		// While a texture is being replaced the first layer takes the second colour with the
		// inverse of the cross-fade's alpha, and the second layer keeps what it had.
		const uint32_t alpha = cloud_alpha.current;
		if (alpha < color2) {
			color2 = (alpha & 0xff000000) | (color2 & 0x00ffffff);
		}
		cloud_vertex_color[0] = ((0xff - (color2 >> 24)) << 24) | (color2 & 0x00ffffff);
	}
}

// CSkyMng::Tick.
void Sky3D::_tick(double p_delta) {
	_update_state_if_dirty();

	const bool advancing = clock_running && !Engine::get_singleton()->is_editor_hint();
	// Fades are timed in the client's real seconds, a tenth of a game second.
	const float fade_delta = advancing ? float(p_delta * time_scale / CLIENT_TIME_SCALE) : 0.0f;
	if (advancing) {
		game_time += p_delta * time_scale;
	}

	if (!changes.is_empty()) {
		uint32_t current_time = uint32_t(game_time);
		if (current_time > SECONDS_PER_DAY) {
			change_position = 0;
			game_time -= SECONDS_PER_DAY;
			current_time -= SECONDS_PER_DAY;

			game_day++;
			moon_phase = (game_month * 30 + game_day) % 24;
			if (game_day > 30) {
				game_day = 0;
				game_month++;
				if (game_month > 12) {
					game_month = 0;
					game_year++;
				}
			}
		}

		if (fixed_sun_hour > 0) {
			current_time = fixed_sun_hour * 3600;
		}

		const float angle_time = current_time / 86400.0f * 360.0f;
		sun_radian = Math::deg_to_rad(angle_time + 270.0f);
		moon_radian = Math::deg_to_rad(angle_time + 90.0f);

		const int count = changes.size();
		while (change_position < count && changes[change_position].when < current_time) {
			const DayChange &change = changes[change_position];
			const uint32_t elapsed = current_time - change.when;
			float take_time = change.how_long - elapsed / CLIENT_TIME_SCALE;
			if (take_time < 0.0f) {
				take_time = 0.0f;
			}
			_apply_change(change, take_time);
			change_position++;
		}
		// The client stops at the last entry instead of running off the end, so that entry is
		// applied again every frame once its time has come.
		if (change_position >= count) {
			change_position = count - 1;
		}
	} else if (game_time > SECONDS_PER_DAY) {
		game_time -= SECONDS_PER_DAY;
	}

	sky_color.tick(fade_delta);
	fog_color.tick(fade_delta);

	for (int i = 0; i < SUN_PART_COUNT; i++) {
		sun_color[i].tick(fade_delta);
		sun_size[i].tick(fade_delta);
	}

	_tick_clouds(float(p_delta), fade_delta);

	// CStar::Tick.
	if (star_add_time != 0.0f) {
		star_pending += fade_delta;
		if (star_pending > star_add_time) {
			const int step = int(star_pending / star_add_time);
			star_pending -= star_add_time * step;
			if (star_increase) {
				star_current += step;
				if (star_current >= star_next) {
					star_current = star_next;
					star_add_time = 0.0f;
					star_pending = 0.0f;
				}
			} else {
				star_current -= step;
				if (star_current <= star_next) {
					star_current = star_next;
					star_add_time = 0.0f;
					star_pending = 0.0f;
				}
			}
		}
	}

	for (int i = 0; i < LIGHT_COUNT; i++) {
		light_diffuse[i].tick(fade_delta);
		light_ambient[i].tick(fade_delta);
	}
}

// Hands the state to the sky layers, the environment and the lights.
void Sky3D::_apply() {
	Color fog = color_from_argb(fog_color.current);
	Color sky = color_from_argb(sky_color.current);

	// CSky::Tick.
	layers[LAYER_HORIZON].material->set_color(fog);

	// CStar::Render: the client's DirectX 11 path draws every star as soon as one is due.
	layers[LAYER_STARS].material->set_star_count(star_current > 0 ? STAR_COUNT : 0);

	// CMoon::SetMoonPhase: 24 phases, six to a row.
	layers[LAYER_MOON].material->set_angle(moon_radian);
	layers[LAYER_MOON].material->set_uv_rect(Rect2((moon_phase % 6) / 6.0f, (moon_phase / 6) / 4.0f, 1.0f / 6.0f, 1.0f / 4.0f));

	for (int i = 0; i < SUN_PART_COUNT; i++) {
		layers[LAYER_SUN + i].material->set_angle(sun_radian);
		layers[LAYER_SUN + i].material->set_size(sun_size[i].current);
	}

	bool visibility_changed = false;
	for (int i = 0; i < CLOUD_LAYER_COUNT; i++) {
		Layer &layer = layers[LAYER_CLOUD + i];
		layer.material->set_color(color_from_argb(cloud_vertex_color[i]));
		layer.material->set_uv_offset(Vector2(cloud_uv[i][0], cloud_uv[i][1]));
		const Ref<Texture2D> &texture = cloud_textures[cloud_type[i]];
		if (layer.material->get_texture() != texture) {
			layer.material->set_texture(texture);
		}
		if (layer.drawn != texture.is_valid()) {
			layer.drawn = texture.is_valid();
			visibility_changed = true;
		}
	}
	if (visibility_changed) {
		_update_layer_visibility();
	}

	if (environment.is_valid()) {
		sky.a = 1.0f;
		fog.a = 1.0f;
		if (environment->get_bg_color() != sky) {
			environment->set_bg_color(sky);
		}
		if (environment->get_fog_light_color() != fog) {
			environment->set_fog_light_color(fog);
		}
		// CCamera::Apply: linear fog from a tenth of the far plane to the far plane.
		if (environment->get_fog_depth_end() != view_distance) {
			environment->set_fog_depth_end(view_distance);
		}
		if (environment->get_fog_depth_begin() != view_distance * 0.1f) {
			environment->set_fog_depth_begin(view_distance * 0.1f);
		}
		// Direct3D adds the ambient term of every light that is on; the two directional lights
		// reach everything.
		const Color ambient0 = color_from_argb(light_ambient[0].current);
		const Color ambient1 = color_from_argb(light_ambient[1].current);
		const Color ambient(MIN(ambient0.r + ambient1.r, 1.0f), MIN(ambient0.g + ambient1.g, 1.0f), MIN(ambient0.b + ambient1.b, 1.0f), 1.0f);
		if (environment->get_ambient_light_color() != ambient) {
			environment->set_ambient_light_color(ambient);
		}
	}

	if (!is_inside_tree()) {
		return;
	}

	// CGameEng::Tick: the sunlight travels along (-1, 0, 1) turned about the Z axis by the sun's
	// angle, in the client's left-handed space; Z is mirrored here.
	const Vector3 sun_direction = Vector3(-Math::cos(sun_radian), -Math::sin(sun_radian), -1.0f).normalized();
	const bool night = is_night_time();

	// CGameProcMain::Render: what the two world effects are given each frame.
	{
		EffectWorld world;
		world.active = true;
		world.light_direction = sun_direction;
		const Color diffuse = color_from_argb(light_diffuse[0].current);
		world.light_color = Vector3(diffuse.r, diffuse.g, diffuse.b);
		world.night = night;
		world.fog_color = Vector3(fog.r, fog.g, fog.b);
		world.fog_far = view_distance;
		effect_world_set(this, world);
	}

	if (!sun_light.is_empty()) {
		Light3D *light = Object::cast_to<Light3D>(get_node_or_null(sun_light));
		if (light) {
			Color diffuse = color_from_argb(light_diffuse[0].current);
			diffuse.a = 1.0f;
			if (light->get_color() != diffuse) {
				light->set_color(diffuse);
			}
			// CShadow::BuildLightMatrices: the shadow light looks from shadow_distance back along
			// the sun's direction (rounded to hundredths, turned around at night) and 100 higher.
			Vector3 direction;
			direction.x = Math::floor(double((night ? -sun_direction.x : sun_direction.x) * 100.0f) + 0.5) * 0.01 * shadow_distance;
			direction.y = Math::floor(double((night ? -sun_direction.y : sun_direction.y) * 100.0f) + 0.5) * 0.01 * shadow_distance - 100.0;
			direction.z = Math::floor(double((night ? -sun_direction.z : sun_direction.z) * 100.0f) + 0.5) * 0.01 * shadow_distance;
			direction.normalize();
			const Basis basis = Basis::looking_at(direction, Math::abs(direction.y) > 0.9999f ? Vector3(1, 0, 0) : Vector3(0, 1, 0));
			if (light->is_inside_tree() && !light->get_global_basis().is_equal_approx(basis)) {
				light->set_global_basis(basis);
			}
		}
	}

	if (!support_light.is_empty()) {
		Light3D *light = Object::cast_to<Light3D>(get_node_or_null(support_light));
		if (light) {
			Color diffuse = color_from_argb(light_diffuse[1].current);
			diffuse.a = 1.0f;
			if (light->get_color() != diffuse) {
				light->set_color(diffuse);
			}
			const Basis basis = Basis::looking_at(Vector3(2.0f, -3.0f, -2.0f).normalized());
			if (light->is_inside_tree() && !light->get_global_basis().is_equal_approx(basis)) {
				light->set_global_basis(basis);
			}
		}
	}

	if (!player_light.is_empty()) {
		Light3D *light = Object::cast_to<Light3D>(get_node_or_null(player_light));
		if (light) {
			Color diffuse = color_from_argb(light_diffuse[2].current);
			diffuse.a = 1.0f;
			if (light->get_color() != diffuse) {
				light->set_color(diffuse);
			}
		}
	}
}

// --- drawing ---------------------------------------------------------------------------------------

// The C runtime's rand() the client fills its star field with.
static int msvc_rand(uint32_t &r_state) {
	r_state = r_state * 214013u + 2531011u;
	return int((r_state >> 16) & 0x7fff);
}

void Sky3D::_create_layers() {
	// CSky::Init: the bottom quad (all fog) and above it the quad that fades out. X is to the
	// right of the camera's heading, Z the distance in front of it.
	{
		const float width = 3.5f;
		const float top_y = 0.5f;
		const float bottom_y = 0.1f;
		const float distance = 1.5f;
		const float bottom_offset = -5.0f;

		PackedVector3Array vertices;
		PackedVector2Array uvs;
		// Bottom quad.
		vertices.push_back(Vector3(width, bottom_y, distance));
		vertices.push_back(Vector3(width, bottom_offset, distance));
		vertices.push_back(Vector3(-width, bottom_offset, distance));
		vertices.push_back(Vector3(-width, bottom_y, distance));
		for (int i = 0; i < 4; i++) {
			uvs.push_back(Vector2(1.0f, 0.0f));
		}
		// Front quad: no alpha along its top edge.
		vertices.push_back(Vector3(width, top_y, distance));
		vertices.push_back(Vector3(width, bottom_y, distance));
		vertices.push_back(Vector3(-width, bottom_y, distance));
		vertices.push_back(Vector3(-width, top_y, distance));
		uvs.push_back(Vector2(0.0f, 0.0f));
		uvs.push_back(Vector2(1.0f, 0.0f));
		uvs.push_back(Vector2(1.0f, 0.0f));
		uvs.push_back(Vector2(0.0f, 0.0f));

		PackedInt32Array indices;
		for (int quad = 0; quad < 2; quad++) {
			static const int fan[6] = { 0, 1, 2, 0, 2, 3 };
			for (int i = 0; i < 6; i++) {
				indices.push_back(quad * 4 + fan[i]);
			}
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		arrays[Mesh::ARRAY_INDEX] = indices;
		horizon_mesh.instantiate();
		horizon_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	}

	// CStar::Init: 200 points in a box around the camera, none nearer than 2, the alpha falling
	// from full to half over the list. The client places them with rand(); its seed at that
	// moment is not known, so the sequence from the runtime's initial seed is used.
	{
		PackedVector3Array vertices;
		PackedVector2Array uvs;
		uint32_t seed = 1;
		const float alpha_step = float(0xff - 0x80) / STAR_COUNT;
		for (int i = 0; i < STAR_COUNT; i++) {
			float x, y, z;
			do {
				x = float(msvc_rand(seed) % 10000) / 1000.0f - 5.0f;
				y = float(msvc_rand(seed) % 10000) / 1000.0f - 2.0f;
				z = float(msvc_rand(seed) % 10000) / 1000.0f - 5.0f;
			} while (x * x + y * y + z * z < 2.0f * 2.0f);
			const int alpha = 0xff - int(alpha_step * i);
			vertices.push_back(Vector3(x, y, -z));
			uvs.push_back(Vector2(alpha / 255.0f, float(i)));
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		star_mesh.instantiate();
		star_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_POINTS, arrays);
	}

	// The square of the sun's sprites and of the moon; the material places and sizes it.
	{
		PackedVector3Array vertices;
		PackedVector2Array uvs;
		vertices.push_back(Vector3(-1.0f, 1.0f, 0.0f));
		vertices.push_back(Vector3(1.0f, 1.0f, 0.0f));
		vertices.push_back(Vector3(1.0f, -1.0f, 0.0f));
		vertices.push_back(Vector3(-1.0f, -1.0f, 0.0f));
		uvs.push_back(Vector2(0.0f, 0.0f));
		uvs.push_back(Vector2(1.0f, 0.0f));
		uvs.push_back(Vector2(1.0f, 1.0f));
		uvs.push_back(Vector2(0.0f, 1.0f));

		PackedInt32Array indices;
		static const int fan[6] = { 0, 1, 2, 0, 2, 3 };
		for (int i = 0; i < 6; i++) {
			indices.push_back(fan[i]);
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		arrays[Mesh::ARRAY_INDEX] = indices;
		disc_mesh.instantiate();
		disc_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	}

	// CCloud::Init: a square frustum, wide and transparent at the horizon, small and opaque
	// overhead, with the texture repeated eight times across the wide end. CCloud::Render
	// turns it about the Y axis by a fixed angle.
	{
		const float big_length = 72.0f;
		const float small_length = 8.0f;
		const float big_height = -2.0f;
		const float small_height = 9.61f;
		const float uv_max = 8.0f;
		const float uv_inset = (1.0f - small_length / big_length) * uv_max / 2.0f;
		const float turn = 4.084070f;
		const float turn_cos = Math::cos(turn);
		const float turn_sin = Math::sin(turn);

		static const float corners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
		PackedVector3Array vertices;
		PackedVector2Array uvs;
		PackedVector2Array uv2s;
		for (int ring = 0; ring < 2; ring++) {
			const float length = ring == 0 ? big_length : small_length;
			const float height = ring == 0 ? big_height : small_height;
			const float inset = ring == 0 ? 0.0f : uv_inset;
			for (int i = 0; i < 4; i++) {
				const float x = corners[i][0] * length;
				const float z = corners[i][1] * length;
				// D3DXMatrixRotationY on a row vector, then Z mirrored.
				vertices.push_back(Vector3(x * turn_cos + z * turn_sin, height, -(-x * turn_sin + z * turn_cos)));
				uvs.push_back(Vector2(corners[i][0] < 0.0f ? inset : uv_max - inset, corners[i][1] < 0.0f ? inset : uv_max - inset));
				uv2s.push_back(Vector2(float(ring), 0.0f));
			}
		}

		// v2610 DAT_010E21AC.
		static const int cloud_indices[30] = { 0, 1, 4, 1, 2, 5, 2, 3, 6, 3, 0, 7, 5, 4, 1, 6, 5, 2, 7, 6, 3, 4, 7, 0, 4, 5, 7, 5, 6, 7 };
		PackedInt32Array indices;
		for (int i = 0; i < 30; i++) {
			indices.push_back(cloud_indices[i]);
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		arrays[Mesh::ARRAY_TEX_UV2] = uv2s;
		arrays[Mesh::ARRAY_INDEX] = indices;
		cloud_mesh.instantiate();
		cloud_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	}

	// CSkyMng::Render draws sky, stars, moon, sun, clouds, in that order.
	for (int i = 0; i < LAYER_MAX; i++) {
		Layer &layer = layers[i];
		Ref<ArrayMesh> mesh;
		SkyMaterial::Part part;
		if (i == LAYER_HORIZON) {
			mesh = horizon_mesh;
			part = SkyMaterial::PART_HORIZON;
		} else if (i == LAYER_STARS) {
			mesh = star_mesh;
			part = SkyMaterial::PART_STARS;
		} else if (i == LAYER_MOON) {
			mesh = disc_mesh;
			part = SkyMaterial::PART_MOON;
		} else if (i < LAYER_CLOUD) {
			mesh = disc_mesh;
			part = SkyMaterial::PART_SUN;
		} else {
			mesh = cloud_mesh;
			part = SkyMaterial::PART_CLOUD;
		}

		layer.material.instantiate();
		layer.material->set_part(part);
		layer.material->set_render_priority(Material::RENDER_PRIORITY_MIN + i);
		if (part == SkyMaterial::PART_MOON) {
			layer.material->set_size(MOON_RATIO);
		}
		// The textured layers appear when they are given a texture.
		layer.drawn = part == SkyMaterial::PART_HORIZON || part == SkyMaterial::PART_STARS;

		layer.instance = RS::get_singleton()->instance_create();
		RS::get_singleton()->instance_set_base(layer.instance, mesh->get_rid());
		RS::get_singleton()->instance_geometry_set_material_override(layer.instance, layer.material->get_rid());
		RS::get_singleton()->instance_geometry_set_cast_shadows_setting(layer.instance, RSE::SHADOW_CASTING_SETTING_OFF);
		// The material puts the layer around the camera; where the mesh itself is does not matter.
		RS::get_singleton()->instance_set_ignore_culling(layer.instance, true);
		RS::get_singleton()->instance_set_visible(layer.instance, false);
	}
}

void Sky3D::_update_layer_visibility() {
	const bool visible = is_inside_tree() && is_visible_in_tree();
	for (int i = 0; i < LAYER_MAX; i++) {
		RS::get_singleton()->instance_set_visible(layers[i].instance, visible && layers[i].drawn);
	}
}

void Sky3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			const RID scenario = get_world_3d()->get_scenario();
			for (int i = 0; i < LAYER_MAX; i++) {
				RS::get_singleton()->instance_set_scenario(layers[i].instance, scenario);
			}
		} break;

		case NOTIFICATION_EXIT_WORLD: {
			for (int i = 0; i < LAYER_MAX; i++) {
				RS::get_singleton()->instance_set_scenario(layers[i].instance, RID());
			}
		} break;

		case NOTIFICATION_ENTER_TREE: {
			set_process_internal(true);
			_update_layer_visibility();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			effect_world_clear(this);
		} break;

		case NOTIFICATION_READY: {
			// The lights and the environment have their colours before the first frame is drawn.
			_tick(0.0);
			_apply();
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			_update_layer_visibility();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			_tick(get_process_delta_time());
			_apply();
		} break;
	}
}

// --- properties --------------------------------------------------------------------------------------

void Sky3D::set_sun_texture(int p_part, const Ref<Texture2D> &p_texture) {
	ERR_FAIL_INDEX(p_part, SUN_PART_COUNT);
	sun_textures[p_part] = p_texture;
	Layer &layer = layers[LAYER_SUN + p_part];
	layer.material->set_texture(p_texture);
	// The client skips a sprite that has no texture.
	layer.drawn = p_texture.is_valid();
	_update_layer_visibility();
}

Ref<Texture2D> Sky3D::get_sun_texture(int p_part) const {
	ERR_FAIL_INDEX_V(p_part, SUN_PART_COUNT, Ref<Texture2D>());
	return sun_textures[p_part];
}

void Sky3D::set_cloud_texture(int p_cloud, const Ref<Texture2D> &p_texture) {
	ERR_FAIL_INDEX(p_cloud, CLOUD_TEXTURE_COUNT);
	cloud_textures[p_cloud] = p_texture;
}

Ref<Texture2D> Sky3D::get_cloud_texture(int p_cloud) const {
	ERR_FAIL_INDEX_V(p_cloud, CLOUD_TEXTURE_COUNT, Ref<Texture2D>());
	return cloud_textures[p_cloud];
}

void Sky3D::set_moon_texture(const Ref<Texture2D> &p_texture) {
	moon_texture = p_texture;
	Layer &layer = layers[LAYER_MOON];
	layer.material->set_texture(p_texture);
	layer.drawn = p_texture.is_valid();
	_update_layer_visibility();
}

Ref<Texture2D> Sky3D::get_moon_texture() const {
	return moon_texture;
}

void Sky3D::set_change_names(const PackedStringArray &p_names) {
	change_names = p_names;
}

PackedStringArray Sky3D::get_change_names() const {
	return change_names;
}

void Sky3D::set_change_types(const PackedInt32Array &p_types) {
	change_types = p_types;
	state_dirty = true;
}

PackedInt32Array Sky3D::get_change_types() const {
	return change_types;
}

void Sky3D::set_change_times(const PackedInt32Array &p_times) {
	change_times = p_times;
	state_dirty = true;
}

PackedInt32Array Sky3D::get_change_times() const {
	return change_times;
}

void Sky3D::set_change_params1(const PackedInt32Array &p_params) {
	change_params1 = p_params;
	state_dirty = true;
}

PackedInt32Array Sky3D::get_change_params1() const {
	return change_params1;
}

void Sky3D::set_change_params2(const PackedInt32Array &p_params) {
	change_params2 = p_params;
	state_dirty = true;
}

PackedInt32Array Sky3D::get_change_params2() const {
	return change_params2;
}

void Sky3D::set_change_durations(const PackedFloat32Array &p_durations) {
	change_durations = p_durations;
	state_dirty = true;
}

PackedFloat32Array Sky3D::get_change_durations() const {
	return change_durations;
}

void Sky3D::set_time_of_day(float p_hours) {
	const uint32_t seconds = uint32_t(CLAMP(double(p_hours) * 3600.0, 0.0, 86400.0 * 1000.0));
	if (state_dirty) {
		// The schedule is not in place yet (the scene is still loading).
		game_time = seconds % SECONDS_PER_DAY;
		return;
	}
	_set_check_game_time(seconds);
}

float Sky3D::get_time_of_day() const {
	return float(game_time / 3600.0);
}

void Sky3D::set_clock_running(bool p_running) {
	clock_running = p_running;
}

bool Sky3D::is_clock_running() const {
	return clock_running;
}

void Sky3D::set_time_scale(float p_scale) {
	time_scale = MAX(p_scale, 0.0f);
}

float Sky3D::get_time_scale() const {
	return time_scale;
}

// CSkyMng::SunAndMoonDirectionFixByHour.
void Sky3D::set_fixed_sun_hour(int p_hour) {
	fixed_sun_hour = p_hour;
	if (!state_dirty) {
		_set_check_game_time(fixed_sun_hour > 0 ? uint32_t(fixed_sun_hour * 3600) : uint32_t(game_time));
	}
}

int Sky3D::get_fixed_sun_hour() const {
	return fixed_sun_hour;
}

void Sky3D::set_moon_phase(int p_phase) {
	moon_phase = ((p_phase % 24) + 24) % 24;
}

int Sky3D::get_moon_phase() const {
	return moon_phase;
}

void Sky3D::set_game_date(int p_year, int p_month, int p_day) {
	game_year = p_year;
	game_month = p_month;
	game_day = p_day;
	set_moon_phase(p_month * 30 + p_day);
}

void Sky3D::set_view_distance(float p_distance) {
	view_distance = MAX(p_distance, 1.0f);
}

float Sky3D::get_view_distance() const {
	return view_distance;
}

void Sky3D::set_shadow_distance(float p_distance) {
	shadow_distance = MAX(p_distance, 1.0f);
}

float Sky3D::get_shadow_distance() const {
	return shadow_distance;
}

void Sky3D::set_environment(const Ref<Environment> &p_environment) {
	environment = p_environment;
}

Ref<Environment> Sky3D::get_environment() const {
	return environment;
}

void Sky3D::set_sun_light(const NodePath &p_path) {
	sun_light = p_path;
}

NodePath Sky3D::get_sun_light() const {
	return sun_light;
}

void Sky3D::set_support_light(const NodePath &p_path) {
	support_light = p_path;
}

NodePath Sky3D::get_support_light() const {
	return support_light;
}

void Sky3D::set_player_light(const NodePath &p_path) {
	player_light = p_path;
}

NodePath Sky3D::get_player_light() const {
	return player_light;
}

Color Sky3D::get_sky_color() const {
	return color_from_argb(sky_color.current);
}

Color Sky3D::get_fog_color() const {
	return color_from_argb(fog_color.current);
}

Color Sky3D::get_light_diffuse_color(int p_light) const {
	ERR_FAIL_INDEX_V(p_light, LIGHT_COUNT, Color());
	return color_from_argb(light_diffuse[p_light].current);
}

Color Sky3D::get_light_ambient_color(int p_light) const {
	ERR_FAIL_INDEX_V(p_light, LIGHT_COUNT, Color());
	return color_from_argb(light_ambient[p_light].current);
}

float Sky3D::get_sun_angle() const {
	return sun_radian;
}

// CSkyMng::IsNightTime: before 06:00 or after 19:00.
bool Sky3D::is_night_time() const {
	uint32_t current_time = uint32_t(game_time);
	if (fixed_sun_hour > 0) {
		current_time = fixed_sun_hour * 3600;
	}
	return (current_time - 6 * 3600) > uint32_t(13 * 3600);
}

void Sky3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sun_texture", "part", "texture"), &Sky3D::set_sun_texture);
	ClassDB::bind_method(D_METHOD("get_sun_texture", "part"), &Sky3D::get_sun_texture);
	ClassDB::bind_method(D_METHOD("set_cloud_texture", "cloud", "texture"), &Sky3D::set_cloud_texture);
	ClassDB::bind_method(D_METHOD("get_cloud_texture", "cloud"), &Sky3D::get_cloud_texture);
	ClassDB::bind_method(D_METHOD("set_moon_texture", "texture"), &Sky3D::set_moon_texture);
	ClassDB::bind_method(D_METHOD("get_moon_texture"), &Sky3D::get_moon_texture);
	ClassDB::bind_method(D_METHOD("set_change_names", "names"), &Sky3D::set_change_names);
	ClassDB::bind_method(D_METHOD("get_change_names"), &Sky3D::get_change_names);
	ClassDB::bind_method(D_METHOD("set_change_types", "types"), &Sky3D::set_change_types);
	ClassDB::bind_method(D_METHOD("get_change_types"), &Sky3D::get_change_types);
	ClassDB::bind_method(D_METHOD("set_change_times", "times"), &Sky3D::set_change_times);
	ClassDB::bind_method(D_METHOD("get_change_times"), &Sky3D::get_change_times);
	ClassDB::bind_method(D_METHOD("set_change_params1", "params"), &Sky3D::set_change_params1);
	ClassDB::bind_method(D_METHOD("get_change_params1"), &Sky3D::get_change_params1);
	ClassDB::bind_method(D_METHOD("set_change_params2", "params"), &Sky3D::set_change_params2);
	ClassDB::bind_method(D_METHOD("get_change_params2"), &Sky3D::get_change_params2);
	ClassDB::bind_method(D_METHOD("set_change_durations", "durations"), &Sky3D::set_change_durations);
	ClassDB::bind_method(D_METHOD("get_change_durations"), &Sky3D::get_change_durations);
	ClassDB::bind_method(D_METHOD("set_time_of_day", "hours"), &Sky3D::set_time_of_day);
	ClassDB::bind_method(D_METHOD("get_time_of_day"), &Sky3D::get_time_of_day);
	ClassDB::bind_method(D_METHOD("set_clock_running", "running"), &Sky3D::set_clock_running);
	ClassDB::bind_method(D_METHOD("is_clock_running"), &Sky3D::is_clock_running);
	ClassDB::bind_method(D_METHOD("set_time_scale", "scale"), &Sky3D::set_time_scale);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &Sky3D::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_fixed_sun_hour", "hour"), &Sky3D::set_fixed_sun_hour);
	ClassDB::bind_method(D_METHOD("get_fixed_sun_hour"), &Sky3D::get_fixed_sun_hour);
	ClassDB::bind_method(D_METHOD("set_moon_phase", "phase"), &Sky3D::set_moon_phase);
	ClassDB::bind_method(D_METHOD("get_moon_phase"), &Sky3D::get_moon_phase);
	ClassDB::bind_method(D_METHOD("set_game_date", "year", "month", "day"), &Sky3D::set_game_date);
	ClassDB::bind_method(D_METHOD("set_view_distance", "distance"), &Sky3D::set_view_distance);
	ClassDB::bind_method(D_METHOD("get_view_distance"), &Sky3D::get_view_distance);
	ClassDB::bind_method(D_METHOD("set_shadow_distance", "distance"), &Sky3D::set_shadow_distance);
	ClassDB::bind_method(D_METHOD("get_shadow_distance"), &Sky3D::get_shadow_distance);
	ClassDB::bind_method(D_METHOD("set_environment", "environment"), &Sky3D::set_environment);
	ClassDB::bind_method(D_METHOD("get_environment"), &Sky3D::get_environment);
	ClassDB::bind_method(D_METHOD("set_sun_light", "path"), &Sky3D::set_sun_light);
	ClassDB::bind_method(D_METHOD("get_sun_light"), &Sky3D::get_sun_light);
	ClassDB::bind_method(D_METHOD("set_support_light", "path"), &Sky3D::set_support_light);
	ClassDB::bind_method(D_METHOD("get_support_light"), &Sky3D::get_support_light);
	ClassDB::bind_method(D_METHOD("set_player_light", "path"), &Sky3D::set_player_light);
	ClassDB::bind_method(D_METHOD("get_player_light"), &Sky3D::get_player_light);
	ClassDB::bind_method(D_METHOD("get_sky_color"), &Sky3D::get_sky_color);
	ClassDB::bind_method(D_METHOD("get_fog_color"), &Sky3D::get_fog_color);
	ClassDB::bind_method(D_METHOD("get_light_diffuse_color", "light"), &Sky3D::get_light_diffuse_color);
	ClassDB::bind_method(D_METHOD("get_light_ambient_color", "light"), &Sky3D::get_light_ambient_color);
	ClassDB::bind_method(D_METHOD("get_sun_angle"), &Sky3D::get_sun_angle);
	ClassDB::bind_method(D_METHOD("is_night_time"), &Sky3D::is_night_time);

	ADD_GROUP("Textures", "");
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "sun_texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_sun_texture", "get_sun_texture", 0);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "sun_glow_texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_sun_texture", "get_sun_texture", 1);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "sun_flare_texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_sun_texture", "get_sun_texture", 2);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "moon_texture", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_moon_texture", "get_moon_texture");
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "cloud_texture_0", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_cloud_texture", "get_cloud_texture", 0);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "cloud_texture_1", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_cloud_texture", "get_cloud_texture", 1);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "cloud_texture_2", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_cloud_texture", "get_cloud_texture", 2);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "cloud_texture_3", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_cloud_texture", "get_cloud_texture", 3);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "cloud_texture_4", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_cloud_texture", "get_cloud_texture", 4);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "cloud_texture_5", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_cloud_texture", "get_cloud_texture", 5);

	ADD_GROUP("Day Changes", "change_");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "change_names"), "set_change_names", "get_change_names");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "change_types"), "set_change_types", "get_change_types");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "change_times"), "set_change_times", "get_change_times");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "change_params1"), "set_change_params1", "get_change_params1");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "change_params2"), "set_change_params2", "get_change_params2");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "change_durations"), "set_change_durations", "get_change_durations");

	ADD_GROUP("Clock", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_of_day", PROPERTY_HINT_RANGE, "0,24,0.01,suffix:h"), "set_time_of_day", "get_time_of_day");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clock_running"), "set_clock_running", "is_clock_running");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_scale", PROPERTY_HINT_RANGE, "0,3600,0.1,or_greater"), "set_time_scale", "get_time_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "fixed_sun_hour", PROPERTY_HINT_RANGE, "0,23,1"), "set_fixed_sun_hour", "get_fixed_sun_hour");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "moon_phase", PROPERTY_HINT_RANGE, "0,23,1"), "set_moon_phase", "get_moon_phase");

	ADD_GROUP("World", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_distance", PROPERTY_HINT_RANGE, "1,4096,0.1,or_greater,suffix:m"), "set_view_distance", "get_view_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shadow_distance", PROPERTY_HINT_RANGE, "1,1024,1,or_greater,suffix:m"), "set_shadow_distance", "get_shadow_distance");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "environment", PROPERTY_HINT_RESOURCE_TYPE, Environment::get_class_static()), "set_environment", "get_environment");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun_light", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Light3D"), "set_sun_light", "get_sun_light");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "support_light", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Light3D"), "set_support_light", "get_support_light");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player_light", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Light3D"), "set_player_light", "get_player_light");

	BIND_ENUM_CONSTANT(CHANGE_SKY_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_FOG_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_STAR_COUNT);
	BIND_ENUM_CONSTANT(CHANGE_MOON_PHASE);
	BIND_ENUM_CONSTANT(CHANGE_SUN_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_GLOW_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_FLARE_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_CLOUD1_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_CLOUD2_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_CLOUD_TEXTURE);
	BIND_ENUM_CONSTANT(CHANGE_LIGHT0_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_LIGHT1_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_LIGHT2_COLOR);
	BIND_ENUM_CONSTANT(CHANGE_MAX);
}

Sky3D::Sky3D() {
	_create_layers();
}

Sky3D::~Sky3D() {
	effect_world_clear(this);
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	for (int i = 0; i < LAYER_MAX; i++) {
		if (layers[i].instance.is_valid()) {
			RS::get_singleton()->free_rid(layers[i].instance);
		}
	}
}
