#include "post_process_effect.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/storage/render_data.h"

namespace {

enum {
	MODE_BRIGHT,
	MODE_BLUR_HORIZONTAL,
	MODE_BLUR_VERTICAL,
	MODE_AVERAGE_FRAME,
	MODE_AVERAGE_REDUCE,
	MODE_COMPOSITE,
};

struct PushConstant {
	int32_t dest_size[2];
	float texel[2];
	int32_t mode;
	float threshold;
	float exposure;
	float after_exposure;
	float saturation;
	float gaussian_scalar;
	float pad[2];
};

const char *shader_code = R"(#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source;
layout(set = 0, binding = 1) uniform sampler2D bloom;
layout(set = 0, binding = 2) uniform sampler2D average;
layout(rgba16f, set = 0, binding = 3) uniform image2D dest;

layout(push_constant, std430) uniform Params {
	ivec2 dest_size;
	vec2 texel;
	int mode;
	float threshold;
	float exposure;
	float after_exposure;
	float saturation;
	float gaussian_scalar;
	vec2 pad;
}
params;

const vec3 LUMINANCE = vec3(0.299, 0.587, 0.114);

// exp(-i * i / 2) for i = 0..4, sigma 1 (DX11_BuildGaussianBlurKernel), and their sum over -4..4.
const float GAUSS[5] = float[](1.0, 0.60653066, 0.13533528, 0.011108997, 0.00033546263);
const float GAUSS_SUM = 2.5066206;

vec3 to_display(vec3 c) {
	c = clamp(c, vec3(0.0), vec3(1.0));
	return mix(pow(c, vec3(1.0 / 2.4)) * 1.055 - 0.055, c * 12.92, lessThan(c, vec3(0.0031308)));
}

vec3 to_linear(vec3 c) {
	return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThan(c, vec3(0.04045)));
}

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, params.dest_size))) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / vec2(params.dest_size);

	if (params.mode == 0) {
		// Bright_ps: four taps one frame pixel apart, summed and scaled by 0.2.
		vec3 sum = vec3(0.0);
		sum += to_display(textureLod(source, uv + params.texel * vec2(-1.0, 1.0), 0.0).rgb);
		sum += to_display(textureLod(source, uv + params.texel * vec2(1.0, 1.0), 0.0).rgb);
		sum += to_display(textureLod(source, uv + params.texel * vec2(-1.0, -1.0), 0.0).rgb);
		sum += to_display(textureLod(source, uv + params.texel * vec2(1.0, -1.0), 0.0).rgb);
		sum *= 0.2;
		vec3 kept = dot(sum, LUMINANCE) < params.threshold ? vec3(0.0) : clamp(sum, vec3(0.0), vec3(1.0));
		imageStore(dest, pixel, vec4(kept, 1.0));
	} else if (params.mode == 1 || params.mode == 2) {
		// GaussianBlur_ps / GaussianBlur_ps_v: nine taps, weights normalised then scaled.
		vec2 step_uv = params.mode == 1 ? vec2(params.texel.x, 0.0) : vec2(0.0, params.texel.y);
		vec3 sum = vec3(0.0);
		for (int i = -4; i <= 4; i++) {
			float weight = GAUSS[abs(i)] / GAUSS_SUM * params.gaussian_scalar;
			sum += textureLod(source, uv + step_uv * float(i), 0.0).rgb * weight;
		}
		imageStore(dest, pixel, vec4(sum, 1.0));
	} else if (params.mode == 3) {
		// First step of the frame average: each output texel averages the block of the frame it covers.
		vec3 sum = vec3(0.0);
		for (int y = 0; y < 8; y++) {
			for (int x = 0; x < 8; x++) {
				vec2 tap = (vec2(pixel) + (vec2(x, y) + 0.5) / 8.0) / vec2(params.dest_size);
				sum += to_display(textureLod(source, tap, 0.0).rgb);
			}
		}
		imageStore(dest, pixel, vec4(sum / 64.0, 1.0));
	} else if (params.mode == 4) {
		// Exact 8 x 8 reduction of the previous average.
		vec3 sum = vec3(0.0);
		for (int y = 0; y < 8; y++) {
			for (int x = 0; x < 8; x++) {
				sum += texelFetch(source, pixel * 8 + ivec2(x, y), 0).rgb;
			}
		}
		imageStore(dest, pixel, vec4(sum / 64.0, 1.0));
	} else {
		// PostProcess_ps.
		vec4 frame = imageLoad(dest, pixel);
		vec3 scene = to_display(frame.rgb);
		vec3 glow = textureLod(bloom, uv, 0.0).rgb;
		float frame_luminance = pow(dot(texelFetch(average, ivec2(0), 0).rgb, LUMINANCE) + 0.00001, 0.25);
		float scaled = dot(scene, LUMINANCE) / frame_luminance * params.exposure;
		float white = (params.gaussian_scalar + 1.0) * (params.gaussian_scalar + 1.0);
		float tone = scaled * (scaled / white + 1.0) / (scaled + 1.0) + params.after_exposure * 1.2;
		vec3 result = scene + params.saturation * ((glow * 0.6 + scene) * tone - scene);
		imageStore(dest, pixel, vec4(to_linear(clamp(result, vec3(0.0), vec3(1.0))), frame.a));
	}
}
)";

} // namespace

bool PostProcessEffect::_ensure_shader() {
	if (pipeline.is_valid()) {
		return true;
	}
	if (shader_failed) {
		return false;
	}

	RD *rd = RD::get_singleton();
	String error;
	RD::ShaderStageSPIRVData stage;
	stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
	stage.spirv = rd->shader_compile_spirv_from_source(RD::SHADER_STAGE_COMPUTE, shader_code, RD::SHADER_LANGUAGE_GLSL, &error);
	if (stage.spirv.is_empty()) {
		shader_failed = true;
		ERR_FAIL_V_MSG(false, "PostProcessEffect: shader did not compile: " + error);
	}
	Vector<RD::ShaderStageSPIRVData> stages;
	stages.push_back(stage);
	shader = rd->shader_create_from_spirv(stages, "PostProcessEffect");
	if (shader.is_null()) {
		shader_failed = true;
		ERR_FAIL_V_MSG(false, "PostProcessEffect: shader could not be created.");
	}
	pipeline = rd->compute_pipeline_create(shader);

	RD::SamplerState state;
	state.mag_filter = RD::SAMPLER_FILTER_LINEAR;
	state.min_filter = RD::SAMPLER_FILTER_LINEAR;
	sampler = rd->sampler_create(state);
	return pipeline.is_valid();
}

void PostProcessEffect::_run_pass(int p_mode, RID p_source, RID p_bloom, RID p_average, RID p_dest, const Size2i &p_dest_size, const Vector2 &p_texel) {
	RD *rd = RD::get_singleton();

	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_source }));
	RD::Uniform u_bloom(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, p_bloom }));
	RD::Uniform u_average(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ sampler, p_average }));
	RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 3, p_dest);
	RID uniform_set = UniformSetCacheRD::get_singleton()->get_cache(shader, 0, u_source, u_bloom, u_average, u_dest);

	PushConstant push_constant;
	push_constant.dest_size[0] = p_dest_size.x;
	push_constant.dest_size[1] = p_dest_size.y;
	push_constant.texel[0] = p_texel.x;
	push_constant.texel[1] = p_texel.y;
	push_constant.mode = p_mode;
	push_constant.threshold = bright_pass_threshold;
	push_constant.exposure = exposure;
	push_constant.after_exposure = after_exposure;
	push_constant.saturation = saturation;
	push_constant.gaussian_scalar = gaussian_scalar;
	push_constant.pad[0] = 0.0;
	push_constant.pad[1] = 0.0;

	RD::ComputeListID compute_list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute_list, pipeline);
	rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
	rd->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
	rd->compute_list_dispatch_threads(compute_list, p_dest_size.x, p_dest_size.y, 1);
	rd->compute_list_end();
}

void PostProcessEffect::_render(int p_effect_callback_type, const RenderData *p_render_data) {
	if (p_render_data == nullptr || RD::get_singleton() == nullptr) {
		return;
	}
	Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	if (buffers.is_null() || !buffers->has_internal_texture()) {
		return;
	}
	const Size2i size = buffers->get_internal_size();
	if (size.x < 16 || size.y < 16 || !_ensure_shader()) {
		return;
	}

	const StringName context = SNAME("post_process_chain");
	const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
	struct Target {
		StringName name;
		Size2i size;
		RID texture;
	};
	Target targets[6] = {
		{ SNAME("half"), Size2i(MAX(size.x / 2, 1), MAX(size.y / 2, 1)), RID() },
		{ SNAME("quarter"), Size2i(MAX(size.x / 4, 1), MAX(size.y / 4, 1)), RID() },
		{ SNAME("eighth"), Size2i(MAX(size.x / 8, 1), MAX(size.y / 8, 1)), RID() },
		{ SNAME("average_64"), Size2i(64, 64), RID() },
		{ SNAME("average_8"), Size2i(8, 8), RID() },
		{ SNAME("average_1"), Size2i(1, 1), RID() },
	};
	for (Target &target : targets) {
		if (buffers->has_texture(context, target.name)) {
			target.texture = buffers->get_texture(context, target.name);
		} else {
			target.texture = buffers->create_texture(context, target.name, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, target.size, 1, 1);
		}
		if (target.texture.is_null()) {
			return;
		}
	}
	const RID color = buffers->get_internal_texture();
	const RID half = targets[0].texture;
	const RID quarter = targets[1].texture;
	const RID eighth = targets[2].texture;

	// The blur offsets are whole texels of the target being drawn (the client builds the horizontal
	// kernel from the quarter size and the vertical one from the eighth size).
	_run_pass(MODE_BRIGHT, color, color, color, half, targets[0].size, Vector2(1.0 / size.x, 1.0 / size.y));
	_run_pass(MODE_BLUR_HORIZONTAL, half, half, half, quarter, targets[1].size, Vector2(1.0 / targets[1].size.x, 1.0 / targets[1].size.y));
	_run_pass(MODE_BLUR_VERTICAL, quarter, quarter, quarter, eighth, targets[2].size, Vector2(1.0 / targets[2].size.x, 1.0 / targets[2].size.y));
	_run_pass(MODE_AVERAGE_FRAME, color, color, color, targets[3].texture, targets[3].size, Vector2());
	_run_pass(MODE_AVERAGE_REDUCE, targets[3].texture, targets[3].texture, targets[3].texture, targets[4].texture, targets[4].size, Vector2());
	_run_pass(MODE_AVERAGE_REDUCE, targets[4].texture, targets[4].texture, targets[4].texture, targets[5].texture, targets[5].size, Vector2());
	_run_pass(MODE_COMPOSITE, eighth, eighth, targets[5].texture, color, size, Vector2());
}

void PostProcessEffect::set_bright_pass_threshold(float p_threshold) {
	bright_pass_threshold = p_threshold;
}

float PostProcessEffect::get_bright_pass_threshold() const {
	return bright_pass_threshold;
}

void PostProcessEffect::set_gaussian_scalar(float p_scalar) {
	gaussian_scalar = p_scalar;
}

float PostProcessEffect::get_gaussian_scalar() const {
	return gaussian_scalar;
}

void PostProcessEffect::set_exposure(float p_exposure) {
	exposure = p_exposure;
}

float PostProcessEffect::get_exposure() const {
	return exposure;
}

void PostProcessEffect::set_after_exposure(float p_exposure) {
	after_exposure = p_exposure;
}

float PostProcessEffect::get_after_exposure() const {
	return after_exposure;
}

void PostProcessEffect::set_saturation(float p_saturation) {
	saturation = p_saturation;
}

float PostProcessEffect::get_saturation() const {
	return saturation;
}

void PostProcessEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bright_pass_threshold", "threshold"), &PostProcessEffect::set_bright_pass_threshold);
	ClassDB::bind_method(D_METHOD("get_bright_pass_threshold"), &PostProcessEffect::get_bright_pass_threshold);
	ClassDB::bind_method(D_METHOD("set_gaussian_scalar", "scalar"), &PostProcessEffect::set_gaussian_scalar);
	ClassDB::bind_method(D_METHOD("get_gaussian_scalar"), &PostProcessEffect::get_gaussian_scalar);
	ClassDB::bind_method(D_METHOD("set_exposure", "exposure"), &PostProcessEffect::set_exposure);
	ClassDB::bind_method(D_METHOD("get_exposure"), &PostProcessEffect::get_exposure);
	ClassDB::bind_method(D_METHOD("set_after_exposure", "exposure"), &PostProcessEffect::set_after_exposure);
	ClassDB::bind_method(D_METHOD("get_after_exposure"), &PostProcessEffect::get_after_exposure);
	ClassDB::bind_method(D_METHOD("set_saturation", "saturation"), &PostProcessEffect::set_saturation);
	ClassDB::bind_method(D_METHOD("get_saturation"), &PostProcessEffect::get_saturation);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bright_pass_threshold", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_bright_pass_threshold", "get_bright_pass_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gaussian_scalar", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_gaussian_scalar", "get_gaussian_scalar");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_exposure", "get_exposure");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "after_exposure", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_after_exposure", "get_after_exposure");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "saturation", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_saturation", "get_saturation");
}

PostProcessEffect::PostProcessEffect() {
	// After the transparent pass, so that effects and water are in the frame as they are in the
	// client's scene target; 2D is drawn later, like the client's UI.
	set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr && get_rid().is_valid()) {
		rs->compositor_effect_set_callback(get_rid(), RSE::CompositorEffectCallbackType(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT), callable_mp(this, &PostProcessEffect::_render));
	}
}

PostProcessEffect::~PostProcessEffect() {
	RD *rd = RD::get_singleton();
	if (rd == nullptr) {
		return;
	}
	if (pipeline.is_valid()) {
		rd->free_rid(pipeline);
	}
	if (shader.is_valid()) {
		rd->free_rid(shader);
	}
	if (sampler.is_valid()) {
		rd->free_rid(sampler);
	}
}
