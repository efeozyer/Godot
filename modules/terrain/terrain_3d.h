#pragma once

#include "terrain_data.h"
#include "terrain_layer_material.h"

#include "modules/fx/terrain_material.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/height_map_shape_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

// Draws a TerrainData and gives it a collision surface.
//
// The terrain can be textured in two ways, chosen with `mode`:
//
//   MODE_TILES   every cell shows up to two tile textures out of a texture array, each in one of
//                eight orientations, drawn by `tile_material` (TerrainMaterial). This is how
//                the zones of the original game are built.
//   MODE_LAYERS  tiling materials blended by painted weights, four of them at a point, drawn by
//                `layer_material` (TerrainLayerMaterial).
//
// Both use the same heights, so a terrain can be switched between them at any time.
//
// The ground collides by itself. What stands on it can be baked into the same body: the meshes
// below `collision_bake_source` are turned into `collision_baked_shape` by bake_collision().
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
	float transparency = 0.0;
	bool wireframe = false;
	Ref<StandardMaterial3D> wireframe_material;
	bool collision_enabled = true;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	NodePath collision_bake_source;
	Ref<ConcavePolygonShape3D> collision_baked_shape;

	Vector<Chunk> chunks;
	int chunks_per_side = 0;

	Ref<ImageTexture> tile_texture;
	// The layer numbers and the weights of every sample.
	Ref<ImageTexture> weight_textures[2];

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
	void _collect_bake_faces(Node *p_node, const Transform3D &p_to_local, Vector<Vector3> &r_faces) const;

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

	// How see-through the terrain is drawn, in percent: 0 is solid, 100 is invisible. It does not
	// change what collides.
	void set_transparency(float p_percent);
	float get_transparency() const;

	// Draws the triangles' edges instead of the textured surface.
	void set_wireframe(bool p_enabled);
	bool is_wireframe() const;

	void set_collision_enabled(bool p_enabled);
	bool is_collision_enabled() const;

	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const;

	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;

	// The node whose meshes bake_collision() reads, itself and everything below it.
	void set_collision_bake_source(const NodePath &p_path);
	NodePath get_collision_bake_source() const;

	// The triangles of the baked meshes, in the terrain's local space. They collide together
	// with the ground.
	void set_collision_baked_shape(const Ref<ConcavePolygonShape3D> &p_shape);
	Ref<ConcavePolygonShape3D> get_collision_baked_shape() const;

	// Fills the baked shape with the triangles of every visible MeshInstance3D of the bake
	// source. A node in the group "no_collision" is left out together with what is below it.
	// Returns the number of triangles, or -1 when there is no source to read.
	int bake_collision();

	// Works out the layers that show what the tiles show, without changing the terrain. The
	// tiles are grouped into grounds (a full tile and the edge and corner pieces that fade it
	// out) and every ground becomes a layer with the full tile as its texture. A sample's
	// weights are what the cells around it show at that corner. Returns false when there are no
	// tiles to read.
	bool build_layers_from_tiles(Vector<uint8_t> &r_weights, Ref<TerrainLayerMaterial> &r_material) const;
	// Does the above and switches the terrain to the result: the data's weights, the layer
	// material and the mode. The tiles are kept, so switching the mode back shows them again.
	bool convert_tiles_to_layers();

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
