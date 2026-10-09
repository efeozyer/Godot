#pragma once

#include "terrain_data.h"

#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

// Terrain drawn as a blend of tiling materials, the weights painted per height sample
// (TerrainData's weights). There can be up to TerrainData::MAX_LAYERS layers, of which a sample
// blends four. Each layer has a colour texture, an optional normal map, a tint, a roughness and
// the size in metres one repeat of its textures covers. The result is lit by the scene's lights
// like any other surface. A layer without a colour texture is drawn as a grey checkerboard with
// one square per cell, which is what a new terrain looks like.
//
// The layers' textures are drawn out of two texture arrays, which the material makes from them:
// every texture is brought to `texture_size` and, where the build can, compressed. A texture of
// any size can be given to a layer; `texture_size` is the detail that is kept of it.
//
// Terrain3D feeds the two weight textures and the terrain's dimensions; the mesh only needs
// positions and normals.
class TerrainLayerMaterial : public Material {
	GDCLASS(TerrainLayerMaterial, Material);

	struct Layer {
		Ref<BaseMaterial3D> material;
		Ref<Texture2D> albedo;
		Ref<Texture2D> normal;
		Color color = Color(1, 1, 1, 1);
		float tile_size = 4.0;
		float roughness = 1.0;
		// The textures as they go into the arrays, kept until the texture or the size changes.
		Ref<Image> albedo_image;
		Ref<Image> normal_image;
	};
	LocalVector<Layer> layers;
	int texture_size = 1024;
	float normal_strength = 1.0;

	Ref<Texture2DArray> albedo_array;
	Ref<Texture2DArray> normal_array;
	bool arrays_dirty = true;
	bool update_queued = false;

	static Mutex shader_mutex;
	static RID shader;

	void _update_shader();
	void _queue_update(bool p_arrays);
	void _update();
	Ref<Image> _prepare_image(const Ref<Texture2D> &p_texture, int p_size, bool p_normal, bool p_compress) const;

protected:
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;
	static void _bind_methods();

public:
	// New layers start as untextured ones; layers that are removed lose their settings.
	void set_layer_count(int p_count);
	int get_layer_count() const;

	// The side, in pixels, of the layers' textures as they are drawn.
	void set_texture_size(int p_size);
	int get_texture_size() const;

	// A ready-made surface material to take the layer from: its colour texture, normal map, tint
	// and roughness are copied into the layer, which can still be adjusted afterwards.
	void set_layer_material(int p_layer, const Ref<BaseMaterial3D> &p_material);
	Ref<BaseMaterial3D> get_layer_material(int p_layer) const;

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

	// Set by Terrain3D: the layer numbers (0) and the weights (1) of every sample.
	void set_weight_texture(int p_index, RID p_texture);
	void set_terrain_dimensions(int p_size, float p_cell_size);

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;

	static void cleanup_shader();

	TerrainLayerMaterial();
	virtual ~TerrainLayerMaterial();
};
