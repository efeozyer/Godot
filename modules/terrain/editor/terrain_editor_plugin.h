#pragma once

#include "../terrain_3d.h"

#include "editor/plugins/editor_plugin.h"

class Button;
class ConfirmationDialog;
class EditorDock;
class EditorFileDialog;
class ItemList;
class Label;
class OptionButton;
class PopupMenu;
class SpinBox;
class StandardMaterial3D;
class TextureRect;
class HBoxContainer;
class VBoxContainer;

// Sculpting and painting for Terrain3D in the 3D editor.
//
// The tools and the brush are in the "Terrain Editor" tab of the bottom panel. With a Terrain3D
// selected, the left mouse button applies the tool under the cursor for as long as it is held.
//
//   Raise / Lower   moves the ground up or down (Ctrl swaps the two)
//   Smooth          evens out the ground (also Shift with any sculpting tool)
//   Flatten         pulls the ground to the height the stroke started on
//   Set Height      pulls the ground to the height typed in the panel
//   Paint Layer     layers mode: paints the chosen material layer
//   Paint Tile      tiles mode: puts the chosen tile, in the chosen orientation, on the cell. A
//                   cell shows two tiles added together: its ground, and an overlay for the
//                   tile set's pre-masked edge pieces. Painting the ground replaces both; painting
//                   an overlay adds to the ground that is there. The tiles are listed at the right
//                   of the tab while this tool is active; clicking one chooses it.
//   Erase Tile      tiles mode: removes the overlay, or the ground together with its overlay
//
// Every stroke is one undo step.
class TerrainEditorPlugin : public EditorPlugin {
	GDCLASS(TerrainEditorPlugin, EditorPlugin);

	enum Tool {
		TOOL_RAISE,
		TOOL_LOWER,
		TOOL_SMOOTH,
		TOOL_FLATTEN,
		TOOL_SET_HEIGHT,
		TOOL_PAINT_LAYER,
		TOOL_PAINT_TILE,
		TOOL_ERASE_TILE,
		TOOL_MAX
	};

	Terrain3D *terrain = nullptr;

	EditorDock *dock = nullptr;
	HBoxContainer *panel = nullptr;
	bool active = false;
	Button *tool_buttons[TOOL_MAX] = {};
	Label *status = nullptr;
	SpinBox *radius = nullptr;
	SpinBox *strength = nullptr;
	SpinBox *hardness = nullptr;
	OptionButton *shape = nullptr;
	SpinBox *height = nullptr;
	OptionButton *layer = nullptr;
	SpinBox *tile_index = nullptr;
	OptionButton *tile_slot = nullptr;
	OptionButton *tile_direction = nullptr;
	TextureRect *tile_preview = nullptr;
	VBoxContainer *tile_column = nullptr;
	// The layers of the terrain's material with their textures, shown in layers mode.
	VBoxContainer *layer_column = nullptr;
	ItemList *layer_list = nullptr;
	PopupMenu *layer_menu = nullptr;
	int layer_menu_layer = 0;
	Ref<TerrainLayerMaterial> layer_list_material;
	bool layer_list_dirty = true;
	bool layer_list_queued = false;
	int revealed_layer = -1;
	enum {
		TILE_MENU_SHOW_IN_FILE_MANAGER,
		TILE_MENU_SHOW_IN_FILESYSTEM,
		TILE_MENU_SHOW_IN_TERRAIN,
		TILE_MENU_REPLACE,
	};
	ItemList *tile_list = nullptr;
	PopupMenu *tile_menu = nullptr;
	// Replacing a tile's texture with an image dropped on the dialog.
	ConfirmationDialog *replace_dialog = nullptr;
	TextureRect *replace_preview = nullptr;
	Label *replace_hint = nullptr;
	EditorFileDialog *replace_file_dialog = nullptr;
	Ref<Image> replace_image;
	int replace_tile = 0;
	// The layer whose texture is replaced, or -1 while it is a tile's.
	int replace_layer = -1;
	String replace_path;
	Label *replace_note = nullptr;
	// The texture array the list was filled from.
	Ref<TextureLayered> tile_list_source;
	OptionButton *mode = nullptr;
	SpinBox *new_size = nullptr;
	SpinBox *new_cell_size = nullptr;
	Button *create_button = nullptr;
	SpinBox *import_min = nullptr;
	SpinBox *import_max = nullptr;
	EditorFileDialog *import_dialog = nullptr;
	Button *bake_button = nullptr;
	Button *convert_button = nullptr;

	Tool tool = TOOL_RAISE;

	// The brush, in the terrain's local space.
	bool brush_valid = false;
	Vector3 brush_position;
	Ref<ArrayMesh> cursor_mesh;
	Ref<StandardMaterial3D> cursor_material;
	RID cursor_instance;

	// The stroke in progress.
	bool stroking = false;
	Tool stroke_tool = TOOL_RAISE;
	int stroke_channel = 0;
	float stroke_height = 0.0;
	Rect2i stroke_rect;
	bool stroke_touched = false;
	Vector<float> heights_before;
	Vector<uint8_t> bytes_before;
	uint64_t last_apply_usec = 0;

	void _select_tool(int p_tool);
	void _update_panel();
	void _update_tile_preview();
	void _update_tile_list();
	void _update_layer_list();
	void _layer_material_changed();
	void _layer_list_selected(int p_index);
	void _layer_option_selected(int p_index);
	void _reveal_layer(int p_index);
	void _layer_list_clicked(int p_index, const Vector2 &p_position, int p_button);
	void _layer_menu_pressed(int p_id);
	void _show_layer_in_terrain(int p_layer);
	void _replace_layer_confirmed();
	void _focus_view(const Vector2 &p_grid);
	void _tile_list_selected(int p_index);
	void _tile_list_clicked(int p_index, const Vector2 &p_position, int p_button);
	void _tile_menu_pressed(int p_id);
	void _show_tile_in_terrain(int p_tile);
	void _replace_tile_popup(int p_tile);
	void _replace_browse_pressed();
	void _replace_files_dropped(const PackedStringArray &p_files);
	void _replace_file_selected(const String &p_path);
	void _replace_confirmed();
	static Error _save_tile_array(const Ref<ImageTextureLayered> &p_textures, const String &p_path);
	void _mode_selected(int p_mode);
	void _create_pressed();
	void _import_pressed();
	void _bake_collision_pressed();
	void _convert_pressed();
	void _import_file_selected(const String &p_path);
	void _terrain_exiting();

	bool _update_brush(Camera3D *p_camera, const Vector2 &p_position);
	void _update_cursor();
	float _falloff(float p_distance) const;
	// How far an offset from the brush's centre is, measured the way the brush's shape needs:
	// straight for the round brush, along the longer axis for the square one.
	float _brush_distance(const Vector2 &p_offset) const;

	void _begin_stroke(Tool p_tool);
	void _apply_brush(float p_delta);
	void _end_stroke();
	void _touch(const Rect2i &p_rect);

	void _sculpt(float p_delta);
	void _paint_layer(float p_delta);
	void _paint_tile();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	virtual EditorPlugin::AfterGUIInput forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) override;
	virtual String get_plugin_name() const override { return "Terrain"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;

	TerrainEditorPlugin();
	~TerrainEditorPlugin();
};
