/**************************************************************************/
/*  editor_autoload_settings.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "editor_autoload_settings.h"

#include "core/config/project_settings.h"
#include "core/core_constants.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_validation_panel.h"
#include "editor/scene/scene_create_dialog.h"
#include "editor/settings/project_settings_editor.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/tree.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/packed_scene.h"

#define PREVIEW_LIST_MAX_SIZE 10

void EditorAutoloadSettings::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			List<String> afn;
			ResourceLoader::get_recognized_extensions_for_type("PackedScene", &afn);

			for (const String &E : afn) {
				scene_file_dialog->add_filter("*." + E);
				autoload_file_dialog->add_filter("*." + E);
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			browse_button->set_button_icon(get_editor_theme_icon(SNAME("FileBrowse")));
			create_scene_autoload->set_button_icon(get_editor_theme_icon(SNAME("Add")));
		} break;
	}
}

bool EditorAutoloadSettings::_autoload_name_is_valid(const String &p_name, String *r_error) {
	if (!p_name.is_valid_unicode_identifier()) {
		if (r_error) {
			*r_error = TTR("Must be a valid Unicode identifier.");
		}

		return false;
	}

	if (ClassDB::class_exists(p_name)) {
		if (r_error) {
			*r_error = TTR("Must not collide with an existing engine class name.");
		}

		return false;
	}

	if (Variant::get_type_by_name(p_name) < Variant::VARIANT_MAX) {
		if (r_error) {
			*r_error = TTR("Must not collide with an existing built-in type name.");
		}

		return false;
	}

	for (int i = 0; i < CoreConstants::get_global_constant_count(); i++) {
		if (CoreConstants::get_global_constant_name(i) == p_name) {
			if (r_error) {
				*r_error = TTR("Must not collide with an existing global constant name.");
			}

			return false;
		}
	}

	return true;
}

void EditorAutoloadSettings::_validate_autoload_name() {
	String error;
	bool is_valid = _autoload_name_is_valid(name_edit->get_text(), &error);
	if (!is_valid) {
		name_validator->set_message(0, error, EditorValidationPanel::MSG_ERROR);
	}
}

void EditorAutoloadSettings::_autoload_selected() {
	TreeItem *ti = tree->get_selected();

	if (!ti) {
		return;
	}

	selected_autoload = "autoload/" + ti->get_text(0);
}

void EditorAutoloadSettings::_autoload_edited() {
	if (updating_autoload) {
		return;
	}

	TreeItem *ti = tree->get_edited();
	int column = tree->get_edited_column();

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();

	if (column == 0) {
		String name = ti->get_text(0);
		String old_name = selected_autoload.get_slicec('/', 1);

		if (name == old_name) {
			return;
		}

		String error;
		if (!_autoload_name_is_valid(name, &error)) {
			ti->set_text(0, old_name);
			EditorNode::get_singleton()->show_warning(error);
			return;
		}

		if (ProjectSettings::get_singleton()->has_setting("autoload/" + name)) {
			ti->set_text(0, old_name);
			EditorNode::get_singleton()->show_warning(vformat(TTR("Autoload '%s' already exists!"), name));
			return;
		}

		updating_autoload = true;

		name = "autoload/" + name;

		int order = ProjectSettings::get_singleton()->get_order(selected_autoload);
		String scr_path = GLOBAL_GET(selected_autoload);

		undo_redo->create_action(TTR("Rename Autoload"));

		undo_redo->add_do_property(ProjectSettings::get_singleton(), name, scr_path);
		undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", name, order);
		undo_redo->add_do_property(ProjectSettings::get_singleton(), selected_autoload, Variant());

		undo_redo->add_undo_property(ProjectSettings::get_singleton(), selected_autoload, scr_path);
		undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", selected_autoload, order);
		undo_redo->add_undo_property(ProjectSettings::get_singleton(), name, Variant());

		undo_redo->add_do_method(this, CoreStringName(call_deferred), "update_autoload");
		undo_redo->add_undo_method(this, CoreStringName(call_deferred), "update_autoload");

		undo_redo->add_do_method(this, "emit_signal", autoload_changed);
		undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

		undo_redo->commit_action();

		selected_autoload = name;
	}

	updating_autoload = false;
}

void EditorAutoloadSettings::_autoload_button_pressed(Object *p_item, int p_column, int p_button, MouseButton p_mouse_button) {
	if (p_mouse_button != MouseButton::LEFT) {
		return;
	}
	TreeItem *ti = Object::cast_to<TreeItem>(p_item);

	String name = "autoload/" + ti->get_text(0);

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();

	switch (p_button) {
		case BUTTON_MOVE_UP:
		case BUTTON_MOVE_DOWN: {
			TreeItem *swap = nullptr;

			if (p_button == BUTTON_MOVE_UP) {
				swap = ti->get_prev();
			} else {
				swap = ti->get_next();
			}

			if (!swap) {
				return;
			}

			String swap_name = "autoload/" + swap->get_text(0);

			int order = ProjectSettings::get_singleton()->get_order(name);
			int swap_order = ProjectSettings::get_singleton()->get_order(swap_name);

			undo_redo->create_action(TTR("Move Autoload"));

			undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", name, swap_order);
			undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", name, order);

			undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", swap_name, order);
			undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", swap_name, swap_order);

			undo_redo->add_do_method(this, "update_autoload");
			undo_redo->add_undo_method(this, "update_autoload");

			undo_redo->add_do_method(this, "emit_signal", autoload_changed);
			undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

			undo_redo->commit_action();
		} break;
		case BUTTON_DELETE: {
			int order = ProjectSettings::get_singleton()->get_order(name);

			undo_redo->create_action(TTR("Remove Autoload"));

			undo_redo->add_do_property(ProjectSettings::get_singleton(), name, Variant());

			undo_redo->add_undo_property(ProjectSettings::get_singleton(), name, GLOBAL_GET(name));
			undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", name, order);

			undo_redo->add_do_method(this, "update_autoload");
			undo_redo->add_undo_method(this, "update_autoload");

			undo_redo->add_do_method(this, "emit_signal", autoload_changed);
			undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

			undo_redo->commit_action();
		} break;
	}
}

void EditorAutoloadSettings::_autoload_activated() {
	TreeItem *ti = tree->get_selected();
	if (!ti) {
		return;
	}
	_autoload_open(ti->get_text(1));
}

void EditorAutoloadSettings::_autoload_open(const String &fpath) {
	EditorNode::get_singleton()->load_scene_or_resource(fpath);
	ProjectSettingsEditor::get_singleton()->hide();
}

void EditorAutoloadSettings::_create_scene_autoload() {
	scene_file_dialog->set_current_file("new_autoload_scene.tscn");
	scene_file_dialog->popup_file_dialog();
}

void EditorAutoloadSettings::_autoload_file_selected(const String &p_path) {
	_try_add_autoload(p_path.get_file().get_basename().to_pascal_case(), p_path);
}

void EditorAutoloadSettings::_scene_file_selected(const String &p_path) {
	if (!scene_create_dialog) {
		scene_create_dialog = memnew(SceneCreateDialog);
		add_child(scene_create_dialog);
		scene_create_dialog->connect(SceneStringName(confirmed), callable_mp(this, &EditorAutoloadSettings::_scene_created));
	}
	scene_create_dialog->config(p_path.get_base_dir(), p_path.get_file().get_basename());
	scene_create_dialog->popup_centered();
}

void EditorAutoloadSettings::_scene_created() {
	Node *root = scene_create_dialog->create_scene_root();

	Ref<PackedScene> ps;
	ps.instantiate();
	ps->pack(root);

	Error err = ResourceSaver::save(ps, scene_create_dialog->get_scene_path());
	if (err != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Failed to create scene. Error: %d."), err));
		return;
	}

	_try_add_autoload(scene_create_dialog->get_root_name().to_pascal_case(), scene_create_dialog->get_scene_path());
}

void EditorAutoloadSettings::_add_autoload(const String &p_name, const String &p_path) {
	autoload_add(p_name, p_path, true);
}

void EditorAutoloadSettings::_try_add_autoload(const String &p_name, const String &p_path) {
	if (_autoload_name_is_valid(p_name)) {
		_add_autoload(p_name, p_path);
		return;
	}
	pending_autoload_path = p_path;

	name_edit->set_text(p_name);
	name_validator->update();
	name_dialog->popup_centered(Vector2i(600 * EDSCALE, 0));
	name_edit->grab_focus();
	name_edit->select_all();
}

void EditorAutoloadSettings::_confirm_autoload_name() {
	_add_autoload(name_edit->get_text(), pending_autoload_path);
}

void EditorAutoloadSettings::update_autoload() {
	if (updating_autoload) {
		return;
	}

	updating_autoload = true;

	autoload_cache.clear();

	tree->clear();
	TreeItem *root = tree->create_item();

	List<PropertyInfo> props;
	ProjectSettings::get_singleton()->get_property_list(&props);

	for (const PropertyInfo &pi : props) {
		if (!pi.name.begins_with("autoload/")) {
			continue;
		}

		String name = pi.name.get_slicec('/', 1);
		String scr_path = GLOBAL_GET(pi.name);

		if (name.is_empty()) {
			continue;
		}

		AutoloadInfo info;

		if (scr_path.begins_with("*")) {
			scr_path = scr_path.substr(1);
		}

		info.name = name;
		info.path = ResourceUID::get_singleton()->path_to_uid(scr_path);
		info.order = ProjectSettings::get_singleton()->get_order(pi.name);

		autoload_cache.push_back(info);

		TreeItem *item = tree->create_item(root);
		item->set_text(0, name);
		item->set_auto_translate_mode(0, AUTO_TRANSLATE_MODE_DISABLED); // For tooltip.
		item->set_editable(0, true);

		item->set_text(1, ResourceUID::ensure_path(scr_path));
		item->set_selectable(1, true);

		item->add_button(2, get_editor_theme_icon(SNAME("MoveUp")), BUTTON_MOVE_UP);
		item->add_button(2, get_editor_theme_icon(SNAME("MoveDown")), BUTTON_MOVE_DOWN);
		item->add_button(2, get_editor_theme_icon(SNAME("Remove")), BUTTON_DELETE);
		item->set_selectable(2, false);
	}

	updating_autoload = false;
}

Variant EditorAutoloadSettings::get_drag_data_fw(const Point2 &p_point, Control *p_control) {
	if (autoload_cache.size() <= 1) {
		return false;
	}

	PackedStringArray autoloads;

	TreeItem *next = tree->get_next_selected(nullptr);

	while (next) {
		autoloads.push_back(next->get_text(0));
		next = tree->get_next_selected(next);
	}

	if (autoloads.is_empty() || autoloads.size() == autoload_cache.size()) {
		return Variant();
	}

	VBoxContainer *preview = memnew(VBoxContainer);

	int max_size = MIN(PREVIEW_LIST_MAX_SIZE, autoloads.size());

	for (int i = 0; i < max_size; i++) {
		Label *label = memnew(Label(autoloads[i]));
		label->set_self_modulate(Color(1, 1, 1, Math::lerp(1, 0, float(i) / PREVIEW_LIST_MAX_SIZE)));
		label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);

		preview->add_child(label);
	}

	tree->set_drop_mode_flags(Tree::DROP_MODE_INBETWEEN);
	tree->set_drag_preview(preview);

	Dictionary drop_data;
	drop_data["type"] = "autoload";
	drop_data["autoloads"] = autoloads;

	return drop_data;
}

bool EditorAutoloadSettings::can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_control) const {
	if (updating_autoload) {
		return false;
	}

	Dictionary drop_data = p_data;

	if (!drop_data.has("type")) {
		return false;
	}

	if (drop_data.has("type")) {
		TreeItem *ti = (p_point == Vector2(Math::INF, Math::INF)) ? tree->get_selected() : tree->get_item_at_position(p_point);

		if (!ti) {
			return false;
		}

		int section = (p_point == Vector2(Math::INF, Math::INF)) ? tree->get_drop_section_at_position(tree->get_item_rect(ti).position) : tree->get_drop_section_at_position(p_point);

		return section >= -1;
	}

	return false;
}

void EditorAutoloadSettings::drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_control) {
	TreeItem *ti = (p_point == Vector2(Math::INF, Math::INF)) ? tree->get_selected() : tree->get_item_at_position(p_point);

	if (!ti) {
		return;
	}

	int section = (p_point == Vector2(Math::INF, Math::INF)) ? tree->get_drop_section_at_position(tree->get_item_rect(ti).position) : tree->get_drop_section_at_position(p_point);

	if (section < -1) {
		return;
	}

	String name;
	bool move_to_back = false;

	if (section < 0) {
		name = ti->get_text(0);
	} else if (ti->get_next()) {
		name = ti->get_next()->get_text(0);
	} else {
		name = ti->get_text(0);
		move_to_back = true;
	}

	int order = ProjectSettings::get_singleton()->get_order("autoload/" + name);

	AutoloadInfo aux;
	List<AutoloadInfo>::Element *E = nullptr;

	if (!move_to_back) {
		aux.order = order;
		E = autoload_cache.find(aux);
	}

	Dictionary drop_data = p_data;
	PackedStringArray autoloads = drop_data["autoloads"];

	// Store the initial order of the autoloads for comparison.
	Vector<int> initial_orders;
	initial_orders.resize(autoload_cache.size());
	int idx = 0;
	for (const AutoloadInfo &F : autoload_cache) {
		initial_orders.write[idx++] = F.order;
	}

	// Perform the drag-and-drop operation.
	Vector<int> orders;
	orders.resize(autoload_cache.size());

	for (int i = 0; i < autoloads.size(); i++) {
		aux.order = ProjectSettings::get_singleton()->get_order("autoload/" + autoloads[i]);

		List<AutoloadInfo>::Element *I = autoload_cache.find(aux);

		if (move_to_back) {
			autoload_cache.move_to_back(I);
		} else if (E != I) {
			autoload_cache.move_before(I, E);
		} else if (E->next()) {
			E = E->next();
		} else {
			break;
		}
	}

	idx = 0;
	for (const AutoloadInfo &F : autoload_cache) {
		orders.write[idx++] = F.order;
	}

	// If the order didn't change, we shouldn't create undo/redo actions.
	if (orders == initial_orders) {
		return;
	}

	orders.sort();

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();

	undo_redo->create_action(TTR("Rearrange Autoloads"));

	idx = 0;
	for (const AutoloadInfo &F : autoload_cache) {
		undo_redo->add_do_method(ProjectSettings::get_singleton(), "set_order", "autoload/" + F.name, orders[idx++]);
		undo_redo->add_undo_method(ProjectSettings::get_singleton(), "set_order", "autoload/" + F.name, F.order);
	}

	orders.clear();

	undo_redo->add_do_method(this, "update_autoload");
	undo_redo->add_undo_method(this, "update_autoload");

	undo_redo->add_do_method(this, "emit_signal", autoload_changed);
	undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

	undo_redo->commit_action();
}

bool EditorAutoloadSettings::autoload_add(const String &p_name, const String &p_path, bool p_use_undo) {
	String name = p_name;

	String error;
	if (!_autoload_name_is_valid(name, &error)) {
		EditorNode::get_singleton()->show_warning(TTR("Can't add Autoload:") + "\n" + vformat(TTR("%s is an invalid name."), p_name) + " " + error);
		return false;
	}

	if (!FileAccess::exists(p_path)) {
		EditorNode::get_singleton()->show_warning(TTR("Can't add Autoload:") + "\n" + vformat(TTR("%s is an invalid path. File does not exist."), p_path));
		return false;
	}

	if (!p_path.begins_with("res://")) {
		EditorNode::get_singleton()->show_warning(TTR("Can't add Autoload:") + "\n" + vformat(TTR("%s is an invalid path. Not in resource path (res://)."), p_path));
		return false;
	}

	name = "autoload/" + name;

	if (!p_use_undo) {
		ProjectSettings::get_singleton()->set(name, "*" + p_path);
		update_autoload();
		emit_signal(autoload_changed);
		return true;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();

	undo_redo->create_action(TTR("Add Autoload"));
	// Singleton autoloads are represented with a leading "*" in their path.
	undo_redo->add_do_property(ProjectSettings::get_singleton(), name, "*" + ResourceUID::get_singleton()->path_to_uid(p_path));

	if (ProjectSettings::get_singleton()->has_setting(name)) {
		undo_redo->add_undo_property(ProjectSettings::get_singleton(), name, GLOBAL_GET(name));
	} else {
		undo_redo->add_undo_property(ProjectSettings::get_singleton(), name, Variant());
	}

	undo_redo->add_do_method(this, "update_autoload");
	undo_redo->add_undo_method(this, "update_autoload");

	undo_redo->add_do_method(this, "emit_signal", autoload_changed);
	undo_redo->add_undo_method(this, "emit_signal", autoload_changed);

	undo_redo->commit_action();

	return true;
}

void EditorAutoloadSettings::autoload_remove(const StringName &p_name) {
	ProjectSettings::get_singleton()->remove_autoload(p_name);
	update_autoload();
	emit_signal(autoload_changed);
}

void EditorAutoloadSettings::_bind_methods() {
	ClassDB::bind_method("update_autoload", &EditorAutoloadSettings::update_autoload);

	ADD_SIGNAL(MethodInfo("autoload_changed"));
}

EditorAutoloadSettings::EditorAutoloadSettings() {
	ProjectSettings::get_singleton()->add_hidden_prefix("autoload/");

	// Make first cache
	List<PropertyInfo> props;
	ProjectSettings::get_singleton()->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (!pi.name.begins_with("autoload/")) {
			continue;
		}

		String name = pi.name.get_slicec('/', 1);
		String scr_path = GLOBAL_GET(pi.name);

		if (name.is_empty()) {
			continue;
		}

		AutoloadInfo info;

		if (scr_path.begins_with("*")) {
			scr_path = scr_path.substr(1);
		}

		info.name = name;
		info.path = ResourceUID::get_singleton()->path_to_uid(scr_path);
		info.order = ProjectSettings::get_singleton()->get_order(pi.name);

		autoload_cache.push_back(info);
	}

	HBoxContainer *hbc = memnew(HBoxContainer);
	add_child(hbc);

	autoload_file_dialog = memnew(EditorFileDialog);
	autoload_file_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
	hbc->add_child(autoload_file_dialog);
	autoload_file_dialog->connect("file_selected", callable_mp(this, &EditorAutoloadSettings::_autoload_file_selected));

	scene_file_dialog = memnew(EditorFileDialog);
	scene_file_dialog->set_file_mode(FileDialog::FILE_MODE_SAVE_FILE);
	hbc->add_child(scene_file_dialog);
	scene_file_dialog->connect("file_selected", callable_mp(this, &EditorAutoloadSettings::_scene_file_selected));

	browse_button = memnew(Button);
	browse_button->set_text(TTRC("Select Scene"));
	hbc->add_child(browse_button);
	browse_button->connect(SceneStringName(pressed), callable_mp(autoload_file_dialog, &FileDialog::popup_file_dialog));

	create_scene_autoload = memnew(Button);
	create_scene_autoload->set_text(TTRC("Create Scene"));
	hbc->add_child(create_scene_autoload);
	create_scene_autoload->connect(SceneStringName(pressed), callable_mp(this, &EditorAutoloadSettings::_create_scene_autoload));

	MarginContainer *mc = memnew(MarginContainer);
	mc->set_v_size_flags(SIZE_EXPAND_FILL);
	mc->set_theme_type_variation("NoBorderBottomWideWindow");
	add_child(mc);

	tree = memnew(Tree);
	tree->set_accessibility_name(TTRC("Autoloads"));
	tree->set_hide_root(true);
	tree->set_select_mode(Tree::SELECT_MULTI);
	tree->set_allow_reselect(true);

	SET_DRAG_FORWARDING_GCD(tree, EditorAutoloadSettings);

	tree->set_theme_type_variation("TreeTable");
	tree->set_hide_folding(true);
	tree->set_columns(3);
	tree->set_column_titles_visible(true);

	tree->set_column_title(0, TTRC("Name"));
	tree->set_column_title_tooltip_text(0, TTRC("Name of the autoload. Double-click to rename."));
	tree->set_column_title_alignment(0, HORIZONTAL_ALIGNMENT_LEFT);
	tree->set_column_expand(0, true);
	tree->set_column_expand_ratio(0, 1);

	tree->set_column_title(1, TTRC("Path"));
	tree->set_column_title_tooltip_text(1, TTRC("Path to the autoload file. Double-click to open it."));
	tree->set_column_title_alignment(1, HORIZONTAL_ALIGNMENT_LEFT);
	tree->set_column_expand(1, true);
	tree->set_column_clip_content(1, true);
	tree->set_column_expand_ratio(1, 2);

	tree->set_column_expand(2, false);

	tree->connect("cell_selected", callable_mp(this, &EditorAutoloadSettings::_autoload_selected));
	tree->connect("item_edited", callable_mp(this, &EditorAutoloadSettings::_autoload_edited));
	tree->connect("button_clicked", callable_mp(this, &EditorAutoloadSettings::_autoload_button_pressed));
	tree->connect("item_activated", callable_mp(this, &EditorAutoloadSettings::_autoload_activated));

	mc->add_child(tree, true);

	name_dialog = memnew(ConfirmationDialog);
	name_dialog->set_title(TTRC("Enter Autoload Name"));
	add_child(name_dialog);
	name_dialog->connect(SceneStringName(confirmed), callable_mp(this, &EditorAutoloadSettings::_confirm_autoload_name));

	VBoxContainer *name_vbox = memnew(VBoxContainer);
	name_dialog->add_child(name_vbox);

	name_edit = memnew(LineEdit);
	name_vbox->add_child(name_edit);
	name_dialog->register_text_enter(name_edit);

	name_validator = memnew(EditorValidationPanel);
	name_validator->set_accept_button(name_dialog->get_ok_button());
	name_validator->set_update_callback(callable_mp(this, &EditorAutoloadSettings::_validate_autoload_name));
	name_validator->add_line(0, TTRC("Autoload name is valid."));
	name_vbox->add_child(name_validator);
	name_edit->connect(SceneStringName(text_changed), callable_mp(name_validator, &EditorValidationPanel::update).unbind(1));
}
