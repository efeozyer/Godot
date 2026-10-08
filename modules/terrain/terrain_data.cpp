#include "terrain_data.h"

#include "core/object/class_db.h"

void TerrainData::_allocate(int p_size) {
	p_size = MAX(p_size, 2);
	const int old_size = size;
	const Vector<float> old_heights = heights;
	const Vector<uint8_t> old_tiles = tiles;
	const Vector<uint8_t> old_weights = weights;

	size = p_size;
	const int cells = size - 1;
	heights.clear();
	heights.resize_initialized(size * size);
	tiles.clear();
	tiles.resize_initialized(cells * cells * 4);
	weights.clear();
	weights.resize_initialized(size * size * LAYER_COUNT);

	// Everything starts as the first layer.
	uint8_t *w = weights.ptrw();
	for (int i = 0; i < size * size; i++) {
		w[i * LAYER_COUNT] = 255;
	}

	if (old_size < 2 || old_heights.size() != old_size * old_size) {
		return;
	}
	const int common = MIN(old_size, size);
	float *h = heights.ptrw();
	uint8_t *t = tiles.ptrw();
	const bool have_weights = old_weights.size() == old_size * old_size * LAYER_COUNT;
	const bool have_tiles = old_tiles.size() == (old_size - 1) * (old_size - 1) * 4;
	for (int z = 0; z < common; z++) {
		for (int x = 0; x < common; x++) {
			h[z * size + x] = old_heights[z * old_size + x];
			if (have_weights) {
				memcpy(w + (z * size + x) * LAYER_COUNT, old_weights.ptr() + (z * old_size + x) * LAYER_COUNT, LAYER_COUNT);
			}
			if (have_tiles && x < common - 1 && z < common - 1) {
				memcpy(t + (z * cells + x) * 4, old_tiles.ptr() + (z * (old_size - 1) + x) * 4, 4);
			}
		}
	}
}

void TerrainData::set_size(int p_size) {
	p_size = CLAMP(p_size, 2, 8193);
	if (p_size == size) {
		return;
	}
	_allocate(p_size);
	emit_signal(SNAME("resized"));
	emit_changed();
}

void TerrainData::set_cell_size(float p_size) {
	p_size = MAX(p_size, 0.01f);
	if (p_size == cell_size) {
		return;
	}
	cell_size = p_size;
	emit_signal(SNAME("resized"));
	emit_changed();
}

void TerrainData::set_heights(const Vector<float> &p_heights) {
	if (p_heights.size() != size * size) {
		return;
	}
	heights = p_heights;
	notify_changed(CHANNEL_HEIGHT, Rect2i(0, 0, size, size));
}

void TerrainData::set_tiles(const Vector<uint8_t> &p_tiles) {
	if (p_tiles.size() != get_cells() * get_cells() * 4) {
		return;
	}
	tiles = p_tiles;
	notify_changed(CHANNEL_TILES, Rect2i(0, 0, get_cells(), get_cells()));
}

void TerrainData::set_weights(const Vector<uint8_t> &p_weights) {
	if (p_weights.size() != size * size * LAYER_COUNT) {
		return;
	}
	weights = p_weights;
	notify_changed(CHANNEL_WEIGHTS, Rect2i(0, 0, size, size));
}

void TerrainData::notify_changed(int p_channels, const Rect2i &p_rect) {
	emit_signal(SNAME("region_changed"), p_channels, p_rect);
	emit_changed();
}

float TerrainData::get_height(int p_x, int p_z) const {
	if (size < 2) {
		return 0.0;
	}
	p_x = CLAMP(p_x, 0, size - 1);
	p_z = CLAMP(p_z, 0, size - 1);
	return heights[p_z * size + p_x];
}

void TerrainData::set_height(int p_x, int p_z, float p_height) {
	ERR_FAIL_INDEX(p_x, size);
	ERR_FAIL_INDEX(p_z, size);
	heights.write[p_z * size + p_x] = p_height;
	notify_changed(CHANNEL_HEIGHT, Rect2i(p_x, p_z, 1, 1));
}

float TerrainData::sample_height(const Vector2 &p_grid_position) const {
	if (size < 2) {
		return 0.0;
	}
	const float fx = CLAMP(p_grid_position.x, 0.0f, float(size - 1));
	const float fz = CLAMP(p_grid_position.y, 0.0f, float(size - 1));
	const int x = MIN(int(fx), size - 2);
	const int z = MIN(int(fz), size - 2);
	const float u = fx - x;
	const float v = fz - z;
	const float *h = heights.ptr();
	const float a = h[z * size + x];
	const float b = h[z * size + x + 1];
	const float c = h[(z + 1) * size + x];
	const float d = h[(z + 1) * size + x + 1];
	// The cell is split along a-d.
	if (v > u) {
		return a + (d - c) * u + (c - a) * v;
	}
	return a + (b - a) * u + (d - b) * v;
}

Vector3 TerrainData::get_normal(int p_x, int p_z) const {
	const float dx = get_height(p_x + 1, p_z) - get_height(p_x - 1, p_z);
	const float dz = get_height(p_x, p_z + 1) - get_height(p_x, p_z - 1);
	// The grid's z axis runs along -Z.
	return Vector3(-dx, 2.0f * cell_size, dz).normalized();
}

void TerrainData::set_tile(int p_x, int p_z, int p_slot, int p_index, int p_direction) {
	const int cells = get_cells();
	ERR_FAIL_INDEX(p_x, cells);
	ERR_FAIL_INDEX(p_z, cells);
	ERR_FAIL_INDEX(p_slot, TILE_SLOTS);
	p_index = CLAMP(p_index, 0, MAX_TILE_INDEX);
	p_direction = CLAMP(p_direction, 0, TILE_DIRECTIONS - 1);
	uint8_t *t = tiles.ptrw() + (p_z * cells + p_x) * 4;
	if (p_slot == 0) {
		t[0] = p_index & 255;
		t[2] = (t[2] & 0x38) | p_direction;
		t[3] = (t[3] & ~0x13) | (p_index >> 8) | 16;
	} else {
		t[1] = p_index & 255;
		t[2] = (t[2] & 0x07) | (p_direction << 3);
		t[3] = (t[3] & ~0x2c) | ((p_index >> 8) << 2) | 32;
	}
	notify_changed(CHANNEL_TILES, Rect2i(p_x, p_z, 1, 1));
}

void TerrainData::clear_tile(int p_x, int p_z, int p_slot) {
	const int cells = get_cells();
	ERR_FAIL_INDEX(p_x, cells);
	ERR_FAIL_INDEX(p_z, cells);
	ERR_FAIL_INDEX(p_slot, TILE_SLOTS);
	tiles.write[(p_z * cells + p_x) * 4 + 3] &= p_slot == 0 ? ~16 : ~32;
	notify_changed(CHANNEL_TILES, Rect2i(p_x, p_z, 1, 1));
}

int TerrainData::get_tile_index(int p_x, int p_z, int p_slot) const {
	const int cells = get_cells();
	ERR_FAIL_INDEX_V(p_x, cells, 0);
	ERR_FAIL_INDEX_V(p_z, cells, 0);
	const uint8_t *t = tiles.ptr() + (p_z * cells + p_x) * 4;
	return p_slot == 0 ? (t[0] | ((t[3] & 3) << 8)) : (t[1] | (((t[3] >> 2) & 3) << 8));
}

int TerrainData::get_tile_direction(int p_x, int p_z, int p_slot) const {
	const int cells = get_cells();
	ERR_FAIL_INDEX_V(p_x, cells, 0);
	ERR_FAIL_INDEX_V(p_z, cells, 0);
	const uint8_t b = tiles[(p_z * cells + p_x) * 4 + 2];
	return p_slot == 0 ? (b & 7) : ((b >> 3) & 7);
}

bool TerrainData::is_tile_used(int p_x, int p_z, int p_slot) const {
	const int cells = get_cells();
	ERR_FAIL_INDEX_V(p_x, cells, false);
	ERR_FAIL_INDEX_V(p_z, cells, false);
	return tiles[(p_z * cells + p_x) * 4 + 3] & (p_slot == 0 ? 16 : 32);
}

float TerrainData::get_layer_weight(int p_x, int p_z, int p_layer) const {
	ERR_FAIL_INDEX_V(p_x, size, 0.0);
	ERR_FAIL_INDEX_V(p_z, size, 0.0);
	ERR_FAIL_INDEX_V(p_layer, LAYER_COUNT, 0.0);
	return weights[(p_z * size + p_x) * LAYER_COUNT + p_layer] / 255.0f;
}

template <typename T>
static Vector<T> _get_region(const Vector<T> &p_from, int p_side, int p_stride, const Rect2i &p_rect) {
	Vector<T> out;
	const Rect2i rect = p_rect.intersection(Rect2i(0, 0, p_side, p_side));
	if (rect.size.x <= 0 || rect.size.y <= 0) {
		return out;
	}
	out.resize(rect.size.x * rect.size.y * p_stride);
	T *w = out.ptrw();
	for (int z = 0; z < rect.size.y; z++) {
		memcpy(w + z * rect.size.x * p_stride, p_from.ptr() + ((rect.position.y + z) * p_side + rect.position.x) * p_stride, rect.size.x * p_stride * sizeof(T));
	}
	return out;
}

template <typename T>
static bool _set_region(Vector<T> &p_to, int p_side, int p_stride, const Rect2i &p_rect, const Vector<T> &p_values, Rect2i &r_rect) {
	r_rect = p_rect.intersection(Rect2i(0, 0, p_side, p_side));
	if (r_rect.size.x <= 0 || r_rect.size.y <= 0 || p_values.size() != r_rect.size.x * r_rect.size.y * p_stride) {
		return false;
	}
	T *w = p_to.ptrw();
	for (int z = 0; z < r_rect.size.y; z++) {
		memcpy(w + ((r_rect.position.y + z) * p_side + r_rect.position.x) * p_stride, p_values.ptr() + z * r_rect.size.x * p_stride, r_rect.size.x * p_stride * sizeof(T));
	}
	return true;
}

Vector<float> TerrainData::get_heights_region(const Rect2i &p_rect) const {
	return _get_region(heights, size, 1, p_rect);
}

void TerrainData::set_heights_region(const Rect2i &p_rect, const Vector<float> &p_heights) {
	Rect2i rect;
	if (_set_region(heights, size, 1, p_rect, p_heights, rect)) {
		notify_changed(CHANNEL_HEIGHT, rect);
	}
}

Vector<uint8_t> TerrainData::get_tiles_region(const Rect2i &p_rect) const {
	return _get_region(tiles, get_cells(), 4, p_rect);
}

void TerrainData::set_tiles_region(const Rect2i &p_rect, const Vector<uint8_t> &p_tiles) {
	Rect2i rect;
	if (_set_region(tiles, get_cells(), 4, p_rect, p_tiles, rect)) {
		notify_changed(CHANNEL_TILES, rect);
	}
}

Vector<uint8_t> TerrainData::get_weights_region(const Rect2i &p_rect) const {
	return _get_region(weights, size, int(LAYER_COUNT), p_rect);
}

void TerrainData::set_weights_region(const Rect2i &p_rect, const Vector<uint8_t> &p_weights) {
	Rect2i rect;
	if (_set_region(weights, size, int(LAYER_COUNT), p_rect, p_weights, rect)) {
		notify_changed(CHANNEL_WEIGHTS, rect);
	}
}

void TerrainData::import_heightmap(const Ref<Image> &p_image, float p_min_height, float p_max_height) {
	ERR_FAIL_COND(p_image.is_null() || p_image->is_empty());
	Ref<Image> image = p_image->duplicate();
	if (image->is_compressed()) {
		ERR_FAIL_COND(image->decompress() != OK);
	}
	image->convert(Image::FORMAT_RF);
	if (image->get_width() != size || image->get_height() != size) {
		image->resize(size, size, Image::INTERPOLATE_BILINEAR);
	}
	const Vector<uint8_t> raw = image->get_data();
	const float *src = (const float *)raw.ptr();
	float *h = heights.ptrw();
	for (int z = 0; z < size; z++) {
		// The top row of the image is the far edge of the terrain.
		const float *row = src + (size - 1 - z) * size;
		for (int x = 0; x < size; x++) {
			h[z * size + x] = p_min_height + (p_max_height - p_min_height) * row[x];
		}
	}
	notify_changed(CHANNEL_HEIGHT, Rect2i(0, 0, size, size));
}

Ref<Image> TerrainData::export_heightmap(float p_min_height, float p_max_height) const {
	Vector<uint8_t> raw;
	raw.resize(size * size * sizeof(float));
	float *dst = (float *)raw.ptrw();
	const float range = MAX(p_max_height - p_min_height, 0.0001f);
	for (int z = 0; z < size; z++) {
		for (int x = 0; x < size; x++) {
			dst[(size - 1 - z) * size + x] = (heights[z * size + x] - p_min_height) / range;
		}
	}
	return Image::create_from_data(size, size, false, Image::FORMAT_RF, raw);
}

Ref<Image> TerrainData::create_tile_image() const {
	const int cells = get_cells();
	if (cells < 1) {
		return Ref<Image>();
	}
	return Image::create_from_data(cells, cells, false, Image::FORMAT_RGBA8, tiles);
}

Ref<Image> TerrainData::create_weight_image(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, LAYER_COUNT / 4, Ref<Image>());
	if (size < 2) {
		return Ref<Image>();
	}
	Vector<uint8_t> raw;
	raw.resize(size * size * 4);
	uint8_t *dst = raw.ptrw();
	const uint8_t *src = weights.ptr() + p_index * 4;
	for (int i = 0; i < size * size; i++) {
		memcpy(dst + i * 4, src + i * LAYER_COUNT, 4);
	}
	return Image::create_from_data(size, size, false, Image::FORMAT_RGBA8, raw);
}

void TerrainData::get_height_range(float &r_min, float &r_max, const Rect2i &p_rect) const {
	const Rect2i rect = p_rect.intersection(Rect2i(0, 0, size, size));
	r_min = 0.0;
	r_max = 0.0;
	bool first = true;
	for (int z = rect.position.y; z < rect.position.y + rect.size.y; z++) {
		for (int x = rect.position.x; x < rect.position.x + rect.size.x; x++) {
			const float h = heights[z * size + x];
			if (first) {
				r_min = r_max = h;
				first = false;
			} else {
				r_min = MIN(r_min, h);
				r_max = MAX(r_max, h);
			}
		}
	}
}

void TerrainData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &TerrainData::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &TerrainData::get_size);
	ClassDB::bind_method(D_METHOD("get_cells"), &TerrainData::get_cells);
	ClassDB::bind_method(D_METHOD("set_cell_size", "size"), &TerrainData::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &TerrainData::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_heights", "heights"), &TerrainData::set_heights);
	ClassDB::bind_method(D_METHOD("get_heights"), &TerrainData::get_heights);
	ClassDB::bind_method(D_METHOD("set_tiles", "tiles"), &TerrainData::set_tiles);
	ClassDB::bind_method(D_METHOD("get_tiles"), &TerrainData::get_tiles);
	ClassDB::bind_method(D_METHOD("set_weights", "weights"), &TerrainData::set_weights);
	ClassDB::bind_method(D_METHOD("get_weights"), &TerrainData::get_weights);

	ClassDB::bind_method(D_METHOD("get_height", "x", "z"), &TerrainData::get_height);
	ClassDB::bind_method(D_METHOD("set_height", "x", "z", "height"), &TerrainData::set_height);
	ClassDB::bind_method(D_METHOD("sample_height", "grid_position"), &TerrainData::sample_height);
	ClassDB::bind_method(D_METHOD("get_normal", "x", "z"), &TerrainData::get_normal);
	ClassDB::bind_method(D_METHOD("set_tile", "x", "z", "slot", "index", "direction"), &TerrainData::set_tile);
	ClassDB::bind_method(D_METHOD("clear_tile", "x", "z", "slot"), &TerrainData::clear_tile);
	ClassDB::bind_method(D_METHOD("get_tile_index", "x", "z", "slot"), &TerrainData::get_tile_index);
	ClassDB::bind_method(D_METHOD("get_tile_direction", "x", "z", "slot"), &TerrainData::get_tile_direction);
	ClassDB::bind_method(D_METHOD("is_tile_used", "x", "z", "slot"), &TerrainData::is_tile_used);
	ClassDB::bind_method(D_METHOD("get_layer_weight", "x", "z", "layer"), &TerrainData::get_layer_weight);

	ClassDB::bind_method(D_METHOD("get_heights_region", "rect"), &TerrainData::get_heights_region);
	ClassDB::bind_method(D_METHOD("set_heights_region", "rect", "heights"), &TerrainData::set_heights_region);
	ClassDB::bind_method(D_METHOD("get_tiles_region", "rect"), &TerrainData::get_tiles_region);
	ClassDB::bind_method(D_METHOD("set_tiles_region", "rect", "tiles"), &TerrainData::set_tiles_region);
	ClassDB::bind_method(D_METHOD("get_weights_region", "rect"), &TerrainData::get_weights_region);
	ClassDB::bind_method(D_METHOD("set_weights_region", "rect", "weights"), &TerrainData::set_weights_region);

	ClassDB::bind_method(D_METHOD("import_heightmap", "image", "min_height", "max_height"), &TerrainData::import_heightmap);
	ClassDB::bind_method(D_METHOD("export_heightmap", "min_height", "max_height"), &TerrainData::export_heightmap);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "size", PROPERTY_HINT_RANGE, "2,8193,1"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cell_size", PROPERTY_HINT_RANGE, "0.01,64,0.01,or_greater,suffix:m"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "heights", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_heights", "get_heights");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "tiles", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_tiles", "get_tiles");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "weights", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE), "set_weights", "get_weights");

	ADD_SIGNAL(MethodInfo("resized"));
	ADD_SIGNAL(MethodInfo("region_changed", PropertyInfo(Variant::INT, "channels"), PropertyInfo(Variant::RECT2I, "rect")));

	BIND_ENUM_CONSTANT(CHANNEL_HEIGHT);
	BIND_ENUM_CONSTANT(CHANNEL_TILES);
	BIND_ENUM_CONSTANT(CHANNEL_WEIGHTS);
}

TerrainData::TerrainData() {
	_allocate(129);
}
