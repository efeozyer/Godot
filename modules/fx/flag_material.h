#pragma once

#include "core/os/mutex.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Draws a banner (CFlag) and gives it its wave.
//
// The client rebuilds the banner's vertices every frame (CFlag::BuildVertices): column i of
// the grid is pushed out of the banner's plane by
//     sin(i - phase * 6) * i * amplitude / columns
// so the wave grows from the pole (column 0, which never moves) to the free edge. The same
// displacement is applied here in the vertex shader; the mesh carries the column index in UV2.x.
// Flag3D advances the phase.
//
// The banner shows its texture and nothing else (CFlagMgr::Render selects the texture as the
// colour and alpha, so the lighting it switches on has no effect): unlit, no fog, both sides,
// alpha blended while still writing depth.
class FlagMaterial : public Material {
	GDCLASS(FlagMaterial, Material);

public:
	enum Transparency {
		// For a texture without alpha: the same pixels as blending, drawn with the opaque geometry.
		TRANSPARENCY_DISABLED,
		// D3DBLEND_SRCALPHA / D3DBLEND_INVSRCALPHA with depth writing left on, as the client draws it.
		TRANSPARENCY_ALPHA,
		TRANSPARENCY_MAX
	};

private:
	Transparency transparency = TRANSPARENCY_DISABLED;
	Ref<Texture2D> texture;
	float amplitude = 0.5;
	int columns = 7;
	float wave_phase = 0.0;

	static Mutex shader_mutex;
	static RID shaders[TRANSPARENCY_MAX];

	void _update_shader();

protected:
	static void _bind_methods();
	virtual bool _can_use_render_priority() const override { return true; }

public:
	void set_transparency(Transparency p_transparency);
	Transparency get_transparency() const;

	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;

	void set_amplitude(float p_amplitude);
	float get_amplitude() const;

	// Vertices along one side of the grid (segments + 1).
	void set_columns(int p_columns);
	int get_columns() const;

	void set_wave_phase(float p_phase);
	float get_wave_phase() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;

	static void cleanup_shader();

	FlagMaterial();
	virtual ~FlagMaterial();
};

VARIANT_ENUM_CAST(FlagMaterial::Transparency)
