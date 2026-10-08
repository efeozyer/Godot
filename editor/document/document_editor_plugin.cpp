/**************************************************************************/
/*  document_editor_plugin.cpp                                            */
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

#include "document_editor_plugin.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "editor/doc/editor_help_search.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/document/document_navigation_marker.h"
#include "editor/document/text_editor.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/script/find_in_files.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "scene/resources/text_file.h"

void DocumentEditor::focus_editor() {
	EditorDockManager::get_singleton()->force_focus_dock(this);
}

void DocumentEditor::update_layout(EditorDock::DockLayout p_layout, int p_slot) {
	begin_bulk_theme_override();
	if (p_layout == DOCK_LAYOUT_FLOATING) {
		remove_theme_constant_override("margin_left");
		remove_theme_constant_override("margin_right");
		remove_theme_constant_override("margin_bottom");
	} else {
		int margin = EditorNode::get_singleton()->get_editor_theme()->get_constant("base_margin", EditorStringName(Editor));
		add_theme_constant_override("margin_left", margin);
		add_theme_constant_override("margin_right", margin);
		add_theme_constant_override("margin_bottom", margin);
	}
	end_bulk_theme_override();
}

void DocumentEditor::_update_margins() {
	update_layout(get_current_layout(), get_current_slot());
}

void DocumentEditor::open_help_search_dialog(const String &p_search_text) {
	help_search_dialog->popup_dialog(p_search_text);
}

DocumentEditor::DocumentEditor() {
	document_editor = this;

	set_allow_switch_screen(false);

	document_container = memnew(DocumentEditorContainer(true, "DocumentEditor", "document_editor_cache.cfg"));
	document_container->set_handled_resource_types({ "JSON" });
	add_child(document_container);

	help_search_dialog = memnew(EditorHelpSearch);
	add_child(help_search_dialog);
	help_search_dialog->connect("go_to_help", callable_mp(document_container, &DocumentEditorContainer::goto_help));

	FindInFilesContainer *find_in_files = FindInFiles::get_singleton()->get_container();
	find_in_files->connect("files_modified", callable_mp(document_container, &DocumentEditorContainer::reload_open_files));

	EditorNode::get_singleton()->get_gui_base()->connect(SceneStringName(theme_changed), callable_mp(this, &DocumentEditor::_update_margins));
}

DocumentEditor::~DocumentEditor() {
	DocumentNavigationMarker::release_singleton();
}

void DocumentEditorPlugin::shortcut_input(const Ref<InputEvent> &p_event) {
	if (p_event.is_null() || !p_event->is_pressed() || p_event->is_echo()) {
		return;
	}

	if (make_floating_shortcut.is_valid() && make_floating_shortcut->matches_event(p_event)) {
		document_editor->make_floating();
	}
}

void DocumentEditorPlugin::edit(Object *p_object) {
	if (!p_object) {
		return;
	}

	if (Object::cast_to<JSON>(p_object) || Object::cast_to<TextFile>(p_object)) {
		document_editor->edit(Object::cast_to<Resource>(p_object));
	}
}

bool DocumentEditorPlugin::handles(Object *p_object) const {
	if (Object::cast_to<JSON>(p_object)) {
		// This is here to stop resource files of class JSON from getting confused
		// with json files and being opened in the text editor.
		return Object::cast_to<JSON>(p_object)->get_path().get_extension().to_lower() == "json";
	}

	return Object::cast_to<TextFile>(p_object) != nullptr;
}

void DocumentEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		document_editor->focus_editor();
		document_editor->get_document_container()->ensure_select_current();
	}
}

String DocumentEditorPlugin::get_unsaved_status(const String &p_for_scene) const {
	const PackedStringArray unsaved_documents = document_editor->get_document_container()->get_unsaved_documents();
	if (unsaved_documents.is_empty()) {
		return String();
	}

	PackedStringArray message;
	if (!p_for_scene.is_empty()) {
		PackedStringArray unsaved_built_in_documents;

		const String scene_file = p_for_scene.get_file();
		for (const String &E : unsaved_documents) {
			if (!E.is_resource_file() && E.contains(scene_file)) {
				unsaved_built_in_documents.append(E);
			}
		}

		if (unsaved_built_in_documents.is_empty()) {
			return String();
		} else {
			message.push_back(TTR("There are unsaved changes in the following built-in resource(s)"));
			message.append_array(unsaved_built_in_documents);
			return String("\n").join(message);
		}
	}

	message.push_back(TTR("Save changes to the following file(s) before quitting?"));
	message.append_array(unsaved_documents);
	return String("\n").join(message);
}

void DocumentEditorPlugin::save_external_data() {
	if (!EditorNode::get_singleton()->is_exiting()) {
		document_editor->save_all_documents();
	}
}

void DocumentEditorPlugin::set_window_layout(Ref<ConfigFile> p_layout) {
	if (bool(EDITOR_GET("text_editor/behavior/files/restore_scripts_on_load"))) {
		document_editor->get_document_container()->set_window_layout(p_layout);
	}
}

void DocumentEditorPlugin::get_window_layout(Ref<ConfigFile> p_layout) {
	document_editor->get_document_container()->get_window_layout(p_layout);
}

DocumentEditorPlugin::DocumentEditorPlugin() {
	document_editor_plugin = this;

	make_floating_shortcut = ED_SHORTCUT_AND_COMMAND("document_editor/make_floating", TTRC("Make Floating"));
	set_process_shortcut_input(true);

	memnew(FindInFiles);

	TextEditor::register_editor();

	document_editor = memnew(DocumentEditor);
	document_editor->set_name(TTRC("Docs"));
	document_editor->set_icon_name("Help");
	document_editor->set_available_layouts(EditorDock::DOCK_LAYOUT_MAIN_SCREEN | EditorDock::DOCK_LAYOUT_FLOATING);
	document_editor->set_default_slot(EditorDock::DOCK_SLOT_MAIN_SCREEN);
	document_editor->set_dock_shortcut(ED_GET_SHORTCUT("editor/editor_docs"));
	document_editor->connect("_focused", callable_mp(document_editor->get_document_container(), &DocumentEditorContainer::focus_active_editor));

	EditorDockManager::get_singleton()->add_dock(document_editor);
}

DocumentEditorPlugin::~DocumentEditorPlugin() {
	memdelete(FindInFiles::get_singleton());
}
