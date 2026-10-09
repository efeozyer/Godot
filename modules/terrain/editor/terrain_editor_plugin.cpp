#include "terrain_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_viewport.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
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
	_update_tile_list();
}

void TerrainEditorPlugin::_update_tile_list() {
	// The tiles are offered while they can be painted.
	Ref<TextureLayered> textures;
	if (active && tool == TOOL_PAINT_TILE && terrain && terrain->get_data().is_valid() && terrain->get_mode() == Terrain3D::MODE_TILES && terrain->get_tile_material().is_valid()) {
		textures = terrain->get_tile_material()->get_tile_textures();
	}
	tile_column->set_visible(textures.is_valid());
	if (textures.is_null()) {
		return;
	}
	if (textures != tile_list_source) {
		tile_list_source = textures;
		tile_list->clear();
		const int icon_size = int(48 * EDSCALE);
		const int count = MIN(textures->get_layers(), int(TerrainData::MAX_TILE_INDEX) + 1);
		for (int i = 0; i < count; i++) {
			Ref<Texture2D> icon;
			Ref<Image> image = textures->get_layer_data(i);
			if (image.is_valid() && !image->is_empty()) {
				image = image->duplicate();
				if (!image->is_compressed() || image->decompress() == OK) {
					image->clear_mipmaps();
					image->resize(icon_size, icon_size, Image::INTERPOLATE_BILINEAR);
					icon = ImageTexture::create_from_image(image);
				}
			}
			tile_list->add_item(itos(i), icon);
		}
	}
	const int index = int(tile_index->get_value());
	if (index < tile_list->get_item_count() && !tile_list->is_selected(index)) {
		tile_list->select(index);
		tile_list->ensure_current_is_visible();
	}
}

void TerrainEditorPlugin::_update_layer_list() {
	layer_list_queued = false;
	Ref<TerrainLayerMaterial> material;
	if (terrain && terrain->get_data().is_valid() && terrain->get_mode() == Terrain3D::MODE_LAYERS) {
		material = terrain->get_layer_material();
	}
	if (material != layer_list_material) {
		const Callable changed = callable_mp(this, &TerrainEditorPlugin::_layer_material_changed);
		if (layer_list_material.is_valid() && layer_list_material->is_connected(CoreStringName(changed), changed)) {
			layer_list_material->disconnect(CoreStringName(changed), changed);
		}
		layer_list_material = material;
		revealed_layer = -1;
		if (material.is_valid()) {
			material->connect(CoreStringName(changed), changed);
		}
		layer_list_dirty = true;
	}
	layer_column->set_visible(material.is_valid());
	if (material.is_null()) {
		return;
	}
	if (layer_list_dirty) {
		layer_list_dirty = false;
		layer_list->clear();
		const int icon_size = int(48 * EDSCALE);
		for (int i = 0; i < material->get_layer_count(); i++) {
			// The layer's texture with its tint, or the tint alone.
			Ref<Image> image;
			const Ref<Texture2D> albedo = material->get_layer_albedo(i);
			if (albedo.is_valid()) {
				image = albedo->get_image();
				if (image.is_valid() && !image->is_empty()) {
					image = image->duplicate();
					if (image->is_compressed() && image->decompress() != OK) {
						image.unref();
					}
				} else {
					image.unref();
				}
			}
			const Color tint = material->get_layer_color(i);
			if (image.is_valid()) {
				image->clear_mipmaps();
				image->convert(Image::FORMAT_RGB8);
				image->resize(icon_size, icon_size, Image::INTERPOLATE_BILINEAR);
				if (tint != Color(1, 1, 1)) {
					for (int y = 0; y < icon_size; y++) {
						for (int x = 0; x < icon_size; x++) {
							image->set_pixel(x, y, image->get_pixel(x, y) * tint);
						}
					}
				}
			} else {
				image = Image::create_empty(icon_size, icon_size, false, Image::FORMAT_RGB8);
				image->fill(tint);
			}
			layer_list->add_item(itos(i + 1), ImageTexture::create_from_image(image));
			if (albedo.is_valid() && albedo->get_path().is_resource_file()) {
				layer_list->set_item_tooltip(i, albedo->get_path());
			}
		}
	}
	const int selected = layer->get_selected();
	if (selected >= 0 && selected < layer_list->get_item_count() && !layer_list->is_selected(selected)) {
		layer_list->select(selected);
		layer_list->ensure_current_is_visible();
	}
}

void TerrainEditorPlugin::_layer_material_changed() {
	// A texture or a tint was changed, or layers were added; several changes make one refresh.
	layer_list_dirty = true;
	if (!layer_list_queued) {
		layer_list_queued = true;
		callable_mp(this, &TerrainEditorPlugin::_update_panel).call_deferred();
	}
}

// Opens the layer's settings in the inspector, where the material is shown, and closes those of
// the layer that was open before.
void TerrainEditorPlugin::_reveal_layer(int p_index) {
	if (layer_list_material.is_null() || p_index == revealed_layer) {
		return;
	}
	if (revealed_layer >= 0) {
		layer_list_material->editor_set_section_unfold(vformat("Layer %d", revealed_layer + 1), false);
	}
	revealed_layer = p_index;
	layer_list_material->editor_set_section_unfold(vformat("Layer %d", p_index + 1), true);
	layer_list_material->notify_property_list_changed();
}

void TerrainEditorPlugin::_layer_list_selected(int p_index) {
	_reveal_layer(p_index);
	if (p_index < layer->get_item_count()) {
		layer->select(p_index);
	}
	// Picking a layer is picking what to paint.
	if (!tool_buttons[TOOL_PAINT_LAYER]->is_disabled()) {
		_select_tool(TOOL_PAINT_LAYER);
	}
}

void TerrainEditorPlugin::_layer_list_clicked(int p_index, const Vector2 &p_position, int p_button) {
	if (p_button != int(MouseButton::RIGHT) || layer_list_material.is_null() || p_index >= layer_list_material->get_layer_count()) {
		return;
	}
	layer_menu_layer = p_index;
	// A texture made by the conversion from tiles lives in the scene and has no file to show.
	const Ref<Texture2D> albedo = layer_list_material->get_layer_albedo(p_index);
	const bool has_file = albedo.is_valid() && albedo->get_path().is_resource_file();
	layer_menu->set_item_disabled(layer_menu->get_item_index(TILE_MENU_SHOW_IN_FILE_MANAGER), !has_file);
	layer_menu->set_item_disabled(layer_menu->get_item_index(TILE_MENU_SHOW_IN_FILESYSTEM), !has_file);
	layer_menu->set_position(layer_list->get_screen_position() + p_position);
	layer_menu->reset_size();
	layer_menu->popup();
}

// Moves the 3D view to a place given in grid units.
void TerrainEditorPlugin::_focus_view(const Vector2 &p_grid) {
	const Ref<TerrainData> data = terrain->get_data();
	const float size = data->get_cell_size();
	const Vector3 position = terrain->get_global_transform().xform(Vector3(p_grid.x * size, data->sample_height(p_grid), -p_grid.y * size));
	Node3DEditorViewport *viewport = Node3DEditor::get_singleton()->get_last_used_viewport();
	if (viewport) {
		Dictionary state;
		state["position"] = position;
		state["distance"] = size * 8.0f;
		viewport->set_state(state);
	}
}

void TerrainEditorPlugin::_show_layer_in_terrain(int p_layer) {
	if (!terrain || terrain->get_data().is_null()) {
		return;
	}
	const Ref<TerrainData> data = terrain->get_data();
	const int size = data->get_size();
	const uint8_t *weights = data->weights_ptr();
	// The places where the layer is what is mostly seen.
	Vector<int> found;
	for (int i = 0; i < size * size; i++) {
		if (TerrainData::get_sample_weight(weights + i * TerrainData::WEIGHT_STRIDE, p_layer) >= 128) {
			found.push_back(i);
		}
	}
	if (found.is_empty()) {
		status->set_text(vformat(TTR("Layer %d is not painted anywhere."), p_layer + 1));
		return;
	}
	const int sample = found[Math::rand() % found.size()];
	_focus_view(Vector2(sample % size, sample / size));
	status->set_text(vformat(TTR("Layer %d at (%d, %d), one of %d places painted with it."), p_layer + 1, sample % size, sample / size, found.size()));
}

void TerrainEditorPlugin::_replace_layer_confirmed() {
	if (layer_list_material.is_null() || replace_layer >= layer_list_material->get_layer_count() || replace_path.is_empty()) {
		return;
	}
	// The layer refers to the image as a file of the project, at the image's own size.
	String path = ProjectSettings::get_singleton()->localize_path(replace_path);
	if (!path.begins_with("res://")) {
		String folder = "res://";
		if (terrain && terrain->get_data().is_valid() && terrain->get_data()->get_path().is_resource_file()) {
			folder = terrain->get_data()->get_path().get_base_dir();
		}
		folder = folder.path_join("layers");
		path = folder.path_join(replace_path.get_file());
		const Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_RESOURCES);
		if (dir->make_dir_recursive(folder) != OK || dir->copy(replace_path, path) != OK) {
			status->set_text(vformat(TTR("Cannot copy %s to %s."), replace_path, path));
			return;
		}
		EditorFileSystem::get_singleton()->update_file(path);
		EditorFileSystem::get_singleton()->reimport_files({ path });
	}
	const Ref<Texture2D> texture = ResourceLoader::load(path);
	if (texture.is_null()) {
		status->set_text(vformat(TTR("Cannot load %s as a texture."), path));
		return;
	}
	const String property = vformat("layer_%d/albedo", replace_layer + 1);
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action_for_history(TTR("Replace Terrain Layer Texture"), ur->get_history_id_for_object(terrain));
	// The material may be a file of its own, which would otherwise put it in another history than the node.
	ur->force_fixed_history();
	ur->add_do_property(layer_list_material.ptr(), property, texture);
	ur->add_undo_property(layer_list_material.ptr(), property, layer_list_material->get_layer_albedo(replace_layer));
	ur->commit_action();
	status->set_text(vformat(TTR("Layer %d now shows %s."), replace_layer + 1, path));
}

void TerrainEditorPlugin::_layer_menu_pressed(int p_id) {
	if (layer_list_material.is_null() || layer_menu_layer >= layer_list_material->get_layer_count()) {
		return;
	}
	if (p_id == TILE_MENU_SHOW_IN_TERRAIN) {
		_show_layer_in_terrain(layer_menu_layer);
		return;
	}
	if (p_id == TILE_MENU_REPLACE) {
		replace_layer = layer_menu_layer;
		replace_image.unref();
		replace_path = String();
		replace_preview->set_texture(Ref<Texture2D>());
		replace_hint->set_text(TTR("Drop an image here"));
		replace_hint->show();
		replace_note->set_text(TTR("The image becomes the layer's colour texture. A file from outside the project is copied into it, next to the terrain's data."));
		replace_dialog->set_title(vformat(TTR("Replace the Texture of Layer %d"), replace_layer + 1));
		replace_dialog->get_ok_button()->set_disabled(true);
		replace_dialog->popup_centered();
		return;
	}
	const Ref<Texture2D> albedo = layer_list_material->get_layer_albedo(layer_menu_layer);
	if (albedo.is_null() || !albedo->get_path().is_resource_file()) {
		return;
	}
	if (p_id == TILE_MENU_SHOW_IN_FILE_MANAGER) {
		OS::get_singleton()->shell_show_in_file_manager(ProjectSettings::get_singleton()->globalize_path(albedo->get_path()), true);
	} else if (p_id == TILE_MENU_SHOW_IN_FILESYSTEM) {
		FileSystemDock::get_singleton()->navigate_to_path(albedo->get_path());
	}
}

void TerrainEditorPlugin::_layer_option_selected(int p_index) {
	_reveal_layer(p_index);
	if (p_index < layer_list->get_item_count()) {
		layer_list->select(p_index);
		layer_list->ensure_current_is_visible();
	}
}

void TerrainEditorPlugin::_tile_list_selected(int p_index) {
	tile_index->set_value(p_index);
}

void TerrainEditorPlugin::_tile_list_clicked(int p_index, const Vector2 &p_position, int p_button) {
	if (p_button != int(MouseButton::RIGHT)) {
		return;
	}
	tile_index->set_value(p_index);
	// Every tile is a layer of one texture array, so they all lead to the same file.
	const bool has_file = tile_list_source.is_valid() && tile_list_source->get_path().is_resource_file();
	tile_menu->set_item_disabled(tile_menu->get_item_index(TILE_MENU_SHOW_IN_FILE_MANAGER), !has_file);
	tile_menu->set_item_disabled(tile_menu->get_item_index(TILE_MENU_SHOW_IN_FILESYSTEM), !has_file);
	tile_menu->set_position(tile_list->get_screen_position() + p_position);
	tile_menu->reset_size();
	tile_menu->popup();
}

void TerrainEditorPlugin::_show_tile_in_terrain(int p_tile) {
	if (!terrain || terrain->get_data().is_null()) {
		return;
	}
	const Ref<TerrainData> data = terrain->get_data();
	const int cells = data->get_cells();
	const uint8_t *tiles = data->tiles_ptr();
	Vector<int> found;
	for (int i = 0; i < cells * cells; i++) {
		const uint8_t *t = tiles + i * 4;
		const bool first = (t[3] & 16) && (t[0] | ((t[3] & 3) << 8)) == p_tile;
		const bool second = (t[3] & 16) && (t[3] & 32) && (t[1] | (((t[3] >> 2) & 3) << 8)) == p_tile;
		if (first || second) {
			found.push_back(i);
		}
	}
	if (found.is_empty()) {
		status->set_text(vformat(TTR("Tile %d is not used by any cell."), p_tile));
		return;
	}
	const int cell = found[Math::rand() % found.size()];
	const int x = cell % cells;
	const int z = cell / cells;
	const float size = data->get_cell_size();
	const Vector2 grid(x + 0.5f, z + 0.5f);
	const Vector3 position = terrain->get_global_transform().xform(Vector3(grid.x * size, data->sample_height(grid), -grid.y * size));
	Node3DEditorViewport *viewport = Node3DEditor::get_singleton()->get_last_used_viewport();
	if (viewport) {
		Dictionary state;
		state["position"] = position;
		state["distance"] = size * 8.0f;
		viewport->set_state(state);
	}
	status->set_text(vformat(TTR("Tile %d at cell (%d, %d), one of %d cells that use it."), p_tile, x, z, found.size()));
}

void TerrainEditorPlugin::_replace_tile_popup(int p_tile) {
	replace_tile = p_tile;
	replace_layer = -1;
	replace_note->set_text(TTR("The image is resized to the tiles' size. Replacing rewrites the tile texture file and cannot be undone."));
	replace_image.unref();
	replace_preview->set_texture(Ref<Texture2D>());
	replace_hint->set_text(TTR("Drop an image here"));
	replace_hint->show();
	replace_dialog->set_title(vformat(TTR("Replace Tile %d"), p_tile));
	replace_dialog->get_ok_button()->set_disabled(true);
	replace_dialog->popup_centered();
}

void TerrainEditorPlugin::_replace_browse_pressed() {
	replace_file_dialog->popup_file_dialog();
}

void TerrainEditorPlugin::_replace_files_dropped(const PackedStringArray &p_files) {
	if (!p_files.is_empty()) {
		_replace_file_selected(p_files[0]);
	}
}

void TerrainEditorPlugin::_replace_file_selected(const String &p_path) {
	replace_path = p_path;
	const Ref<Image> image = Image::load_from_file(p_path);
	if (image.is_null() || image->is_empty() || (image->is_compressed() && image->decompress() != OK)) {
		replace_image.unref();
		replace_preview->set_texture(Ref<Texture2D>());
		replace_hint->set_text(vformat(TTR("Cannot read %s as an image."), p_path.get_file()));
		replace_hint->show();
		replace_dialog->get_ok_button()->set_disabled(true);
		return;
	}
	replace_image = image;
	replace_preview->set_texture(ImageTexture::create_from_image(image));
	replace_hint->hide();
	replace_dialog->get_ok_button()->set_disabled(false);
}

// Writes a texture array as a DDS file, every level of the first layer, then of the next.
Error TerrainEditorPlugin::_save_tile_array(const Ref<ImageTextureLayered> &p_textures, const String &p_path) {
	uint32_t dxgi = 0;
	switch (p_textures->get_format()) {
		case Image::FORMAT_DXT1:
			dxgi = 71; // DXGI_FORMAT_BC1_UNORM
			break;
		case Image::FORMAT_DXT3:
			dxgi = 74; // DXGI_FORMAT_BC2_UNORM
			break;
		case Image::FORMAT_DXT5:
			dxgi = 77; // DXGI_FORMAT_BC3_UNORM
			break;
		case Image::FORMAT_RGBA8:
			dxgi = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
			break;
		default:
			return ERR_UNAVAILABLE;
	}
	Vector<Ref<Image>> layers;
	for (int i = 0; i < p_textures->get_layers(); i++) {
		const Ref<Image> layer = p_textures->get_layer_data(i);
		ERR_FAIL_COND_V(layer.is_null(), ERR_INVALID_DATA);
		layers.push_back(layer);
	}
	ERR_FAIL_COND_V(layers.is_empty(), ERR_INVALID_DATA);
	const bool compressed = dxgi != 28;
	const int levels = layers[0]->get_mipmap_count() + 1;
	const uint32_t width = p_textures->get_width();
	const uint32_t height = p_textures->get_height();
	const uint32_t first_level = compressed ? MAX(1u, width / 4) * MAX(1u, height / 4) * (dxgi == 71 ? 8 : 16) : width * 4;

	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	if (file.is_null()) {
		return err;
	}
	file->store_buffer((const uint8_t *)"DDS ", 4);
	file->store_32(124);
	// Caps, height, width and pixel format, the size of the first level, and the level count.
	file->store_32(0x1 | 0x2 | 0x4 | 0x1000 | (compressed ? 0x80000 : 0x8) | (levels > 1 ? 0x20000 : 0));
	file->store_32(height);
	file->store_32(width);
	file->store_32(first_level);
	file->store_32(0);
	file->store_32(levels > 1 ? levels : 0);
	for (int i = 0; i < 11; i++) {
		file->store_32(0);
	}
	file->store_32(32);
	file->store_32(0x4);
	file->store_buffer((const uint8_t *)"DX10", 4);
	for (int i = 0; i < 5; i++) {
		file->store_32(0);
	}
	file->store_32(0x1000 | (levels > 1 ? 0x400008 : 0));
	for (int i = 0; i < 4; i++) {
		file->store_32(0);
	}
	file->store_32(dxgi);
	file->store_32(3); // D3D10_RESOURCE_DIMENSION_TEXTURE2D
	file->store_32(0);
	file->store_32(layers.size());
	file->store_32(0);
	for (const Ref<Image> &layer : layers) {
		file->store_buffer(layer->get_data());
	}
	return file->get_error();
}

void TerrainEditorPlugin::_replace_confirmed() {
	if (replace_layer >= 0) {
		_replace_layer_confirmed();
		return;
	}
	const Ref<ImageTextureLayered> textures = tile_list_source;
	if (replace_image.is_null() || textures.is_null() || replace_tile >= textures->get_layers()) {
		return;
	}
	// The image takes the size, the levels and the format of the layers it joins.
	Ref<Image> image = replace_image->duplicate();
	const Image::Format format = textures->get_format();
	image->convert(format == Image::FORMAT_DXT1 ? Image::FORMAT_RGB8 : Image::FORMAT_RGBA8);
	image->resize(textures->get_width(), textures->get_height(), Image::INTERPOLATE_LANCZOS);
	if (textures->has_mipmaps()) {
		image->generate_mipmaps();
	}
	if (format == Image::FORMAT_DXT1 || format == Image::FORMAT_DXT5) {
		image->compress_from_channels(Image::COMPRESS_S3TC, format == Image::FORMAT_DXT1 ? Image::USED_CHANNELS_RGB : Image::USED_CHANNELS_RGBA);
	} else if (!Image::is_format_compressed(format)) {
		image->convert(format);
	}
	if (image->get_format() != format) {
		status->set_text(vformat(TTR("The tile textures are stored as %s, which an image cannot be turned into here."), Image::get_format_name(format)));
		return;
	}
	textures->update_layer(image, replace_tile);

	String saved;
	const String path = textures->get_path();
	if (path.is_resource_file()) {
		const Error err = path.has_extension("dds") ? _save_tile_array(textures, path) : ResourceSaver::save(textures, path);
		if (err == OK) {
			saved = " " + vformat(TTR("Saved to %s."), path);
		} else {
			saved = " " + vformat(TTR("It could not be saved to %s, so the change lasts until the editor closes."), path);
		}
	}
	// The list keeps small copies of the tiles; make them again.
	tile_list_source.unref();
	_update_panel();
	status->set_text(vformat(TTR("Replaced the texture of tile %d."), replace_tile) + saved);
}

void TerrainEditorPlugin::_tile_menu_pressed(int p_id) {
	if (p_id == TILE_MENU_REPLACE) {
		_replace_tile_popup(int(tile_index->get_value()));
		return;
	}
	if (p_id == TILE_MENU_SHOW_IN_TERRAIN) {
		_show_tile_in_terrain(int(tile_index->get_value()));
		return;
	}
	if (tile_list_source.is_null() || !tile_list_source->get_path().is_resource_file()) {
		return;
	}
	const String path = tile_list_source->get_path();
	if (p_id == TILE_MENU_SHOW_IN_FILE_MANAGER) {
		OS::get_singleton()->shell_show_in_file_manager(ProjectSettings::get_singleton()->globalize_path(path), true);
	} else if (p_id == TILE_MENU_SHOW_IN_FILESYSTEM) {
		FileSystemDock::get_singleton()->navigate_to_path(path);
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
	bake_button->set_disabled(!terrain);
	convert_button->set_disabled(!has_data || !tiles || terrain->get_tile_material().is_null());
	layer->set_disabled(!has_data || tiles);
	{
		// One entry for each layer of the terrain's material.
		const int layers = terrain && terrain->get_layer_material().is_valid() ? terrain->get_layer_material()->get_layer_count() : 8;
		if (layer->get_item_count() != layers) {
			const int selected = MAX(layer->get_selected(), 0);
			layer->clear();
			for (int i = 0; i < layers; i++) {
				layer->add_item(vformat(TTR("Layer %d"), i + 1));
			}
			layer->select(MIN(selected, layers - 1));
		}
	}
	tile_index->set_editable(has_data && tiles);
	tile_slot->set_disabled(!has_data || !tiles);
	tile_direction->set_disabled(!has_data || !tiles);
	_update_tile_list();
	_update_layer_list();
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
	if (tile_column->is_visible()) {
		const int listed = int(tile_index->get_value());
		if (listed < tile_list->get_item_count() && !tile_list->is_selected(listed)) {
			tile_list->select(listed);
			tile_list->ensure_current_is_visible();
		}
	}
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
	ur->create_action_for_history(TTR("Change Terrain Mode"), ur->get_history_id_for_object(terrain));
	// The data may be a file of its own, which would otherwise put it in another history than the node.
	ur->force_fixed_history();
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
		ur->create_action_for_history(TTR("Create Terrain Data"), ur->get_history_id_for_object(terrain));
		// The data may be a file of its own, which would otherwise put it in another history than the node.
		ur->force_fixed_history();
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
		ur->create_action_for_history(TTR("Resize Terrain"), ur->get_history_id_for_object(terrain));
		// The data may be a file of its own, which would otherwise put it in another history than the node.
		ur->force_fixed_history();
		ur->add_do_property(data.ptr(), "cell_size", cell);
		ur->add_do_property(data.ptr(), "size", cells + 1);
		ur->add_undo_property(data.ptr(), "cell_size", data->get_cell_size());
		ur->add_undo_property(data.ptr(), "size", data->get_size());
		ur->add_undo_property(data.ptr(), "heights", data->get_heights());
		ur->add_undo_property(data.ptr(), "tiles", data->get_tiles());
		ur->add_undo_property(data.ptr(), "layer_weights", data->get_weights());
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
	ur->create_action_for_history(TTR("Import Terrain Heightmap"), ur->get_history_id_for_object(terrain));
	// The data may be a file of its own, which would otherwise put it in another history than the node.
	ur->force_fixed_history();
	ur->add_do_property(data.ptr(), "heights", data->get_heights());
	ur->add_undo_property(data.ptr(), "heights", before);
	ur->commit_action(false);
}

void TerrainEditorPlugin::_convert_pressed() {
	if (!terrain || terrain->get_data().is_null()) {
		return;
	}
	Vector<uint8_t> weights;
	Ref<TerrainLayerMaterial> material;
	if (!terrain->build_layers_from_tiles(weights, material)) {
		status->set_text(TTR("Nothing to convert: the terrain has no tiles or no tile textures."));
		return;
	}
	const Ref<TerrainData> data = terrain->get_data();
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action_for_history(TTR("Convert Terrain Tiles to Layers"), ur->get_history_id_for_object(terrain));
	// The data may be a file of its own, which would otherwise put it in another history than the node.
	ur->force_fixed_history();
	ur->add_do_property(data.ptr(), "layer_weights", weights);
	ur->add_undo_property(data.ptr(), "layer_weights", data->get_weights());
	ur->add_do_property(terrain, "layer_material", material);
	ur->add_undo_property(terrain, "layer_material", terrain->get_layer_material());
	ur->add_do_property(terrain, "mode", Terrain3D::MODE_LAYERS);
	ur->add_undo_property(terrain, "mode", terrain->get_mode());
	ur->add_do_method(this, "_update_panel");
	ur->add_undo_method(this, "_update_panel");
	ur->commit_action();
}

void TerrainEditorPlugin::_bake_collision_pressed() {
	if (!terrain) {
		return;
	}
	const Ref<ConcavePolygonShape3D> before = terrain->get_collision_baked_shape();
	const int triangles = terrain->bake_collision();
	if (triangles < 0) {
		status->set_text(TTR("Nothing to bake: set Collision > Bake Source of the terrain to the node that holds the objects."));
		return;
	}
	const Ref<ConcavePolygonShape3D> baked = terrain->get_collision_baked_shape();
	// Many triangles make a scene file slow to load as text, so the shape goes into a file of its
	// own next to the terrain's data when that has one.
	String path = baked->get_path();
	if (!path.is_resource_file() && terrain->get_data().is_valid() && terrain->get_data()->get_path().is_resource_file()) {
		path = terrain->get_data()->get_path().get_basename() + "_collision.res";
	}
	String where;
	if (path.is_resource_file()) {
		if (ResourceSaver::save(baked, path, ResourceSaver::FLAG_CHANGE_PATH | ResourceSaver::FLAG_COMPRESS) == OK) {
			where = " " + vformat(TTR("Saved as %s."), path);
		} else {
			ERR_PRINT(vformat("Terrain: cannot save the baked collision to '%s'.", path));
		}
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(TTR("Bake Terrain Collision"));
	ur->add_do_property(terrain, "collision_baked_shape", baked);
	ur->add_undo_property(terrain, "collision_baked_shape", before);
	ur->commit_action();
	status->set_text(vformat(TTR("Baked %d collision triangles."), triangles) + where);
}

void TerrainEditorPlugin::_terrain_exiting() {
	edit(nullptr);
}

float TerrainEditorPlugin::_brush_distance(const Vector2 &p_offset) const {
	if (shape->get_selected() == 1) {
		return MAX(Math::abs(p_offset.x), Math::abs(p_offset.y));
	}
	return p_offset.length();
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
				Vector3 direction(Math::cos(angle), 0.0, Math::sin(angle));
				if (shape->get_selected() == 1) {
					// Pushed out onto the square around the circle.
					direction /= MAX(Math::abs(direction.x), Math::abs(direction.z));
				}
				Vector3 p = brush_position + direction * pass_radius;
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
			const float f = _falloff(_brush_distance(Vector2(x - centre.x, z - centre.y)) * cell);
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
			const float f = _falloff(_brush_distance(Vector2(x - centre.x, z - centre.y)) * cell);
			if (f <= 0.0f) {
				continue;
			}
			uint8_t *sample = weights + (z * size + x) * TerrainData::WEIGHT_STRIDE;
			const float amount = MIN(power * f * p_delta * 8.0f, 1.0f);
			const int now = TerrainData::get_sample_weight(sample, target);
			// At least one step, or a weak brush would never arrive.
			const int wanted = MIN(255, MAX(int(Math::round((now / 255.0f + (1.0f - now / 255.0f) * amount) * 255.0f)), now + 1));
			TerrainData::set_sample_weight(sample, target, wanted);
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
			if (!under_cursor && _brush_distance(Vector2(x + 0.5f - centre.x, z + 0.5f - centre.y)) * cell > r) {
				continue;
			}
			uint8_t *t = tiles + (z * cells + x) * 4;
			uint8_t next[4] = { t[0], t[1], t[2], t[3] };
			// The two tiles of a cell are added together, so the first one is the cell's ground and
			// the second an overlay on it. Painting or erasing the ground takes the overlay with it;
			// left in place it would brighten whatever is painted next.
			if (erase) {
				next[3] &= slot == 0 ? ~(16 | 32) : ~32;
			} else if (slot == 0) {
				next[0] = index & 255;
				next[2] = (next[2] & 0x38) | direction;
				next[3] = (next[3] & ~(0x13 | 32)) | (index >> 8) | 16;
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
	ur->create_action_for_history(vformat(TTR("Terrain: %s"), TTR(tool_names[stroke_tool])), ur->get_history_id_for_object(terrain));
	// The data may be a file of its own, which would otherwise put it in another history than the node.
	ur->force_fixed_history();
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
		const int stride = tiles ? 4 : int(TerrainData::WEIGHT_STRIDE);
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
	// Alt and the right button pick up the tile under the cursor: it becomes the tile to paint,
	// with its orientation, and Paint Tile becomes the tool.
	if (button.is_valid() && button->get_button_index() == MouseButton::RIGHT && button->is_alt_pressed() && terrain->get_mode() == Terrain3D::MODE_TILES) {
		if (button->is_pressed() && !stroking && _update_brush(p_camera, button->get_position())) {
			const Ref<TerrainData> data = terrain->get_data();
			const Vector2 grid = terrain->to_grid(brush_position);
			const int x = CLAMP(int(Math::floor(grid.x)), 0, data->get_cells() - 1);
			const int z = CLAMP(int(Math::floor(grid.y)), 0, data->get_cells() - 1);
			// The slot being painted, or the other one if the cell does not use it.
			int slot = tile_slot->get_selected();
			if (!data->is_tile_used(x, z, slot) && data->is_tile_used(x, z, 1 - slot)) {
				slot = 1 - slot;
			}
			if (data->is_tile_used(x, z, slot)) {
				tile_slot->select(slot);
				tile_direction->select(data->get_tile_direction(x, z, slot));
				_select_tool(TOOL_PAINT_TILE);
				tile_index->set_value(data->get_tile_index(x, z, slot));
			}
		}
		return AFTER_GUI_INPUT_STOP;
	}
	// In layers mode they pick up the layer under the cursor: the strongest one of the nearest
	// sample becomes the layer to paint, and Paint Layer becomes the tool.
	if (button.is_valid() && button->get_button_index() == MouseButton::RIGHT && button->is_alt_pressed() && terrain->get_mode() == Terrain3D::MODE_LAYERS) {
		if (button->is_pressed() && !stroking && _update_brush(p_camera, button->get_position())) {
			const Ref<TerrainData> data = terrain->get_data();
			const Vector2 grid = terrain->to_grid(brush_position);
			const int x = CLAMP(int(Math::round(grid.x)), 0, data->get_size() - 1);
			const int z = CLAMP(int(Math::round(grid.y)), 0, data->get_size() - 1);
			const uint8_t *sample = data->weights_ptr() + (z * data->get_size() + x) * TerrainData::WEIGHT_STRIDE;
			int strongest = 0;
			for (int i = 1; i < TerrainData::LAYER_BLEND; i++) {
				if (sample[TerrainData::LAYER_BLEND + i] > sample[TerrainData::LAYER_BLEND + strongest]) {
					strongest = i;
				}
			}
			const int picked = sample[strongest];
			if (picked < layer->get_item_count()) {
				layer->select(picked);
				_layer_option_selected(picked);
				if (!tool_buttons[TOOL_PAINT_LAYER]->is_disabled()) {
					_select_tool(TOOL_PAINT_LAYER);
				}
			}
		}
		return AFTER_GUI_INPUT_STOP;
	}
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
	brush->add_child(memnew(Label(TTR("Shape"))));
	shape = memnew(OptionButton);
	shape->add_item(TTR("Round"));
	shape->add_item(TTR("Square"));
	shape->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	brush->add_child(shape);
	height = _add_spin(brush, TTR("Height"), -10000, 10000, 0.01, 0, "m");

	column = _add_column(panel);

	GridContainer *paint = memnew(GridContainer);
	paint->set_columns(2);
	column->add_child(paint);
	paint->add_child(memnew(Label(TTR("Layer"))));
	layer = memnew(OptionButton);
	layer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	paint->add_child(layer);

	tile_index = _add_spin(paint, TTR("Tile"), 0, TerrainData::MAX_TILE_INDEX, 1, 0);
	tile_index->connect(SceneStringName(value_changed), callable_mp(this, &TerrainEditorPlugin::_update_tile_preview).unbind(1));
	paint->add_child(memnew(Label(TTR("Paint As"))));
	tile_slot = memnew(OptionButton);
	tile_slot->add_item(TTR("Ground"));
	tile_slot->add_item(TTR("Overlay (added)"));
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

	convert_button = memnew(Button(TTR("Convert Tiles to Layers")));
	convert_button->set_tooltip_text(TTR("Turns the tiles into painted layers: every kind of ground becomes a layer with its tile as the texture.\nThe tiles are kept, so the Texturing selector brings them back."));
	convert_button->connect(SceneStringName(pressed), callable_mp(this, &TerrainEditorPlugin::_convert_pressed));
	column->add_child(convert_button);

	bake_button = memnew(Button(TTR("Bake Collision")));
	bake_button->set_tooltip_text(TTR("Turns the meshes below the terrain's Bake Source into collision that is kept with the terrain.\nNodes in the group \"no_collision\" are left out."));
	bake_button->connect(SceneStringName(pressed), callable_mp(this, &TerrainEditorPlugin::_bake_collision_pressed));
	column->add_child(bake_button);

	import_dialog = memnew(EditorFileDialog);
	import_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
	import_dialog->set_access(FileDialog::ACCESS_FILESYSTEM);
	import_dialog->add_filter("*.png,*.exr,*.jpg,*.jpeg,*.tga,*.bmp,*.webp", TTR("Images"));
	import_dialog->connect("file_selected", callable_mp(this, &TerrainEditorPlugin::_import_file_selected));
	column->add_child(import_dialog);

	// Every tile of the terrain's texture array, shown while Paint Tile is the tool.
	tile_column = _add_column(panel);
	tile_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tile_column->add_child(memnew(Label(TTR("Tiles"))));
	tile_list = memnew(ItemList);
	tile_list->set_icon_mode(ItemList::ICON_MODE_TOP);
	tile_list->set_max_columns(0);
	tile_list->set_same_column_width(true);
	tile_list->set_fixed_icon_size(Size2i(int(48 * EDSCALE), int(48 * EDSCALE)));
	tile_list->set_custom_minimum_size(Size2(320, 120) * EDSCALE);
	tile_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tile_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tile_list->connect(SceneStringName(item_selected), callable_mp(this, &TerrainEditorPlugin::_tile_list_selected));
	tile_list->set_allow_rmb_select(true);
	tile_list->connect("item_clicked", callable_mp(this, &TerrainEditorPlugin::_tile_list_clicked));
	tile_column->add_child(tile_list);

	tile_menu = memnew(PopupMenu);
	tile_menu->add_item(TTR("Show in File Manager"), TILE_MENU_SHOW_IN_FILE_MANAGER);
	tile_menu->add_item(TTR("Show in FileSystem"), TILE_MENU_SHOW_IN_FILESYSTEM);
	tile_menu->add_separator();
	tile_menu->add_item(TTR("Show in Terrain (Random)"), TILE_MENU_SHOW_IN_TERRAIN);
	tile_menu->add_separator();
	tile_menu->add_item(TTR("Replace..."), TILE_MENU_REPLACE);

	replace_dialog = memnew(ConfirmationDialog);
	replace_dialog->set_ok_button_text(TTR("Replace"));
	replace_dialog->connect(SceneStringName(confirmed), callable_mp(this, &TerrainEditorPlugin::_replace_confirmed));
	replace_dialog->connect("files_dropped", callable_mp(this, &TerrainEditorPlugin::_replace_files_dropped));
	panel->add_child(replace_dialog);
	VBoxContainer *replace_box = memnew(VBoxContainer);
	replace_dialog->add_child(replace_box);
	PanelContainer *replace_area = memnew(PanelContainer);
	replace_area->set_custom_minimum_size(Size2(320, 320) * EDSCALE);
	replace_box->add_child(replace_area);
	replace_preview = memnew(TextureRect);
	replace_preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	replace_preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	replace_area->add_child(replace_preview);
	replace_hint = memnew(Label);
	replace_hint->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	replace_hint->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	replace_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	replace_area->add_child(replace_hint);
	Button *replace_browse = memnew(Button(TTR("Browse...")));
	replace_browse->connect(SceneStringName(pressed), callable_mp(this, &TerrainEditorPlugin::_replace_browse_pressed));
	replace_box->add_child(replace_browse);
	replace_note = memnew(Label);
	replace_note->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	replace_note->set_custom_minimum_size(Size2(320, 0) * EDSCALE);
	replace_box->add_child(replace_note);

	replace_file_dialog = memnew(EditorFileDialog);
	replace_file_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
	replace_file_dialog->set_access(FileDialog::ACCESS_FILESYSTEM);
	replace_file_dialog->add_filter("*.png,*.jpg,*.jpeg,*.tga,*.bmp,*.webp,*.dds", TTR("Images"));
	replace_file_dialog->connect("file_selected", callable_mp(this, &TerrainEditorPlugin::_replace_file_selected));
	replace_dialog->add_child(replace_file_dialog);
	tile_menu->connect(SceneStringName(id_pressed), callable_mp(this, &TerrainEditorPlugin::_tile_menu_pressed));
	tile_column->add_child(tile_menu);
	tile_column->hide();

	// Every layer of the terrain's material, shown while the terrain is in layers mode.
	layer_column = _add_column(panel);
	layer_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	layer_column->add_child(memnew(Label(TTR("Layers"))));
	layer_list = memnew(ItemList);
	layer_list->set_icon_mode(ItemList::ICON_MODE_TOP);
	layer_list->set_max_columns(0);
	layer_list->set_same_column_width(true);
	layer_list->set_fixed_icon_size(Size2i(int(48 * EDSCALE), int(48 * EDSCALE)));
	layer_list->set_custom_minimum_size(Size2(320, 120) * EDSCALE);
	layer_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	layer_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	layer_list->connect(SceneStringName(item_selected), callable_mp(this, &TerrainEditorPlugin::_layer_list_selected));
	layer_list->set_allow_rmb_select(true);
	layer_list->connect("item_clicked", callable_mp(this, &TerrainEditorPlugin::_layer_list_clicked));
	layer_column->add_child(layer_list);

	layer_menu = memnew(PopupMenu);
	layer_menu->add_item(TTR("Show in File Manager"), TILE_MENU_SHOW_IN_FILE_MANAGER);
	layer_menu->add_item(TTR("Show in FileSystem"), TILE_MENU_SHOW_IN_FILESYSTEM);
	layer_menu->add_separator();
	layer_menu->add_item(TTR("Show in Terrain (Random)"), TILE_MENU_SHOW_IN_TERRAIN);
	layer_menu->add_separator();
	layer_menu->add_item(TTR("Replace..."), TILE_MENU_REPLACE);
	layer_menu->connect(SceneStringName(id_pressed), callable_mp(this, &TerrainEditorPlugin::_layer_menu_pressed));
	layer_column->add_child(layer_menu);
	layer_column->hide();
	layer->connect(SceneStringName(item_selected), callable_mp(this, &TerrainEditorPlugin::_layer_option_selected));

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
