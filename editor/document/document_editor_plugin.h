/**************************************************************************/
/*  document_editor_plugin.h                                              */
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

#pragma once

#include "editor/docks/editor_dock.h"
#include "editor/document/document_editor_container.h"
#include "editor/plugins/editor_plugin.h"

class EditorHelpSearch;

class DocumentEditor : public EditorDock {
	GDCLASS(DocumentEditor, EditorDock);

	inline static DocumentEditor *document_editor = nullptr;

	DocumentEditorContainer *document_container = nullptr;

	EditorHelpSearch *help_search_dialog = nullptr;

	void _update_margins();

protected:
	virtual void update_layout(EditorDock::DockLayout p_layout, int p_slot) override;

public:
	static DocumentEditor *get_singleton() { return document_editor; }

	DocumentEditorContainer *get_document_container() { return document_container; }

	void focus_editor();

	void reload_documents(bool p_refresh_only = false) { document_container->reload_documents(p_refresh_only); }

	void open_help_search_dialog(const String &p_search_text = String());

	void open_text_file_create_dialog(const String &p_base_path, const String &p_base_name = "") { document_container->open_text_file_create_dialog(p_base_path, p_base_name); }
	Ref<Resource> open_file(const String &p_file) { return document_container->open_file(p_file); }
	Error close_file(const String &p_file) { return document_container->close_file(p_file); }

	_FORCE_INLINE_ bool edit(const Ref<Resource> &p_resource, bool p_grab_focus = true) { return edit(p_resource, -1, 0, p_grab_focus); }
	bool edit(const Ref<Resource> &p_resource, int p_line, int p_col, bool p_grab_focus = true) { return document_container->edit(p_resource, p_line, p_col, p_grab_focus); }

	void reload_open_files() { document_container->reload_open_files(); }
	PackedStringArray get_unsaved_files() const { return document_container->get_unsaved_files(); }
	void save_current_document() { document_container->save_current_document(); }
	void save_all_documents() { document_container->save_all_documents(); }
	void update_document_times() { document_container->update_document_times(); }

	DocumentEditorBase *get_current_editor() const { return document_container->get_current_editor(); }

	void goto_help(const String &p_desc) { document_container->goto_help(p_desc); }

	void register_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter) { document_container->register_syntax_highlighter(p_syntax_highlighter); }
	void unregister_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter) { document_container->unregister_syntax_highlighter(p_syntax_highlighter); }

	DocumentEditor();
	~DocumentEditor();
};

class DocumentEditorPlugin : public EditorPlugin {
	GDCLASS(DocumentEditorPlugin, EditorPlugin);

	DocumentEditor *document_editor = nullptr;

	Ref<Shortcut> make_floating_shortcut;

	static inline DocumentEditorPlugin *document_editor_plugin = nullptr;

protected:
	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;

public:
	static DocumentEditorPlugin *get_singleton() { return document_editor_plugin; }

	virtual String get_plugin_name() const override { return "Docs"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;

	virtual String get_unsaved_status(const String &p_for_scene) const override;
	virtual void save_external_data() override;
	virtual void apply_changes() override { document_editor->get_document_container()->apply_documents(); }

	virtual void set_window_layout(Ref<ConfigFile> p_layout) override;
	virtual void get_window_layout(Ref<ConfigFile> p_layout) override;

	DocumentEditorPlugin();
	~DocumentEditorPlugin();
};
