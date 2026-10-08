/**************************************************************************/
/*  document_editor_container.cpp                                         */
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

#include "document_editor_container.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/editor_language.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/string/fuzzy_search.h"
#include "core/variant/dictionary.h"
#include "core/version.h"
#include "editor/doc/editor_help.h"
#include "editor/doc/editor_help_search.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/document/document_editor_base.h"
#include "editor/document/document_editor_plugin.h"
#include "editor/document/document_navigation_marker.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/code_editor.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_toaster.h"
#include "editor/gui/filter_line_edit.h"
#include "editor/gui/window_wrapper.h"
#include "editor/run/editor_run_bar.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/script/find_in_files.h"
#include "editor/script/syntax_highlighters.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/shader/shader_create_dialog.h"
#include "editor/shader/shader_editor_plugin.h"
#include "editor/shader/shader_text_editor.h"
#include "editor/themes/editor_scale.h"
#include "editor/themes/editor_theme_manager.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/tree.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/timer.h"
#include "scene/main/window.h"
#include "scene/resources/shader.h"
#include "scene/resources/shader_include.h"
#include "servers/display/display_server.h"

void DocumentList::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			filter->set_right_icon(get_editor_theme_icon(SNAME("Search")));
			item_list->set_fixed_icon_size(Vector2i(1, 1) * get_theme_constant("class_icon_size", EditorStringName(Editor)));

			const Ref<Texture2D> &help_icon = get_editor_theme_icon(SNAME("Help"));
			for (ItemData &item : items) {
				Control *editor = document_editor_container->tab_container->get_tab_control(item.index);
				DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(editor);
				EditorHelp *eh = Object::cast_to<EditorHelp>(editor);
				if (seb) {
					item.icon = seb->get_theme_icon();
				} else if (eh) {
					item.icon = help_icon;
				}
			}
			_update_item_list();
		} break;
	}
}

Variant DocumentList::get_drag_data_fw(const Point2 &p_point, Control *p_from) {
	Node *cur_node = document_editor_container->tab_container->get_current_tab_control();
	if (cur_node == nullptr) {
		return Variant();
	}

	HBoxContainer *drag_preview = memnew(HBoxContainer);
	String preview_name;
	Ref<Texture2D> preview_icon;

	DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(cur_node);
	if (seb) {
		preview_name = seb->get_document_name();
		preview_icon = seb->get_theme_icon();
	}
	EditorHelp *eh = Object::cast_to<EditorHelp>(cur_node);
	if (eh) {
		preview_name = eh->get_class();
		preview_icon = get_editor_theme_icon(SNAME("Help"));
	}

	if (preview_icon.is_valid()) {
		TextureRect *tf = memnew(TextureRect);
		tf->set_texture(preview_icon);
		tf->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
		drag_preview->add_child(tf);
	}
	Label *label = memnew(Label(preview_name));
	label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED); // Don't translate script names and class names.
	drag_preview->add_child(label);
	set_drag_preview(drag_preview);

	Dictionary drag_data;
	drag_data["type"] = "script_list_element";
	drag_data["script_list_element"] = cur_node;
	drag_data["editor"] = document_editor_container->config_section;

	return drag_data;
}

bool DocumentList::can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const {
	Dictionary d = p_data;
	const String type = d.get("type", "");

	if (type == "script_list_element") {
		if (d["editor"] != document_editor_container->config_section) {
			return false;
		}
		Node *node = Object::cast_to<Node>(d["script_list_element"]);

		if (Object::cast_to<DocumentEditorBase>(node) || Object::cast_to<EditorHelp>(node)) {
			return true;
		}
	}

	if (type == "files") {
		Vector<String> files = d["files"];

		if (files.is_empty()) {
			return false; // Weird.
		}

		for (const String &file : files) {
			if (document_editor_container->can_open_file(file)) {
				return true;
			}
		}
		return false;
	}

	return false;
}

void DocumentList::drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) {
	if (!can_drop_data_fw(p_point, p_data, p_from)) {
		return;
	}

	Dictionary d = p_data;
	const String type = d.get("type", "");

	if (type == "script_list_element") {
		Node *node = Object::cast_to<Node>(d["script_list_element"]);

		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(node);
		EditorHelp *eh = Object::cast_to<EditorHelp>(node);
		if (seb || eh) {
			int new_index = 0;
			if (item_list->get_item_count() > 0) {
				int pos = 0;
				if (p_point == Vector2(Math::INF, Math::INF)) {
					if (item_list->is_anything_selected()) {
						pos = item_list->get_selected_items()[0];
					}
				} else {
					pos = item_list->get_item_at_position(p_point);
				}
				new_index = item_list->get_item_metadata(pos);
			}
			TabContainer *tab_container = document_editor_container->tab_container;
			tab_container->move_child(node, new_index);
			tab_container->set_current_tab(new_index);
			update_list();
		}
	}

	if (type == "files") {
		Vector<String> files = d["files"];

		int new_index = 0;
		if (item_list->get_item_count() > 0) {
			int pos = 0;
			if (p_point == Vector2(Math::INF, Math::INF)) {
				if (item_list->is_anything_selected()) {
					pos = item_list->get_selected_items()[0];
				}
			} else {
				pos = item_list->get_item_at_position(p_point);
			}
			new_index = item_list->get_item_metadata(pos);
		}
		TabContainer *tab_container = document_editor_container->tab_container;
		int num_tabs_before = tab_container->get_tab_count();
		for (const String &file : files) {
			if (!document_editor_container->can_open_file(file)) {
				continue;
			}

			Ref<Resource> res = document_editor_container->open_file(file);
			if (res.is_valid()) {
				const int num_tabs = tab_container->get_tab_count();
				if (num_tabs > num_tabs_before) {
					tab_container->move_child(tab_container->get_tab_control(tab_container->get_tab_count() - 1), new_index);
					num_tabs_before = num_tabs;
				} else if (num_tabs > 0) { // Maybe script was already open.
					tab_container->move_child(tab_container->get_current_tab_control(), new_index);
				}
			}
		}
		if (tab_container->get_tab_count() > 0) {
			tab_container->set_current_tab(new_index);
		}
		update_list();
	}
}

void DocumentList::_document_selected(int p_index) {
	document_editor_container->_go_to_tab(item_list->get_item_metadata(p_index), true);
}

void DocumentList::_document_clicked(int p_index, Vector2 p_local_mouse_pos, MouseButton p_mouse_button_index) {
	if (p_mouse_button_index == MouseButton::MIDDLE) {
		_document_selected(p_index);
		document_editor_container->_menu_option(DocumentEditorContainer::FILE_MENU_CLOSE);
	}

	if (p_mouse_button_index == MouseButton::RIGHT) {
		_show_context_menu();
	}
}

void DocumentList::_show_context_menu() {
	context_menu->clear();

	int selected = document_editor_container->tab_container->get_current_tab();
	if (selected < 0) {
		return;
	}

	DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(document_editor_container->tab_container->get_tab_control(selected));
	const Ref<Resource> res = seb ? seb->get_edited_resource() : Ref<Resource>();
	{
		if (seb) {
			context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/save"), DocumentEditorContainer::FILE_MENU_SAVE);
			context_menu->add_separator();
		}
		context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/close_file"), DocumentEditorContainer::FILE_MENU_CLOSE);
		context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/close_other_tabs"), DocumentEditorContainer::FILE_MENU_CLOSE_OTHER_TABS);
		context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/close_tabs_below"), DocumentEditorContainer::FILE_MENU_CLOSE_TABS_BELOW);
		context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/close_all"), DocumentEditorContainer::FILE_MENU_CLOSE_ALL);
		if (document_editor_container->is_main_editor) {
			context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/close_docs"), DocumentEditorContainer::FILE_MENU_CLOSE_DOCS);
		}
		context_menu->add_separator();

		if (seb) {
			context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/copy_path"), DocumentEditorContainer::FILE_MENU_COPY_PATH);
			context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/copy_uid"), DocumentEditorContainer::FILE_MENU_COPY_UID);
			context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/show_in_file_system"), DocumentEditorContainer::FILE_MENU_SHOW_IN_FILE_SYSTEM);
			if (!document_editor_container->is_main_editor) {
				context_menu->add_shortcut(ED_SHORTCUT("document_editor/open_in_inspector", TTRC("Open File in Inspector")), DocumentEditorContainer::FILE_MENU_INSPECT);
				context_menu->add_shortcut(ED_SHORTCUT("document_editor/inspect_native_code", TTRC("Inspect Native Shader Code...")), DocumentEditorContainer::FILE_MENU_INSPECT_NATIVE_SHADER_CODE);
			}
			context_menu->add_separator();
		}

		context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/move_document_up"), DocumentEditorContainer::FILE_MENU_MOVE_UP);
		context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/move_document_down"), DocumentEditorContainer::FILE_MENU_MOVE_DOWN);
		context_menu->add_shortcut(ED_GET_SHORTCUT("document_editor/sort_documents"), DocumentEditorContainer::FILE_MENU_SORT);
	}

	{
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_SAVE), !seb);
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_CLOSE), document_editor_container->tab_container->get_tab_count() < 1);
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_CLOSE_OTHER_TABS), document_editor_container->tab_container->get_tab_count() <= 1);
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_CLOSE_TABS_BELOW), document_editor_container->tab_container->get_current_tab() >= document_editor_container->tab_container->get_tab_count() - 1);
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_CLOSE_ALL), document_editor_container->tab_container->get_tab_count() < 1);
		if (document_editor_container->is_main_editor) {
			context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_CLOSE_DOCS), !document_editor_container->_has_docs_tab());
		}
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_COPY_PATH), res.is_null() || res->get_path().is_empty());
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_COPY_UID), res.is_null() || ResourceLoader::get_resource_uid(res->get_path()) == ResourceUID::INVALID_ID);
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_SHOW_IN_FILE_SYSTEM), res.is_null() || res->get_path().is_empty());
		if (!document_editor_container->is_main_editor) {
			context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_INSPECT), !seb);
			context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_INSPECT_NATIVE_SHADER_CODE), !seb);
		}
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_MOVE_UP), document_editor_container->tab_container->get_current_tab() <= 0);
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_MOVE_DOWN), document_editor_container->tab_container->get_current_tab() >= document_editor_container->tab_container->get_tab_count() - 1);
		context_menu->set_item_disabled(context_menu->get_item_index(DocumentEditorContainer::FILE_MENU_SORT), document_editor_container->tab_container->get_tab_count() <= 1);
	}

	context_menu->set_position(get_screen_position() + get_local_mouse_position());
	context_menu->reset_size();
	context_menu->popup();
}

void DocumentList::update_list() {
	TabContainer *tab_container = document_editor_container->tab_container;
	const int num_open_editors = tab_container->get_tab_count();
	const Ref<Texture2D> &help_icon = get_editor_theme_icon(SNAME("Help"));

	items.clear();
	items.reserve(num_open_editors);

	for (int i = 0; i < num_open_editors; i++) {
		ItemData item;
		item.index = i;
		item.display_name = tab_container->get_tab_title(i);

		Control *editor = tab_container->get_tab_control(i);
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(editor);
		EditorHelp *eh = Object::cast_to<EditorHelp>(editor);
		if (seb) {
			const String &path = seb->get_edited_resource()->get_path();

			item.tooltip = path.is_empty() ? TTR("Unsaved file.") : path;
			item.icon = seb->get_theme_icon();

			TextEditorBase *teb = Object::cast_to<TextEditorBase>(seb);
			if (teb) {
				item.has_error = !teb->get_validation_success();
			}
		}
		if (eh) {
			const String name = eh->get_class().is_empty() ? TTR("Unknown Help Class") : eh->get_class().unquote();
			const String tooltip = vformat(TTR("%s Class Reference"), name);

			item.tooltip = tooltip;
			item.icon = help_icon;
		}

		items.push_back(item);
	}

	const String &filter_text = filter->get_text();
	if (!filter_text.is_empty()) {
		PackedStringArray search_names;
		for (ItemData &item : items) {
			item.hidden = true;
			search_names.append(item.display_name);
		}

		FuzzySearch fuzzy;
		fuzzy.set_case_sensitive(false);
		Vector<Ref<FuzzySearchMatch>> results = fuzzy.search_all(filter_text, search_names);

		for (const Ref<FuzzySearchMatch> &res : results) {
			items[res->get_original_index()].hidden = false;
		}
	}

	_update_item_list();
}

void DocumentList::_update_item_list() {
	item_list->clear();
	if (items.is_empty()) {
		return;
	}

	TabContainer *tab_container = document_editor_container->tab_container;
	const int current_index = tab_container->get_current_tab();

	const Ref<Texture2D> &error_icon = get_editor_theme_icon(SNAME("Error"));

	for (const ItemData &item : items) {
		if (item.hidden) {
			continue;
		}
		item_list->add_item(item.display_name, item.icon);
		item_list->set_item_tooltip(-1, item.tooltip);
		item_list->set_item_metadata(-1, item.index); // Save the script's index in the tab container and not the filtered one.
		if (item.has_error) {
			item_list->set_item_tag_icon(-1, error_icon);
		}
		if (item.index == current_index) {
			item_list->select(item_list->get_item_count() - 1);
		}
	}
	item_list->ensure_current_is_visible();

	// Update colors.
	if (!script_temperature_enabled) {
		return;
	}

	Color hot_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	hot_color.set_s(hot_color.get_s() * 0.9);
	Color cold_color = get_theme_color(SceneStringName(font_color), EditorStringName(Editor));

	int edit_pass = document_editor_container->edit_pass;

	for (int i = 0; i < item_list->get_item_count(); i++) {
		int index = item_list->get_item_metadata(i);
		Node *open_editor = tab_container->get_tab_control(index);
		if (!open_editor) {
			continue;
		}

		int pass = open_editor->get_meta("__editor_pass", -1);
		if (pass < 0) {
			continue;
		}

		int h = edit_pass - pass;
		if (h > temperature_history_size) {
			continue;
		}
		int non_zero_hist_size = (temperature_history_size == 0) ? 1 : temperature_history_size;
		float v = Math::ease(h / float(non_zero_hist_size), 0.4);

		item_list->set_item_custom_fg_color(i, hot_color.lerp(cold_color, v));
	}
}

void DocumentList::ensure_current_is_visible() {
	item_list->ensure_current_is_visible();
}

void DocumentList::goto_next_document(bool p_previous) {
	if (item_list->get_item_count() == 0) {
		return;
	}
	int next_tab = item_list->get_current() + (p_previous ? -1 : 1);
	next_tab = Math::wrapi(next_tab, 0, item_list->get_item_count());
	document_editor_container->_go_to_tab(item_list->get_item_metadata(next_tab), true);
}

void DocumentList::update_editor_settings() {
	script_temperature_enabled = EDITOR_GET("text_editor/script_list/script_temperature_enabled");
	temperature_history_size = EDITOR_GET("text_editor/script_list/script_temperature_history_size");

	update_list();
}

DocumentList::DocumentList(DocumentEditorContainer *p_document_editor_container) {
	document_editor_container = p_document_editor_container;
	set_v_size_flags(SIZE_EXPAND_FILL);

	filter = memnew(LineEdit);
	filter->set_clear_button_enabled(true);
	filter->set_placeholder(TTRC("Filter Documents"));
	filter->set_accessibility_name(TTRC("Filter Documents"));
	filter->connect(SceneStringName(text_changed), callable_mp(this, &DocumentList::update_list).unbind(1));
	add_child(filter);

	item_list = memnew(ItemList);
	item_list->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	item_list->set_custom_minimum_size(Size2(60, 40) * EDSCALE);
	item_list->set_v_size_flags(SIZE_EXPAND_FILL);
	item_list->set_theme_type_variation("ItemListSecondary");
	item_list->set_allow_rmb_select(true);
	item_list->connect("item_clicked", callable_mp(this, &DocumentList::_document_clicked));
	item_list->connect(SceneStringName(item_selected), callable_mp(this, &DocumentList::_document_selected));
	add_child(item_list);
	SET_DRAG_FORWARDING_GCD(item_list, DocumentList);

	context_menu = memnew(PopupMenu);
	add_child(context_menu);
	context_menu->connect(SceneStringName(id_pressed), callable_mp(document_editor_container, &DocumentEditorContainer::_menu_option));
}

/////////////////////////////////

void DocumentOutline::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			sort_button->set_button_icon(get_editor_theme_icon(SNAME("Sort")));

			update_visibility();
		} break;
	}
}

void DocumentOutline::_tree_selected() {
	if (updating_outline) {
		return;
	}
	Control *active_editor = document_editor_container->get_active_editor();
	int line = tree->get_selected()->get_metadata(0);
	if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(active_editor)) {
		teb->goto_line_centered(line);
	} else if (EditorHelp *eh = Object::cast_to<EditorHelp>(active_editor)) {
		eh->scroll_to_section(line);
	}
}

void DocumentOutline::_toggle_sort(bool p_alphabetic_sort) {
	EditorSettings::get_singleton()->set("text_editor/script_list/sort_members_outline_alphabetically", p_alphabetic_sort);
	update_outline();
}

void DocumentOutline::update_editor_settings() {
	members_overview_enabled = EDITOR_GET("text_editor/script_list/show_members_overview");
	help_overview_enabled = EDITOR_GET("text_editor/help/show_help_index");
	update_visibility();
}

void DocumentOutline::update_outline() {
	String selected;
	if (tree->is_anything_selected()) {
		selected = tree->get_selected()->get_meta("function");
	}

	Control *active_editor = document_editor_container->get_active_editor();
	if (current_editor != active_editor) {
		selected = ""; // Script/doc changed, forget the last selection.
	}
	current_editor = active_editor;

	updating_outline = true;

	tree->clear();
	tree->create_item();

	if (CodeEditorBase *ceb = Object::cast_to<CodeEditorBase>(active_editor)) {
		PackedStringArray functions = ceb->get_functions();
		if (EDITOR_GET("text_editor/script_list/sort_members_outline_alphabetically")) {
			functions.sort();
		}

		const String &filter_text = filter->get_text();
		HashMap<String, TreeItem *> parents;
		Color disabled_color = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));

		if (filter_text.is_empty()) {
			for (const String &function_name : functions) {
				TreeItem *parent = tree->get_root();
				String name = function_name.get_slicec(':', 0);

				// Organize functions inside subclasses.
				Vector<String> subclasses = name.split(".");
				if (!subclasses.is_empty()) {
					int last = subclasses.size() - 1;
					name = subclasses[last];
					subclasses.remove_at(last); // Remove the function itself.

					for (const String &subclass : subclasses) {
						if (parents.has(subclass)) {
							parent = parents[subclass];
						} else {
							parent = tree->create_item(parent);
							parent->set_text(0, subclass);
							parent->set_selectable(0, false);
							parent->set_custom_color(0, disabled_color);
							parents[subclass] = parent;
						}
					}
				}

				TreeItem *ti = tree->create_item(parent);
				ti->set_text(0, name);
				int line = function_name.get_slicec(':', 1).to_int() - 1;
				ti->set_metadata(0, line);
				ti->set_meta("function", function_name);
				if (selected == function_name) {
					ti->select(0);
				}
			}
		} else {
			PackedStringArray search_names;
			for (const String &function_name : functions) {
				search_names.append(function_name.get_slicec(':', 0));
			}

			FuzzySearch fuzzy;
			fuzzy.set_case_sensitive(false);
			Vector<Ref<FuzzySearchMatch>> results = fuzzy.search_all(filter_text, search_names);

			for (const Ref<FuzzySearchMatch> &res : results) {
				TreeItem *parent = tree->get_root();
				String function_name = functions[res->get_original_index()];
				String name = function_name.get_slicec(':', 0);

				// Organize functions inside subclasses.
				Vector<String> subclasses = name.split(".");
				if (!subclasses.is_empty()) {
					int last = subclasses.size() - 1;
					name = subclasses[last];
					subclasses.remove_at(last); // Remove the function itself.

					for (const String &subclass : subclasses) {
						if (parents.has(subclass)) {
							parent = parents[subclass];
						} else {
							parent = tree->create_item(parent);
							parent->set_text(0, subclass);
							parent->set_selectable(0, false);
							parent->set_custom_color(0, disabled_color);
							parents[subclass] = parent;
						}
					}
				}

				TreeItem *ti = tree->create_item(parent);
				ti->set_text(0, name);
				int line = function_name.get_slicec(':', 1).to_int() - 1;
				ti->set_metadata(0, line);
				ti->set_meta("function", function_name);
				if (selected == function_name) {
					ti->select(0);
				}
			}
		}
	} else if (EditorHelp *eh = Object::cast_to<EditorHelp>(active_editor)) {
		const Vector<Pair<String, int>> sections = eh->get_sections();
		for (const Pair<String, int> &section : sections) {
			TreeItem *ti = tree->create_item();
			ti->set_text(0, section.first);
			ti->set_metadata(0, section.second);
			ti->set_meta("function", section.first);
			if (selected == section.first) {
				ti->select(0);
			}
		}
	}
	updating_outline = false;
}

void DocumentOutline::update_visibility() {
	Control *active_editor = document_editor_container->get_active_editor();
	DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(active_editor);
	EditorHelp *eh = Object::cast_to<EditorHelp>(active_editor);

	bool members_overview_visible = members_overview_enabled && seb && seb->show_members_overview();
	bool help_overview_visible = help_overview_enabled && eh;

	buttons_hbox->set_visible(members_overview_visible);
	set_visible(members_overview_visible || help_overview_visible);

	bool use_monospace_font = members_overview_visible && EDITOR_GET("interface/theme/use_monospace_font_for_editor_symbols");
	if (use_monospace_font) {
		Ref<Font> monospace_font = get_theme_font(SNAME("source"), EditorStringName(EditorFonts));
		if (tree->get_theme_font(SceneStringName(font)) != monospace_font) {
			tree->add_theme_font_override(SceneStringName(font), monospace_font);
		}
	} else if (tree->has_theme_font_override(SceneStringName(font))) {
		tree->remove_theme_font_override(SceneStringName(font));
	}
}

DocumentOutline::DocumentOutline(DocumentEditorContainer *p_document_editor_container) {
	document_editor_container = p_document_editor_container;
	buttons_hbox = memnew(HBoxContainer);

	filter = memnew(FilterLineEdit);
	filter->set_placeholder(TTRC("Filter Methods"));
	filter->set_accessibility_name(TTRC("Filter Methods"));
	filter->set_clear_button_enabled(true);
	filter->set_h_size_flags(SIZE_EXPAND_FILL);
	filter->connect(SceneStringName(text_changed), callable_mp(this, &DocumentOutline::update_outline).unbind(1));
	buttons_hbox->add_child(filter);

	sort_button = memnew(Button);
	sort_button->set_theme_type_variation(SceneStringName(FlatButton));
	sort_button->set_tooltip_text(TTRC("Toggle alphabetical sorting of the method list."));
	sort_button->set_toggle_mode(true);
	sort_button->set_pressed(EDITOR_GET("text_editor/script_list/sort_members_outline_alphabetically"));
	sort_button->connect(SceneStringName(toggled), callable_mp(this, &DocumentOutline::_toggle_sort));
	buttons_hbox->add_child(sort_button);

	add_child(buttons_hbox);

	tree = memnew(Tree);
	filter->set_forward_control(tree);
	tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	tree->set_theme_type_variation("TreeSecondary");
	tree->add_theme_constant_override("h_separation", 0);
	tree->set_allow_reselect(true);
	tree->set_allow_rmb_select(true);
	tree->set_hide_root(true);
	tree->set_hide_folding(true);
	tree->set_custom_minimum_size(Size2(0, 40 * EDSCALE));
	tree->set_v_size_flags(SIZE_EXPAND_FILL);
	tree->connect(SceneStringName(item_selected), callable_mp(this, &DocumentOutline::_tree_selected));
	add_child(tree);
}

void DocumentEditorContainer::_resource_created(Ref<Resource> p_res) {
	EditorNode::get_singleton()->push_item(p_res.ptr());
}

DocumentEditorBase *DocumentEditorContainer::_get_current_editor() const {
	return Object::cast_to<DocumentEditorBase>(tab_container->get_current_tab_control());
}

void DocumentEditorContainer::_update_history_arrows() {
	script_back->set_disabled(history_pos <= 0);
	script_forward->set_disabled(history_pos >= history.size() - 1);
}

void DocumentEditorContainer::_save_new_history(const Dictionary &p_state, Control *p_control) {
	if (restoring_layout) {
		return;
	}

	if (history_pos >= 0 && history_pos < history.size()) {
		if (history[history_pos].control == p_control) {
			if (history[history_pos].state == p_state) {
				return;
			}
			if (p_state["row"] == history[history_pos].state["row"]) {
				_save_previous_state(p_state, p_control);
				return;
			}
		}
	}

	ScriptHistory sh;
	sh.control = p_control;
	sh.state = p_state;

	history.resize(history_pos + 1);
	history.push_back(sh);
	_compress_history_patterns(true);

	for (int i = history.size() - 1; i >= 0; i--) {
		if (history[i].control == tab_container->get_current_tab_control()) {
			history_pos = i;
			break;
		}
	}

	_update_history_arrows();
}

void DocumentEditorContainer::_save_previous_state(const Dictionary &p_state, Control *p_control) {
	if (history_pos < 0 || history_pos >= history.size()) {
		return;
	}
	ScriptHistory sh = history[history_pos];
	if (sh.control != p_control) {
		return;
	}
	history.write[history_pos].state = p_state;
}

// Compress the history and remove duplicate patterns.
// Example 1: If the history is ...ABAB..., it will be compressed to ...AB....
// Example 2: If the history is ...ABCABC..., it will be compressed to ...ABC....
void DocumentEditorContainer::_compress_history_patterns(bool p_once) {
	bool stop = false;
	bool changed = true;
	int iterations = 0;

	while (!stop && changed && history.size() > 1 && iterations++ < 100) {
		changed = false;
		for (int end_idx = history.size() - 1; end_idx >= 1; end_idx--) {
			bool found_duplicate = false;
			int max_possible_len = (end_idx + 1) / 2;

			for (int len = 1; len <= max_possible_len; len++) {
				// Compare [first_start, first_start + len) and [second_start, second_start + len)
				int second_start = end_idx - len + 1;
				int first_start = second_start - len;

				if (first_start < 0) {
					continue;
				}

				bool is_match = true;

				for (int k = 0; k < len; k++) {
					const ScriptHistory &h1 = history[first_start + k];
					const ScriptHistory &h2 = history[second_start + k];

					if (h1.control != h2.control || h1.state["row"] != h2.state["row"]) {
						is_match = false;
						break;
					}
				}

				if (is_match) {
					for (int r = 0; r < len; r++) {
						history.remove_at(first_start);
					}

					if (history_pos >= second_start) {
						history_pos -= len;
					} else if (first_start <= history_pos && history_pos < second_start) {
						history_pos = first_start;
					}

					found_duplicate = true;
					changed = true;
					stop = p_once;
					break;
				}
			}

			if (found_duplicate) {
				break;
			}
		}
	}

	if (history.is_empty()) {
		history_pos = -1;
	} else {
		if (history_pos >= history.size()) {
			history_pos = history.size() - 1;
		}
		if (history_pos < -1) {
			history_pos = -1;
		}
	}
}

void DocumentEditorContainer::_go_to_tab(int p_idx, bool p_save_history) {
	if (restoring_layout) {
		return;
	}
	if (DocumentEditorBase *current = _get_current_editor()) {
		if (current->is_unsaved()) {
			current->apply_code();
		}
	}

	Control *c = tab_container->get_tab_control(p_idx);
	if (!c) {
		return;
	}

	tab_container->set_current_tab(p_idx);

	c = tab_container->get_current_tab_control();

	if (DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(c)) {
		if (is_visible_in_tree() && !grab_focus_block) {
			seb->ensure_focus();
		}

		TextEditorBase *teb = Object::cast_to<TextEditorBase>(c);
		if (p_save_history && teb) {
			DocumentNavigationMarker::get_singleton()->locate_begin();
			teb->trigger_history_save_on_navigate();
			DocumentNavigationMarker::get_singleton()->locate_end();
		}

		seb->validate_script();
	}

	if (EditorHelp *eh = Object::cast_to<EditorHelp>(c)) {
		if (is_visible_in_tree()) {
			eh->set_focused();
		}

		if (p_save_history) {
			DocumentNavigationMarker::get_singleton()->locate_begin();
			eh->trigger_history_save_on_navigate();
			DocumentNavigationMarker::get_singleton()->locate_end();
		}
	}

	c->set_meta("__editor_pass", ++edit_pass);
	_update_history_arrows();
}

void DocumentEditorContainer::_add_recent_script(const String &p_path) {
	if (p_path.is_empty()) {
		return;
	}

	Array rc = EditorSettings::get_singleton()->get_project_metadata("recent_files", config_section, Array());
	if (rc.has(p_path)) {
		rc.erase(p_path);
	}
	rc.push_front(p_path);
	if (rc.size() > 10) {
		rc.resize(10);
	}

	EditorSettings::get_singleton()->set_project_metadata("recent_files", config_section, rc);
	_update_recent_scripts();
}

void DocumentEditorContainer::_update_recent_scripts() {
	Array rc = EditorSettings::get_singleton()->get_project_metadata("recent_files", config_section, Array());
	recent_scripts->clear();

	String path;
	for (int i = 0; i < rc.size(); i++) {
		path = rc[i];
		recent_scripts->add_item(path.replace("res://", ""));
	}

	recent_scripts->add_separator();
	recent_scripts->add_shortcut(ED_GET_SHORTCUT("document_editor/clear_recent"));
	recent_scripts->set_item_auto_translate_mode(-1, AUTO_TRANSLATE_MODE_ALWAYS);
	recent_scripts->set_item_disabled(-1, rc.is_empty());

	recent_scripts->reset_size();
}

void DocumentEditorContainer::_open_recent_script(int p_idx) {
	// clear button
	if (p_idx == recent_scripts->get_item_count() - 1) {
		EditorSettings::get_singleton()->set_project_metadata("recent_files", config_section, Array());
		callable_mp(this, &DocumentEditorContainer::_update_recent_scripts).call_deferred();
		return;
	}

	Array rc = EditorSettings::get_singleton()->get_project_metadata("recent_files", config_section, Array());
	ERR_FAIL_INDEX(p_idx, rc.size());

	String path = rc[p_idx];
	// if its not on disk its a help file or deleted
	if (FileAccess::exists(path)) {
		if (_get_recognized_extensions().find(path.get_extension())) {
			Ref<Resource> scr = ResourceLoader::load(path);
			if (scr.is_valid()) {
				edit(scr, true);
				return;
			}
		}

		Error err;
		Ref<TextFile> text_file = _load_text_file(path, &err);
		if (text_file.is_valid()) {
			edit(text_file, true);
			return;
		}
		// if it's a path then it's most likely a deleted file not help
	} else if (path.contains("::")) {
		// Built-in resource.
		String res_path = path.get_slice("::", 0);
		EditorNode::get_singleton()->load_scene_or_resource(res_path, false, false);

		Ref<Resource> res = ResourceLoader::load(path);
		if (res.is_valid()) {
			edit(res, true);
			return;
		}
	} else if (!path.is_resource_file()) {
		_help_class_open(path);
		return;
	}

	rc.remove_at(p_idx);
	EditorSettings::get_singleton()->set_project_metadata("recent_files", config_section, rc);
	_update_recent_scripts();
	_show_error_dialog(path);
}

void DocumentEditorContainer::_show_error_dialog(const String &p_path) {
	error_dialog->set_text(vformat(TTR("Can't open '%s'. The file could have been moved or deleted."), p_path));
	error_dialog->popup_centered();
}

void DocumentEditorContainer::_close_tab(int p_idx, bool p_save) {
	int selected = p_idx;
	if (selected < 0 || selected >= tab_container->get_tab_count()) {
		return;
	}

	Node *tselected = tab_container->get_tab_control(selected);

	if (DocumentEditorBase *current = Object::cast_to<DocumentEditorBase>(tselected)) {
		Ref<Resource> file = current->get_edited_resource();
		if (p_save && file.is_valid()) {
			// Do not try to save internal scripts, but prompt to save in-memory
			// scripts which are not saved to disk yet (have empty path).
			if (!file->is_built_in()) {
				save_current_document();
			}
		}
		if (file.is_valid()) {
			if (!file->get_path().is_empty()) {
				// Only saved scripts can be restored.
				previous_scripts.push_back(file->get_path());
			}
		}
	}

	// Roll back to previous tab instead of just previous history state
	_roll_back_to_pre_tab();

	// Remove all history states related to the closed tab.
	for (int i = 0; i < history.size(); i++) {
		if (history[i].control == tselected) {
			history.remove_at(i);
			i--;
			history_pos--;
		}
	}

	_compress_history_patterns(false);

	int idx = tab_container->get_current_tab();
	if (TextEditorBase *current = Object::cast_to<TextEditorBase>(tselected)) {
		_save_editor_state(current);
	}
	memdelete(tselected);

	if (script_close_queue.is_empty()) {
		if (idx >= tab_container->get_tab_count()) {
			idx = tab_container->get_tab_count() - 1;
		}
		if (idx >= 0) {
			if (history_pos >= 0) {
				idx = tab_container->get_tab_idx_from_control(history[history_pos].control);
			}
			_go_to_tab(idx, true);
		} else {
			_update_history_arrows();
		}

		document_list->update_list();
		_save_layout();
		_update_find_replace_bar();
	}
}

void DocumentEditorContainer::_close_current_tab(bool p_save) {
	_close_tab(tab_container->get_current_tab(), p_save);
}

void DocumentEditorContainer::_close_discard_current_tab(const String &p_str) {
	_close_tab(tab_container->get_current_tab(), false);
	erase_tab_confirm->hide();
}

void DocumentEditorContainer::_close_docs_tab() {
	int child_count = tab_container->get_tab_count();
	for (int i = child_count - 1; i >= 0; i--) {
		if (Object::cast_to<EditorHelp>(tab_container->get_tab_control(i))) {
			_close_tab(i, true);
		}
	}
}

void DocumentEditorContainer::_copy_script_path() {
	if (DocumentEditorBase *seb = _get_current_editor()) {
		Ref<Resource> scr = seb->get_edited_resource();
		DisplayServer::get_singleton()->clipboard_set(scr->get_path());
	}
}

void DocumentEditorContainer::_copy_script_uid() {
	if (DocumentEditorBase *seb = _get_current_editor()) {
		Ref<Resource> scr = seb->get_edited_resource();
		ResourceUID::ID uid = ResourceLoader::get_resource_uid(scr->get_path());
		DisplayServer::get_singleton()->clipboard_set(ResourceUID::get_singleton()->id_to_text(uid));
	}
}

void DocumentEditorContainer::_close_other_tabs() {
	int current_idx = tab_container->get_current_tab();
	for (int i = tab_container->get_tab_count() - 1; i >= 0; i--) {
		if (i != current_idx) {
			script_close_queue.push_back(i);
		}
	}
	_queue_close_tabs();
}

void DocumentEditorContainer::_close_tabs_below() {
	int current_idx = tab_container->get_current_tab();
	for (int i = tab_container->get_tab_count() - 1; i > current_idx; i--) {
		script_close_queue.push_back(i);
	}
	_go_to_tab(current_idx, true);
	_queue_close_tabs();
}

void DocumentEditorContainer::_close_all_tabs() {
	for (int i = tab_container->get_tab_count() - 1; i >= 0; i--) {
		script_close_queue.push_back(i);
	}
	_queue_close_tabs();
}

void DocumentEditorContainer::_queue_close_tabs() {
	while (!script_close_queue.is_empty()) {
		int idx = script_close_queue.front()->get();
		script_close_queue.pop_front();

		tab_container->set_current_tab(idx);
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(idx));
		if (seb) {
			// Maybe there are unsaved changes.
			if (seb->is_unsaved()) {
				_ask_close_current_unsaved_tab(seb);
				erase_tab_confirm->connect(SceneStringName(visibility_changed), callable_mp(this, &DocumentEditorContainer::_queue_close_tabs), CONNECT_ONE_SHOT);
				break;
			}
		}

		_close_current_tab(false);
	}
	_update_find_replace_bar();
}

void DocumentEditorContainer::_ask_close_current_unsaved_tab(DocumentEditorBase *p_seb) {
	erase_tab_confirm->set_text(TTR("Close and save changes?") + "\n\"" + p_seb->get_document_name() + "\"");
	erase_tab_confirm->popup_centered();
}

void DocumentEditorContainer::_resave_scripts(const String &p_str) {
	apply_documents();

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (!seb) {
			continue;
		}

		Ref<Resource> scr = seb->get_edited_resource();

		if (scr->is_built_in()) {
			continue; // Internal script, who cares.
		}

		_auto_format_text(seb);

		Ref<TextFile> text_file = scr;
		if (text_file.is_valid()) {
			seb->apply_code();
			_save_text_file(text_file, text_file->get_path());
			break;
		} else {
			EditorNode::get_singleton()->save_resource(scr);
		}
		seb->tag_saved_version();
	}

	disk_changed->hide();
}

void DocumentEditorContainer::_res_saved_callback(const Ref<Resource> &p_res) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		if (DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i))) {
			if (seb->get_edited_resource() == p_res) {
				seb->tag_saved_version();
			}
		}
	}

	if (p_res.is_valid()) {
		// In case the Resource has built-in scripts.
		_mark_built_in_scripts_as_saved(p_res->get_path());
	}

	_update_filenames();
}

void DocumentEditorContainer::_scene_saved_callback(const String &p_path) {
	// If scene was saved, mark all built-in scripts from that scene as saved.
	_mark_built_in_scripts_as_saved(p_path);
}

void DocumentEditorContainer::_mark_built_in_scripts_as_saved(const String &p_parent_path) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (!seb) {
			continue;
		}

		Ref<Resource> edited_res = seb->get_edited_resource();
		if (!edited_res->is_built_in()) {
			continue; // External script, who cares.
		}

		if (edited_res->get_path().get_slice("::", 0) != p_parent_path) {
			continue; // Wrong scene.
		}
		seb->tag_saved_version();
	}
}

bool DocumentEditorContainer::_test_script_times_on_disk(Ref<Resource> p_for_script) {
	disk_changed_list->clear();
	TreeItem *r = disk_changed_list->create_item();

	bool need_ask = false;
	bool need_reload = false;
	bool use_autoreload = EDITOR_GET("text_editor/behavior/files/auto_reload_scripts_on_external_change");

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (!seb) {
			continue;
		}

		Ref<Resource> edited_res = seb->get_edited_resource();
		if (p_for_script.is_valid() && edited_res.is_valid() && p_for_script != edited_res) {
			continue;
		}

		if (edited_res->is_built_in()) {
			continue; // Internal script, who cares.
		}

		uint64_t last_date = seb->edited_file_data.last_modified_time;
		uint64_t date = FileAccess::get_modified_time(seb->edited_file_data.path);

		if (last_date != date) {
			TreeItem *ti = disk_changed_list->create_item(r);
			ti->set_text(0, seb->edited_file_data.path.get_file());

			if (!use_autoreload || seb->is_unsaved()) {
				need_ask = true;
			}
			need_reload = true;
		}
	}

	if (need_reload) {
		if (!need_ask) {
			reload_documents();
			need_reload = false;
		} else {
			callable_mp((Window *)disk_changed, &Window::popup_centered_ratio).call_deferred(0.3);
		}
	}

	return need_reload;
}

void _import_text_editor_theme(const String &p_file) {
	if (p_file.get_extension() != "tet") {
		EditorToaster::get_singleton()->popup_str(TTR("Importing theme failed. File is not a text editor theme file (.tet)."), EditorToaster::SEVERITY_ERROR);
		return;
	}
	const String theme_name = p_file.get_file().get_basename();
	if (EditorSettings::is_default_text_editor_theme(theme_name.to_lower())) {
		EditorToaster::get_singleton()->popup_str(TTR("Importing theme failed. File name cannot be 'Default', 'Custom', or 'Godot 2'."), EditorToaster::SEVERITY_ERROR);
		return;
	}

	const String theme_dir = EditorPaths::get_singleton()->get_text_editor_themes_dir();
	Ref<DirAccess> d = DirAccess::open(theme_dir);
	Error err = FAILED;
	if (d.is_valid()) {
		err = d->copy(p_file, theme_dir.path_join(p_file.get_file()));
	}

	if (err != OK) {
		EditorToaster::get_singleton()->popup_str(TTR("Importing theme failed. Failed to copy theme file."), EditorToaster::SEVERITY_ERROR);
		return;
	}

	// Reload themes and switch to new theme.
	EditorSettings::get_singleton()->update_text_editor_themes_list();
	EditorSettings::get_singleton()->set_manually("text_editor/theme/color_theme", theme_name, true);
	EditorSettings::get_singleton()->notify_changes();
}

void _save_text_editor_theme_as(const String &p_file) {
	String file = p_file;
	if (p_file.get_extension() != "tet") {
		file += ".tet";
	}

	const String theme_name = file.get_file().get_basename();
	if (EditorSettings::is_default_text_editor_theme(theme_name.to_lower())) {
		EditorToaster::get_singleton()->popup_str(TTR("Saving theme failed. File name cannot be 'Default', 'Custom', or 'Godot 2'."), EditorToaster::SEVERITY_ERROR);
		return;
	}

	const String theme_section = "color_theme";
	const Ref<ConfigFile> cf = memnew(ConfigFile);

	// Use the keys from the Godot 2 theme to know which settings to save.
	HashMap<StringName, Color> text_colors = EditorSettings::get_godot2_text_editor_theme();
	text_colors.sort();
	for (const KeyValue<StringName, Color> &text_color : text_colors) {
		const Color val = EditorSettings::get_singleton()->get_setting(text_color.key);
		const String &key = text_color.key.string().replace("text_editor/theme/highlighting/", "");
		cf->set_value(theme_section, key, val.to_html());
	}

	const Error err = cf->save(file);
	if (err != OK) {
		EditorToaster::get_singleton()->popup_str(TTR("Saving theme failed."), EditorToaster::SEVERITY_ERROR);
		return;
	}

	// Reload themes and switch to saved theme.
	EditorSettings::get_singleton()->update_text_editor_themes_list();
	if (p_file.get_base_dir() == EditorPaths::get_singleton()->get_text_editor_themes_dir()) {
		// Don't need to emit signal or notify changes as the colors are already set.
		EditorSettings::get_singleton()->set_manually("text_editor/theme/color_theme", theme_name, false);
	}
}

bool DocumentEditorContainer::_script_exists(const String &p_path) const {
	if (p_path.is_empty()) {
		return false;
	} else if (p_path.is_resource_file()) {
		return FileAccess::exists(p_path);
	} else {
		return FileAccess::exists(p_path.get_slice("::", 0));
	}
}

void DocumentEditorContainer::_file_dialog_action(const String &p_file) {
	switch (file_dialog_option) {
		case FILE_MENU_NEW_TEXTFILE: {
			Error err;
			{
				Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);
				if (err) {
					EditorNode::get_singleton()->show_warning(TTR("Error writing TextFile:") + "\n" + p_file, TTR("Error!"));
					break;
				}
			}

			if (EditorFileSystem::get_singleton()) {
				if (textfile_extensions.has(p_file.get_extension())) {
					EditorFileSystem::get_singleton()->update_file(p_file);
				}
			}
			[[fallthrough]];
		}
		case FILE_MENU_OPEN: {
			open_file(p_file);
		} break;
		case FILE_MENU_SAVE_AS: {
			if (DocumentEditorBase *current = _get_current_editor()) {
				Ref<Resource> resource = current->get_edited_resource();
				String path = ProjectSettings::get_singleton()->localize_path(p_file);
				Error err = _save_text_file(resource, path);

				if (err != OK) {
					EditorNode::get_singleton()->show_warning(TTR("Error saving file!"));
					return;
				}

				resource->set_path(path);
				_update_filenames();
			}
		} break;
		case THEME_SAVE_AS: {
			_save_text_editor_theme_as(p_file);
		} break;
		case THEME_IMPORT: {
			_import_text_editor_theme(p_file);
		} break;
	}
	file_dialog_option = -1;
}

List<String> DocumentEditorContainer::_get_recognized_extensions() {
	List<String> extensions;
	for (const String &type : handled_resource_types) {
		ResourceLoader::get_recognized_extensions_for_type(type, &extensions);
	}
	return extensions;
}

void DocumentEditorContainer::_menu_option(int p_option) {
	Node *current_editor = tab_container->get_current_tab_control();
	DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(current_editor);
	EditorHelp *eh = Object::cast_to<EditorHelp>(current_editor);
	switch (p_option) {
		case FILE_MENU_NEW_TEXTFILE: {
			file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
			file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			file_dialog_option = FILE_MENU_NEW_TEXTFILE;

			file_dialog->clear_filters();
			for (const String &E : textfile_extensions) {
				file_dialog->add_filter("*." + E, E.to_upper());
			}
			file_dialog->set_title(TTRC("New Text File..."));
			file_dialog->popup_file_dialog();
		} break;
		case FILE_MENU_NEW_SHADER: {
			String base_path = FileSystemDock::get_singleton()->get_current_path().get_base_dir();
			shader_create_dialog->config(base_path.path_join("new_shader"), false, false, "Shader");
			shader_create_dialog->popup_centered();
		} break;
		case FILE_MENU_NEW_INCLUDE: {
			String base_path = FileSystemDock::get_singleton()->get_current_path().get_base_dir();
			shader_create_dialog->config(base_path.path_join("new_shader"), false, false, "ShaderInclude");
			shader_create_dialog->popup_centered();
		} break;
		case FILE_MENU_OPEN: {
			file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
			file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			file_dialog_option = FILE_MENU_OPEN;

			file_dialog->clear_filters();
			for (const String &E : textfile_extensions) {
				file_dialog->add_filter("*." + E, E.to_upper());
			}

			file_dialog->set_title(TTRC("Open File"));
			file_dialog->popup_file_dialog();
			return;
		} break;
		case FILE_MENU_OPEN_SHADER: {
			InspectorDock::get_singleton()->open_resource("Shader");
		} break;
		case FILE_MENU_OPEN_INCLUDE: {
			InspectorDock::get_singleton()->open_resource("ShaderInclude");
		} break;
		case FILE_MENU_REOPEN_CLOSED: {
			if (previous_scripts.is_empty()) {
				return;
			}

			String path = previous_scripts.back()->get();
			previous_scripts.pop_back();

			bool built_in = !path.is_resource_file();

			if (_get_recognized_extensions().find(path.get_extension()) || built_in) {
				if (built_in) {
					String res_path = path.get_slice("::", 0);
					EditorNode::get_singleton()->load_scene_or_resource(res_path, false, false);
				}

				Ref<Resource> scr = ResourceLoader::load(path);
				if (scr.is_null()) {
					EditorNode::get_singleton()->show_warning(TTR("Could not load file at:") + "\n\n" + path, TTR("Error!"));
					file_dialog_option = -1;
					return;
				}

				edit(scr);
				file_dialog_option = -1;
			} else {
				Error error;
				Ref<TextFile> text_file = _load_text_file(path, &error);
				if (error != OK) {
					EditorNode::get_singleton()->show_warning(TTR("Could not load file at:") + "\n\n" + path, TTR("Error!"));
				}

				if (text_file.is_valid()) {
					edit(text_file);
					file_dialog_option = -1;
				}
			}
		} break;
		case FILE_MENU_SAVE_ALL: {
			if (_test_script_times_on_disk()) {
				return;
			}

			save_all_documents();
		} break;
		case FILE_MENU_INSPECT: {
			if (seb && seb->get_edited_resource().is_valid()) {
				EditorNode::get_singleton()->push_item(seb->get_edited_resource().ptr());
			}
		} break;
		case FILE_MENU_INSPECT_NATIVE_SHADER_CODE: {
			if (seb) {
				Ref<Shader> shader = seb->get_edited_resource();
				if (shader.is_valid()) {
					shader->inspect_native_shader_code();
				}
			}
		} break;
		case SEARCH_IN_FILES:
		case REPLACE_IN_FILES: {
			FindInFiles::get_singleton()->open_dock("", REPLACE_IN_FILES == p_option);
		}; break;
		case SEARCH_HELP: {
			_help_search();
		} break;
		case SEARCH_WEBSITE: {
			bool native_class_doc = false;
			if (eh) {
				native_class_doc = EditorHelp::get_doc_data()->class_list.has(eh->get_class());
			}
			if (native_class_doc) {
				String name = eh->get_class().to_lower();
				String doc_url = vformat(GODOT_VERSION_DOCS_URL "/classes/class_%s.html", name);
				OS::get_singleton()->shell_open(doc_url);
			} else if (seb) {
				String doc_url = vformat(GODOT_VERSION_DOCS_URL "%s", seb->get_doc_url_path());
				OS::get_singleton()->shell_open(doc_url);
			} else {
				OS::get_singleton()->shell_open(GODOT_VERSION_DOCS_URL "/");
			}
		} break;
		case FILE_MENU_HISTORY_NEXT: {
			_history_forward();
		} break;
		case FILE_MENU_HISTORY_PREV: {
			_history_back();
		} break;
		case FILE_MENU_SORT: {
			sort_document_editors();
		} break;
		case FILE_MENU_CLOSE: {
			if (seb && seb->is_unsaved()) {
				_ask_close_current_unsaved_tab(seb);
			} else {
				_close_current_tab(false);
			}
		} break;
		case FILE_MENU_CLOSE_DOCS: {
			_close_docs_tab();
		} break;
		case FILE_MENU_CLOSE_OTHER_TABS: {
			_close_other_tabs();
		} break;
		case FILE_MENU_CLOSE_TABS_BELOW: {
			_close_tabs_below();
		} break;
		case FILE_MENU_CLOSE_ALL: {
			_close_all_tabs();
		} break;
		case FILE_MENU_MOVE_UP: {
			if (tab_container->get_current_tab() > 0) {
				tab_container->move_child(current_editor, tab_container->get_current_tab() - 1);
				document_list->update_list();
			}
		} break;
		case FILE_MENU_MOVE_DOWN: {
			if (tab_container->get_current_tab() < tab_container->get_tab_count() - 1) {
				tab_container->move_child(current_editor, tab_container->get_current_tab() + 1);
				document_list->update_list();
			}
		} break;
	}

	if (seb) {
		switch (p_option) {
			case FILE_MENU_SAVE: {
				save_current_document();
			} break;
			case FILE_MENU_SAVE_AS: {
				_auto_format_text(seb);

				Ref<Resource> resource = seb->get_edited_resource();
				Ref<TextFile> text_file = resource;

				if (text_file.is_valid()) {
					file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
					file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
					file_dialog_option = FILE_MENU_SAVE_AS;

					file_dialog->clear_filters();
					file_dialog->set_current_dir(text_file->get_path().get_base_dir());
					file_dialog->set_current_file(text_file->get_path().get_file());
					file_dialog->set_title(TTRC("Save File As..."));
					file_dialog->popup_file_dialog();
					break;
				}

				EditorNode::get_singleton()->push_item(resource.ptr());
				EditorNode::get_singleton()->save_resource_as(resource);

			} break;

			case FILE_MENU_COPY_PATH: {
				_copy_script_path();
			} break;
			case FILE_MENU_COPY_UID: {
				_copy_script_uid();
			} break;
			case FILE_MENU_SHOW_IN_FILE_SYSTEM: {
				const Ref<Resource> res = seb->get_edited_resource();
				String path = res->get_path();
				if (!path.is_empty()) {
					if (res->is_built_in()) {
						path = path.get_slice("::", 0); // Show the scene instead.
					}

					FileSystemDock::get_singleton()->navigate_to_path(path);
				}
			} break;
		}
	} else if (eh) {
		switch (p_option) {
			case HELP_SEARCH_FIND: {
				eh->popup_search();
			} break;
			case HELP_SEARCH_FIND_NEXT: {
				eh->search_again();
			} break;
			case HELP_SEARCH_FIND_PREVIOUS: {
				eh->search_again(true);
			} break;
		}
	}
}

void DocumentEditorContainer::_theme_option(int p_option) {
	switch (p_option) {
		case THEME_IMPORT: {
			file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
			file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			file_dialog_option = THEME_IMPORT;
			file_dialog->clear_filters();
			file_dialog->add_filter("*.tet");
			file_dialog->set_title(TTRC("Import Theme"));
			file_dialog->popup_file_dialog();
		} break;
		case THEME_RELOAD: {
			EditorSettings::get_singleton()->mark_setting_changed("text_editor/theme/color_theme");
			EditorSettings::get_singleton()->notify_changes();
		} break;
		case THEME_SAVE_AS: {
			DocumentEditorContainer::_show_save_theme_as_dialog();
		} break;
	}
}

void DocumentEditorContainer::_show_save_theme_as_dialog() {
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	file_dialog_option = THEME_SAVE_AS;
	file_dialog->clear_filters();
	file_dialog->add_filter("*.tet");
	file_dialog->set_current_path(EditorPaths::get_singleton()->get_text_editor_themes_dir().path_join(EDITOR_GET("text_editor/theme/color_theme")) + " New");
	file_dialog->set_title(TTRC("Save Theme As..."));
	file_dialog->popup_file_dialog();
}

bool DocumentEditorContainer::_has_docs_tab() const {
	const int child_count = tab_container->get_tab_count();
	for (int i = 0; i < child_count; i++) {
		if (Object::cast_to<EditorHelp>(tab_container->get_tab_control(i))) {
			return true;
		}
	}
	return false;
}

bool DocumentEditorContainer::_has_script_tab() const {
	const int child_count = tab_container->get_tab_count();
	for (int i = 0; i < child_count; i++) {
		if (Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i))) {
			return true;
		}
	}
	return false;
}

void DocumentEditorContainer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_apply_editor_settings();
			[[fallthrough]];
		}

		case NOTIFICATION_TRANSLATION_CHANGED: {
			_update_filenames();
			_update_online_doc();
			[[fallthrough]];
		}
		case NOTIFICATION_LAYOUT_DIRECTION_CHANGED:
		case NOTIFICATION_THEME_CHANGED: {
			tab_container->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("DocumentEditor"), EditorStringName(EditorStyles)));

			_calculate_script_name_button_size();

			if (is_main_editor) {
				help_search->set_button_icon(get_editor_theme_icon(SNAME("HelpSearch")));
			}
			site_search->set_button_icon(get_editor_theme_icon(SNAME("ExternalLink")));

			if (is_layout_rtl()) {
				script_forward->set_button_icon(get_editor_theme_icon(SNAME("Back")));
				script_back->set_button_icon(get_editor_theme_icon(SNAME("Forward")));
			} else {
				script_forward->set_button_icon(get_editor_theme_icon(SNAME("Forward")));
				script_back->set_button_icon(get_editor_theme_icon(SNAME("Back")));
			}

			recent_scripts->reset_size();

			toggle_files_button->set_button_icon(get_editor_theme_icon(SNAME("SidebarLeft")));
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (EditorThemeManager::is_generated_theme_outdated() ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("interface/editor/fonts") ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("text_editor") ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("docks/filesystem")) {
				_apply_editor_settings();
			}
		} break;

		case NOTIFICATION_READY: {
			if (is_main_editor) {
				InspectorDock::get_singleton()->connect("request_help", callable_mp(this, &DocumentEditorContainer::_help_class_open));
				EditorNode::get_singleton()->connect("request_help_search", callable_mp(this, &DocumentEditorContainer::_help_search));
			}
			EditorNode::get_singleton()->connect("scene_closed", callable_mp(this, &DocumentEditorContainer::_close_builtin_scripts_from_scene));
			EditorNode::get_singleton()->connect("resource_saved", callable_mp(this, &DocumentEditorContainer::_res_saved_callback));
			EditorNode::get_singleton()->connect("scene_saved", callable_mp(this, &DocumentEditorContainer::_scene_saved_callback));
			FileSystemDock::get_singleton()->connect("files_moved", callable_mp(this, &DocumentEditorContainer::_files_moved));
			FileSystemDock::get_singleton()->connect("file_removed", callable_mp(this, &DocumentEditorContainer::_file_removed));

			script_split->connect("dragged", callable_mp(this, &DocumentEditorContainer::_split_dragged));
			list_split->connect("dragged", callable_mp(this, &DocumentEditorContainer::_split_dragged));

			EditorFileSystem::get_singleton()->connect("filesystem_changed", callable_mp(this, &DocumentEditorContainer::_update_filenames));
#ifdef ANDROID_ENABLED
			set_process(true);
#endif
		} break;

#ifdef ANDROID_ENABLED
		case NOTIFICATION_VISIBILITY_CHANGED: {
			set_process(is_visible_in_tree());
		} break;

		case NOTIFICATION_PROCESS: {
			if (!is_visible()) {
				break;
			}

			const int kb_height = DisplayServer::get_singleton()->virtual_keyboard_get_height();
			if (kb_height == last_kb_height) {
				break;
			}

			last_kb_height = kb_height;
			float spacer_height = 0.0f;

			if (EditorNode::get_singleton()->is_portrait()) {
				// Reset virtual_keyboard_spacer and let EditorNode handle resizing in portrait mode.
				virtual_keyboard_spacer->set_custom_minimum_size(Size2());
			} else {
				const float status_bar_height = 28 * EDSCALE;
				const float control_bottom = get_global_position().y + get_size().y;
				const float extra_bottom = get_viewport_rect().size.y - control_bottom;
				spacer_height = float(kb_height) - extra_bottom - status_bar_height;
				spacer_height = fmax(0, spacer_height);
				virtual_keyboard_spacer->set_custom_minimum_size(Size2(0, spacer_height));
				EditorSceneTabs::get_singleton()->set_visible(kb_height == 0);
			}

			// Make sure caret is visible after resize.
			if (TextEditorBase *editor = Object::cast_to<TextEditorBase>(_get_current_editor())) {
				if (CodeTextEditor *code_editor = editor->get_code_editor()) {
					if (CodeEdit *text_editor = code_editor->get_text_editor()) {
						if (text_editor->has_focus()) {
							text_editor->adjust_viewport_to_caret();
						}
					}
				}
			}
		} break;
#endif
		case NOTIFICATION_APPLICATION_FOCUS_IN: {
			if (is_inside_tree()) {
				_test_script_times_on_disk();
			}
		} break;
	}
}

void DocumentEditorContainer::_close_builtin_scripts_from_scene(const String &p_scene) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		if (DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i))) {
			Ref<Resource> res = seb->get_edited_resource();
			if (res.is_valid() && res->is_built_in() && res->get_path().begins_with(p_scene)) { // Is an internal script and belongs to scene being closed.
				_close_tab(i, false);
				i--;
			}
		}
	}
}

void DocumentEditorContainer::_toggle_files_pressed(bool p_pressed) {
	ERR_FAIL_NULL(list_split);
	list_split->set_visible(p_pressed);
	EditorSettings::get_singleton()->set_project_metadata("files_panel", "show_files_panel", list_split->is_visible());
}

void DocumentEditorContainer::ensure_select_current() {
	if (restoring_layout) {
		return;
	}
	if (tab_container->get_current_tab() >= 0) {
		TextEditorBase *teb = Object::cast_to<TextEditorBase>(_get_current_editor());
		if (teb) {
			teb->enable_editor();
		}
		if (_get_current_editor() && !grab_focus_block && is_visible_in_tree()) {
			_get_current_editor()->ensure_focus();
		}
	}
	_update_find_replace_bar();

	_update_selected_editor_menu();
	_update_online_doc();

	document_list->update_list();

	document_outline->update_outline();
	document_outline->update_visibility();

	_update_document_name_button();
}

void DocumentEditorContainer::_update_online_doc() {
	Node *current = tab_container->get_current_tab_control();

	EditorHelp *eh = Object::cast_to<EditorHelp>(current);
	bool native_class_doc = false;
	if (eh) {
		native_class_doc = EditorHelp::get_doc_data()->class_list.has(eh->get_class());
	}
	if (native_class_doc) {
		String name = eh->get_class();
		String tooltip = vformat(TTR("Open '%s' in Godot online documentation."), name);
		site_search->set_text(TTRC("Open in Online Docs"));
		site_search->set_tooltip_text(tooltip);
	} else {
		site_search->set_text(TTRC("Online Docs"));
		site_search->set_tooltip_text(TTRC("Open Godot online documentation."));
	}
}

Ref<TextFile> DocumentEditorContainer::_load_text_file(const String &p_path, Error *r_error) const {
	if (r_error) {
		*r_error = ERR_FILE_CANT_OPEN;
	}

	String local_path = ProjectSettings::get_singleton()->localize_path(p_path);
	String path = ResourceLoader::path_remap(local_path);

	// Reuse an already-open TextFile resource to avoid creating duplicates.
	// This prevents opening the same file multiple times as "unsaved" tabs.
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (!seb) {
			continue;
		}

		if (seb->edited_file_data.path == local_path) {
			Ref<TextFile> existing_text_file = seb->get_edited_resource();
			if (existing_text_file.is_valid()) {
				if (r_error) {
					*r_error = OK;
				}
				return existing_text_file;
			}
		}
	}

	Ref<TextFile> text_file;
	text_file.instantiate();
	Error err = text_file->load_text(path);

	ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), "Cannot load text file '" + path + "'.");

	text_file->set_file_path(local_path);
	text_file->set_path(local_path, true);

	if (ResourceLoader::get_timestamp_on_load()) {
		text_file->set_last_modified_time(FileAccess::get_modified_time(path));
	}

	if (r_error) {
		*r_error = OK;
	}

	return text_file;
}

Error DocumentEditorContainer::_save_text_file(Ref<TextFile> p_text_file, const String &p_path) {
	Ref<TextFile> sqscr = p_text_file;
	ERR_FAIL_COND_V(sqscr.is_null(), ERR_INVALID_PARAMETER);

	String source = sqscr->get_text();

	Error err;
	{
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);

		ERR_FAIL_COND_V_MSG(err, err, "Cannot save text file '" + p_path + "'.");

		file->store_string(source);
		if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
			return ERR_CANT_CREATE;
		}
	}

	if (ResourceSaver::get_timestamp_on_save()) {
		p_text_file->set_last_modified_time(FileAccess::get_modified_time(p_path));
	}

	EditorFileSystem::get_singleton()->update_file(p_path);

	_res_saved_callback(sqscr);
	return OK;
}

bool DocumentEditorContainer::edit(const Ref<Resource> &p_resource, int p_line, int p_col, bool p_grab_focus) {
	if (p_resource.is_null()) {
		return false;
	}

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (!seb) {
			continue;
		}

		if (seb->get_edited_resource()->get_path() == p_resource->get_path()) {
			TextEditorBase *teb = Object::cast_to<TextEditorBase>(seb);
			if (teb) {
				teb->enable_editor();
			}

			if (tab_container->get_current_tab() != i) {
				grab_focus_block = !p_grab_focus;
				_go_to_tab(i, !teb);
				grab_focus_block = false;
			} else if (is_visible_in_tree() && p_grab_focus) {
				seb->ensure_focus();
			}

			if (teb) {
				DocumentNavigationMarker::get_singleton()->locate_begin();
				if (p_line >= 0) {
					teb->goto_line_and_center_if_necessary(p_line, p_col);
				} else {
					teb->trigger_history_save_on_navigate();
				}
				DocumentNavigationMarker::get_singleton()->locate_end();
			}
			document_list->ensure_current_is_visible();
			return true;
		}
	}

	// doesn't have it, make a new one
	DocumentEditorBase *seb = nullptr;

	for (int i = document_editor_func_count - 1; i >= 0; i--) {
		seb = document_editor_funcs[i](p_resource);
		if (seb) {
			break;
		}
	}
	ERR_FAIL_NULL_V(seb, false);

	seb->set_edited_resource(p_resource);

	grab_focus_block = !p_grab_focus;
	tab_container->add_child(seb);
	grab_focus_block = false;

	if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(seb)) {
		if (p_grab_focus) {
			teb->enable_editor();
		}

		DocumentEditorBase::EditMenusBase *editor_edit_menu = nullptr;
		for (DocumentEditorBase::EditMenusBase *editor_menu : editor_menus) {
			if (editor_menu->handles(teb)) {
				editor_edit_menu = editor_menu;
				break;
			}
		}
		if (!editor_edit_menu) {
			editor_edit_menu = teb->create_edit_menu(this);
		}
		if (!editor_edit_menu->get_parent()) {
			editor_edit_menu->hide();
			editor_menus.push_back(editor_edit_menu);
			menu_hb->add_child(editor_edit_menu);
			menu_hb->move_child(editor_edit_menu, 3);
			for (int i = 0; i < editor_edit_menu->get_child_count(); ++i) {
				Control *c = Object::cast_to<Control>(editor_edit_menu->get_child(i));
				if (c) {
					c->set_shortcut_context(this);
				}
			}
		}
	}

	_update_filenames();
	grab_focus_block = !p_grab_focus;
	_go_to_tab(tab_container->get_tab_count() - 1);
	ensure_select_current();
	grab_focus_block = false;
	_add_recent_script(p_resource->get_path());

	// If we delete a script within the filesystem, the original resource path
	// is lost, so keep it as `edited_file_data` to figure out the exact tab to delete.
	seb->edited_file_data.path = p_resource->get_path();
	seb->edited_file_data.last_modified_time = FileAccess::get_modified_time(p_resource->get_path());

	seb->connect("name_changed", callable_mp(this, &DocumentEditorContainer::_update_filenames));
	seb->connect("_validation_updated", callable_mp(this, &DocumentEditorContainer::_validation_updated));

	if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(seb)) {
		// Syntax highlighting.
		bool highlighter_set = false;
		for (int i = 0; i < syntax_highlighters.size(); i++) {
			Ref<EditorSyntaxHighlighter> highlighter = syntax_highlighters[i]->_create();
			if (highlighter.is_null()) {
				continue;
			}
			teb->add_syntax_highlighter(highlighter);

			if (highlighter_set) {
				continue;
			}

			PackedStringArray languages = highlighter->_get_supported_languages();
			if (languages.has(p_resource->get_path().get_extension())) {
				teb->set_syntax_highlighter(highlighter);
				highlighter_set = true;
				continue;
			}

			// For built-in shaders, which do not use file extensions in their path.
			ShaderTextEditor *ste = Object::cast_to<ShaderTextEditor>(teb);
			if (ste && languages.has("gdshader")) {
				teb->set_syntax_highlighter(highlighter);
				highlighter_set = true;
			}
		}

		teb->connect("_request_save_new_history", callable_mp(this, &DocumentEditorContainer::_save_new_history).bind(teb));
		teb->connect("_request_save_previous_state", callable_mp(this, &DocumentEditorContainer::_save_previous_state).bind(teb));

#ifndef DISABLE_DEPRECATED
		teb->connect("search_in_files_requested", callable_mp(FindInFiles::get_singleton(), &FindInFiles::open_dock).bind("", false));
		teb->connect("replace_in_files_requested", callable_mp(FindInFiles::get_singleton(), &FindInFiles::open_dock).bind("", true));
#endif

		if (document_editor_cache->has_section(p_resource->get_path())) {
			teb->set_edit_state(document_editor_cache->get_value(p_resource->get_path(), "state"), p_grab_focus);
		}

		if (CodeTextEditor *cte = teb->get_code_editor()) {
			cte->set_zoom_factor(zoom_factor);
			cte->connect("zoomed", callable_mp(this, &DocumentEditorContainer::_set_script_zoom_factor));
			cte->connect(SceneStringName(visibility_changed), callable_mp(this, &DocumentEditorContainer::_update_code_editor_zoom_factor).bind(cte));
		}
	}

	sort_document_editors();
	_save_layout();

	//test for modification, maybe the script was not edited but was loaded

	_test_script_times_on_disk(p_resource);

	if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(seb)) {
		DocumentNavigationMarker::get_singleton()->locate_begin();
		if (p_line >= 0) {
			teb->goto_line_and_center_if_necessary(p_line, p_col);
		} else {
			teb->trigger_history_save_on_navigate();
		}
		DocumentNavigationMarker::get_singleton()->locate_end();
	}

	return true;
}

void DocumentEditorContainer::reload_open_files() {
	_test_script_times_on_disk();
}

Control *DocumentEditorContainer::get_active_editor() const {
	return tab_container->get_current_tab_control();
}

Vector<Control *> DocumentEditorContainer::get_all_editors() const {
	Vector<Control *> editors;
	editors.reserve(tab_container->get_tab_count());
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		editors.push_back(tab_container->get_tab_control(i));
	}
	return editors;
}

void DocumentEditorContainer::focus_active_editor() {
	DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(get_active_editor());
	EditorHelp *eh = Object::cast_to<EditorHelp>(get_active_editor());
	if (seb) {
		seb->get_base_editor()->grab_focus();
	} else if (eh) {
		eh->set_focused();
	}
}

PackedStringArray DocumentEditorContainer::get_unsaved_documents() const {
	PackedStringArray unsaved_list;

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (seb && seb->is_unsaved()) {
			unsaved_list.append(seb->get_document_name());
		}
	}
	return unsaved_list;
}

PackedStringArray DocumentEditorContainer::get_unsaved_files() const {
	PackedStringArray unsaved_list;

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (seb && seb->is_unsaved()) {
			unsaved_list.append(seb->get_edited_resource()->get_path());
		}
	}
	return unsaved_list;
}

void DocumentEditorContainer::save_current_document() {
	DocumentEditorBase *current = _get_current_editor();
	if (!current || _test_script_times_on_disk()) {
		return;
	}

	_auto_format_text(current);

	Ref<Resource> resource = current->get_edited_resource();
	Ref<TextFile> text_file = resource;

	if (text_file.is_valid()) {
		current->apply_code();
		_save_text_file(text_file, text_file->get_path());
		return;
	}

	EditorNode::get_singleton()->save_resource(resource);
}

void DocumentEditorContainer::save_all_documents() {
	HashSet<String> scenes_to_save;

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (!seb) {
			continue;
		}

		_auto_format_text(seb);

		if (!seb->is_unsaved()) {
			continue;
		}

		Ref<Resource> edited_res = seb->get_edited_resource();
		if (edited_res.is_valid()) {
			seb->apply_code();
		}

		if (!edited_res->is_built_in()) {
			Ref<TextFile> text_file = edited_res;
			if (text_file.is_valid()) {
				_save_text_file(text_file, text_file->get_path());
				continue;
			}

			// External script, save it.
			EditorNode::get_singleton()->save_resource(edited_res);
		} else {
			// For built-in scripts, save their scenes instead.
			const String scene_path = edited_res->get_path().get_slice("::", 0);
			if (!scene_path.is_empty() && !scenes_to_save.has(scene_path)) {
				scenes_to_save.insert(scene_path);
			}
		}
	}

	if (!scenes_to_save.is_empty()) {
		EditorNode::get_singleton()->save_scene_list(scenes_to_save);
	}

	_update_filenames();
}

void DocumentEditorContainer::update_document_times() {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		if (DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i))) {
			seb->edited_file_data.last_modified_time = FileAccess::get_modified_time(seb->edited_file_data.path);
		}
	}
}

void DocumentEditorContainer::apply_documents() const {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (!seb) {
			continue;
		}

		if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(seb)) {
			teb->insert_final_newline();
		}
		seb->apply_code();
	}
}

void DocumentEditorContainer::reload_documents(bool p_refresh_only) {
	// Call deferred to make sure it runs on the main thread.
	if (!Thread::is_main_thread()) {
		callable_mp(this, &DocumentEditorContainer::_reload_documents).call_deferred(p_refresh_only);
		return;
	}
	_reload_documents(p_refresh_only);
}

void DocumentEditorContainer::_reload_documents(bool p_refresh_only) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (!seb) {
			continue;
		}

		Ref<Resource> edited_res = seb->get_edited_resource();

		if (edited_res->is_built_in()) {
			continue; // Internal script, who cares.
		}

		if (p_refresh_only) {
			// Make sure the modified time is correct.
			seb->edited_file_data.last_modified_time = FileAccess::get_modified_time(edited_res->get_path());
		} else {
			uint64_t last_date = seb->edited_file_data.last_modified_time;
			uint64_t date = FileAccess::get_modified_time(edited_res->get_path());

			if (last_date == date) {
				continue;
			}
			seb->edited_file_data.last_modified_time = date;

			Ref<JSON> json = edited_res;
			if (json.is_valid()) {
				Ref<JSON> rel_json = ResourceLoader::load(json->get_path(), json->get_class(), ResourceFormatLoader::CACHE_MODE_IGNORE);
				ERR_CONTINUE(rel_json.is_null());
				json->parse(rel_json->get_parsed_text(), true);
			}

			Ref<TextFile> text_file = edited_res;
			if (text_file.is_valid()) {
				text_file->reload_from_file();
			}

			ShaderTextEditor *ste = Object::cast_to<ShaderTextEditor>(seb);
			if (ste) {
				Ref<Shader> shader = edited_res;
				Ref<ShaderInclude> shader_inc = edited_res;
				String new_code;
				if (shader.is_valid()) {
					Ref<Shader> rel_sh = ResourceLoader::load(shader->get_path(), shader->get_class(), ResourceFormatLoader::CACHE_MODE_IGNORE);
					ERR_CONTINUE(rel_sh.is_null());
					new_code = rel_sh->get_code();
				} else if (shader_inc.is_valid()) {
					Ref<ShaderInclude> rel_sh_inc = ResourceLoader::load(shader_inc->get_path(), shader_inc->get_class(), ResourceFormatLoader::CACHE_MODE_IGNORE);
					ERR_CONTINUE(rel_sh_inc.is_null());
					new_code = rel_sh_inc->get_code();
				}
				ste->set_code_block_changed(new_code);
			}
		}

		if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(seb)) {
			Dictionary state = teb->get_edit_state();
			teb->reload_text();
			teb->set_edit_state(state);
		}
	}

	disk_changed->hide();
}

void DocumentEditorContainer::_auto_format_text(DocumentEditorBase *p_seb) {
	if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(p_seb)) {
		if (trim_trailing_whitespace_on_save) {
			teb->trim_trailing_whitespace();
		}
		if (trim_final_newlines_on_save) {
			teb->trim_final_newlines();
		}
		if (convert_indent_on_save) {
			teb->convert_indent();
		}
	}
}

void DocumentEditorContainer::open_text_file_create_dialog(const String &p_base_path, const String &p_base_name) {
	_menu_option(FILE_MENU_NEW_TEXTFILE);
	file_dialog->set_current_dir(p_base_path);
	file_dialog->set_current_file(p_base_name);
}

Ref<Resource> DocumentEditorContainer::open_file(const String &p_file) {
	if (_get_recognized_extensions().find(p_file.get_extension())) {
		Ref<Resource> scr = ResourceLoader::load(p_file);
		if (scr.is_null()) {
			EditorNode::get_singleton()->show_warning(TTR("Could not load file at:") + "\n\n" + p_file, TTR("Error!"));
			return Ref<Resource>();
		}

		edit(scr);
		return scr;
	}

	Error error;
	Ref<TextFile> text_file = _load_text_file(p_file, &error);
	if (error != OK) {
		EditorNode::get_singleton()->show_warning(TTR("Could not load file at:") + "\n\n" + p_file, TTR("Error!"));
		return Ref<Resource>();
	}

	if (text_file.is_valid()) {
		edit(text_file);
		return text_file;
	}
	return Ref<Resource>();
}

bool DocumentEditorContainer::can_open_file(const String &p_file) const {
	if (p_file.is_empty() || !FileAccess::exists(p_file)) {
		return false;
	}

	const String &file_type = ResourceLoader::get_resource_type(p_file);
	if (handled_resource_types.has(file_type)) {
		return true;
	}

	if (is_main_editor && textfile_extensions.has(p_file.get_extension())) {
		Error err;
		Ref<TextFile> text_file = _load_text_file(p_file, &err);
		if (text_file.is_valid() && err == OK) {
			return true;
		}
	}
	return false;
}

Error DocumentEditorContainer::close_file(const String &p_file) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (seb && seb->get_edited_resource()->get_path() == p_file) {
			if (seb->is_unsaved()) {
				seb->get_edited_resource()->reload_from_file();
			}
			_close_tab(i, false);
			return OK;
		}
	}
	return ERR_FILE_NOT_FOUND;
}

void DocumentEditorContainer::_save_editor_state(DocumentEditorBase *p_editor) {
	if (restoring_layout) {
		return;
	}

	const String &path = p_editor->get_edited_resource()->get_path();
	if (path.is_empty()) {
		return;
	}

	if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(p_editor)) {
		document_editor_cache->set_value(path, "state", teb->get_edit_state());
	}
	// This is saved later when we save the editor layout.
}

void DocumentEditorContainer::_save_layout() {
	if (restoring_layout) {
		return;
	}

	EditorNode::get_singleton()->save_editor_layout_delayed();
}

void DocumentEditorContainer::_apply_editor_settings() {
	textfile_extensions.clear();
	const Vector<String> textfile_ext = ((String)(EDITOR_GET("docks/filesystem/textfile_extensions"))).split(",", false);
	for (const String &E : textfile_ext) {
		textfile_extensions.insert(E);
	}

	trim_trailing_whitespace_on_save = EDITOR_GET("text_editor/behavior/files/trim_trailing_whitespace_on_save");
	trim_final_newlines_on_save = EDITOR_GET("text_editor/behavior/files/trim_final_newlines_on_save");
	convert_indent_on_save = EDITOR_GET("text_editor/behavior/files/convert_indent_on_save");

	document_outline->update_editor_settings();

	_update_autosave_timer();

	DocumentNameDisplayMode previous_document_display_name_mode = document_display_name_mode;
	document_display_name_mode = (DocumentNameDisplayMode)(int)EDITOR_GET("text_editor/script_list/list_script_names_as");
	if (previous_document_display_name_mode != document_display_name_mode) {
		_update_filenames();
	}

	DocumentSortMode previous_document_sort_by_mode = document_sort_by_mode;
	bool previous_document_sort_group_help_pages = document_sort_group_help_pages;
	document_sort_by_mode = (DocumentSortMode)(int)EDITOR_GET("text_editor/script_list/sort_scripts_by");
	document_sort_group_help_pages = EDITOR_GET("text_editor/script_list/group_help_pages");
	if (document_sort_by_mode != previous_document_sort_by_mode || document_sort_group_help_pages != previous_document_sort_group_help_pages) {
		sort_document_editors();
	}

	document_list->update_editor_settings();

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(tab_container->get_tab_control(i))) {
			teb->update_settings();
		}
	}
}

void DocumentEditorContainer::_update_filenames() {
	if (restoring_layout) {
		return;
	}

	// Disambiguate filenames.
	const int num_open_editors = tab_container->get_tab_count();

	PackedStringArray disambiguated_names;
	disambiguated_names.reserve(num_open_editors);
	PackedStringArray full_paths;
	full_paths.reserve(num_open_editors);

	LocalVector<bool> unsaved;
	unsaved.reserve(num_open_editors);

	for (int i = 0; i < num_open_editors; i++) {
		Control *editor = tab_container->get_tab_control(i);
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(editor);
		EditorHelp *eh = Object::cast_to<EditorHelp>(editor);
		if (seb) {
			unsaved.push_back(seb->is_unsaved());

			const String &path = seb->get_edited_resource()->get_path();
			const String &name = seb->get_document_name();
			String display_name;

			switch (document_display_name_mode) {
				case DISPLAY_NAME: {
					display_name = name;
				} break;
				case DISPLAY_DIR_AND_NAME: {
					if (!path.get_base_dir().get_file().is_empty()) {
						display_name = path.get_base_dir().get_file().path_join(name);
					} else {
						display_name = name;
					}
				} break;
				case DISPLAY_FULL_PATH: {
					display_name = path;
				} break;
			}
			full_paths.push_back(path);
			disambiguated_names.push_back(display_name);
		}
		if (eh) {
			const String name = eh->get_class().is_empty() ? TTR("Unknown Help Class") : eh->get_class().unquote();
			unsaved.push_back(false);

			full_paths.push_back(name);
			disambiguated_names.push_back(name);
		}
	}

	EditorNode::disambiguate_filenames(full_paths, disambiguated_names);

	for (int i = 0; i < disambiguated_names.size(); i++) {
		String name = disambiguated_names[i];
		if (unsaved[i]) {
			name += "(*)";
		}
		tab_container->set_tab_title(i, name);
		tab_container->get_tab_control(i)->set_name(name);
	}

	document_list->update_list();
	_update_document_name_button();
}

void DocumentEditorContainer::_files_moved(const String &p_old_file, const String &p_new_file) {
	if (!document_editor_cache->has_section(p_old_file)) {
		return;
	}

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (seb && seb->edited_file_data.path == p_old_file) {
			seb->edited_file_data.path = p_new_file;
			break;
		}
	}

	Variant state = document_editor_cache->get_value(p_old_file, "state");
	document_editor_cache->erase_section(p_old_file);
	document_editor_cache->set_value(p_new_file, "state", state);
	// This is saved later when we save the editor layout.
}

void DocumentEditorContainer::_file_removed(const String &p_removed_file) {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (seb && seb->edited_file_data.path == p_removed_file) {
			// The script is deleted with no undo, so just close the tab.
			_close_tab(i, false);
		}
	}

	// Check closed.
	if (document_editor_cache->has_section(p_removed_file)) {
		document_editor_cache->erase_section(p_removed_file);
	}
}

void DocumentEditorContainer::_update_find_replace_bar() {
	if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(_get_current_editor())) {
		teb->set_find_replace_bar(find_replace_bar);
	} else {
		find_replace_bar->set_text_edit(nullptr);
		find_replace_bar->hide();
	}
}

void DocumentEditorContainer::_autosave_scripts() {
	save_all_documents();
}

void DocumentEditorContainer::_update_autosave_timer() {
	if (!autosave_timer->is_inside_tree()) {
		return;
	}

	float autosave_time = EDITOR_GET("text_editor/behavior/files/autosave_interval_secs");
	if (autosave_time > 0) {
		autosave_timer->set_wait_time(autosave_time);
		autosave_timer->start();
	} else {
		autosave_timer->stop();
	}
}

void DocumentEditorContainer::_split_dragged(float) {
	_save_layout();
}

void DocumentEditorContainer::input(const Ref<InputEvent> &p_event) {
	// This feature can be disabled to avoid interfering with other uses of the additional
	// mouse buttons, such as push-to-talk in a VoIP program.
	if (EDITOR_GET("interface/editor/input/mouse_extra_buttons_navigate_history")) {
		const Ref<InputEventMouseButton> mb = p_event;

		// Navigate the script history using additional mouse buttons present on some mice.
		// This must be hardcoded as the editor shortcuts dialog doesn't allow assigning
		// more than one shortcut per action.
		Control *focus_owner = get_viewport()->gui_get_focus_owner();
		if (focus_owner && (this == focus_owner || is_ancestor_of(focus_owner))) {
			if (mb.is_valid() && mb->is_pressed() && is_visible_in_tree()) {
				if (mb->get_button_index() == MouseButton::MB_XBUTTON1) {
					_history_back();
				}

				if (mb->get_button_index() == MouseButton::MB_XBUTTON2) {
					_history_forward();
				}
			}
		}
	}
}

void DocumentEditorContainer::shortcut_input(const Ref<InputEvent> &p_event) {
	ERR_FAIL_COND(p_event.is_null());

	if (!is_visible_in_tree() || !p_event->is_pressed()) {
		return;
	}

	Control *focus_owner = get_viewport()->gui_get_focus_owner();
	if (!(focus_owner && (this == focus_owner || is_ancestor_of(focus_owner)))) {
		return;
	}

	if (ED_IS_SHORTCUT("document_editor/next_script", p_event)) {
		document_list->goto_next_document();
		accept_event();
	}
	if (ED_IS_SHORTCUT("document_editor/prev_script", p_event)) {
		document_list->goto_next_document(true);
		accept_event();
	}

	static const HashMap<String, int> shortcut_mappings = {
		{ "document_editor/close_other_tabs", FILE_MENU_CLOSE_OTHER_TABS },
		{ "document_editor/close_tabs_below", FILE_MENU_CLOSE_TABS_BELOW },
		{ "document_editor/close_docs", FILE_MENU_CLOSE_DOCS },
		{ "document_editor/copy_path", FILE_MENU_COPY_PATH },
		{ "document_editor/copy_uid", FILE_MENU_COPY_UID },
		{ "document_editor/show_in_file_system", FILE_MENU_SHOW_IN_FILE_SYSTEM },
		{ "document_editor/move_document_up", FILE_MENU_MOVE_UP },
		{ "document_editor/move_document_down", FILE_MENU_MOVE_DOWN },
		{ "document_editor/sort_documents", FILE_MENU_SORT },
	};

	for (const KeyValue<String, int> &E : shortcut_mappings) {
		if (ED_IS_SHORTCUT(E.key, p_event)) {
			_menu_option(E.value);
			accept_event();
			break;
		}
	}

}

void DocumentEditorContainer::_prepare_file_menu() {
	PopupMenu *p_menu = file_menu->get_popup();
	p_menu->set_item_disabled(p_menu->get_item_index(FILE_MENU_REOPEN_CLOSED), previous_scripts.is_empty());

	DocumentEditorBase *seb = _get_current_editor();
	const Ref<Resource> res = seb ? seb->get_edited_resource() : Ref<Resource>();
	if (res.is_valid()) {
		p_menu->set_item_disabled(p_menu->get_item_index(FILE_MENU_SAVE), res.is_null());
		p_menu->set_item_disabled(p_menu->get_item_index(FILE_MENU_SAVE_AS), res.is_null());
	}
	p_menu->set_item_disabled(p_menu->get_item_index(FILE_MENU_SAVE_ALL), !_has_script_tab());
	p_menu->set_item_disabled(p_menu->get_item_index(FILE_MENU_CLOSE), tab_container->get_tab_count() < 1);
	p_menu->set_item_disabled(p_menu->get_item_index(FILE_MENU_CLOSE_ALL), tab_container->get_tab_count() < 1);
}

void DocumentEditorContainer::_file_menu_closed() {
	PopupMenu *menu = file_menu->get_popup();
	for (int i = 0; i < menu->get_item_count(); i++) {
		menu->set_item_disabled(i, false);
	}
}

struct SortItemData {
	String sort_key;
	int category = 0;
	int index = 0;
	Node *editor = nullptr;

	bool operator<(const SortItemData &id) const {
		if (category == id.category) {
			if (sort_key == id.sort_key) {
				return index < id.index;
			} else {
				return sort_key.filenocasecmp_to(id.sort_key) < 0;
			}
		} else {
			return category < id.category;
		}
	}
};

void DocumentEditorContainer::sort_document_editors() {
	if (restoring_layout) {
		return;
	}
	if (document_sort_by_mode == SORT_BY_NONE && !document_sort_group_help_pages) {
		return;
	}
	const int num_open_editors = tab_container->get_tab_count();

	LocalVector<SortItemData> sort_items;
	sort_items.reserve(num_open_editors);

	for (int i = 0; i < num_open_editors; i++) {
		Control *editor = tab_container->get_tab_control(i);
		SortItemData item;
		item.index = i;
		item.editor = editor;

		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(editor);
		EditorHelp *eh = Object::cast_to<EditorHelp>(editor);
		if (seb) {
			item.category = 0;

			switch (document_sort_by_mode) {
				case SORT_BY_NAME: {
					const String &name = seb->get_document_name();
					item.sort_key = name.to_lower();
				} break;
				case SORT_BY_PATH: {
					const String &path = seb->get_edited_resource()->get_path();
					item.sort_key = path;
				} break;
				default:
					break;
			}
		}
		if (eh) {
			const String name = eh->get_class().is_empty() ? TTR("Unknown Help Class") : eh->get_class().unquote();

			item.category = document_sort_group_help_pages ? 1 : 0;
			item.sort_key = name.to_lower();
		}
		sort_items.push_back(item);
	}

	sort_items.sort();

	for (int i = 0; i < (int)sort_items.size(); i++) {
		tab_container->move_child(sort_items[i].editor, i);
	}

	document_list->update_list();
}

void DocumentEditorContainer::set_window_layout(Ref<ConfigFile> p_layout) {
	if (!p_layout->has_section_key(config_section, "open_scripts") && !p_layout->has_section_key(config_section, "open_help")) {
		return;
	}

	Array scripts = p_layout->get_value(config_section, "open_scripts");
	Array helps;
	if (p_layout->has_section_key(config_section, "open_help")) {
		helps = p_layout->get_value(config_section, "open_help");
	}

	restoring_layout = true;

	HashSet<String> loaded_scripts;
	List<String> extensions = _get_recognized_extensions();

	DocumentNavigationMarker::get_singleton()->init_begin();

	_set_script_zoom_factor(p_layout->get_value(config_section, "zoom_factor", 1.0f));

	for (const Variant &v : scripts) {
		String path = v;

		Dictionary script_info = v;
		if (!script_info.is_empty()) {
			path = script_info["path"];
		}

		if (!_script_exists(path)) {
			if (document_editor_cache->has_section(path)) {
				document_editor_cache->erase_section(path);
			}
			continue;
		} else if (!path.is_resource_file() && !EditorNode::get_singleton()->is_scene_open(path.get_slice("::", 0))) {
			continue;
		}
		loaded_scripts.insert(path);

		bool is_script = false;
		if (path.is_resource_file()) {
			is_script = extensions.find(path.get_extension());
		} else {
			Ref<Resource> res = ResourceCache::get_ref(path);
			if (res.is_valid()) {
				is_script = true;
			} else {
				continue;
			}
		}

		if (is_script) {
			Ref<Resource> scr = ResourceLoader::load(path);
			if (scr.is_null()) {
				continue;
			}
			if (!edit(scr, false)) {
				continue;
			}
		} else {
			Error error;
			Ref<TextFile> text_file = _load_text_file(path, &error);
			if (error != OK || text_file.is_null()) {
				continue;
			}
			if (!edit(text_file, false)) {
				continue;
			}
		}

		if (!script_info.is_empty()) {
			if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(tab_container->get_tab_control(tab_container->get_tab_count() - 1))) {
				teb->set_edit_state(script_info["state"]);
			}
		}
	}

	for (int i = 0; i < helps.size(); i++) {
		String path = helps[i];
		if (path.is_empty()) { // invalid, skip
			continue;
		}
		_help_class_open(path);
	}

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		tab_container->get_tab_control(i)->set_meta("__editor_pass", Variant());
	}

	if (p_layout->has_section_key(config_section, "script_split_offset")) {
		script_split->set_split_offset(p_layout->get_value(config_section, "script_split_offset"));
	}

	if (p_layout->has_section_key(config_section, "list_split_offset")) {
		list_split->set_split_offset(p_layout->get_value(config_section, "list_split_offset"));
	}

	// Remove any deleted editors that have been removed between launches.
	Vector<String> cached_editors = document_editor_cache->get_sections();
	for (const String &E : cached_editors) {
		if (loaded_scripts.has(E)) {
			continue;
		}

		if (!_script_exists(E)) {
			document_editor_cache->erase_section(E);
		}
	}

	restoring_layout = false;

	_update_filenames();
	sort_document_editors();
	DocumentNavigationMarker::get_singleton()->init_end();

	bool selected_saved_history = false;
	if (p_layout->has_section_key(config_section, "selected_script")) {
		String selected_script = p_layout->get_value(config_section, "selected_script");
		// If the selected script is not in the list of open scripts, select nothing.
		for (int i = 0; i < tab_container->get_tab_count() && !selected_script.is_empty(); i++) {
			DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
			if (seb && seb->get_edited_resource()->get_path() == selected_script) {
				_go_to_tab(i, true);
				selected_saved_history = true;
				break;
			}
		}
	}
	ensure_select_current();

	if (!selected_saved_history && tab_container->get_child_count() > 0) {
		DocumentNavigationMarker::get_singleton()->locate_begin();
		Control *tselected = tab_container->get_current_tab_control();
		TextEditorBase *teb = Object::cast_to<TextEditorBase>(tselected);
		if (teb && teb->get_code_editor()) {
			teb->get_code_editor()->trigger_history_save_on_navigate();
		}

		EditorHelp *eh = Object::cast_to<EditorHelp>(tselected);
		if (eh) {
			eh->trigger_history_save_on_navigate();
		}
		DocumentNavigationMarker::get_singleton()->locate_end();
	}
}

void DocumentEditorContainer::get_window_layout(Ref<ConfigFile> p_layout) {
	Array scripts;
	Array helps;
	String selected_script;
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		if (DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i))) {
			const String path = seb->get_edited_resource()->get_path();
			if (path.is_empty()) {
				continue;
			}

			if (tab_container->get_current_tab_control() == tab_container->get_tab_control(i)) {
				selected_script = path;
			}

			_save_editor_state(seb);
			scripts.push_back(path);
		}

		if (EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i))) {
			helps.push_back(eh->get_class());
		}
	}

	p_layout->set_value(config_section, "open_scripts", scripts);
	p_layout->set_value(config_section, "selected_script", selected_script);
	p_layout->set_value(config_section, "open_help", helps);
	p_layout->set_value(config_section, "script_split_offset", script_split->get_split_offset());
	p_layout->set_value(config_section, "list_split_offset", list_split->get_split_offset());
	p_layout->set_value(config_section, "zoom_factor", zoom_factor);

	// Save the cache.
	document_editor_cache->save(EditorPaths::get_singleton()->get_project_settings_dir().path_join(cache_path));
}

void DocumentEditorContainer::_help_class_open(const String &p_class) {
	if (p_class.is_empty()) {
		return;
	}

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));

		if (eh && eh->get_class() == p_class) {
			_go_to_tab(i, true);
			return;
		}
	}

	EditorHelp *eh = memnew(EditorHelp);

	eh->connect("go_to_help", callable_mp(this, &DocumentEditorContainer::_help_class_goto));
	eh->connect("_request_save_new_history", callable_mp(this, &DocumentEditorContainer::_save_new_history).bind(eh));

	// Go to the class first, so it has a class name when switching to it.
	DocumentNavigationMarker::get_singleton()->locate_begin();
	eh->go_to_class(p_class);
	DocumentNavigationMarker::get_singleton()->locate_end();

	tab_container->add_child(eh);
	_update_filenames();
	_go_to_tab(tab_container->get_tab_count() - 1);
	sort_document_editors();
	_add_recent_script(p_class);
	_save_layout();
}

void DocumentEditorContainer::_help_class_goto(const String &p_desc) {
	const String cname = p_desc.get_slicec(':', 1);

	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		EditorHelp *eh = Object::cast_to<EditorHelp>(tab_container->get_tab_control(i));

		if (eh && eh->get_class() == cname) {
			DocumentNavigationMarker::get_singleton()->locate_begin();
			eh->go_to_help(p_desc);
			DocumentNavigationMarker::get_singleton()->locate_end();
			_go_to_tab(i);
			return;
		}
	}

	EditorHelp *eh = memnew(EditorHelp);

	eh->connect("go_to_help", callable_mp(this, &DocumentEditorContainer::_help_class_goto));
	eh->connect("_request_save_new_history", callable_mp(this, &DocumentEditorContainer::_save_new_history).bind(eh));

	DocumentNavigationMarker::get_singleton()->locate_begin();
	eh->go_to_help(p_desc);
	DocumentNavigationMarker::get_singleton()->locate_end();

	tab_container->add_child(eh);
	_update_filenames();
	_go_to_tab(tab_container->get_tab_count() - 1);
	sort_document_editors();
	_add_recent_script(eh->get_class());
	_save_layout();
}

void DocumentEditorContainer::_update_selected_editor_menu() {
	for (DocumentEditorBase::EditMenusBase *editor_menu : editor_menus) {
		editor_menu->set_visible(editor_menu->handles(_get_current_editor()));
	}

	PopupMenu *search_popup = script_search_menu->get_popup();
	search_popup->clear();
	if (Object::cast_to<EditorHelp>(tab_container->get_current_tab_control())) {
		search_popup->add_shortcut(ED_SHORTCUT("document_editor/find", TTRC("Find..."), KeyModifierMask::CMD_OR_CTRL | Key::F), HELP_SEARCH_FIND);
		search_popup->add_shortcut(ED_SHORTCUT("document_editor/find_next", TTRC("Find Next"), Key::F3), HELP_SEARCH_FIND_NEXT);
		search_popup->add_shortcut(ED_SHORTCUT("document_editor/find_previous", TTRC("Find Previous"), KeyModifierMask::SHIFT | Key::F3), HELP_SEARCH_FIND_PREVIOUS);
		search_popup->add_separator();
		search_popup->add_shortcut(ED_GET_SHORTCUT("editor/find_in_files"), SEARCH_IN_FILES);
		search_popup->add_shortcut(ED_GET_SHORTCUT("editor/replace_in_files"), REPLACE_IN_FILES);
		script_search_menu->show();
	} else {
		if (tab_container->get_tab_count() == 0) {
			search_popup->add_shortcut(ED_GET_SHORTCUT("editor/find_in_files"), SEARCH_IN_FILES);
			search_popup->add_shortcut(ED_GET_SHORTCUT("editor/replace_in_files"), REPLACE_IN_FILES);
			script_search_menu->show();
		} else {
			script_search_menu->hide();
		}
	}
}

void DocumentEditorContainer::_update_history_pos(int p_new_pos) {
	Node *n = tab_container->get_current_tab_control();
	history_pos = p_new_pos;
	tab_container->set_current_tab(tab_container->get_tab_idx_from_control(history[history_pos].control));

	n = history[history_pos].control;

	if (DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(n)) {
		if (TextEditorBase *teb = Object::cast_to<TextEditorBase>(n)) {
			teb->set_edit_state(history[history_pos].state);
		}
		seb->ensure_focus();

		seb->validate_script();
	}

	if (EditorHelp *eh = Object::cast_to<EditorHelp>(n)) {
		eh->set_scroll(history[history_pos].state["row"]);
		eh->set_focused();
	}

	n->set_meta("__editor_pass", ++edit_pass);
	_update_history_arrows();
}

void DocumentEditorContainer::_history_forward() {
	if (history_pos < history.size() - 1) {
		DocumentNavigationMarker::get_singleton()->traverse_begin();
		_update_history_pos(history_pos + 1);
		DocumentNavigationMarker::get_singleton()->traverse_end();
	}
}

void DocumentEditorContainer::_history_back() {
	if (history_pos > 0) {
		DocumentNavigationMarker::get_singleton()->traverse_begin();
		_update_history_pos(history_pos - 1);
		DocumentNavigationMarker::get_singleton()->traverse_end();
	}
}

void DocumentEditorContainer::_roll_back_to_pre_tab() {
	Control *tselected = tab_container->get_current_tab_control();
	if (history_pos == -1 || history[history_pos].control != tselected) {
		return;
	}

	int pos = -1;
	for (int i = history_pos - 1; i >= 0; i--) {
		if (history[i].control != tselected) {
			pos = i;
			break;
		}
	}

	if (pos == -1) {
		history_pos = -1;
		history.clear();
	} else {
		_update_history_pos(pos);
		history.resize(history_pos + 1);
	}
}

DocumentEditorBase *DocumentEditorContainer::get_resource_editor(const Ref<Resource> &p_res) const {
	for (int i = 0; i < tab_container->get_tab_count(); i++) {
		DocumentEditorBase *seb = Object::cast_to<DocumentEditorBase>(tab_container->get_tab_control(i));
		if (seb && seb->get_edited_resource() == p_res) {
			return seb;
		}
	}
	return nullptr;
}

void DocumentEditorContainer::_calculate_script_name_button_size() {
	Ref<Font> font = script_name_button->get_theme_font(SceneStringName(font), SNAME("Button"));
	HorizontalAlignment alignment = script_name_button->get_text_alignment();
	int font_size = script_name_button->get_theme_font_size(SceneStringName(font_size), SNAME("Button"));
	String text = script_name_button->get_text();
	int jst_flags = TextServer::JUSTIFICATION_WORD_BOUND | TextServer::JUSTIFICATION_KASHIDA | TextServer::JUSTIFICATION_SKIP_LAST_LINE | TextServer::JUSTIFICATION_DO_NOT_SKIP_SINGLE_LINE;
	TextServer::Direction direction = TextServer::Direction(script_name_button->get_text_direction());
	Vector2 text_size = font->get_string_size(text, alignment, -1, font_size, jst_flags, direction, TextServer::ORIENTATION_HORIZONTAL);

	script_name_width = text_size.x + script_name_button->get_theme_stylebox(CoreStringName(normal))->get_content_margin(SIDE_LEFT) + script_name_button->get_theme_stylebox(CoreStringName(normal))->get_content_margin(SIDE_RIGHT);
	_calculate_script_name_button_ratio();
}

void DocumentEditorContainer::_calculate_script_name_button_ratio() {
	const float total_width = script_name_button_hbox->get_size().width;
	if (total_width <= 0) {
		return;
	}

	// Make the ratios a fraction bigger, to avoid unnecessary trimming.
	const float extra_ratio = 4 / total_width;

	const float script_name_ratio = MIN(1, script_name_width / total_width + extra_ratio);
	script_name_button->set_stretch_ratio(script_name_ratio);

	float ratio_left = 1 - script_name_ratio;
	script_name_button_left_spacer->set_stretch_ratio(ratio_left / 2);
	script_name_button_right_spacer->set_stretch_ratio(ratio_left / 2);
}

void DocumentEditorContainer::_update_document_name_button() {
	if (tab_container->get_current_tab() >= 0) {
		const String &name = tab_container->get_tab_title(tab_container->get_current_tab());
		script_name_button->set_text(name);
		script_name_button->show();
		_calculate_script_name_button_size();
	} else {
		script_name_button->hide();
		script_name_button->set_text(String());
	}
}

void DocumentEditorContainer::_help_search(const String &p_text) {
	DocumentEditor::get_singleton()->open_help_search_dialog(p_text);
}

void DocumentEditorContainer::register_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter) {
	ERR_FAIL_COND(p_syntax_highlighter.is_null());

	if (!syntax_highlighters.has(p_syntax_highlighter)) {
		syntax_highlighters.push_back(p_syntax_highlighter);
	}
}

void DocumentEditorContainer::unregister_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter) {
	ERR_FAIL_COND(p_syntax_highlighter.is_null());

	syntax_highlighters.erase(p_syntax_highlighter);
}

int DocumentEditorContainer::document_editor_func_count = 0;
CreateDocumentEditorFunc DocumentEditorContainer::document_editor_funcs[DocumentEditorContainer::DOCUMENT_EDITOR_FUNC_MAX];

void DocumentEditorContainer::register_create_editor_function(CreateDocumentEditorFunc p_func) {
	ERR_FAIL_COND(document_editor_func_count == DOCUMENT_EDITOR_FUNC_MAX);
	document_editor_funcs[document_editor_func_count++] = p_func;
}

void DocumentEditorContainer::_validation_updated() {
	document_list->update_list();

	document_outline->update_outline();
	document_outline->update_visibility();
}

void DocumentEditorContainer::_set_script_zoom_factor(float p_zoom_factor) {
	if (zoom_factor == p_zoom_factor) {
		return;
	}

	zoom_factor = p_zoom_factor;
}

void DocumentEditorContainer::_update_code_editor_zoom_factor(CodeTextEditor *p_code_text_editor) {
	if (p_code_text_editor && p_code_text_editor->is_visible_in_tree() && zoom_factor != p_code_text_editor->get_zoom_factor()) {
		p_code_text_editor->set_zoom_factor(zoom_factor);
	}
}

DocumentEditorContainer::DocumentEditorContainer(bool p_is_main_editor, const String &p_config_section, const String &p_cache_path) {
	is_main_editor = p_is_main_editor;
	config_section = p_config_section;
	cache_path = p_cache_path;

	document_editor_cache.instantiate();
	document_editor_cache->load(EditorPaths::get_singleton()->get_project_settings_dir().path_join(cache_path));

	VBoxContainer *main_container = memnew(VBoxContainer);
	add_child(main_container);

	menu_hb = memnew(HBoxContainer);
	main_container->add_child(menu_hb);

	script_split = memnew(HSplitContainer);
	main_container->add_child(script_split);
	script_split->set_v_size_flags(SIZE_EXPAND_FILL);
	script_split->set_drag_nested_intersections(true);

#ifdef ANDROID_ENABLED
	virtual_keyboard_spacer = memnew(Control);
	virtual_keyboard_spacer->set_h_size_flags(SIZE_EXPAND_FILL);
	main_container->add_child(virtual_keyboard_spacer);
#endif

	list_split = memnew(VSplitContainer);
	script_split->add_child(list_split);
	list_split->set_v_size_flags(SIZE_EXPAND_FILL);
	list_split->set_drag_nested_intersections(true);

	document_list = memnew(DocumentList(this));

	list_split->add_child(document_list);
	script_split->set_split_offset(200 * EDSCALE);

	document_outline = memnew(DocumentOutline(this));
	document_outline->set_custom_minimum_size(Size2(0, 90));
	document_outline->set_v_size_flags(SIZE_EXPAND_FILL);
	list_split->add_child(document_outline);

	VBoxContainer *code_editor_container = memnew(VBoxContainer);
	script_split->add_child(code_editor_container);

	tab_container = memnew(TabContainer);
	tab_container->set_tabs_visible(false);
	tab_container->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	code_editor_container->add_child(tab_container);
	tab_container->set_h_size_flags(SIZE_EXPAND_FILL);
	tab_container->set_v_size_flags(SIZE_EXPAND_FILL);

	find_replace_bar = memnew(FindReplaceBar);
	code_editor_container->add_child(find_replace_bar);
	find_replace_bar->hide();

	ED_SHORTCUT("document_editor/sort_documents", TTRC("Sort Documents"));
	ED_SHORTCUT("document_editor/move_document_up", TTRC("Move Document Up"));
	ED_SHORTCUT("document_editor/move_document_down", TTRC("Move Document Down"));
	// FIXME: These should be `Key::GREATER` and `Key::LESS` but those don't work.
	ED_SHORTCUT("document_editor/next_script", TTRC("Next Document"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::PERIOD);
	ED_SHORTCUT("document_editor/prev_script", TTRC("Previous Document"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::COMMA);

	ED_SHORTCUT("document_editor/save", TTRC("Save"), KeyModifierMask::ALT | KeyModifierMask::CMD_OR_CTRL | Key::S);
	ED_SHORTCUT("document_editor/save_as", TTRC("Save As..."));
	ED_SHORTCUT("document_editor/save_all", TTRC("Save All"), KeyModifierMask::SHIFT | KeyModifierMask::ALT | Key::S);
	ED_SHORTCUT_OVERRIDE("document_editor/save_all", "macos", KeyModifierMask::META | KeyModifierMask::CTRL | Key::S);

	ED_SHORTCUT("document_editor/copy_path", TTRC("Copy File Path"));
	ED_SHORTCUT("document_editor/copy_uid", TTRC("Copy File UID"));
	ED_SHORTCUT("document_editor/show_in_file_system", TTRC("Show in FileSystem"));

	ED_SHORTCUT_ARRAY("document_editor/history_previous", TTRC("History Previous"), { int32_t(KeyModifierMask::ALT | Key::LEFT), int32_t(Key::BACK) });
	ED_SHORTCUT_ARRAY("document_editor/history_next", TTRC("History Next"), { int32_t(KeyModifierMask::ALT | Key::RIGHT), int32_t(Key::FORWARD) });
	ED_SHORTCUT_OVERRIDE("document_editor/history_previous", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::LEFT);
	ED_SHORTCUT_OVERRIDE("document_editor/history_next", "macos", KeyModifierMask::ALT | KeyModifierMask::META | Key::RIGHT);

	ED_SHORTCUT("document_editor/close_file", TTRC("Close"), KeyModifierMask::CMD_OR_CTRL | Key::W);
	ED_SHORTCUT("document_editor/close_all", TTRC("Close All"));
	ED_SHORTCUT("document_editor/close_other_tabs", TTRC("Close Other Tabs"));
	ED_SHORTCUT("document_editor/close_tabs_below", TTRC("Close Tabs Below"));
	ED_SHORTCUT("document_editor/close_docs", TTRC("Close Docs"));

	ED_SHORTCUT("document_editor/toggle_files_panel", TTRC("Toggle Files Panel"), KeyModifierMask::CMD_OR_CTRL | Key::BACKSLASH);

	ED_SHORTCUT("document_editor/reopen_closed_script", TTRC("Reopen Closed Document"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::T);
	ED_SHORTCUT("document_editor/clear_recent", TTRC("Clear Recent Files"));

	TextEditorBase::register_shortcuts();

	set_process_input(true);
	set_process_shortcut_input(true);

	toggle_files_button = memnew(Button);
	toggle_files_button->set_theme_type_variation(SceneStringName(FlatButton));
	toggle_files_button->set_v_size_flags(SIZE_EXPAND | SIZE_SHRINK_CENTER);
	toggle_files_button->set_tooltip_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	toggle_files_button->connect(SceneStringName(toggled), callable_mp(this, &DocumentEditorContainer::_toggle_files_pressed));
	toggle_files_button->set_accessibility_name(TTRC("Toggle Document List"));
	toggle_files_button->set_toggle_mode(true);
	toggle_files_button->set_flat(true);
	toggle_files_button->set_shortcut_context(this);
	toggle_files_button->set_shortcut(ED_GET_SHORTCUT("document_editor/toggle_files_panel"));
	menu_hb->add_child(toggle_files_button);
	bool initial_state = EditorSettings::get_singleton()->get_project_metadata("files_panel", "show_files_panel", true);
	toggle_files_button->set_pressed_no_signal(initial_state);
	_toggle_files_pressed(initial_state);

	VSeparator *sep = memnew(VSeparator);
	menu_hb->add_child(sep);

	file_menu = memnew(MenuButton);
	file_menu->set_flat(false);
	file_menu->set_theme_type_variation("FlatMenuButton");
	file_menu->set_text(TTRC("File"));
	file_menu->set_switch_on_hover(true);
	file_menu->set_shortcut_context(this);
	file_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &DocumentEditorContainer::_menu_option));
	file_menu->get_popup()->connect("about_to_popup", callable_mp(this, &DocumentEditorContainer::_prepare_file_menu));
	file_menu->get_popup()->connect("popup_hide", callable_mp(this, &DocumentEditorContainer::_file_menu_closed));
	menu_hb->add_child(file_menu);
	recent_scripts = memnew(PopupMenu);
	recent_scripts->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	recent_scripts->connect(SceneStringName(id_pressed), callable_mp(this, &DocumentEditorContainer::_open_recent_script));
	_update_recent_scripts();

	theme_submenu = memnew(PopupMenu);
	theme_submenu->connect(SceneStringName(id_pressed), callable_mp(this, &DocumentEditorContainer::_theme_option));
	theme_submenu->add_shortcut(ED_SHORTCUT("document_editor/import_theme", TTRC("Import Theme...")), THEME_IMPORT);
	theme_submenu->add_shortcut(ED_SHORTCUT("document_editor/reload_theme", TTRC("Reload Theme")), THEME_RELOAD);
	theme_submenu->add_separator();
	theme_submenu->add_shortcut(ED_SHORTCUT("document_editor/save_theme_as", TTRC("Save Theme As...")), THEME_SAVE_AS);

	if (is_main_editor) {
		file_menu->get_popup()->add_shortcut(ED_SHORTCUT("document_editor/new_textfile", TTRC("New Text File..."), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::N), FILE_MENU_NEW_TEXTFILE);
		file_menu->get_popup()->add_shortcut(ED_SHORTCUT("document_editor/open", TTRC("Open...")), FILE_MENU_OPEN);
	} else {
		file_menu->get_popup()->add_shortcut(ED_SHORTCUT("document_editor/new_shader", TTRC("New Shader..."), KeyModifierMask::CMD_OR_CTRL | Key::N), FILE_MENU_NEW_SHADER);
		file_menu->get_popup()->add_shortcut(ED_SHORTCUT("document_editor/new_include", TTRC("New Shader Include..."), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::N), FILE_MENU_NEW_INCLUDE);
		file_menu->get_popup()->add_shortcut(ED_SHORTCUT("document_editor/open_shader", TTRC("Load Shader File..."), KeyModifierMask::CMD_OR_CTRL | Key::O), FILE_MENU_OPEN_SHADER);
		file_menu->get_popup()->add_shortcut(ED_SHORTCUT("document_editor/open_include", TTRC("Load Shader Include File..."), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::O), FILE_MENU_OPEN_INCLUDE);
	}
	file_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("document_editor/reopen_closed_script"), FILE_MENU_REOPEN_CLOSED);
	file_menu->get_popup()->add_submenu_node_item(TTRC("Open Recent"), recent_scripts, FILE_MENU_OPEN_RECENT);
	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("document_editor/save"), FILE_MENU_SAVE);
	file_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("document_editor/save_as"), FILE_MENU_SAVE_AS);
	file_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("document_editor/save_all"), FILE_MENU_SAVE_ALL);
	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("document_editor/close_file"), FILE_MENU_CLOSE);
	file_menu->get_popup()->add_shortcut(ED_GET_SHORTCUT("document_editor/close_all"), FILE_MENU_CLOSE_ALL);
	file_menu->get_popup()->add_separator();
	file_menu->get_popup()->add_submenu_node_item(TTRC("Theme"), theme_submenu, FILE_MENU_THEME_SUBMENU);

	script_search_menu = memnew(MenuButton);
	script_search_menu->set_flat(false);
	script_search_menu->set_theme_type_variation("FlatMenuButton");
	script_search_menu->set_text(TTRC("Search"));
	script_search_menu->set_switch_on_hover(true);
	script_search_menu->set_shortcut_context(this);
	script_search_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &DocumentEditorContainer::_menu_option));
	menu_hb->add_child(script_search_menu);

	script_name_button_hbox = memnew(HBoxContainer);
	script_name_button_hbox->set_h_size_flags(SIZE_EXPAND_FILL);
	script_name_button_hbox->add_theme_constant_override("separation", 0);
	script_name_button_hbox->connect(SceneStringName(item_rect_changed), callable_mp(this, &DocumentEditorContainer::_calculate_script_name_button_ratio));
	menu_hb->add_child(script_name_button_hbox);

	script_name_button_left_spacer = memnew(Control);
	script_name_button_left_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	script_name_button_hbox->add_child(script_name_button_left_spacer);

	script_name_button = memnew(Button);
	script_name_button->set_flat(true);
	script_name_button->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	script_name_button->set_h_size_flags(SIZE_EXPAND_FILL);
	script_name_button->set_tooltip_text(TTRC("Navigate to document list."));
	script_name_button->connect(SceneStringName(pressed), callable_mp(document_list, &DocumentList::ensure_current_is_visible));
	script_name_button_hbox->add_child(script_name_button);

	script_name_button_right_spacer = memnew(Control);
	script_name_button_right_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	script_name_button_hbox->add_child(script_name_button_right_spacer);

	site_search = memnew(Button);
	site_search->set_theme_type_variation(SceneStringName(FlatButton));
	site_search->set_accessibility_name(TTRC("Site Search"));
	site_search->connect(SceneStringName(pressed), callable_mp(this, &DocumentEditorContainer::_menu_option).bind(SEARCH_WEBSITE));
	menu_hb->add_child(site_search);

	if (is_main_editor) {
		help_search = memnew(Button);
		help_search->set_theme_type_variation(SceneStringName(FlatButton));
		help_search->set_text(TTRC("Search Help"));
		help_search->connect(SceneStringName(pressed), callable_mp(this, &DocumentEditorContainer::_menu_option).bind(SEARCH_HELP));
		menu_hb->add_child(help_search);
		help_search->set_tooltip_text(TTRC("Search the reference documentation."));
	}

	menu_hb->add_child(memnew(VSeparator));

	script_back = memnew(Button);
	script_back->set_theme_type_variation(SceneStringName(FlatButton));
	script_back->set_tooltip_text(TTRC("Go to previous edited document."));
	script_back->set_shortcut(ED_GET_SHORTCUT("document_editor/history_previous"));
	script_back->set_shortcut_context(this);
	script_back->set_disabled(true);
	menu_hb->add_child(script_back);
	script_back->connect(SceneStringName(pressed), callable_mp(this, &DocumentEditorContainer::_history_back));

	script_forward = memnew(Button);
	script_forward->set_theme_type_variation(SceneStringName(FlatButton));
	script_forward->set_tooltip_text(TTRC("Go to next edited document."));
	script_forward->set_shortcut(ED_GET_SHORTCUT("document_editor/history_next"));
	script_forward->set_shortcut_context(this);
	script_forward->set_disabled(true);
	menu_hb->add_child(script_forward);
	script_forward->connect(SceneStringName(pressed), callable_mp(this, &DocumentEditorContainer::_history_forward));

	tab_container->connect("tab_changed", callable_mp(this, &DocumentEditorContainer::ensure_select_current).unbind(1));

	erase_tab_confirm = memnew(ConfirmationDialog);
	erase_tab_confirm->set_flag(Window::FLAG_RESIZE_DISABLED, true);
	erase_tab_confirm->set_ok_button_text(TTRC("Save"));
	erase_tab_confirm->add_button(TTRC("Discard"), DisplayServer::get_singleton()->get_swap_cancel_ok(), "discard");
	erase_tab_confirm->connect(SceneStringName(confirmed), callable_mp(this, &DocumentEditorContainer::_close_current_tab).bind(true));
	erase_tab_confirm->connect("custom_action", callable_mp(this, &DocumentEditorContainer::_close_discard_current_tab));
	add_child(erase_tab_confirm);

	file_dialog_option = -1;
	file_dialog = memnew(EditorFileDialog);
	add_child(file_dialog);
	file_dialog->connect("file_selected", callable_mp(this, &DocumentEditorContainer::_file_dialog_action));

	shader_create_dialog = memnew(ShaderCreateDialog);
	shader_create_dialog->connect("shader_created", callable_mp(this, &DocumentEditorContainer::_resource_created));
	shader_create_dialog->connect("shader_include_created", callable_mp(this, &DocumentEditorContainer::_resource_created));
	add_child(shader_create_dialog);

	error_dialog = memnew(AcceptDialog);
	add_child(error_dialog);

	disk_changed = memnew(ConfirmationDialog);
	{
		disk_changed->set_title(TTRC("Files have been modified outside Godot"));

		VBoxContainer *vbc = memnew(VBoxContainer);
		disk_changed->add_child(vbc);

		Label *files_are_newer_label = memnew(Label);
		files_are_newer_label->set_text(TTRC("The following files are newer on disk:"));
		vbc->add_child(files_are_newer_label);

		disk_changed_list = memnew(Tree);
		disk_changed_list->set_hide_root(true);
		disk_changed_list->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		disk_changed_list->set_accessibility_name(TTRC("The following files are newer on disk:"));
		disk_changed_list->set_v_size_flags(SIZE_EXPAND_FILL);
		vbc->add_child(disk_changed_list);

		Label *what_action_label = memnew(Label);
		what_action_label->set_text(TTRC("What action should be taken?"));
		vbc->add_child(what_action_label);

		disk_changed->connect(SceneStringName(confirmed), callable_mp(this, &DocumentEditorContainer::reload_documents).bind(false));
		disk_changed->set_ok_button_text(TTRC("Reload from disk"));

		disk_changed->add_button(TTRC("Ignore external changes"), !DisplayServer::get_singleton()->get_swap_cancel_ok(), "resave");
		disk_changed->connect("custom_action", callable_mp(this, &DocumentEditorContainer::_resave_scripts));
	}

	add_child(disk_changed);

	autosave_timer = memnew(Timer);
	autosave_timer->set_one_shot(false);
	autosave_timer->connect(SceneStringName(tree_entered), callable_mp(this, &DocumentEditorContainer::_update_autosave_timer));
	autosave_timer->connect("timeout", callable_mp(this, &DocumentEditorContainer::_autosave_scripts));
	add_child(autosave_timer);

	history_pos = -1;

	trim_trailing_whitespace_on_save = EDITOR_GET("text_editor/behavior/files/trim_trailing_whitespace_on_save");
	trim_final_newlines_on_save = EDITOR_GET("text_editor/behavior/files/trim_final_newlines_on_save");
	convert_indent_on_save = EDITOR_GET("text_editor/behavior/files/convert_indent_on_save");

	//// Highlighters ////

	Ref<EditorJSONSyntaxHighlighter> json_syntax_highlighter;
	json_syntax_highlighter.instantiate();
	register_syntax_highlighter(json_syntax_highlighter);

	Ref<EditorMarkdownSyntaxHighlighter> markdown_syntax_highlighter;
	markdown_syntax_highlighter.instantiate();
	register_syntax_highlighter(markdown_syntax_highlighter);

	Ref<EditorConfigFileSyntaxHighlighter> config_file_syntax_highlighter;
	config_file_syntax_highlighter.instantiate();
	register_syntax_highlighter(config_file_syntax_highlighter);

	Ref<GDShaderSyntaxHighlighter> gdshader_syntax_highlighter;
	gdshader_syntax_highlighter.instantiate();
	register_syntax_highlighter(gdshader_syntax_highlighter);
}

