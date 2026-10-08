#pragma once

#include "effect_common.h"

#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// The client's terrain effect, SHC/TerranBlend_Shader.fx (technique "NormalMap", one pass), as
// CTerrain::Render and CTerrainPatch::Render drive it through CTerrainBlendShader.
//
// The effect draws two texture layers and adds them. Each layer is lit by a normal it derives from
// the layer's own texture with a Sobel filter (the "bump"), plus half the vertex-normal term; there
// is no ambient term and no vertex colour. The sum is darkened where the shadow map says so and
// fades into the fog colour with depth.
//
// The client feeds the two layers per terrain cell: on the patches around the camera, the cell's
// two tile textures in their stored orientations; on the patches further out, the zone's colour map
// alone. Here the mesh carries positions and normals only and the cell's tiles are looked up per
// pixel in cell_map, so any mesh that follows the height field works.
//
//   tile_textures  one layer per tile texture of the zone
//   cell_map       one texel per cell (x = cell x, y = cell z):
//                    r = first tile index (low 8 bits)    g = second tile index (low 8 bits)
//                    b = first direction | second direction << 3
//                    a = first index high bits | second index high bits << 2
//                        | 16 if the first tile is used | 32 if the second tile is used
//   color_map      the colour map textures side by side; each keeps its one-texel border
//   light_map      the light map textures side by side, rgb = shadow colour, a = coverage
//
// The light map is not part of the effect: the client draws it over the terrain afterwards, and
// only at the shadow setting that has no shadow map, so it and shadow_enabled exclude each other.
//
// The terrain lies in the client's left-handed space with Z mirrored: client (x, z) = (VERTEX.x, -VERTEX.z).
// Directions are given in Godot's space.
class TerrainMaterial : public Material, public EffectWorldListener {
	GDCLASS(TerrainMaterial, Material);

	Ref<TextureLayered> tile_textures;
	Ref<Texture2D> cell_map;
	Ref<Texture2D> color_map;
	Ref<Texture2D> light_map;
	bool light_map_enabled = false;
	float cell_size = 4.0;
	int map_cells = 256;
	int color_map_cells = 32;
	int color_map_texture_size = 128;
	int light_map_cells = 4;
	int light_map_texture_size = 32;
	int tile_patches = 8;
	bool tile_alpha_test = true;
	bool bump_enabled = true;
	bool night = false;
	Vector3 light_direction = Vector3(-0.353553, -0.612372, -0.707107);
	Color light_diffuse = Color(1, 1, 1, 1);
	Color light_specular = Color(1, 1, 1, 1);
	bool shadow_enabled = true;
	float shadow_distance = 150.0;
	bool fog_enabled = true;
	Color fog_color = Color(0.662745, 0.796078, 0.843137, 1);
	float fog_near = 0.7;
	float fog_far = 512.0;

	static Mutex shader_mutex;
	// Index 1 holds the variant with the alpha test.
	static RID shaders[2];

	void _update_shader();
	void _update_light();
	void _apply_world(const EffectWorld &p_world);

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_tile_textures(const Ref<TextureLayered> &p_texture);
	Ref<TextureLayered> get_tile_textures() const;

	void set_cell_map(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_cell_map() const;

	void set_color_map(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_color_map() const;

	void set_light_map(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_light_map() const;

	void set_light_map_enabled(bool p_enable);
	bool is_light_map_enabled() const;

	void set_cell_size(float p_size);
	float get_cell_size() const;

	void set_map_cells(int p_cells);
	int get_map_cells() const;

	void set_color_map_cells(int p_cells);
	int get_color_map_cells() const;

	void set_color_map_texture_size(int p_size);
	int get_color_map_texture_size() const;

	void set_light_map_cells(int p_cells);
	int get_light_map_cells() const;

	void set_light_map_texture_size(int p_size);
	int get_light_map_texture_size() const;

	void set_tile_patches(int p_patches);
	int get_tile_patches() const;

	void set_tile_alpha_test(bool p_enable);
	bool is_tile_alpha_test() const;

	void set_bump_enabled(bool p_enable);
	bool is_bump_enabled() const;

	void set_night(bool p_night);
	bool is_night() const;

	void set_light_direction(const Vector3 &p_direction);
	Vector3 get_light_direction() const;

	void set_light_diffuse(const Color &p_color);
	Color get_light_diffuse() const;

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

	TerrainMaterial();
	virtual ~TerrainMaterial();
};
