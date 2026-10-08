#pragma once

#include "terrain_data.h"
#include "terrain_layer_material.h"

#include "modules/fx/terrain_material.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/3d/height_map_shape_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/mesh.h"

// Draws a TerrainData and gives it a collision surface.
//
// The terrain can be textured in two ways, chosen with `mode`:
//
//   MODE_TILES   every cell shows up to two tile textures out of a texture array, each in one of
//                eight orientations, drawn by `tile_material` (TerrainMaterial). This is how
//                the zones of the original game are built.
//   MODE_LAYERS  up to eight tiling materials blended by painted weights, drawn by
//                `layer_material` (TerrainLayerMaterial).
//
// Both use the same heights, so a terrain can be switched between them at any time.
//
// The surface is split into chunks of chunk_cells x chunk_cells cells so that only what is in
// view is drawn and only what a brush touches is rebuilt. Sample (x, z) of the data lies at the
// local position (x * cell_size, height, -z * cell_size).
class Terrain3D : public Node3D {
	GDCLASS(Terrain3D, Node3D);

public:
	enum Mode {
		MODE_TILES,
		MODE_LAYERS,
	};

private:
	struct Chunk {
		Ref<ArrayMesh> mesh;
		RID instance;
	};

	Ref<TerrainData> data;
	Mode mode = MODE_LAYERS;
	Ref<TerrainMaterial> tile_material;
	Ref<TerrainLayerMaterial> layer_material;
	// The checkerboard a terrain shows while the chosen mode has nothing to draw it with.
	Ref<TerrainLayerMaterial> default_material;
	int chunk_cells = 32;
	bool collision_enabled = true;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;

	Vector<Chunk> chunks;
	int chunks_per_side = 0;

	Ref<ImageTexture> tile_texture;
	Ref<ImageTexture> weight_textures[TerrainData::LAYER_COUNT / 4];

	RID body;
	Ref<HeightMapShape3D> shape;

	void _data_resized();
	void _data_region_changed(int p_channels, const Rect2i &p_rect);

	void _clear_chunks();
	void _create_chunks();
	void _build_chunk(int p_chunk_x, int p_chunk_z);
	void _update_instances();
	RID _get_material_rid() const;
	void _apply_material();
	void _update_tile_texture();
	void _update_weight_textures();

	void _clear_collision();
	void _create_collision();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_data(const Ref<TerrainData> &p_data);
	Ref<TerrainData> get_data() const;

	void set_mode(Mode p_mode);
	Mode get_mode() const;

	void set_tile_material(const Ref<TerrainMaterial> &p_material);
	Ref<TerrainMaterial> get_tile_material() const;

	void set_layer_material(const Ref<TerrainLayerMaterial> &p_material);
	Ref<TerrainLayerMaterial> get_layer_material() const;

	void set_chunk_cells(int p_cells);
	int get_chunk_cells() const;

	void set_collision_enabled(bool p_enabled);
	bool is_collision_enabled() const;

	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const;

	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;

	// Rebuilds the collision surface from the current heights. Brushes call this once a stroke
	// is finished rather than on every change.
	void update_collision();

	// Height of the ground under (or above) a global position.
	float get_height_at(const Vector3 &p_global_position) const;

	// Where a ray given in global space first meets the ground.
	bool intersect_ray(const Vector3 &p_from, const Vector3 &p_direction, Vector3 &r_position) const;
	Variant intersect_ray_bind(const Vector3 &p_from, const Vector3 &p_direction) const;

	// A local position in grid units (sample x, sample z).
	Vector2 to_grid(const Vector3 &p_local_position) const;

	Terrain3D();
	~Terrain3D();
};

VARIANT_ENUM_CAST(Terrain3D::Mode)
