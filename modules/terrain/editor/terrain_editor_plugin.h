#pragma once

#include "../terrain_3d.h"

#include "editor/plugins/editor_plugin.h"

class Button;
class EditorDock;
class EditorFileDialog;
class Label;
class OptionButton;
class SpinBox;
class StandardMaterial3D;
class TextureRect;
class HBoxContainer;

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
//   Paint Tile      tiles mode: puts the chosen tile, in the chosen orientation, on the chosen
//                   one of the cell's two tile slots
//   Erase Tile      tiles mode: empties that slot
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
	SpinBox *height = nullptr;
	OptionButton *layer = nullptr;
	SpinBox *tile_index = nullptr;
	OptionButton *tile_slot = nullptr;
	OptionButton *tile_direction = nullptr;
	TextureRect *tile_preview = nullptr;
	OptionButton *mode = nullptr;
	SpinBox *new_size = nullptr;
	SpinBox *new_cell_size = nullptr;
	Button *create_button = nullptr;
	SpinBox *import_min = nullptr;
	SpinBox *import_max = nullptr;
	EditorFileDialog *import_dialog = nullptr;

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
	void _mode_selected(int p_mode);
	void _create_pressed();
	void _import_pressed();
	void _import_file_selected(const String &p_path);
	void _terrain_exiting();

	bool _update_brush(Camera3D *p_camera, const Vector2 &p_position);
	void _update_cursor();
	float _falloff(float p_distance) const;

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
