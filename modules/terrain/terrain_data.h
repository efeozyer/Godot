#pragma once

#include "core/io/image.h"
#include "core/io/resource.h"
#include "core/math/rect2i.h"

// Everything a terrain is made of, for both ways of texturing it.
//
// The terrain is a square grid of `size` x `size` height samples, `cell_size` apart, so it has
// (size - 1) x (size - 1) cells. Sample (x, z) sits at the local position
// (x * cell_size, height, -z * cell_size): the grid's z axis runs along -Z.
//
//   heights   one float per sample, row by row (index = z * size + x)
//   tiles     four bytes per cell (index = (z * (size - 1) + x) * 4), the texel TerrainMaterial
//             reads from its cell map: two tile textures per cell, each in one of 8 orientations
//               byte 0  first tile index, low 8 bits     byte 1  second tile index, low 8 bits
//               byte 2  first direction | second direction << 3
//               byte 3  first index high bits | second index high bits << 2
//                       | 16 if the first tile is used | 32 if the second tile is used
//   weights   eight bytes per sample (index = (z * size + x) * 8): how much of each of the
//             eight material layers TerrainLayerMaterial shows there. They add up to 255.
class TerrainData : public Resource {
	GDCLASS(TerrainData, Resource);

public:
	enum {
		LAYER_COUNT = 8,
		TILE_SLOTS = 2,
		TILE_DIRECTIONS = 8,
		MAX_TILE_INDEX = 1023,
	};

	enum Channel {
		CHANNEL_HEIGHT = 1,
		CHANNEL_TILES = 2,
		CHANNEL_WEIGHTS = 4,
	};

private:
	int size = 0;
	float cell_size = 1.0;
	Vector<float> heights;
	Vector<uint8_t> tiles;
	Vector<uint8_t> weights;

	void _allocate(int p_size);

protected:
	static void _bind_methods();

public:
	// Resizing keeps what the old and the new grid have in common.
	void set_size(int p_size);
	int get_size() const { return size; }
	int get_cells() const { return MAX(size - 1, 0); }

	void set_cell_size(float p_size);
	float get_cell_size() const { return cell_size; }

	void set_heights(const Vector<float> &p_heights);
	Vector<float> get_heights() const { return heights; }

	void set_tiles(const Vector<uint8_t> &p_tiles);
	Vector<uint8_t> get_tiles() const { return tiles; }

	void set_weights(const Vector<uint8_t> &p_weights);
	Vector<uint8_t> get_weights() const { return weights; }

	// Direct access for brushes; call notify_changed() afterwards.
	float *heights_ptrw() { return heights.ptrw(); }
	uint8_t *tiles_ptrw() { return tiles.ptrw(); }
	uint8_t *weights_ptrw() { return weights.ptrw(); }
	const float *heights_ptr() const { return heights.ptr(); }
	const uint8_t *tiles_ptr() const { return tiles.ptr(); }
	const uint8_t *weights_ptr() const { return weights.ptr(); }

	// p_rect is in samples for heights and weights and in cells for tiles.
	void notify_changed(int p_channels, const Rect2i &p_rect);

	float get_height(int p_x, int p_z) const;
	void set_height(int p_x, int p_z, float p_height);
	// Height at a position given in grid units (sample x, sample z), interpolated the way the
	// two triangles of the cell are drawn.
	float sample_height(const Vector2 &p_grid_position) const;
	Vector3 get_normal(int p_x, int p_z) const;

	void set_tile(int p_x, int p_z, int p_slot, int p_index, int p_direction);
	void clear_tile(int p_x, int p_z, int p_slot);
	int get_tile_index(int p_x, int p_z, int p_slot) const;
	int get_tile_direction(int p_x, int p_z, int p_slot) const;
	bool is_tile_used(int p_x, int p_z, int p_slot) const;

	float get_layer_weight(int p_x, int p_z, int p_layer) const;

	// Rectangles of each channel, for undo and for copying.
	Vector<float> get_heights_region(const Rect2i &p_rect) const;
	void set_heights_region(const Rect2i &p_rect, const Vector<float> &p_heights);
	Vector<uint8_t> get_tiles_region(const Rect2i &p_rect) const;
	void set_tiles_region(const Rect2i &p_rect, const Vector<uint8_t> &p_tiles);
	Vector<uint8_t> get_weights_region(const Rect2i &p_rect) const;
	void set_weights_region(const Rect2i &p_rect, const Vector<uint8_t> &p_weights);

	// Sets every height from an image stretched over the terrain: black is p_min_height and
	// white is p_max_height (the red channel is used).
	void import_heightmap(const Ref<Image> &p_image, float p_min_height, float p_max_height);
	Ref<Image> export_heightmap(float p_min_height, float p_max_height) const;

	// The textures the two materials sample.
	Ref<Image> create_tile_image() const;
	Ref<Image> create_weight_image(int p_index) const;

	void get_height_range(float &r_min, float &r_max, const Rect2i &p_rect) const;

	TerrainData();
};

VARIANT_ENUM_CAST(TerrainData::Channel)
