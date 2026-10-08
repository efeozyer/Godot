#include "terrain_editor_plugin.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/io/image.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "servers/rendering/rendering_server.h"

static const char *tool_names[] = { "Raise", "Lower", "Smooth", "Flatten", "Set Height", "Paint Layer", "Paint Tile", "Erase Tile" };

static VBoxContainer *_add_column(HBoxContainer *p_parent) {
	if (p_parent->get_child_count() > 0) {
		p_parent->add_child(memnew(VSeparator));
	}
	VBoxContainer *column = memnew(VBoxContainer);
	column->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
	p_parent->add_child(column);
	return column;
}

static SpinBox *_add_spin(GridContainer *p_grid, const String &p_label, double p_min, double p_max, double p_step, double p_value, const String &p_suffix = String()) {
	Label *label = memnew(Label(p_label));
	p_grid->add_child(label);
	SpinBox *spin = memnew(SpinBox);
	spin->set_min(p_min);
	spin->set_max(p_max);
	spin->set_step(p_step);
	spin->set_value(p_value);
	spin->set_suffix(p_suffix);
	spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_grid->add_child(spin);
	return spin;
}

void TerrainEditorPlugin::_select_tool(int p_tool) {
	tool = Tool(p_tool);
	for (int i = 0; i < TOOL_MAX; i++) {
		tool_buttons[i]->set_pressed_no_signal(i == p_tool);
	}
}

void TerrainEditorPlugin::_update_panel() {
	const bool has_data = terrain && terrain->get_data().is_valid();
	const bool tiles = terrain && terrain->get_mode() == Terrain3D::MODE_TILES;
	for (int i = 0; i < TOOL_MAX; i++) {
		bool usable = has_data;
		if (i == TOOL_PAINT_LAYER) {
			usable = usable && !tiles;
		} else if (i == TOOL_PAINT_TILE || i == TOOL_ERASE_TILE) {
			usable = usable && tiles;
		}
		tool_buttons[i]->set_disabled(!usable);
	}
	if (has_data && tool_buttons[tool]->is_disabled()) {
		_select_tool(TOOL_RAISE);
	}
	if (terrain) {
		mode->select(terrain->get_mode());
	}
	mode->set_disabled(!terrain);
	create_button->set_disabled(!terrain);
	layer->set_disabled(!has_data || tiles);
	tile_index->set_editable(has_data && tiles);
	tile_slot->set_disabled(!has_data || !tiles);
	tile_direction->set_disabled(!has_data || !tiles);
	create_button->set_text(has_data ? TTR("Resize") : TTR("Create"));
	if (has_data) {
		const Ref<TerrainData> data = terrain->get_data();
		status->set_text(vformat(TTR("%d x %d cells of %s m"), data->get_cells(), data->get_cells(), String::num(data->get_cell_size(), 2)));
	} else if (terrain) {
		status->set_text(TTR("This terrain has no data yet. Choose its size and press Create."));
	} else {
		status->set_text(TTR("Select a Terrain3D node to edit it."));
	}
	_update_tile_preview();
}

void TerrainEditorPlugin::_update_tile_preview() {
	tile_preview->set_texture(Ref<Texture2D>());
	if (!terrain || terrain->get_mode() != Terrain3D::MODE_TILES || terrain->get_tile_material().is_null()) {
		return;
	}
	const Ref<TextureLayered> textures = terrain->get_tile_material()->get_tile_textures();
	const int index = int(tile_index->get_value());
	if (textures.is_null() || index >= textures->get_layers()) {
		return;
	}
	Ref<Image> image = textures->get_layer_data(index);
	if (image.is_null() || image->is_empty()) {
		return;
	}
	image = image->duplicate();
	if (image->is_compressed() && image->decompress() != OK) {
		return;
	}
	image->clear_mipmaps();
	tile_preview->set_texture(ImageTexture::create_from_image(image));
}

void TerrainEditorPlugin::_mode_selected(int p_mode) {
	if (!terrain) {
		return;
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Change Terrain Mode"));
	ur->add_do_property(terrain, "mode", p_mode);
	ur->add_undo_property(terrain, "mode", terrain->get_mode());
	if (p_mode == Terrain3D::MODE_LAYERS && terrain->get_layer_material().is_null()) {
		Ref<TerrainLayerMaterial> material;
		material.instantiate();
		ur->add_do_property(terrain, "layer_material", material);
		ur->add_undo_property(terrain, "layer_material", Ref<TerrainLayerMaterial>());
	}
	if (p_mode == Terrain3D::MODE_TILES && terrain->get_tile_material().is_null()) {
		Ref<TerrainMaterial> material;
		material.instantiate();
		ur->add_do_property(terrain, "tile_material", material);
		ur->add_undo_property(terrain, "tile_material", Ref<TerrainMaterial>());
	}
	ur->add_do_method(this, "_update_panel");
	ur->add_undo_method(this, "_update_panel");
	ur->commit_action();
}

void TerrainEditorPlugin::_create_pressed() {
	if (!terrain) {
		return;
	}
	const int cells = int(new_size->get_value());
	const float cell = float(new_cell_size->get_value());
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	if (terrain->get_data().is_null()) {
		Ref<TerrainData> data;
		data.instantiate();
		data->set_cell_size(cell);
		data->set_size(cells + 1);
		ur->create_action(TTR("Create Terrain Data"));
		ur->add_do_property(terrain, "data", data);
		ur->add_undo_property(terrain, "data", Ref<TerrainData>());
		if (terrain->get_mode() == Terrain3D::MODE_LAYERS && terrain->get_layer_material().is_null()) {
			Ref<TerrainLayerMaterial> material;
			material.instantiate();
			ur->add_do_property(terrain, "layer_material", material);
			ur->add_undo_property(terrain, "layer_material", Ref<TerrainLayerMaterial>());
		}
		if (terrain->get_mode() == Terrain3D::MODE_TILES && terrain->get_tile_material().is_null()) {
			Ref<TerrainMaterial> material;
			material.instantiate();
			ur->add_do_property(terrain, "tile_material", material);
			ur->add_undo_property(terrain, "tile_material", Ref<TerrainMaterial>());
		}
	} else {
		// Resizing drops what falls outside, so the whole state is kept for undo.
		const Ref<TerrainData> data = terrain->get_data();
		ur->create_action(TTR("Resize Terrain"));
		ur->add_do_property(data.ptr(), "cell_size", cell);
		ur->add_do_property(data.ptr(), "size", cells + 1);
		ur->add_undo_property(data.ptr(), "cell_size", data->get_cell_size());
		ur->add_undo_property(data.ptr(), "size", data->get_size());
		ur->add_undo_property(data.ptr(), "heights", data->get_heights());
		ur->add_undo_property(data.ptr(), "tiles", data->get_tiles());
		ur->add_undo_property(data.ptr(), "weights", data->get_weights());
	}
	ur->add_do_method(this, "_update_panel");
	ur->add_undo_method(this, "_update_panel");
	ur->commit_action();
}

void TerrainEditorPlugin::_import_pressed() {
	if (!terrain || terrain->get_data().is_null()) {
		return;
	}
	import_dialog->popup_file_dialog();
}

void TerrainEditorPlugin::_import_file_selected(const String &p_path) {
	if (!terrain || terrain->get_data().is_null()) {
		return;
	}
	Ref<Image> image;
	image.instantiate();
	if (image->load(p_path) != OK) {
		ERR_PRINT(vformat("Terrain: cannot read the heightmap image '%s'.", p_path));
		return;
	}
	const Ref<TerrainData> data = terrain->get_data();
	const Vector<float> before = data->get_heights();
	data->import_heightmap(image, float(import_min->get_value()), float(import_max->get_value()));
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Import Terrain Heightmap"));
	ur->add_do_property(data.ptr(), "heights", data->get_heights());
	ur->add_undo_property(data.ptr(), "heights", before);
	ur->commit_action(false);
}

void TerrainEditorPlugin::_terrain_exiting() {
	edit(nullptr);
}

float TerrainEditorPlugin::_falloff(float p_distance) const {
	const float r = float(radius->get_value());
	const float t = p_distance / r;
	if (t >= 1.0f) {
		return 0.0f;
	}
	const float hard = float(hardness->get_value()) * 0.01f;
	if (t <= hard) {
		return 1.0f;
	}
	const float s = (t - hard) / MAX(1.0f - hard, 0.0001f);
	return 1.0f - s * s * (3.0f - 2.0f * s);
}

bool TerrainEditorPlugin::_update_brush(Camera3D *p_camera, const Vector2 &p_position) {
	brush_valid = false;
	if (terrain && terrain->get_data().is_valid() && terrain->is_inside_tree()) {
		Vector3 hit;
		if (terrain->intersect_ray(p_camera->project_ray_origin(p_position), p_camera->project_ray_normal(p_position), hit)) {
			brush_position = terrain->get_global_transform().affine_inverse().xform(hit);
			brush_valid = true;
		}
	}
	_update_cursor();
	return brush_valid;
}

void TerrainEditorPlugin::_update_cursor() {
	RenderingServer *rs = RS::get_singleton();
	if (!brush_valid || !terrain || !terrain->is_inside_tree() || terrain->get_data().is_null()) {
		rs->instance_set_visible(cursor_instance, false);
		return;
	}
	const Ref<TerrainData> data = terrain->get_data();
	const float r = float(radius->get_value());
	const float hard = float(hardness->get_value()) * 0.01f;
	const int points = 64;
	Vector<Vector3> ring;
	// The outer ring is the brush; the inner one is where it starts to fade.
	for (int pass = 0; pass < 2; pass++) {
		const float pass_radius = pass == 0 ? r : r * hard;
		if (pass_radius < 0.01f) {
			continue;
		}
		for (int i = 0; i < points; i++) {
			for (int k = 0; k < 2; k++) {
				const float angle = Math::TAU * float(i + k) / points;
				Vector3 p = brush_position + Vector3(Math::cos(angle), 0.0, Math::sin(angle)) * pass_radius;
				p.y = data->sample_height(terrain->to_grid(p)) + 0.05f;
				ring.push_back(p);
			}
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = ring;
	cursor_mesh->clear_surfaces();
	cursor_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, arrays);
	cursor_mesh->surface_set_material(0, cursor_material);
	rs->instance_set_scenario(cursor_instance, terrain->get_world_3d()->get_scenario());
	rs->instance_set_transform(cursor_instance, terrain->get_global_transform());
	rs->instance_set_visible(cursor_instance, true);
}

void TerrainEditorPlugin::_touch(const Rect2i &p_rect) {
	stroke_rect = stroke_touched ? stroke_rect.merge(p_rect) : p_rect;
	stroke_touched = true;
}

void TerrainEditorPlugin::_begin_stroke(Tool p_tool) {
	const Ref<TerrainData> data = terrain->get_data();
	stroking = true;
	stroke_tool = p_tool;
	stroke_touched = false;
	stroke_rect = Rect2i();
	stroke_height = stroke_tool == TOOL_SET_HEIGHT ? float(height->get_value()) : data->sample_height(terrain->to_grid(brush_position));
	heights_before.clear();
	bytes_before.clear();
	if (stroke_tool == TOOL_PAINT_LAYER) {
		stroke_channel = TerrainData::CHANNEL_WEIGHTS;
		bytes_before = data->get_weights();
	} else if (stroke_tool == TOOL_PAINT_TILE || stroke_tool == TOOL_ERASE_TILE) {
		stroke_channel = TerrainData::CHANNEL_TILES;
		bytes_before = data->get_tiles();
	} else {
		stroke_channel = TerrainData::CHANNEL_HEIGHT;
		heights_before = data->get_heights();
	}
	last_apply_usec = OS::get_singleton()->get_ticks_usec();
	set_process(true);
	// The first dab does not wait for a frame.
	_apply_brush(1.0f / 60.0f);
}

void TerrainEditorPlugin::_apply_brush(float p_delta) {
	if (!stroking || !brush_valid || !terrain || terrain->get_data().is_null()) {
		return;
	}
	switch (stroke_channel) {
		case TerrainData::CHANNEL_HEIGHT:
			_sculpt(p_delta);
			break;
		case TerrainData::CHANNEL_WEIGHTS:
			_paint_layer(p_delta);
			break;
		case TerrainData::CHANNEL_TILES:
			_paint_tile();
			break;
	}
	_update_cursor();
}

void TerrainEditorPlugin::_sculpt(float p_delta) {
	const Ref<TerrainData> data = terrain->get_data();
	const int size = data->get_size();
	const float cell = data->get_cell_size();
	const float r = float(radius->get_value());
	const float power = float(strength->get_value()) * 0.01f;
	const Vector2 centre = terrain->to_grid(brush_position);
	const int reach = int(Math::ceil(r / cell));
	const int x0 = MAX(int(Math::floor(centre.x)) - reach, 0);
	const int z0 = MAX(int(Math::floor(centre.y)) - reach, 0);
	const int x1 = MIN(int(Math::ceil(centre.x)) + reach, size - 1);
	const int z1 = MIN(int(Math::ceil(centre.y)) + reach, size - 1);
	if (x1 < x0 || z1 < z0) {
		return;
	}

	Tool action = stroke_tool;
	if (Input::get_singleton()->is_key_pressed(Key::SHIFT)) {
		action = TOOL_SMOOTH;
	} else if (Input::get_singleton()->is_key_pressed(Key::CTRL)) {
		if (action == TOOL_RAISE) {
			action = TOOL_LOWER;
		} else if (action == TOOL_LOWER) {
			action = TOOL_RAISE;
		}
	}

	float *heights = data->heights_ptrw();
	// Smoothing reads the neighbours as they were before this dab.
	Vector<float> source;
	if (action == TOOL_SMOOTH) {
		source = data->get_heights();
	}
	// Metres per second at full strength, growing with the brush so that large brushes do not crawl.
	const float speed = power * MAX(r, 2.0f) * 2.0f;
	const float blend = MIN(power * p_delta * 10.0f, 1.0f);

	for (int z = z0; z <= z1; z++) {
		for (int x = x0; x <= x1; x++) {
			const float f = _falloff(Vector2(x - centre.x, z - centre.y).length() * cell);
			if (f <= 0.0f) {
				continue;
			}
			float &h = heights[z * size + x];
			switch (action) {
				case TOOL_RAISE:
					h += speed * f * p_delta;
					break;
				case TOOL_LOWER:
					h -= speed * f * p_delta;
					break;
				case TOOL_SMOOTH: {
					const float *s = source.ptr();
					float sum = 0.0;
					int count = 0;
					for (int dz = -1; dz <= 1; dz++) {
						for (int dx = -1; dx <= 1; dx++) {
							const int sx = x + dx;
							const int sz = z + dz;
							if (sx >= 0 && sz >= 0 && sx < size && sz < size) {
								sum += s[sz * size + sx];
								count++;
							}
						}
					}
					h = Math::lerp(h, sum / count, MIN(blend * f * 2.0f, 1.0f));
				} break;
				case TOOL_FLATTEN:
				case TOOL_SET_HEIGHT:
					h = Math::lerp(h, stroke_height, blend * f);
					break;
				default:
					break;
			}
		}
	}
	const Rect2i rect(x0, z0, x1 - x0 + 1, z1 - z0 + 1);
	_touch(rect);
	data->notify_changed(TerrainData::CHANNEL_HEIGHT, rect);
}

void TerrainEditorPlugin::_paint_layer(float p_delta) {
	const Ref<TerrainData> data = terrain->get_data();
	const int size = data->get_size();
	const float cell = data->get_cell_size();
	const float r = float(radius->get_value());
	const float power = float(strength->get_value()) * 0.01f;
	const int target = layer->get_selected();
	const Vector2 centre = terrain->to_grid(brush_position);
	const int reach = int(Math::ceil(r / cell));
	const int x0 = MAX(int(Math::floor(centre.x)) - reach, 0);
	const int z0 = MAX(int(Math::floor(centre.y)) - reach, 0);
	const int x1 = MIN(int(Math::ceil(centre.x)) + reach, size - 1);
	const int z1 = MIN(int(Math::ceil(centre.y)) + reach, size - 1);
	if (x1 < x0 || z1 < z0 || target < 0) {
		return;
	}
	uint8_t *weights = data->weights_ptrw();
	for (int z = z0; z <= z1; z++) {
		for (int x = x0; x <= x1; x++) {
			const float f = _falloff(Vector2(x - centre.x, z - centre.y).length() * cell);
			if (f <= 0.0f) {
				continue;
			}
			uint8_t *w = weights + (z * size + x) * TerrainData::LAYER_COUNT;
			const float amount = MIN(power * f * p_delta * 8.0f, 1.0f);
			const float now = w[target] / 255.0f;
			// At least one step, or a weak brush would never arrive.
			const int wanted = MIN(255, MAX(int(Math::round((now + (1.0f - now) * amount) * 255.0f)), w[target] + 1));
			// The other layers give way in proportion, so that the weights still add up to 255.
			int others = 0;
			for (int i = 0; i < TerrainData::LAYER_COUNT; i++) {
				if (i != target) {
					others += w[i];
				}
			}
			const int room = 255 - wanted;
			int given = 0;
			int largest = -1;
			for (int i = 0; i < TerrainData::LAYER_COUNT; i++) {
				if (i == target) {
					continue;
				}
				w[i] = others > 0 ? uint8_t(w[i] * room / others) : 0;
				given += w[i];
				if (largest < 0 || w[i] > w[largest]) {
					largest = i;
				}
			}
			w[target] = uint8_t(wanted);
			// What rounding lost goes to the painted layer.
			w[target] += uint8_t(255 - wanted - given);
			(void)largest;
		}
	}
	const Rect2i rect(x0, z0, x1 - x0 + 1, z1 - z0 + 1);
	_touch(rect);
	data->notify_changed(TerrainData::CHANNEL_WEIGHTS, rect);
}

void TerrainEditorPlugin::_paint_tile() {
	const Ref<TerrainData> data = terrain->get_data();
	const int cells = data->get_cells();
	const float cell = data->get_cell_size();
	const float r = float(radius->get_value());
	const Vector2 centre = terrain->to_grid(brush_position);
	const int reach = int(Math::ceil(r / cell));
	const int x0 = MAX(int(Math::floor(centre.x)) - reach, 0);
	const int z0 = MAX(int(Math::floor(centre.y)) - reach, 0);
	const int x1 = MIN(int(Math::floor(centre.x)) + reach, cells - 1);
	const int z1 = MIN(int(Math::floor(centre.y)) + reach, cells - 1);
	if (x1 < x0 || z1 < z0) {
		return;
	}
	const int slot = tile_slot->get_selected();
	const int index = int(tile_index->get_value());
	const int direction = tile_direction->get_selected();
	const bool erase = stroke_tool == TOOL_ERASE_TILE;
	uint8_t *tiles = data->tiles_ptrw();
	bool changed = false;
	for (int z = z0; z <= z1; z++) {
		for (int x = x0; x <= x1; x++) {
			// A cell is painted when its centre is under the brush; the cell under the cursor always is.
			const bool under_cursor = x == int(Math::floor(centre.x)) && z == int(Math::floor(centre.y));
			if (!under_cursor && Vector2(x + 0.5f - centre.x, z + 0.5f - centre.y).length() * cell > r) {
				continue;
			}
			uint8_t *t = tiles + (z * cells + x) * 4;
			uint8_t next[4] = { t[0], t[1], t[2], t[3] };
			if (erase) {
				next[3] &= slot == 0 ? ~16 : ~32;
			} else if (slot == 0) {
				next[0] = index & 255;
				next[2] = (next[2] & 0x38) | direction;
				next[3] = (next[3] & ~0x13) | (index >> 8) | 16;
			} else {
				next[1] = index & 255;
				next[2] = (next[2] & 0x07) | (direction << 3);
				next[3] = (next[3] & ~0x2c) | ((index >> 8) << 2) | 32;
			}
			if (memcmp(t, next, 4) != 0) {
				memcpy(t, next, 4);
				changed = true;
			}
		}
	}
	if (changed) {
		const Rect2i rect(x0, z0, x1 - x0 + 1, z1 - z0 + 1);
		_touch(rect);
		data->notify_changed(TerrainData::CHANNEL_TILES, rect);
	}
}

void TerrainEditorPlugin::_end_stroke() {
	if (!stroking) {
		return;
	}
	stroking = false;
	set_process(false);
	if (!terrain || terrain->get_data().is_null() || !stroke_touched) {
		heights_before.clear();
		bytes_before.clear();
		return;
	}
	const Ref<TerrainData> data = terrain->get_data();
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	const Rect2i rect = stroke_rect;
	ur->create_action(vformat(TTR("Terrain: %s"), TTR(tool_names[stroke_tool])));
	if (stroke_channel == TerrainData::CHANNEL_HEIGHT) {
		Vector<float> before;
		before.resize(rect.size.x * rect.size.y);
		for (int z = 0; z < rect.size.y; z++) {
			memcpy(before.ptrw() + z * rect.size.x, heights_before.ptr() + (rect.position.y + z) * data->get_size() + rect.position.x, rect.size.x * sizeof(float));
		}
		ur->add_do_method(data.ptr(), "set_heights_region", rect, data->get_heights_region(rect));
		ur->add_undo_method(data.ptr(), "set_heights_region", rect, before);
		ur->add_do_method(terrain, "update_collision");
		ur->add_undo_method(terrain, "update_collision");
	} else {
		const bool tiles = stroke_channel == TerrainData::CHANNEL_TILES;
		const int side = tiles ? data->get_cells() : data->get_size();
		const int stride = tiles ? 4 : int(TerrainData::LAYER_COUNT);
		Vector<uint8_t> before;
		before.resize(rect.size.x * rect.size.y * stride);
		for (int z = 0; z < rect.size.y; z++) {
			memcpy(before.ptrw() + z * rect.size.x * stride, bytes_before.ptr() + ((rect.position.y + z) * side + rect.position.x) * stride, rect.size.x * stride);
		}
		const StringName method = tiles ? StringName("set_tiles_region") : StringName("set_weights_region");
		ur->add_do_method(data.ptr(), method, rect, tiles ? data->get_tiles_region(rect) : data->get_weights_region(rect));
		ur->add_undo_method(data.ptr(), method, rect, before);
	}
	// The stroke is already applied.
	ur->commit_action(false);
	heights_before.clear();
	bytes_before.clear();
	terrain->update_collision();
}

EditorPlugin::AfterGUIInput TerrainEditorPlugin::forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	if (!active || !terrain || terrain->get_data().is_null()) {
		return AFTER_GUI_INPUT_PASS;
	}

	// Ctrl with + or - changes the brush radius, on the main keys or the keypad.
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && key->is_command_or_control_pressed() && !key->is_alt_pressed()) {
		const Key code = key->get_keycode();
		int direction = 0;
		if (code == Key::EQUAL || code == Key::PLUS || code == Key::KP_ADD) {
			direction = 1;
		} else if (code == Key::MINUS || code == Key::KP_SUBTRACT) {
			direction = -1;
		}
		if (direction != 0) {
			// A tenth of the radius at a time, and never less than the smallest step.
			const double step = MAX(radius->get_value() * 0.1, radius->get_step());
			radius->set_value(radius->get_value() + direction * step);
			_update_cursor();
			return AFTER_GUI_INPUT_STOP;
		}
	}

	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid()) {
		_update_brush(p_camera, motion->get_position());
		return stroking ? AFTER_GUI_INPUT_STOP : AFTER_GUI_INPUT_PASS;
	}

	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->get_button_index() == MouseButton::LEFT) {
		if (button->is_pressed()) {
			// Alt is left to the viewport, which orbits with it.
			if (button->is_alt_pressed() || !_update_brush(p_camera, button->get_position())) {
				return AFTER_GUI_INPUT_PASS;
			}
			_begin_stroke(tool);
			return AFTER_GUI_INPUT_STOP;
		}
		if (stroking) {
			_end_stroke();
			return AFTER_GUI_INPUT_STOP;
		}
	}
	return AFTER_GUI_INPUT_PASS;
}

void TerrainEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			if (!stroking) {
				set_process(false);
				break;
			}
			if (!Input::get_singleton()->is_mouse_button_pressed(MouseButton::LEFT)) {
				// The release went somewhere else.
				_end_stroke();
				break;
			}
			const uint64_t now = OS::get_singleton()->get_ticks_usec();
			const float delta = MIN((now - last_apply_usec) / 1000000.0f, 0.1f);
			last_apply_usec = now;
			_apply_brush(delta);
		} break;
	}
}

void TerrainEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_update_panel"), &TerrainEditorPlugin::_update_panel);
}

void TerrainEditorPlugin::edit(Object *p_object) {
	Terrain3D *next = Object::cast_to<Terrain3D>(p_object);
	if (next == terrain) {
		return;
	}
	_end_stroke();
	if (terrain) {
		terrain->disconnect(SceneStringName(tree_exiting), callable_mp(this, &TerrainEditorPlugin::_terrain_exiting));
	}
	terrain = next;
	brush_valid = false;
	if (terrain) {
		terrain->connect(SceneStringName(tree_exiting), callable_mp(this, &TerrainEditorPlugin::_terrain_exiting));
		if (terrain->get_data().is_valid()) {
			new_size->set_value(terrain->get_data()->get_cells());
			new_cell_size->set_value(terrain->get_data()->get_cell_size());
		}
	}
	_update_cursor();
	_update_panel();
}

bool TerrainEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<Terrain3D>(p_object) != nullptr;
}

void TerrainEditorPlugin::make_visible(bool p_visible) {
	active = p_visible;
	if (p_visible) {
		dock->make_visible();
	} else {
		_end_stroke();
		brush_valid = false;
		_update_cursor();
	}
	_update_panel();
}

TerrainEditorPlugin::TerrainEditorPlugin() {
	// A tab of its own in the bottom panel, next to the shader editor's.
	dock = memnew(EditorDock);
	dock->set_name(TTRC("Terrain Editor"));
	dock->set_icon_name("HeightMapShape3D");
	dock->set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	dock->set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	EditorDockManager::get_singleton()->add_dock(dock);

	panel = memnew(HBoxContainer);
	panel->add_theme_constant_override("separation", 16 * EDSCALE);
	dock->add_child(panel);

	VBoxContainer *column = _add_column(panel);
	status = memnew(Label);
	status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	column->add_child(status);

	GridContainer *setup = memnew(GridContainer);
	setup->set_columns(2);
	column->add_child(setup);
	setup->add_child(memnew(Label(TTR("Texturing"))));
	mode = memnew(OptionButton);
	mode->add_item(TTR("Tiles"));
	mode->add_item(TTR("Layers"));
	mode->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mode->connect(SceneStringName(item_selected), callable_mp(this, &TerrainEditorPlugin::_mode_selected));
	setup->add_child(mode);
	new_size = _add_spin(setup, TTR("Cells"), 1, 8192, 1, 256);
	new_cell_size = _add_spin(setup, TTR("Cell Size"), 0.25, 64, 0.25, 1, "m");
	create_button = memnew(Button(TTR("Create")));
	create_button->connect(SceneStringName(pressed), callable_mp(this, &TerrainEditorPlugin::_create_pressed));
	column->add_child(create_button);

	column = _add_column(panel);

	GridContainer *tools = memnew(GridContainer);
	tools->set_columns(2);
	column->add_child(tools);
	for (int i = 0; i < TOOL_MAX; i++) {
		Button *button = memnew(Button(TTR(tool_names[i])));
		button->set_toggle_mode(true);
		button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		button->connect(SceneStringName(pressed), callable_mp(this, &TerrainEditorPlugin::_select_tool).bind(i));
		tools->add_child(button);
		tool_buttons[i] = button;
	}
	tool_buttons[TOOL_RAISE]->set_pressed_no_signal(true);

	GridContainer *brush = memnew(GridContainer);
	brush->set_columns(2);
	column->add_child(brush);
	radius = _add_spin(brush, TTR("Radius"), 0.25, 1000, 0.25, 8, "m");
	strength = _add_spin(brush, TTR("Strength"), 1, 100, 1, 50, "%");
	hardness = _add_spin(brush, TTR("Hardness"), 0, 100, 1, 30, "%");
	height = _add_spin(brush, TTR("Height"), -10000, 10000, 0.01, 0, "m");

	column = _add_column(panel);

	GridContainer *paint = memnew(GridContainer);
	paint->set_columns(2);
	column->add_child(paint);
	paint->add_child(memnew(Label(TTR("Layer"))));
	layer = memnew(OptionButton);
	for (int i = 0; i < TerrainData::LAYER_COUNT; i++) {
		layer->add_item(vformat(TTR("Layer %d"), i + 1));
	}
	layer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	paint->add_child(layer);

	tile_index = _add_spin(paint, TTR("Tile"), 0, TerrainData::MAX_TILE_INDEX, 1, 0);
	tile_index->connect(SceneStringName(value_changed), callable_mp(this, &TerrainEditorPlugin::_update_tile_preview).unbind(1));
	paint->add_child(memnew(Label(TTR("Slot"))));
	tile_slot = memnew(OptionButton);
	tile_slot->add_item(TTR("First"));
	tile_slot->add_item(TTR("Second"));
	tile_slot->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	paint->add_child(tile_slot);
	paint->add_child(memnew(Label(TTR("Direction"))));
	tile_direction = memnew(OptionButton);
	for (int i = 0; i < TerrainData::TILE_DIRECTIONS; i++) {
		tile_direction->add_item(itos(i));
	}
	tile_direction->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	paint->add_child(tile_direction);

	tile_preview = memnew(TextureRect);
	tile_preview->set_custom_minimum_size(Size2(96, 96) * EDSCALE);
	tile_preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	tile_preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	column->add_child(tile_preview);

	column = _add_column(panel);

	GridContainer *import = memnew(GridContainer);
	import->set_columns(2);
	column->add_child(import);
	import_min = _add_spin(import, TTR("Black"), -10000, 10000, 0.01, 0, "m");
	import_max = _add_spin(import, TTR("White"), -10000, 10000, 0.01, 100, "m");
	Button *import_button = memnew(Button(TTR("Import Heightmap...")));
	import_button->connect(SceneStringName(pressed), callable_mp(this, &TerrainEditorPlugin::_import_pressed));
	column->add_child(import_button);

	import_dialog = memnew(EditorFileDialog);
	import_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
	import_dialog->set_access(FileDialog::ACCESS_FILESYSTEM);
	import_dialog->add_filter("*.png,*.exr,*.jpg,*.jpeg,*.tga,*.bmp,*.webp", TTR("Images"));
	import_dialog->connect("file_selected", callable_mp(this, &TerrainEditorPlugin::_import_file_selected));
	column->add_child(import_dialog);

	cursor_mesh.instantiate();
	cursor_material.instantiate();
	cursor_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	cursor_material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
	cursor_material->set_albedo(Color(1.0, 0.85, 0.2));
	cursor_instance = RS::get_singleton()->instance_create();
	RS::get_singleton()->instance_set_base(cursor_instance, cursor_mesh->get_rid());
	RS::get_singleton()->instance_geometry_set_cast_shadows_setting(cursor_instance, RSE::SHADOW_CASTING_SETTING_OFF);
	RS::get_singleton()->instance_set_visible(cursor_instance, false);

	_update_panel();
}

TerrainEditorPlugin::~TerrainEditorPlugin() {
	if (cursor_instance.is_valid() && RenderingServer::get_singleton()) {
		RS::get_singleton()->free_rid(cursor_instance);
	}
}
