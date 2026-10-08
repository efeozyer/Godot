#pragma once

#include "core/os/mutex.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Draws one layer of the client engine's sky (CSkyMng::Render and the classes it calls). Sky3D
// owns one of these per layer and feeds it every frame.
//
// The client's sky is geometry a few units around the camera, drawn first with the depth buffer off and
// with the translation removed from the view matrix. Here every layer is drawn in the transparent
// pass at the far plane instead, with the same view rotation, so the scene covers it; the layers
// keep the client's order through their render priority.
//
//  PART_HORIZON  CSky: two quads that always face the camera's heading, filled with the fog
//                colour: opaque below the horizon and fading out above it. The colour behind them
//                is the clear colour, which the client sets to the sky colour.
//  PART_STARS    CStar: white points with a per-star alpha, in view-rotated sky space.
//  PART_MOON     CMoon: a screen-aligned square, a fixed fraction of the viewport width, centred
//                on the projection of a point that turns around the Z axis. Alpha blended, one
//                cell of the phase atlas.
//  PART_SUN      CSun: the same square, added to what is behind it.
//  PART_CLOUD    CCloud: a frustum-shaped dome, the texture multiplied by the layer colour and
//                fading out towards the horizon, scrolling.
class SkyMaterial : public Material {
	GDCLASS(SkyMaterial, Material);

public:
	enum Part {
		PART_HORIZON,
		PART_STARS,
		PART_MOON,
		PART_SUN,
		PART_CLOUD,
		PART_MAX
	};

private:
	Part part = PART_HORIZON;
	Color color = Color(1, 1, 1, 1);
	Ref<Texture2D> texture;
	float angle = 0.0;
	float size = 0.1;
	Rect2 uv_rect = Rect2(0, 0, 1, 1);
	Vector2 uv_offset;
	int star_count = 0;

	static Mutex shader_mutex;
	static RID shaders[PART_MAX];

	void _update_shader();

protected:
	static void _bind_methods();
	virtual bool _can_use_render_priority() const override { return true; }

public:
	void set_part(Part p_part);
	Part get_part() const;

	// The fog colour (horizon) or the layer colour including its alpha (cloud).
	void set_color(const Color &p_color);
	Color get_color() const;

	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;

	// Sun and moon: where on their circle around the Z axis they are; 0 is +X, a quarter turn is straight up.
	void set_angle(float p_angle);
	float get_angle() const;

	// Sun and moon: side of the square as a fraction of the viewport width.
	void set_size(float p_size);
	float get_size() const;

	// Sun and moon: the part of the texture shown.
	void set_uv_rect(const Rect2 &p_rect);
	Rect2 get_uv_rect() const;

	// Cloud: how far the texture has scrolled.
	void set_uv_offset(const Vector2 &p_offset);
	Vector2 get_uv_offset() const;

	// Stars: how many of the mesh's points are drawn.
	void set_star_count(int p_count);
	int get_star_count() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;

	static void cleanup_shader();

	SkyMaterial();
	virtual ~SkyMaterial();
};

VARIANT_ENUM_CAST(SkyMaterial::Part)
