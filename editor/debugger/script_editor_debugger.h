/**************************************************************************/
/*  script_editor_debugger.h                                              */
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

#include "core/os/process_id.h"
#include "core/os/thread.h"
#include "editor/debugger/editor_debugger_inspector.h"
#include "editor/debugger/editor_debugger_node.h"
#include "scene/gui/margin_container.h"

class Button;
class Tree;
class LineEdit;
class TabContainer;
class TextureButton;
class AcceptDialog;
class TreeItem;
class HSplitContainer;
class ItemList;
class EditorProfiler;
class EditorFileDialog;
class EditorVisualProfiler;
class EditorPerformanceProfiler;
class SceneDebuggerTree;
class EditorDebuggerPlugin;

class ScriptEditorDebugger : public MarginContainer {
	GDCLASS(ScriptEditorDebugger, MarginContainer);

	friend class EditorDebuggerNode;

private:
	enum ProfilerType {
		PROFILER_VISUAL,
		PROFILER_SERVERS
	};

	enum Actions {
		ACTION_COPY_ERROR,
		ACTION_OPEN_SOURCE,
	};

	enum VMemMenu {
		VMEM_MENU_SHOW_IN_FILESYSTEM,
		VMEM_MENU_SHOW_IN_EXPLORER,
		VMEM_MENU_OWNERS,
	};

	LineEdit *clicked_ctrl = nullptr;
	LineEdit *clicked_ctrl_type = nullptr;
	LineEdit *live_edit_root = nullptr;
	Button *le_set = nullptr;
	Button *le_clear = nullptr;
	Button *export_csv = nullptr;

	VBoxContainer *errors_tab = nullptr;
	Tree *error_tree = nullptr;
	Button *expand_all_button = nullptr;
	Button *collapse_all_button = nullptr;
	Button *clear_button = nullptr;
	PopupMenu *item_menu = nullptr;

	EditorFileDialog *file_dialog = nullptr;
	enum FileDialogPurpose {
		SAVE_MONITORS_CSV,
		SAVE_VRAM_CSV,
	};
	FileDialogPurpose file_dialog_purpose = SAVE_MONITORS_CSV;

	int error_count = 0;
	int warning_count = 0;

	TabContainer *tabs = nullptr;

	// Reference to "Remote" tab in scene tree. Needed by _live_edit_set and buttons state.
	// Each debugger should have it's tree in the future I guess.
	const Tree *editor_remote_tree = nullptr;

	MarginContainer *vmem_mc = nullptr;
	Tree *vmem_tree = nullptr;
	Button *vmem_refresh = nullptr;
	Button *vmem_export = nullptr;
	LineEdit *vmem_total = nullptr;
	TextureRect *vmem_notice_icon = nullptr;
	PopupMenu *vmem_item_menu = nullptr;

	EditorDebuggerInspector *inspector = nullptr;
	SceneDebuggerTree *scene_tree = nullptr;

	Ref<RemoteDebuggerPeer> peer;

	HashMap<NodePath, int> node_path_cache;
	int last_path_id = 0;
	HashMap<String, int> res_path_cache;

	EditorProfiler *profiler = nullptr;
	EditorVisualProfiler *visual_profiler = nullptr;
	EditorPerformanceProfiler *performance_profiler = nullptr;

	ProcessID remote_pid = 0;

	bool live_debug = true;

	bool debug_mute_audio = false;

	EditorDebuggerNode::CameraOverride camera_override = EditorDebuggerNode::OVERRIDE_NONE;

	void _file_selected(const String &p_file);

	/// Message handler function for _parse_message.
	typedef void (ScriptEditorDebugger::*ParseMessageFunc)(uint64_t p_thread_id, const Array &p_data);
	static HashMap<String, ParseMessageFunc> parse_message_handlers;
	static void _init_parse_message_handlers();

	void _msg_set_pid(uint64_t p_thread_id, const Array &p_data);
	void _msg_scene_click_ctrl(uint64_t p_thread_id, const Array &p_data);
	void _msg_scene_scene_tree(uint64_t p_thread_id, const Array &p_data);
	void _msg_scene_inspect_objects(uint64_t p_thread_id, const Array &p_data);
#ifndef DISABLE_DEPRECATED
	void _msg_scene_inspect_object(uint64_t p_thread_id, const Array &p_data);
#endif // DISABLE_DEPRECATED
	void _msg_scene_debug_mute_audio(uint64_t p_thread_id, const Array &p_data);
	void _msg_servers_memory_usage(uint64_t p_thread_id, const Array &p_data);
	void _msg_output(uint64_t p_thread_id, const Array &p_data);
	void _msg_performance_profile_frame(uint64_t p_thread_id, const Array &p_data);
	void _msg_visual_hardware_info(uint64_t p_thread_id, const Array &p_data);
	void _msg_visual_profile_frame(uint64_t p_thread_id, const Array &p_data);
	void _msg_error(uint64_t p_thread_id, const Array &p_data);
	void _msg_servers_profile_common(const Array &p_data, const bool p_final);
	void _msg_servers_profile_frame(uint64_t p_thread_id, const Array &p_data);
	void _msg_servers_profile_total(uint64_t p_thread_id, const Array &p_data);
	void _msg_request_quit(uint64_t p_thread_id, const Array &p_data);
	void _msg_remote_objects_selected(uint64_t p_thread_id, const Array &p_data);
	void _msg_remote_nothing_selected(uint64_t p_thread_id, const Array &p_data);
	void _msg_remote_selection_invalidated(uint64_t p_thread_id, const Array &p_data);
	void _msg_remote_undo_redo_action(uint64_t p_thread_id, const Array &p_data);
	void _msg_performance_profile_names(uint64_t p_thread_id, const Array &p_data);
	void _msg_filesystem_update_file(uint64_t p_thread_id, const Array &p_data);
	void _msg_window_title(uint64_t p_thread_id, const Array &p_data);
	void _msg_editor_shortcut_pressed(uint64_t p_thread_id, const Array &p_data);

	void _parse_message(const String &p_msg, uint64_t p_thread_id, const Array &p_data);
	void _update_buttons_state();
	void _remote_object_selected(ObjectID p_object);
	void _remote_objects_edited(const String &p_prop, const TypedDictionary<uint64_t, Variant> &p_values, const String &p_field);
	void _remote_object_property_updated(ObjectID p_id, const String &p_property);

	void _video_mem_request();
	void _video_mem_export();

	void _resources_reimported(const PackedStringArray &p_resources);

	int _get_node_path_cache(const NodePath &p_path);

	int _get_res_path_cache(const String &p_path);

	void _live_edit_set();
	void _live_edit_clear();

	void _method_changed(Object *p_base, const StringName &p_name, const Variant **p_args, int p_argcount);
	void _property_changed(Object *p_base, const StringName &p_property, const Variant &p_value);

	void _error_activated();

	void _expand_errors_list();
	void _collapse_errors_list();

	void _vmem_item_activated();
	void _vmem_tree_rmb_selected(const Vector2 &p_pos, MouseButton p_button);
	void _vmem_item_menu_id_pressed(int p_option);

	void _profiler_activate(bool p_enable, int p_profiler);

	void _clear_errors_list();

	void _error_tree_item_rmb_selected(const Vector2 &p_pos, MouseButton p_button);
	void _item_menu_id_pressed(int p_option);
	void _tab_changed(int p_tab);

	void _put_msg(const String &p_message, const Array &p_data, uint64_t p_thread_id = Thread::MAIN_ID);
	void _export_csv();

	void _stop_and_notify();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void request_remote_objects(const TypedArray<uint64_t> &p_obj_ids, bool p_update_selection = true);
	void update_remote_object(ObjectID p_obj_id, const String &p_prop, const Variant &p_value, const String &p_field = "");
	void change_canvas_item_objects(const Dictionary &p_states);

	void clear_inspector(bool p_send_msg = true);

	// Needed by _live_edit_set, buttons state.
	void set_editor_remote_tree(const Tree *p_tree) { editor_remote_tree = p_tree; }

	void request_remote_tree();
	const SceneDebuggerTree *get_remote_tree();

	void start(Ref<RemoteDebuggerPeer> p_peer);
	void stop();

	bool is_session_active() { return peer.is_valid() && peer->is_peer_connected(); }
	int get_remote_pid() const { return remote_pid; }

	int get_error_count() const { return error_count; }
	int get_warning_count() const { return warning_count; }

	void update_tabs();
	void add_style();
	void clear_style();

	void save_node(ObjectID p_id, const String &p_file);
	void set_live_debugging(bool p_enable);

	void live_debug_create_node(const NodePath &p_parent, const String &p_type, const String &p_name);
	void live_debug_instantiate_node(const NodePath &p_parent, const String &p_path, const String &p_name);
	void live_debug_remove_node(const NodePath &p_at);
	void live_debug_remove_and_keep_node(const NodePath &p_at, ObjectID p_keep_id);
	void live_debug_restore_node(ObjectID p_id, const NodePath &p_at, int p_at_pos);
	void live_debug_duplicate_node(const NodePath &p_at, const String &p_new_name);
	void live_debug_reparent_node(const NodePath &p_at, const NodePath &p_new_place, const String &p_new_name, int p_at_pos);

	bool get_debug_mute_audio() const;
	void set_debug_mute_audio(bool p_mute);

	void set_debug_collisions(bool p_enable);

	EditorDebuggerNode::CameraOverride get_camera_override() const;
	void set_camera_override(EditorDebuggerNode::CameraOverride p_override);

	void update_live_edit_root();

	virtual Size2 get_minimum_size() const override;

	void add_debugger_tab(Control *p_control);
	void remove_debugger_tab(Control *p_control);
	int get_current_debugger_tab() const;
	void switch_to_debugger(int p_debugger_tab_idx);

	void send_message(const String &p_message, const Array &p_args);
	void toggle_profiler(const String &p_profiler, bool p_enable, const Array &p_data);

	void update_layout(EditorDock::DockLayout p_layout, int p_slot);

	ScriptEditorDebugger();
	~ScriptEditorDebugger();
};
