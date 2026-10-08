#pragma once

#include "core/templates/hash_map.h"
#include "effect_common.h"

#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// The client's character and object effect, SHC/NormalMap_shader.fx (technique "NormalMap",
// one pass), as CChr::Render and CCPart::Render drive it through CShader.
//
// Despite its name the effect takes no normal map. It has one texture, the part's own, and derives
// a normal from it with a Sobel filter one texel wide. The texture is lit by that normal plus the
// vertex normal, both against the sun, and the result is held between one and three times the
// material's ambient share of the texture (twice at night), which is what keeps the unlit side of
// a character from going black. The material's emissive colour only enters when emissive_zero is
// off. The result is darkened where the shadow map says so and fades into the fog colour.
//
// The client draws every character part and plug through it, and placed objects (CShape) too,
// with character switched off and fixed colours. The render states the client sets around the
// draw are properties here as well: blending and its factors, culling, and the alpha test.
//
// Skinning is left to the engine, which has deformed the mesh before the shader runs.
// Directions are given in Godot's space (the client's with Z mirrored).
class NormalMapMaterial : public Material, public EffectWorldListener {
	GDCLASS(NormalMapMaterial, Material);

public:
	enum BlendFactor {
		BLEND_FACTOR_ZERO,
		BLEND_FACTOR_ONE,
		BLEND_FACTOR_SRC_COLOR,
		BLEND_FACTOR_INV_SRC_COLOR,
		BLEND_FACTOR_SRC_ALPHA,
		BLEND_FACTOR_INV_SRC_ALPHA,
		BLEND_FACTOR_MAX
	};

private:
	Ref<Texture2D> texture;
	Color ambient = Color(1, 1, 1, 1);
	Color diffuse = Color(1, 1, 1, 1);
	Color emissive = Color(0, 0, 0, 0);
	bool emissive_zero = false;
	bool character = true;
	float alpha = 1.0;
	bool alpha_blend = false;
	BlendFactor blend_src_factor = BLEND_FACTOR_SRC_ALPHA;
	BlendFactor blend_dst_factor = BLEND_FACTOR_INV_SRC_ALPHA;
	bool double_sided = false;
	int alpha_ref = 1;
	bool night = false;
	Vector3 light_direction = Vector3(-0.353553, -0.612372, -0.707107);
	Color light_specular = Color(0.5, 0.5, 0.5, 1);
	bool shadow_enabled = true;
	float shadow_distance = 150.0;
	bool fog_enabled = true;
	Color fog_color = Color(0.662745, 0.796078, 0.843137, 1);
	float fog_near = 0.7;
	float fog_far = 512.0;

	static Mutex shader_mutex;
	// One shader per set of render states, keyed by _get_shader_key().
	static HashMap<int, RID> shaders;

	int _get_shader_key() const;
	void _update_shader();
	void _update_light();
	void _apply_world(const EffectWorld &p_world);

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;

	void set_ambient(const Color &p_color);
	Color get_ambient() const;

	void set_diffuse(const Color &p_color);
	Color get_diffuse() const;

	void set_emissive(const Color &p_color);
	Color get_emissive() const;

	void set_emissive_zero(bool p_enable);
	bool is_emissive_zero() const;

	void set_character(bool p_enable);
	bool is_character() const;

	void set_alpha(float p_alpha);
	float get_alpha() const;

	void set_alpha_blend(bool p_enable);
	bool is_alpha_blend() const;

	void set_blend_src_factor(BlendFactor p_factor);
	BlendFactor get_blend_src_factor() const;

	void set_blend_dst_factor(BlendFactor p_factor);
	BlendFactor get_blend_dst_factor() const;

	void set_double_sided(bool p_enable);
	bool is_double_sided() const;

	void set_alpha_ref(int p_ref);
	int get_alpha_ref() const;

	void set_night(bool p_night);
	bool is_night() const;

	void set_light_direction(const Vector3 &p_direction);
	Vector3 get_light_direction() const;

	void set_light_specular(const Color &p_color);
	Color get_light_specular() const;

	void set_shadow_enabled(bool p_enable);
	bool is_shadow_enabled() const;

	void set_shadow_distance(float p_distance);
	float get_shadow_distance() const;

	void set_fog_enabled(bool p_enable);
	bool is_fog_enabled() const;

	void set_fog_color(const Color &p_color);
	Color get_fog_color() const;

	void set_fog_near(float p_near);
	float get_fog_near() const;

	void set_fog_far(float p_far);
	float get_fog_far() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;

	virtual void _effect_world_changed(const EffectWorld &p_world) override;

	static void cleanup_shader();

	NormalMapMaterial();
	virtual ~NormalMapMaterial();
};

VARIANT_ENUM_CAST(NormalMapMaterial::BlendFactor)
