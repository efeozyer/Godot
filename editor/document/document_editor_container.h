/**************************************************************************/
/*  document_editor_container.h                                           */
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

#include "core/error/error_list.h"
#include "core/io/config_file.h"
#include "editor/document/document_editor_base.h"
#include "scene/gui/margin_container.h"
#include "scene/resources/text_file.h"

class CodeTextEditor;
class EditorFileDialog;
class EditorHelpSearch;
class FilterLineEdit;
class HSplitContainer;
class ItemList;
class TabContainer;
class Tree;
class WindowWrapper;

class DocumentList : public VBoxContainer {
	GDCLASS(DocumentList, VBoxContainer);

	struct ItemData {
		String display_name;
		String tooltip;
		Ref<Texture2D> icon;
		int index = 0;
		bool hidden = false;
		bool has_error = false;
	};

	LocalVector<ItemData> items;

	ItemList *item_list = nullptr;
	LineEdit *filter = nullptr;
	PopupMenu *context_menu = nullptr;

	DocumentEditorContainer *document_editor_container = nullptr;

	bool script_temperature_enabled = false;
	int temperature_history_size = 0;

	void _document_selected(int p_index);
	void _document_clicked(int p_index, Vector2 p_local_mouse_pos, MouseButton p_mouse_button_index);
	void _show_context_menu();

	void _update_item_list();

	Variant get_drag_data_fw(const Point2 &p_point, Control *p_from);
	bool can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const;
	void drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from);

protected:
	void _notification(int p_what);

public:
	void update_list();
	void ensure_current_is_visible();
	void goto_next_document(bool p_previous = false);
	void update_editor_settings();

	DocumentList(DocumentEditorContainer *p_document_editor_container);
};

class DocumentOutline : public VBoxContainer {
	GDCLASS(DocumentOutline, VBoxContainer);

	DocumentEditorContainer *document_editor_container = nullptr;
	Tree *tree = nullptr;
	HBoxContainer *buttons_hbox = nullptr;
	FilterLineEdit *filter = nullptr;
	Button *sort_button = nullptr;

	bool members_overview_enabled = false;
	bool help_overview_enabled = false;

	Control *current_editor = nullptr;
	bool updating_outline = false;

	void _toggle_sort(bool p_alphabetic_sort);
	void _tree_selected();

protected:
	void _notification(int p_what);

public:
	void update_editor_settings();
	void update_outline();
	void update_visibility();

	DocumentOutline(DocumentEditorContainer *p_document_editor_container);
};

class FindInFiles;
class TextFile;
class ShaderCreateDialog;

class DocumentEditorContainer : public MarginContainer {
	GDCLASS(DocumentEditorContainer, MarginContainer);

	enum MenuOptions {
		// File.
		FILE_MENU_NEW_TEXTFILE,
		FILE_MENU_NEW_SHADER,
		FILE_MENU_NEW_INCLUDE,

		FILE_MENU_OPEN,
		FILE_MENU_OPEN_SHADER,
		FILE_MENU_OPEN_INCLUDE,
		FILE_MENU_REOPEN_CLOSED,
		FILE_MENU_OPEN_RECENT,

		FILE_MENU_SAVE,
		FILE_MENU_SAVE_AS,
		FILE_MENU_SAVE_ALL,

		FILE_MENU_INSPECT,
		FILE_MENU_INSPECT_NATIVE_SHADER_CODE,

		FILE_MENU_COPY_PATH,
		FILE_MENU_COPY_UID,
		FILE_MENU_SHOW_IN_FILE_SYSTEM,

		FILE_MENU_HISTORY_PREV,
		FILE_MENU_HISTORY_NEXT,

		FILE_MENU_THEME_SUBMENU,

		FILE_MENU_CLOSE,
		FILE_MENU_CLOSE_ALL,
		FILE_MENU_CLOSE_OTHER_TABS,
		FILE_MENU_CLOSE_TABS_BELOW,
		FILE_MENU_CLOSE_DOCS,

		FILE_MENU_MOVE_UP,
		FILE_MENU_MOVE_DOWN,
		FILE_MENU_SORT,

		// Search.
		HELP_SEARCH_FIND,
		HELP_SEARCH_FIND_NEXT,
		HELP_SEARCH_FIND_PREVIOUS,

		SEARCH_IN_FILES,
		REPLACE_IN_FILES,

		SEARCH_HELP,
		SEARCH_WEBSITE,

		// Theme.
		THEME_IMPORT,
		THEME_RELOAD,
		THEME_SAVE_AS,
	};

	enum DocumentSortMode {
		SORT_BY_NAME,
		SORT_BY_PATH,
		SORT_BY_NONE,
	};

	enum DocumentNameDisplayMode {
		DISPLAY_NAME,
		DISPLAY_DIR_AND_NAME,
		DISPLAY_FULL_PATH,
	};

	HBoxContainer *menu_hb = nullptr;
	Button *toggle_files_button = nullptr;
	MenuButton *file_menu = nullptr;
	MenuButton *script_search_menu = nullptr;
	Timer *autosave_timer = nullptr;
	LocalVector<DocumentEditorBase::EditMenusBase *> editor_menus;

	PopupMenu *recent_scripts = nullptr;
	PopupMenu *theme_submenu = nullptr;

	Button *help_search = nullptr;
	Button *site_search = nullptr;

	friend class DocumentList;

	DocumentList *document_list = nullptr;
	HSplitContainer *script_split = nullptr;
	DocumentOutline *document_outline = nullptr;
	VSplitContainer *list_split = nullptr;
	TabContainer *tab_container = nullptr;
	AcceptDialog *error_dialog = nullptr;
	ConfirmationDialog *erase_tab_confirm = nullptr;
	FindReplaceBar *find_replace_bar = nullptr;

	EditorFileDialog *file_dialog = nullptr;
	ShaderCreateDialog *shader_create_dialog = nullptr;

	float zoom_factor = 1.0f;

	HBoxContainer *script_name_button_hbox = nullptr;
	Control *script_name_button_left_spacer = nullptr;
	Control *script_name_button_right_spacer = nullptr;
	Button *script_name_button = nullptr;
	int script_name_width = 0;

	bool document_sort_group_help_pages = false;
	DocumentSortMode document_sort_by_mode = SORT_BY_NAME;
	DocumentNameDisplayMode document_display_name_mode = DISPLAY_NAME;

	Button *script_back = nullptr;
	Button *script_forward = nullptr;

#ifdef ANDROID_ENABLED
	Control *virtual_keyboard_spacer = nullptr;
	int last_kb_height = -1;
#endif

	enum {
		DOCUMENT_EDITOR_FUNC_MAX = 32,
	};

	static int document_editor_func_count;
	static CreateDocumentEditorFunc document_editor_funcs[DOCUMENT_EDITOR_FUNC_MAX];
	Vector<Ref<EditorSyntaxHighlighter>> syntax_highlighters;

	struct ScriptHistory {
		Control *control = nullptr;
		Dictionary state;
	};

	Vector<ScriptHistory> history;
	int history_pos;

	List<String> previous_scripts;
	List<int> script_close_queue;

	bool restoring_layout = false;
	bool grab_focus_block = false;

	int edit_pass = 0;

	bool is_main_editor = false;

	void _menu_option(int p_option);
	void _theme_option(int p_option);
	void _show_save_theme_as_dialog();
	bool _has_docs_tab() const;
	bool _has_script_tab() const;

	Tree *disk_changed_list = nullptr;
	ConfirmationDialog *disk_changed = nullptr;

	void _resave_scripts(const String &p_str);

	bool _test_script_times_on_disk(Ref<Resource> p_for_script = Ref<Resource>());
	bool _script_exists(const String &p_path) const;

	void _add_recent_script(const String &p_path);
	void _update_recent_scripts();
	void _open_recent_script(int p_idx);

	void _show_error_dialog(const String &p_path);

	void _close_tab(int p_idx, bool p_save = true);
	void _update_find_replace_bar();

	void _close_current_tab(bool p_save = true);
	void _close_discard_current_tab(const String &p_str);
	void _close_docs_tab();
	void _close_other_tabs();
	void _close_tabs_below();
	void _close_all_tabs();
	void _queue_close_tabs();

	void _copy_script_path();
	void _copy_script_uid();

	void _ask_close_current_unsaved_tab(DocumentEditorBase *p_seb);

	void _update_selected_editor_menu();

	void _res_saved_callback(const Ref<Resource> &p_res);
	void _scene_saved_callback(const String &p_path);
	void _mark_built_in_scripts_as_saved(const String &p_parent_path);

	bool trim_trailing_whitespace_on_save;
	bool trim_final_newlines_on_save;
	bool convert_indent_on_save;

	void _resource_created(Ref<Resource> p_res);

	DocumentEditorBase *_get_current_editor() const;

	Ref<ConfigFile> document_editor_cache;
	String cache_path;

	void _save_editor_state(DocumentEditorBase *p_editor);
	void _save_layout();
	void _apply_editor_settings();
	void _update_filenames();
	void _files_moved(const String &p_old_file, const String &p_new_file);
	void _file_removed(const String &p_file);
	void _autosave_scripts();
	void _update_autosave_timer();
	void _reload_documents(bool p_refresh_only = false);
	void _auto_format_text(DocumentEditorBase *p_seb);

	void _update_online_doc();

	void _split_dragged(float);

	virtual void input(const Ref<InputEvent> &p_event) override;
	virtual void shortcut_input(const Ref<InputEvent> &p_event) override;

	void _prepare_file_menu();
	void _file_menu_closed();

	void _calculate_script_name_button_size();
	void _calculate_script_name_button_ratio();
	void _update_document_name_button();

	void _help_search(const String &p_text = String());

	void _history_forward();
	void _history_back();
	void _roll_back_to_pre_tab();

	void _help_class_open(const String &p_class);
	void _help_class_goto(const String &p_desc);
	void _update_history_arrows();
	void _save_new_history(const Dictionary &p_state, Control *p_control);
	void _save_previous_state(const Dictionary &p_state, Control *p_control);
	void _compress_history_patterns(bool p_once);
	void _go_to_tab(int p_idx, bool p_save_history = false);
	void _update_history_pos(int p_new_pos);

	void _validation_updated();
	int file_dialog_option;
	void _file_dialog_action(const String &p_file);

	String config_section;

	HashSet<String> handled_resource_types;
	List<String> _get_recognized_extensions();
	HashSet<String> textfile_extensions;
	Ref<TextFile> _load_text_file(const String &p_path, Error *r_error) const;
	Error _save_text_file(Ref<TextFile> p_text_file, const String &p_path);

	void _set_script_zoom_factor(float p_zoom_factor);
	void _update_code_editor_zoom_factor(CodeTextEditor *p_code_text_editor);

	void _close_builtin_scripts_from_scene(const String &p_scene);

	void _toggle_files_pressed(bool p_pressed);

protected:
	void _notification(int p_what);

public:
	void apply_documents() const;
	void reload_documents(bool p_refresh_only = false);
	void open_text_file_create_dialog(const String &p_base_path, const String &p_base_name = "");
	Ref<Resource> open_file(const String &p_file);
	bool can_open_file(const String &p_file) const;
	Error close_file(const String &p_file);

	void ensure_select_current();

	void set_handled_resource_types(const HashSet<String> &p_file_types) { handled_resource_types = p_file_types; }

	_FORCE_INLINE_ bool edit(const Ref<Resource> &p_resource, bool p_grab_focus = true) { return edit(p_resource, -1, 0, p_grab_focus); }
	bool edit(const Ref<Resource> &p_resource, int p_line, int p_col, bool p_grab_focus = true);

	Control *get_active_editor() const;
	Vector<Control *> get_all_editors() const;
	void focus_active_editor();

	void reload_open_files();
	PackedStringArray get_unsaved_files() const;
	PackedStringArray get_unsaved_documents() const;
	void save_current_document();
	void save_all_documents();
	void update_document_times();
	void sort_document_editors();

	void set_window_layout(Ref<ConfigFile> p_layout);
	void get_window_layout(Ref<ConfigFile> p_layout);

	DocumentEditorBase *get_resource_editor(const Ref<Resource> &p_res) const;

	DocumentEditorBase *get_current_editor() const { return _get_current_editor(); }

	void goto_help(const String &p_desc) { _help_class_goto(p_desc); }

	void register_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter);
	void unregister_syntax_highlighter(const Ref<EditorSyntaxHighlighter> &p_syntax_highlighter);

	static void register_create_editor_function(CreateDocumentEditorFunc p_func);

	DocumentEditorContainer(bool p_is_main_editor, const String &p_config_section, const String &p_cache_path);
};

