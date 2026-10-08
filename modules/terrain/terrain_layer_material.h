#pragma once

#include "terrain_data.h"

#include "core/os/mutex.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Terrain drawn as a blend of up to eight tiling materials, the weights painted per height
// sample (TerrainData's weights). Each layer has a colour texture, an optional normal map, a
// tint, a roughness and the size in metres one repeat of its textures covers. The result is lit
// by the scene's lights like any other surface. A layer without a colour texture is drawn as a
// grey checkerboard with one square per cell, which is what a new terrain looks like.
//
// Terrain3D feeds the two weight textures and the terrain's dimensions; the mesh only needs
// positions and normals.
class TerrainLayerMaterial : public Material {
	GDCLASS(TerrainLayerMaterial, Material);

	struct Layer {
		Ref<Texture2D> albedo;
		Ref<Texture2D> normal;
		Color color = Color(1, 1, 1, 1);
		float tile_size = 4.0;
		float roughness = 1.0;
	};
	Layer layers[TerrainData::LAYER_COUNT];
	float normal_strength = 1.0;

	static Mutex shader_mutex;
	static RID shader;

	void _update_shader();

protected:
	static void _bind_methods();

public:
	void set_layer_albedo(int p_layer, const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_layer_albedo(int p_layer) const;

	void set_layer_normal(int p_layer, const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_layer_normal(int p_layer) const;

	void set_layer_color(int p_layer, const Color &p_color);
	Color get_layer_color(int p_layer) const;

	void set_layer_tile_size(int p_layer, float p_size);
	float get_layer_tile_size(int p_layer) const;

	void set_layer_roughness(int p_layer, float p_roughness);
	float get_layer_roughness(int p_layer) const;

	void set_normal_strength(float p_strength);
	float get_normal_strength() const;

	// Set by Terrain3D.
	void set_weight_texture(int p_index, RID p_texture);
	void set_terrain_dimensions(int p_size, float p_cell_size);

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;

	static void cleanup_shader();

	TerrainLayerMaterial();
	virtual ~TerrainLayerMaterial();
};
