/**************************************************************************/
/*  editor_debugger_node.cpp                                              */
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

#include "editor_debugger_node.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/undo_redo.h"
#include "editor/debugger/editor_debugger_plugin.h"
#include "editor/debugger/editor_debugger_tree.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/run/editor_run_bar.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_theme_manager.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/tab_container.h"
#include "scene/resources/packed_scene.h"
#include "servers/display/display_server.h"

template <typename Func>
void _for_all(TabContainer *p_node, const Func &p_func) {
	for (int i = 0; i < p_node->get_tab_count(); i++) {
		ScriptEditorDebugger *dbg = Object::cast_to<ScriptEditorDebugger>(p_node->get_tab_control(i));
		ERR_FAIL_NULL(dbg);
		p_func(dbg);
	}
}

EditorDebuggerNode *EditorDebuggerNode::singleton = nullptr;

EditorDebuggerNode::EditorDebuggerNode() {
	set_name(TTRC("Debugger"));
	set_icon_name("Debug");
	set_layout_key("Debugger");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_debugger_bottom_panel", TTRC("Toggle Debugger Dock"), KeyModifierMask::ALT | Key::D));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);

	_update_margins();

	if (!singleton) {
		singleton = this;
	}

	single_session_style.instantiate();

	tabs = memnew(TabContainer);
	tabs->set_tabs_visible(false);
	tabs->connect("tab_changed", callable_mp(this, &EditorDebuggerNode::_debugger_changed));
	tabs->add_theme_style_override(SceneStringName(panel), single_session_style);
	add_child(tabs);

	auto_switch_remote_scene_tree = EDITOR_GET("debugger/auto_switch_to_remote_scene_tree");
	_add_debugger();

	// Remote scene tree
	remote_scene_tree = memnew(EditorDebuggerTree);
	remote_scene_tree->connect("objects_selected", callable_mp(this, &EditorDebuggerNode::_remote_objects_requested));
	remote_scene_tree->connect("selection_cleared", callable_mp(this, &EditorDebuggerNode::_remote_selection_cleared));
	remote_scene_tree->connect("save_node", callable_mp(this, &EditorDebuggerNode::_save_node_requested));
	remote_scene_tree->connect("button_clicked", callable_mp(this, &EditorDebuggerNode::_remote_tree_button_pressed));
	SceneTreeDock::get_singleton()->add_remote_tree_editor(remote_scene_tree);
	SceneTreeDock::get_singleton()->connect("remote_tree_selected", callable_mp(this, &EditorDebuggerNode::request_remote_tree));

	remote_scene_tree_timeout = EDITOR_GET("debugger/remote_scene_tree_refresh_interval");
	inspect_edited_object_timeout = EDITOR_GET("debugger/remote_inspect_refresh_interval");
}

ScriptEditorDebugger *EditorDebuggerNode::_add_debugger() {
	ScriptEditorDebugger *node = memnew(ScriptEditorDebugger);

	int id = tabs->get_tab_count();
	node->connect("stop_requested", callable_mp(this, &EditorDebuggerNode::_debugger_wants_stop).bind(id));
	node->connect("stopped", callable_mp(this, &EditorDebuggerNode::_debugger_stopped).bind(id));
	node->connect("debug_data", callable_mp(this, &EditorDebuggerNode::_debug_data).bind(id));
	node->connect("remote_tree_select_requested", callable_mp(this, &EditorDebuggerNode::_remote_tree_select_requested).bind(id));
	node->connect("remote_tree_clear_selection_requested", callable_mp(this, &EditorDebuggerNode::_remote_tree_clear_selection_requested).bind(id));
	node->connect("remote_tree_updated", callable_mp(this, &EditorDebuggerNode::_remote_tree_updated).bind(id));
	node->connect("remote_objects_updated", callable_mp(this, &EditorDebuggerNode::_remote_objects_updated));
	node->connect("remote_object_property_updated", callable_mp(this, &EditorDebuggerNode::_remote_object_property_updated).bind(id));
	node->connect("remote_objects_requested", callable_mp(this, &EditorDebuggerNode::_remote_objects_requested).bind(id));
	node->connect("errors_cleared", callable_mp(this, &EditorDebuggerNode::_update_errors).bind(false));

	tabs->add_child(node);

	if (!debugger_plugins.is_empty()) {
		for (Ref<EditorDebuggerPlugin> plugin : debugger_plugins) {
			plugin->create_session(node);
		}
	}

	return node;
}

void EditorDebuggerNode::_update_debugger_tabs() {
	int active_session = 0;
	for (int i = 0; i < tabs->get_child_count(); i++) {
		if (ScriptEditorDebugger *idle_dbg = Object::cast_to<ScriptEditorDebugger>(tabs->get_tab_control(i))) {
			idle_dbg->set_name(vformat(TTR("Session %d"), i + 1));
			bool is_session_active = idle_dbg->is_session_active();
			tabs->set_tab_hidden(i, !is_session_active);
			if (is_session_active) {
				active_session++;
			}
		}
	}
	tabs->set_tabs_visible(active_session > 1);

	if (tabs->are_tabs_visible()) {
		get_debugger(0)->clear_style();
		tabs->add_theme_style_override(SceneStringName(panel), multi_session_style);
	} else {
		get_debugger(0)->add_style();
		tabs->add_theme_style_override(SceneStringName(panel), single_session_style);
	}
}

void EditorDebuggerNode::_bind_methods() {
	// LiveDebug.
	ClassDB::bind_method("live_debug_create_node", &EditorDebuggerNode::live_debug_create_node);
	ClassDB::bind_method("live_debug_instantiate_node", &EditorDebuggerNode::live_debug_instantiate_node);
	ClassDB::bind_method("live_debug_remove_node", &EditorDebuggerNode::live_debug_remove_node);
	ClassDB::bind_method("live_debug_remove_and_keep_node", &EditorDebuggerNode::live_debug_remove_and_keep_node);
	ClassDB::bind_method("live_debug_restore_node", &EditorDebuggerNode::live_debug_restore_node);
	ClassDB::bind_method("live_debug_duplicate_node", &EditorDebuggerNode::live_debug_duplicate_node);
	ClassDB::bind_method("live_debug_reparent_node", &EditorDebuggerNode::live_debug_reparent_node);
}

void EditorDebuggerNode::update_layout(EditorDock::DockLayout p_layout, int p_slot) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->update_layout(p_layout, p_slot);
	});
}

void EditorDebuggerNode::register_undo_redo(UndoRedo *p_undo_redo) {
	p_undo_redo->set_method_notify_callback(_methods_changed, this);
	p_undo_redo->set_property_notify_callback(_properties_changed, this);
}

void EditorDebuggerNode::set_current_debugger(int p_debugger) {
	tabs->set_current_tab(p_debugger);
}

ScriptEditorDebugger *EditorDebuggerNode::get_debugger(int p_id) const {
	return Object::cast_to<ScriptEditorDebugger>(tabs->get_tab_control(p_id));
}

int EditorDebuggerNode::get_debugger_id(const ScriptEditorDebugger *p_debugger) {
	for (int i = 0; i < tabs->get_tab_count(); i++) {
		const ScriptEditorDebugger *debugger = Object::cast_to<ScriptEditorDebugger>(tabs->get_tab_control(i));
		if (p_debugger == debugger) {
			return i;
		}
	}

	ERR_FAIL_V(-1);
}

ScriptEditorDebugger *EditorDebuggerNode::get_previous_debugger() const {
	return Object::cast_to<ScriptEditorDebugger>(tabs->get_tab_control(tabs->get_previous_tab()));
}

ScriptEditorDebugger *EditorDebuggerNode::get_current_debugger() const {
	return Object::cast_to<ScriptEditorDebugger>(tabs->get_tab_control(tabs->get_current_tab()));
}

ScriptEditorDebugger *EditorDebuggerNode::get_default_debugger() const {
	return Object::cast_to<ScriptEditorDebugger>(tabs->get_tab_control(0));
}

String EditorDebuggerNode::get_server_uri() const {
#ifdef WEB_ENABLED
	return server.is_valid() ? server->get_uri() : "messageport://";
#else
	return server.is_valid() ? server->get_uri() : "";
#endif
}

void EditorDebuggerNode::set_keep_open(bool p_keep_open) {
	keep_open = p_keep_open;
	if (keep_open) {
		if (server.is_null() || !server->is_active()) {
			start();
		}
	} else {
		bool found = false;
		_for_all(tabs, [&](ScriptEditorDebugger *p_debugger) {
			if (p_debugger->is_session_active()) {
				found = true;
			}
		});
		if (!found) {
			stop();
		}
	}
}

Error EditorDebuggerNode::start(const String &p_uri) {
	if (Engine::get_singleton()->is_recovery_mode_hint()) {
		return ERR_UNAVAILABLE;
	}

	ERR_FAIL_COND_V(!p_uri.contains("://"), ERR_INVALID_PARAMETER);
	if (keep_open && current_uri == p_uri && server.is_valid()) {
		return OK;
	}
	stop(true);
	current_uri = p_uri;

	server = Ref<EditorDebuggerServer>(EditorDebuggerServer::create(current_uri.substr(0, current_uri.find("://") + 3)));
	RETURN_IF_ERROR(server->start(current_uri));
	set_process(true);
	EditorNode::get_log()->add_message("--- Debugging process started ---", EditorLog::MSG_TYPE_EDITOR);
	return OK;
}

void EditorDebuggerNode::stop(bool p_force) {
	if (keep_open && !p_force) {
		return;
	}

	remote_scene_tree_wait = false;
	inspect_edited_object_wait = false;

	current_uri.clear();
	// Also close all debugging sessions.
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		// If the server is also still active, let the debugger notify that it stopped.
		// Otherwise, just stop it silently.
		if (server.is_valid() || dbg->is_session_active()) {
			dbg->_stop_and_notify();
		} else {
			dbg->stop();
		}
	});

	if (server.is_valid()) {
		server->stop();
		EditorNode::get_log()->add_message("--- Debugging process stopped ---", EditorLog::MSG_TYPE_EDITOR);

		if (EditorRunBar::get_singleton()->is_movie_maker_enabled()) {
			// Request attention in case the user was doing something else when movie recording is finished.
			DisplayServer::get_singleton()->window_request_attention();
		}

		server.unref();
	}

	EditorUndoRedoManager::get_singleton()->clear_history(EditorUndoRedoManager::REMOTE_HISTORY, false);
	set_process(false);
}

void EditorDebuggerNode::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			multi_session_style = EditorNode::get_singleton()->get_editor_theme()->get_stylebox(SNAME("DebuggerPanel"), EditorStringName(EditorStyles));
			if (tabs->are_tabs_visible()) {
				tabs->add_theme_style_override(SceneStringName(panel), multi_session_style);
			}
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (!EditorThemeManager::is_generated_theme_outdated()) {
				return;
			}

			_update_errors(true);
			_update_margins();

			remote_scene_tree->update_icon_max_width();
		} break;

		case NOTIFICATION_PROCESS: {
			if (server.is_null()) {
				return;
			}

			if (!server->is_active()) {
				stop();
				return;
			}
			server->poll();

			_update_errors();

			// Remote scene tree update.
			if (!remote_scene_tree_wait) {
				if (remote_scene_tree_queue_update) {
					if (remote_scene_tree->is_visible_in_tree()) {
						remote_scene_tree_queue_update = false;
						request_remote_tree();
					}
				} else {
					remote_scene_tree_timeout -= get_process_delta_time();
					if (remote_scene_tree_timeout < 0) {
						remote_scene_tree_timeout = EDITOR_GET("debugger/remote_scene_tree_refresh_interval");

						if (remote_scene_tree->is_visible_in_tree()) {
							remote_scene_tree_wait = true;
							get_current_debugger()->request_remote_tree();
						}
					}
				}
			}

			// Remote inspector update.
			if (!inspect_edited_object_wait) {
				inspect_edited_object_timeout -= get_process_delta_time();
				if (inspect_edited_object_timeout < 0) {
					inspect_edited_object_timeout = EDITOR_GET("debugger/remote_inspect_refresh_interval");

					if (EditorDebuggerRemoteObjects *robjs = Object::cast_to<EditorDebuggerRemoteObjects>(InspectorDock::get_inspector_singleton()->get_edited_object())) {
						inspect_edited_object_wait = true;
						get_current_debugger()->request_remote_objects(robjs->remote_object_ids, false);
					}
				}
			}

			// Take connections.
			if (server->is_connection_available()) {
				ScriptEditorDebugger *debugger = nullptr;
				_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
					if (debugger || dbg->is_session_active()) {
						return;
					}
					debugger = dbg;
				});
				if (debugger == nullptr) {
					if (tabs->get_tab_count() <= 4) { // Max 4 debugging sessions active.
						debugger = _add_debugger();
					} else {
						// We already have too many sessions, disconnecting new clients to prevent them from hanging.
						server->take_connection()->close();
						return; // Can't add, stop here.
					}
				}

				// Switch to remote tree view if so desired.
				remote_scene_tree->set_new_session();
				auto_switch_remote_scene_tree = EDITOR_GET("debugger/auto_switch_to_remote_scene_tree");
				if (auto_switch_remote_scene_tree) {
					SceneTreeDock::get_singleton()->show_remote_tree();
				}
				// Good to go.
				SceneTreeDock::get_singleton()->show_tab_buttons();
				debugger->set_editor_remote_tree(remote_scene_tree);
				debugger->start(server->take_connection());

				debugger->update_live_edit_root();

				_update_debugger_tabs();
			}
		} break;
	}
}

void EditorDebuggerNode::_update_errors(bool p_force) {
	int error_count = 0;
	int warning_count = 0;
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		error_count += dbg->get_error_count();
		warning_count += dbg->get_warning_count();
	});

	if (p_force || error_count != last_error_count || warning_count != last_warning_count) {
		_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
			dbg->update_tabs();
		});

		last_error_count = error_count;
		last_warning_count = warning_count;

		if (error_count == 0 && warning_count == 0) {
			set_title("");
			set_dock_icon(Ref<Texture2D>());
			set_title_color(Color(0, 0, 0, 0));
			set_force_show_icon(false);
		} else {
			set_title(TTR("Debugger") + " (" + itos(error_count + warning_count) + ")");
			if (error_count >= 1 && warning_count >= 1) {
				set_dock_icon(get_editor_theme_icon(SNAME("ErrorWarning")));
				// Use error color to represent the highest level of severity reported.
				set_title_color(get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			} else if (error_count >= 1) {
				set_dock_icon(get_editor_theme_icon(SNAME("Error")));
				set_title_color(get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			} else {
				set_dock_icon(get_editor_theme_icon(SNAME("Warning")));
				set_title_color(get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
			}
			set_force_show_icon(true);
		}
	}
}

void EditorDebuggerNode::_update_margins() {
	Ref<StyleBox> bottom_panel_margins = EditorNode::get_singleton()->get_editor_theme()->get_stylebox(SNAME("BottomPanel"), EditorStringName(EditorStyles));
	add_theme_constant_override("margin_top", -bottom_panel_margins->get_margin(SIDE_TOP));
	add_theme_constant_override("margin_left", -bottom_panel_margins->get_margin(SIDE_LEFT));
	add_theme_constant_override("margin_right", -bottom_panel_margins->get_margin(SIDE_RIGHT));
	add_theme_constant_override("margin_bottom", -bottom_panel_margins->get_margin(SIDE_BOTTOM));
}

void EditorDebuggerNode::_debugger_stopped(int p_id) {
	ScriptEditorDebugger *dbg = get_debugger(p_id);
	ERR_FAIL_NULL(dbg);

	bool found = false;
	_for_all(tabs, [&](ScriptEditorDebugger *p_debugger) {
		if (p_debugger->is_session_active()) {
			found = true;
		}
	});

	if (found) {
		if (!get_current_debugger()->is_session_active()) {
			remote_scene_tree->clear();
		}
	} else {
		SceneTreeDock *dock = SceneTreeDock::get_singleton();
		if (dock->is_inside_tree()) {
			dock->hide_remote_tree();
			dock->hide_tab_buttons();
		}
		EditorNode::get_singleton()->notify_all_debug_sessions_exited();
	}
}

void EditorDebuggerNode::_debugger_wants_stop(int p_id) {
	// Ask editor to kill PID.
	if (int pid = get_debugger(p_id)->get_remote_pid()) {
		callable_mp(EditorNode::get_singleton(), &EditorNode::stop_child_process).call_deferred(pid);
	}
}

void EditorDebuggerNode::_debugger_changed(int p_tab) {
	remote_scene_tree_wait = false;
	inspect_edited_object_wait = false;

	if (ScriptEditorDebugger *prev_debug = get_previous_debugger()) {
		prev_debug->clear_inspector();
	}

	if (get_current_debugger()->is_session_active()) {
		if (remote_scene_tree->is_visible_in_tree()) {
			request_remote_tree();
		} else {
			remote_scene_tree_queue_update = true;
		}
	} else {
		remote_scene_tree->clear();
	}
}

void EditorDebuggerNode::_debug_data(const String &p_msg, const Array &p_data, int p_debugger) {
	if (p_debugger != tabs->get_current_tab()) {
		return;
	}

	if (p_msg == "scene:scene_tree") {
		remote_scene_tree_wait = false;
	} else if (p_msg == "scene:inspect_objects") {
		inspect_edited_object_wait = false;
	}
}

// LiveEdit/Inspector
void EditorDebuggerNode::request_remote_tree() {
	if (!get_current_debugger()->is_session_active()) {
		remote_scene_tree_queue_update = true;
		return;
	}

	remote_scene_tree_wait = true;
	remote_scene_tree_timeout = EDITOR_GET("debugger/remote_scene_tree_refresh_interval");
	get_current_debugger()->request_remote_tree();
}

void EditorDebuggerNode::set_remote_selection(const TypedArray<int64_t> &p_ids, int p_debugger) {
	inspect_edited_object_wait = true;
	inspect_edited_object_timeout = EDITOR_GET("debugger/remote_inspect_refresh_interval");
	get_debugger(p_debugger)->request_remote_objects(p_ids);
}

void EditorDebuggerNode::clear_remote_tree_selection() {
	remote_scene_tree->clear_selection();
	get_debugger(remote_scene_tree->get_current_debugger())->clear_inspector(clear_remote_selection);
}

void EditorDebuggerNode::stop_waiting_inspection() {
	inspect_edited_object_timeout = EDITOR_GET("debugger/remote_inspect_refresh_interval");
	inspect_edited_object_wait = false;
}

bool EditorDebuggerNode::match_remote_selection(const TypedArray<uint64_t> &p_ids) const {
	return p_ids == remote_scene_tree->get_selection();
}

void EditorDebuggerNode::_remote_tree_select_requested(const TypedArray<int64_t> &p_ids, int p_debugger) {
	if (p_debugger == tabs->get_current_tab()) {
		remote_scene_tree->select_nodes(p_ids);
	}
}

void EditorDebuggerNode::_remote_tree_clear_selection_requested(int p_debugger) {
	if (p_debugger != tabs->get_current_tab()) {
		return;
	}
	remote_scene_tree->clear_selection();
	get_current_debugger()->clear_inspector(false);
}

void EditorDebuggerNode::_remote_tree_updated(int p_debugger) {
	if (p_debugger != tabs->get_current_tab()) {
		return;
	}
	remote_scene_tree->clear();
	remote_scene_tree->update_scene_tree(get_current_debugger()->get_remote_tree(), p_debugger);
}

void EditorDebuggerNode::_remote_tree_button_pressed(Object *p_item, int p_column, int p_id, MouseButton p_button) {
	if (p_button != MouseButton::LEFT) {
		return;
	}

	TreeItem *item = Object::cast_to<TreeItem>(p_item);
	ERR_FAIL_NULL(item);

	if (p_id == EditorDebuggerTree::BUTTON_SUBSCENE) {
		remote_scene_tree->emit_signal(SNAME("open"), item->get_meta("scene_file_path"));
	} else if (p_id == EditorDebuggerTree::BUTTON_VISIBILITY) {
		ObjectID obj_id = item->get_metadata(0);
		ERR_FAIL_COND(obj_id.is_null());
		get_current_debugger()->update_remote_object(obj_id, "visible", !item->get_meta("visible"));
		request_remote_tree();
	}
}

void EditorDebuggerNode::_remote_objects_updated(EditorDebuggerRemoteObjects *p_remote_objects) {
	if (p_remote_objects->debugger_id == tabs->get_current_tab() && p_remote_objects != InspectorDock::get_inspector_singleton()->get_edited_object()) {
		// Pushing an item to the inspector also clears it, which sends a message to clear the remote selection.
		// We workaround it by temporarily not sending messages when this happens.
		clear_remote_selection = false;
		EditorNode::get_singleton()->push_item(p_remote_objects);
		clear_remote_selection = true;
	}
}

void EditorDebuggerNode::_remote_object_property_updated(ObjectID p_id, const String &p_property, int p_debugger) {
	if (p_debugger != tabs->get_current_tab()) {
		return;
	}

	Object *obj = InspectorDock::get_inspector_singleton()->get_edited_object();
	if (obj && obj->get_instance_id() == p_id) {
		InspectorDock::get_inspector_singleton()->update_property(p_property);
	}
}

void EditorDebuggerNode::_remote_objects_requested(const TypedArray<uint64_t> &p_ids, int p_debugger) {
	if (p_debugger != tabs->get_current_tab()) {
		return;
	}
	inspect_edited_object_wait = true;
	inspect_edited_object_timeout = EDITOR_GET("debugger/remote_inspect_refresh_interval");
	get_current_debugger()->request_remote_objects(p_ids);
}

void EditorDebuggerNode::_remote_selection_cleared(int p_debugger) {
	if (p_debugger != tabs->get_current_tab()) {
		return;
	}
	stop_waiting_inspection();
	get_current_debugger()->clear_inspector();
}

void EditorDebuggerNode::_save_node_requested(ObjectID p_id, const String &p_file, int p_debugger) {
	if (p_debugger != tabs->get_current_tab()) {
		return;
	}
	get_current_debugger()->save_node(p_id, p_file);
}

// Remote inspector/edit.
void EditorDebuggerNode::_methods_changed(void *p_ud, Object *p_base, const StringName &p_name, const Variant **p_args, int p_argcount) {
	if (!singleton) {
		return;
	}
	_for_all(singleton->tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->_method_changed(p_base, p_name, p_args, p_argcount);
	});
}

void EditorDebuggerNode::_properties_changed(void *p_ud, Object *p_base, const StringName &p_property, const Variant &p_value) {
	if (!singleton) {
		return;
	}
	_for_all(singleton->tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->_property_changed(p_base, p_property, p_value);
	});
}

// LiveDebug
void EditorDebuggerNode::set_live_debugging(bool p_enabled) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->set_live_debugging(p_enabled);
	});
}

void EditorDebuggerNode::update_live_edit_root() {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->update_live_edit_root();
	});
}

void EditorDebuggerNode::live_debug_create_node(const NodePath &p_parent, const String &p_type, const String &p_name) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->live_debug_create_node(p_parent, p_type, p_name);
	});
}

void EditorDebuggerNode::live_debug_instantiate_node(const NodePath &p_parent, const String &p_path, const String &p_name) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->live_debug_instantiate_node(p_parent, p_path, p_name);
	});
}

void EditorDebuggerNode::live_debug_remove_node(const NodePath &p_at) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->live_debug_remove_node(p_at);
	});
}

void EditorDebuggerNode::live_debug_remove_and_keep_node(const NodePath &p_at, ObjectID p_keep_id) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->live_debug_remove_and_keep_node(p_at, p_keep_id);
	});
}

void EditorDebuggerNode::live_debug_restore_node(ObjectID p_id, const NodePath &p_at, int p_at_pos) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->live_debug_restore_node(p_id, p_at, p_at_pos);
	});
}

void EditorDebuggerNode::live_debug_duplicate_node(const NodePath &p_at, const String &p_new_name) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->live_debug_duplicate_node(p_at, p_new_name);
	});
}

void EditorDebuggerNode::live_debug_reparent_node(const NodePath &p_at, const NodePath &p_new_place, const String &p_new_name, int p_at_pos) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->live_debug_reparent_node(p_at, p_new_place, p_new_name, p_at_pos);
	});
}

void EditorDebuggerNode::set_debug_mute_audio(bool p_mute) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->set_debug_mute_audio(p_mute);
	});
	debug_mute_audio = p_mute;
}

bool EditorDebuggerNode::get_debug_mute_audio() const {
	return debug_mute_audio;
}

void EditorDebuggerNode::set_debug_collisions(bool p_enabled) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->set_debug_collisions(p_enabled);
	});
}

void EditorDebuggerNode::set_camera_override(CameraOverride p_override) {
	_for_all(tabs, [&](ScriptEditorDebugger *dbg) {
		dbg->set_camera_override(p_override);
	});
	camera_override = p_override;
}

EditorDebuggerNode::CameraOverride EditorDebuggerNode::get_camera_override() {
	return camera_override;
}

void EditorDebuggerNode::add_debugger_plugin(const Ref<EditorDebuggerPlugin> &p_plugin) {
	ERR_FAIL_COND_MSG(p_plugin.is_null(), "Debugger plugin is null.");
	ERR_FAIL_COND_MSG(debugger_plugins.has(p_plugin), "Debugger plugin already exists.");
	debugger_plugins.insert(p_plugin);

	Ref<EditorDebuggerPlugin> plugin = p_plugin;
	for (int i = 0; get_debugger(i); i++) {
		plugin->create_session(get_debugger(i));
	}
}

void EditorDebuggerNode::remove_debugger_plugin(const Ref<EditorDebuggerPlugin> &p_plugin) {
	ERR_FAIL_COND_MSG(p_plugin.is_null(), "Debugger plugin is null.");
	ERR_FAIL_COND_MSG(!debugger_plugins.has(p_plugin), "Debugger plugin doesn't exists.");
	debugger_plugins.erase(p_plugin);
	Ref<EditorDebuggerPlugin>(p_plugin)->clear();
}

bool EditorDebuggerNode::plugins_capture(ScriptEditorDebugger *p_debugger, const String &p_message, const Array &p_data) {
	int session_index = tabs->get_tab_idx_from_control(p_debugger);
	ERR_FAIL_COND_V(session_index < 0, false);
	int colon_index = p_message.find_char(':');
	ERR_FAIL_COND_V_MSG(colon_index < 1, false, "Invalid message received.");

	const String cap = p_message.substr(0, colon_index);
	bool parsed = false;
	for (Ref<EditorDebuggerPlugin> plugin : debugger_plugins) {
		if (plugin->has_capture(cap)) {
			parsed |= plugin->capture(p_message, p_data, session_index);
		}
	}
	return parsed;
}
