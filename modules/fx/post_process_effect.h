#pragma once

#include "scene/resources/compositor.h"
#include "servers/rendering/rendering_device.h"

class RenderData;

// The client's full-frame bloom and exposure chain (DX11_PostProcess_RenderBloomChain and the
// Bright / GaussianBlur / PostProcess pixel shaders), run on the finished 3D frame before any 2D.
//
// 1. Bright pass into a half-size target: four neighbouring pixels summed and scaled by 0.2; kept
//    only where their luminance reaches the threshold.
// 2. Nine-tap horizontal blur into a quarter-size target, then nine-tap vertical blur into an
//    eighth-size target, each scaled by the gaussian scalar.
// 3. The frame's average colour, reduced to one pixel.
// 4. Composite: (frame + bloom * 0.6) scaled by the exposure curve, blended over the frame by the
//    saturation value.
//
// Nothing is masked: a pixel blooms only if it is bright enough. The client works on the colours
// it sends to the display, so the frame is converted to display space for the maths and back.
// The five parameters are the per-zone values of the zones' post-process table.
class PostProcessEffect : public CompositorEffect {
	GDCLASS(PostProcessEffect, CompositorEffect);

	// CPostProcess_SelectByZone_dx11: what a zone without an entry runs with.
	float bright_pass_threshold = 0.0;
	float gaussian_scalar = 1.5;
	float exposure = 1.2;
	float after_exposure = 0.0;
	float saturation = 1.2;

	RID shader;
	RID pipeline;
	RID sampler;
	bool shader_failed = false;

	bool _ensure_shader();
	void _run_pass(int p_mode, RID p_source, RID p_bloom, RID p_average, RID p_dest, const Size2i &p_dest_size, const Vector2 &p_texel);
	void _render(int p_effect_callback_type, const RenderData *p_render_data);

protected:
	static void _bind_methods();

public:
	void set_bright_pass_threshold(float p_threshold);
	float get_bright_pass_threshold() const;

	void set_gaussian_scalar(float p_scalar);
	float get_gaussian_scalar() const;

	void set_exposure(float p_exposure);
	float get_exposure() const;

	void set_after_exposure(float p_exposure);
	float get_after_exposure() const;

	void set_saturation(float p_saturation);
	float get_saturation() const;

	PostProcessEffect();
	~PostProcessEffect();
};
