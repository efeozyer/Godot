/**************************************************************************/
/*  script_editor_debugger.cpp                                            */
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

#include "script_editor_debugger.h"

#include "core/config/project_settings.h"
#include "core/debugger/debugger_marshalls.h"
#include "core/debugger/remote_debugger.h"
#include "core/input/input.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "core/variant/typed_dictionary.h"
#include "core/version.h"
#include "editor/debugger/editor_debugger_plugin.h"
#include "editor/debugger/editor_performance_profiler.h"
#include "editor/debugger/editor_profiler.h"
#include "editor/debugger/editor_visual_profiler.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/inspector/editor_property_name_processor.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_viewport.h"
#include "editor/scene/canvas_item_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "main/performance.h"
#include "scene/3d/camera_3d.h"
#include "scene/debugger/scene_debugger_object.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"
#include "servers/debugger/servers_debugger.h"
#include "servers/display/display_server.h"

using CameraOverride = EditorDebuggerNode::CameraOverride;

void ScriptEditorDebugger::_put_msg(const String &p_message, const Array &p_data, uint64_t p_thread_id) {
	ERR_FAIL_COND(p_thread_id == Thread::UNASSIGNED_ID);
	if (is_session_active()) {
		Array msg = { p_message, p_thread_id, p_data };
		Error err = peer->put_message(msg);
		ERR_FAIL_COND_MSG(err != OK, vformat("Failed to send message %d", err));
	}
}

void ScriptEditorDebugger::update_tabs() {
	if (error_count == 0 && warning_count == 0) {
		errors_tab->set_name(TTRC("Errors"));
		tabs->set_tab_icon(tabs->get_tab_idx_from_control(errors_tab), Ref<Texture2D>());
	} else {
		errors_tab->set_name(TTR("Errors") + " (" + itos(error_count + warning_count) + ")");
		if (error_count >= 1 && warning_count >= 1) {
			tabs->set_tab_icon(tabs->get_tab_idx_from_control(errors_tab), get_editor_theme_icon(SNAME("ErrorWarning")));
		} else if (error_count >= 1) {
			tabs->set_tab_icon(tabs->get_tab_idx_from_control(errors_tab), get_editor_theme_icon(SNAME("Error")));
		} else {
			tabs->set_tab_icon(tabs->get_tab_idx_from_control(errors_tab), get_editor_theme_icon(SNAME("Warning")));
		}
	}
}

void ScriptEditorDebugger::add_style() {
	tabs->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("DebuggerPanel"), EditorStringName(EditorStyles)));
}

void ScriptEditorDebugger::clear_style() {
	tabs->remove_theme_style_override(SceneStringName(panel));
}

void ScriptEditorDebugger::save_node(ObjectID p_id, const String &p_file) {
	Array msg = { p_id, p_file };
	_put_msg("scene:save_node", msg);
}

void ScriptEditorDebugger::_file_selected(const String &p_file) {
	switch (file_dialog_purpose) {
		case SAVE_MONITORS_CSV: {
			Error err;
			Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);

			if (err != OK) {
				ERR_PRINT("Failed to open " + p_file);
				return;
			}
			Vector<String> line;
			line.resize(Performance::MONITOR_MAX);

			// signatures
			for (int i = 0; i < Performance::MONITOR_MAX; i++) {
				line.write[i] = Performance::get_singleton()->get_monitor_name(Performance::Monitor(i));
			}
			file->store_csv_line(line);

			// values
			Vector<List<float>::Element *> iterators;
			iterators.resize(Performance::MONITOR_MAX);
			bool continue_iteration = false;
			for (int i = 0; i < Performance::MONITOR_MAX; i++) {
				iterators.write[i] = performance_profiler->get_monitor_data(Performance::get_singleton()->get_monitor_name(Performance::Monitor(i)))->back();
				continue_iteration = continue_iteration || iterators[i];
			}
			while (continue_iteration) {
				continue_iteration = false;
				for (int i = 0; i < Performance::MONITOR_MAX; i++) {
					if (iterators[i]) {
						line.write[i] = String::num_real(iterators[i]->get());
						iterators.write[i] = iterators[i]->prev();
					} else {
						line.write[i] = "";
					}
					continue_iteration = continue_iteration || iterators[i];
				}
				file->store_csv_line(line);
			}
			file->store_string("\n");

			Vector<Vector<String>> profiler_data = profiler->get_data_as_csv();
			for (int i = 0; i < profiler_data.size(); i++) {
				file->store_csv_line(profiler_data[i]);
			}
		} break;
		case SAVE_VRAM_CSV: {
			Error err;
			Ref<FileAccess> file = FileAccess::open(p_file, FileAccess::WRITE, &err);

			if (err != OK) {
				ERR_PRINT("Failed to open " + p_file);
				return;
			}

			Vector<String> headers;
			headers.resize(vmem_tree->get_columns());
			for (int i = 0; i < vmem_tree->get_columns(); ++i) {
				headers.write[i] = vmem_tree->get_column_title(i);
			}
			file->store_csv_line(headers);

			if (vmem_tree->get_root()) {
				TreeItem *ti = vmem_tree->get_root()->get_first_child();
				while (ti) {
					Vector<String> values;
					values.resize(vmem_tree->get_columns());
					for (int i = 0; i < vmem_tree->get_columns(); ++i) {
						values.write[i] = ti->get_text(i);
					}
					file->store_csv_line(values);

					ti = ti->get_next();
				}
			}
		} break;
	}
}

void ScriptEditorDebugger::request_remote_tree() {
	_put_msg("scene:request_scene_tree", Array());
}

const SceneDebuggerTree *ScriptEditorDebugger::get_remote_tree() {
	return scene_tree;
}

void ScriptEditorDebugger::update_remote_object(ObjectID p_obj_id, const String &p_prop, const Variant &p_value, const String &p_field) {
	Array msg = { p_obj_id, p_prop };

	Ref<Resource> res = p_value;
	if (res.is_valid() && !res->get_path().is_empty()) {
		msg.append(res->get_path());
	} else {
		msg.append(p_value);
	}

	if (p_field.is_empty()) {
		_put_msg("scene:set_object_property", msg);
	} else {
		msg.push_back(p_field);
		_put_msg("scene:set_object_property_field", msg);
	}
}

void ScriptEditorDebugger::change_canvas_item_objects(const Dictionary &p_states) {
	ERR_FAIL_COND(p_states.is_empty());
	Array msg = { p_states.duplicate() };
	_put_msg("scene:change_canvas_item_states", msg);
}

void ScriptEditorDebugger::request_remote_objects(const TypedArray<uint64_t> &p_obj_ids, bool p_update_selection) {
	ERR_FAIL_COND(p_obj_ids.is_empty());
	Array msg = { p_obj_ids.duplicate(), p_update_selection };
	_put_msg("scene:inspect_objects", msg);
}

void ScriptEditorDebugger::clear_inspector(bool p_send_msg) {
	inspector->clear_remote_inspector();
	if (p_send_msg) {
		_put_msg("scene:clear_selection", Array());
	}
}

void ScriptEditorDebugger::_remote_object_selected(ObjectID p_id) {
	emit_signal(SNAME("remote_objects_requested"), Array{ p_id });
}

void ScriptEditorDebugger::_remote_objects_edited(const String &p_prop, const TypedDictionary<uint64_t, Variant> &p_values, const String &p_field) {
	for (const KeyValue<Variant, Variant> &kv : p_values) {
		update_remote_object(ObjectID(static_cast<uint64_t>(kv.key)), p_prop, kv.value, p_field);
	}
	request_remote_objects(p_values.keys(), false);
}

void ScriptEditorDebugger::_remote_object_property_updated(ObjectID p_id, const String &p_property) {
	emit_signal(SNAME("remote_object_property_updated"), p_id, p_property);
}

void ScriptEditorDebugger::_video_mem_request() {
	_put_msg("servers:memory", Array());
}

void ScriptEditorDebugger::_video_mem_export() {
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	file_dialog->clear_filters();
	file_dialog_purpose = SAVE_VRAM_CSV;
	file_dialog->popup_file_dialog();
}

Size2 ScriptEditorDebugger::get_minimum_size() const {
	Size2 ms = MarginContainer::get_minimum_size();
	ms.y = MAX(ms.y, 250 * EDSCALE);
	return ms;
}

void ScriptEditorDebugger::_msg_set_pid(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.is_empty());
	remote_pid = p_data[0];
	// We emit the started signal after we've set the PID.
	emit_signal(SNAME("started"));
}

void ScriptEditorDebugger::_msg_scene_click_ctrl(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.size() < 2);
	clicked_ctrl->set_text(p_data[0]);
	clicked_ctrl_type->set_text(p_data[1]);
}

void ScriptEditorDebugger::_msg_scene_scene_tree(uint64_t p_thread_id, const Array &p_data) {
	scene_tree->nodes.clear();
	scene_tree->deserialize(p_data);
	emit_signal(SNAME("remote_tree_updated"));
	_update_buttons_state();
}

void ScriptEditorDebugger::_msg_scene_inspect_objects(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.is_empty());
	EditorDebuggerRemoteObjects *robjs = inspector->set_objects(p_data, get_current_debugger_tab());
	if (robjs && EditorDebuggerNode::get_singleton()->match_remote_selection(robjs->remote_object_ids)) {
		EditorDebuggerNode::get_singleton()->stop_waiting_inspection();
		emit_signal(SNAME("remote_objects_updated"), robjs);
	}
}

#ifndef DISABLE_DEPRECATED
void ScriptEditorDebugger::_msg_scene_inspect_object(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.is_empty());
	// Legacy compatibility: convert single object response to new format.
	// p_data is [id, className, properties] - wrap it as first element of array for new handler.
	Array wrapped_data;
	wrapped_data.push_back(p_data);
	_msg_scene_inspect_objects(p_thread_id, wrapped_data);
}
#endif // DISABLE_DEPRECATED

void ScriptEditorDebugger::_msg_scene_debug_mute_audio(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.is_empty());
	// This is handled by SceneDebugger, we need to ignore here to not show a warning.
}

void ScriptEditorDebugger::_msg_servers_memory_usage(uint64_t p_thread_id, const Array &p_data) {
	vmem_tree->clear();
	TreeItem *root = vmem_tree->create_item();
	ServersDebugger::ResourceUsage usage;
	usage.deserialize(p_data);

	uint64_t total = 0;

	for (const ServersDebugger::ResourceInfo &E : usage.infos) {
		TreeItem *it = vmem_tree->create_item(root);
		String type = E.type;
		int bytes = E.vram;
		it->set_text(0, E.path);
		it->set_text(1, type);
		it->set_text(2, E.format);
		it->set_text(3, String::humanize_size(bytes));
		total += bytes;

		// If it does not have a theme icon, just go up the inheritance tree until we find one.
		if (!has_theme_icon(type, EditorStringName(EditorIcons))) {
			StringName base_type = type;
			while (base_type != "Resource" && base_type != "") {
				base_type = ClassDB::get_parent_class(base_type);
				if (has_theme_icon(base_type, EditorStringName(EditorIcons))) {
					type = base_type;
					break;
				}
			}
		}

		it->set_icon(0, get_editor_theme_icon(type));
	}

	vmem_total->set_tooltip_text(TTR("Bytes:") + " " + itos(total));
	vmem_total->set_text(String::humanize_size(total));
}

void ScriptEditorDebugger::_msg_output(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.size() != 2);

	ERR_FAIL_COND(p_data[0].get_type() != Variant::PACKED_STRING_ARRAY);
	Vector<String> output_strings = p_data[0];

	ERR_FAIL_COND(p_data[1].get_type() != Variant::PACKED_INT32_ARRAY);
	Vector<int> output_types = p_data[1];

	ERR_FAIL_COND(output_strings.size() != output_types.size());

	for (int i = 0; i < output_strings.size(); i++) {
		RemoteDebugger::MessageType type = (RemoteDebugger::MessageType)(int)(output_types[i]);
		EditorLog::MessageType msg_type;
		switch (type) {
			case RemoteDebugger::MESSAGE_TYPE_LOG: {
				msg_type = EditorLog::MSG_TYPE_STD;
			} break;
			case RemoteDebugger::MESSAGE_TYPE_LOG_RICH: {
				msg_type = EditorLog::MSG_TYPE_STD_RICH;
			} break;
			case RemoteDebugger::MESSAGE_TYPE_ERROR: {
				msg_type = EditorLog::MSG_TYPE_ERROR;
			} break;
			case RemoteDebugger::MESSAGE_TYPE_EDITOR: {
				msg_type = EditorLog::MSG_TYPE_EDITOR;
			} break;
			default: {
				WARN_PRINT("Unhandled script debugger message type: " + itos(type));
				msg_type = EditorLog::MSG_TYPE_STD;
			} break;
		}
		EditorNode::get_log()->add_message(output_strings[i], msg_type);
		emit_signal(SceneStringName(output), output_strings[i], msg_type);
	}
}

void ScriptEditorDebugger::_msg_performance_profile_frame(uint64_t p_thread_id, const Array &p_data) {
	Vector<float> frame_data;
	frame_data.resize(p_data.size());
	for (int i = 0; i < p_data.size(); i++) {
		frame_data.write[i] = p_data[i];
	}
	performance_profiler->add_profile_frame(frame_data);
}

void ScriptEditorDebugger::_msg_visual_hardware_info(uint64_t p_thread_id, const Array &p_data) {
	const String cpu_name = p_data[0];
	const String gpu_name = p_data[1];
	visual_profiler->set_hardware_info(cpu_name, gpu_name);
}

void ScriptEditorDebugger::_msg_visual_profile_frame(uint64_t p_thread_id, const Array &p_data) {
	ServersDebugger::VisualProfilerFrame frame;
	frame.deserialize(p_data);

	EditorVisualProfiler::Metric metric;
	metric.areas.resize(frame.areas.size());
	metric.frame_number = frame.frame_number;
	metric.valid = true;

	{
		EditorVisualProfiler::Metric::Area *areas_ptr = metric.areas.ptrw();
		for (int i = 0; i < frame.areas.size(); i++) {
			areas_ptr[i].name = frame.areas[i].name;
			areas_ptr[i].cpu_time = frame.areas[i].cpu_msec;
			areas_ptr[i].gpu_time = frame.areas[i].gpu_msec;
		}
	}
	visual_profiler->add_frame_metric(metric);
}

void ScriptEditorDebugger::_msg_error(uint64_t p_thread_id, const Array &p_data) {
	DebuggerMarshalls::OutputError oe;
	ERR_FAIL_COND_MSG(oe.deserialize(p_data) == false, "Failed to deserialize error message");

	// Format time.
	Array time_vals = { oe.hr, oe.min, oe.sec, oe.msec };
	bool e;
	String time = String("%d:%02d:%02d:%03d").sprintf(time_vals, &e);

	// Rest of the error data.
	bool source_is_project_file = oe.source_file.begins_with("res://");

	// Create error tree to display above error or warning details.
	TreeItem *r = error_tree->get_root();
	if (!r) {
		r = error_tree->create_item();
	}

	// Also provide the relevant details as tooltip to quickly check without
	// uncollapsing the tree.
	String tooltip = oe.warning ? TTRC("Warning:") : TTRC("Error:");

	TreeItem *error = error_tree->create_item(r);
	if (oe.warning) {
		error->set_meta("_is_warning", true);
	} else {
		error->set_meta("_is_error", true);
	}
	error->set_collapsed(true);

	error->set_text_overrun_behavior(0, TextServer::OVERRUN_NO_TRIMMING);
	error->set_icon(0, get_editor_theme_icon(oe.warning ? SNAME("Warning") : SNAME("Error")));
	error->set_text(0, time);
	error->set_text_alignment(0, HORIZONTAL_ALIGNMENT_LEFT);

	const Color color = get_theme_color(oe.warning ? SNAME("warning_color") : SNAME("error_color"), EditorStringName(Editor));
	error->set_custom_color(0, color);
	error->set_custom_color(1, color);

	String error_title;
	if (!oe.source_func.is_empty()) {
		error_title += oe.source_func + ": ";
	}
	// If we have a (custom) error message, use it as title, and add a C++ Error
	// item with the original error condition.
	error_title += oe.error_descr.is_empty() ? oe.error : oe.error_descr;
	error->set_text(1, error_title);
	error->set_autowrap_mode(1, TextServer::AUTOWRAP_WORD_SMART);
	error->set_autowrap_trim_flags(1, 0);
	tooltip += " " + error_title + "\n";

	if (!oe.error_descr.is_empty()) {
		// Add item for C++ error condition.
		TreeItem *cpp_cond = error_tree->create_item(error);
		cpp_cond->set_text(0, "<" + TTR("C++ Error") + ">");
		cpp_cond->set_text(1, oe.error);
		cpp_cond->set_text_alignment(0, HORIZONTAL_ALIGNMENT_LEFT);
		tooltip += TTR("C++ Error:") + " " + oe.error + "\n";
	}
	Vector<uint8_t> v;
	v.resize(100);

	// Source of the error.
	String source_txt = (source_is_project_file ? oe.source_file.get_file() : oe.source_file) + ":" + itos(oe.source_line);
	if (!oe.source_func.is_empty()) {
		source_txt += " @ " + oe.source_func;
		if (!oe.source_func.ends_with(")")) {
			source_txt += "()";
		}
	}

	TreeItem *cpp_source = error_tree->create_item(error);
	cpp_source->set_text(0, "<" + TTR("C++ Source") + ">");
	cpp_source->set_text(1, source_txt);
	cpp_source->set_text_alignment(0, HORIZONTAL_ALIGNMENT_LEFT);
	tooltip += TTR("C++ Source:") + " " + source_txt + "\n";

	error->set_tooltip_text(0, tooltip);
	error->set_tooltip_text(1, tooltip);

	if (warning_count == 0 && error_count == 0) {
		expand_all_button->set_disabled(false);
		collapse_all_button->set_disabled(false);
		clear_button->set_disabled(false);
	}

	if (oe.warning) {
		warning_count++;
	} else {
		error_count++;
	}
}

void ScriptEditorDebugger::_msg_servers_profile_common(const Array &p_data, const bool p_final) {
	EditorProfiler::Metric metric;
	ServersDebugger::ServersProfilerFrame frame;
	frame.deserialize(p_data);
	metric.valid = true;
	metric.frame_number = frame.frame_number;
	metric.frame_time = frame.frame_time;
	metric.process_time = frame.process_time;
	metric.physics_time = frame.physics_time;
	metric.physics_frame_time = frame.physics_frame_time;

	if (frame.servers.size()) {
		EditorProfiler::Metric::Category frame_time;
		frame_time.signature = "category_frame_time";
		frame_time.name = "Frame Time";
		frame_time.total_time = metric.frame_time;

		EditorProfiler::Metric::Category::Item item;
		item.calls = 1;

		item.name = "Physics Time";
		item.total = metric.physics_time;
		item.signature = "physics_time";

		frame_time.items.push_back(item);

		item.name = "Process Time";
		item.total = metric.process_time;
		item.signature = "process_time";

		frame_time.items.push_back(item);

		item.name = "Physics Frame Time";
		item.total = metric.physics_frame_time;
		item.signature = "physics_frame_time";

		frame_time.items.push_back(item);

		metric.categories.push_back(frame_time);
	}

	for (const ServersDebugger::ServerInfo &srv : frame.servers) {
		EditorProfiler::Metric::Category c;
		const String name = srv.name;
		c.name = EditorPropertyNameProcessor::get_singleton()->process_name(name, EditorPropertyNameProcessor::STYLE_CAPITALIZED);
		c.items.resize(srv.functions.size());
		c.total_time = 0;
		c.signature = "categ::" + name;
		int j = 0;
		for (List<ServersDebugger::ServerFunctionInfo>::ConstIterator itr = srv.functions.begin(); itr != srv.functions.end(); ++itr, ++j) {
			EditorProfiler::Metric::Category::Item item;
			item.calls = 1;
			item.name = itr->name;
			item.total = itr->time;
			item.signature = "categ::" + name + "::" + item.name;
			item.name = EditorPropertyNameProcessor::get_singleton()->process_name(item.name, EditorPropertyNameProcessor::STYLE_CAPITALIZED);
			c.total_time += item.total;
			c.items.write[j] = item;
		}
		metric.categories.push_back(c);
	}

	profiler->add_frame_metric(metric, p_final);
}

void ScriptEditorDebugger::_msg_servers_profile_frame(uint64_t p_thread_id, const Array &p_data) {
	_msg_servers_profile_common(p_data, false);
}
void ScriptEditorDebugger::_msg_servers_profile_total(uint64_t p_thread_id, const Array &p_data) {
	_msg_servers_profile_common(p_data, true);
}

void ScriptEditorDebugger::_msg_request_quit(uint64_t p_thread_id, const Array &p_data) {
	emit_signal(SNAME("stop_requested"));
	_stop_and_notify();
}

void ScriptEditorDebugger::_msg_remote_objects_selected(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.is_empty());
	EditorDebuggerNode *dbg = EditorDebuggerNode::get_singleton();
	EditorDebuggerRemoteObjects *robjs = inspector->set_objects(p_data, dbg->get_debugger_id(this));
	if (robjs) {
		dbg->stop_waiting_inspection();
		if (dbg->get_current_debugger() != this) {
			dbg->set_current_debugger(robjs->debugger_id);
		}

		emit_signal(SNAME("remote_objects_updated"), robjs);
		emit_signal(SNAME("remote_tree_select_requested"), robjs->remote_object_ids.duplicate());
	}
}

void ScriptEditorDebugger::_msg_remote_nothing_selected(uint64_t p_thread_id, const Array &p_data) {
	EditorDebuggerNode::get_singleton()->stop_waiting_inspection();

	emit_signal(SNAME("remote_tree_clear_selection_requested"));
}

void ScriptEditorDebugger::_msg_remote_selection_invalidated(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.is_empty());
	inspector->invalidate_selection_from_cache(p_data[0]);
}

void ScriptEditorDebugger::_msg_remote_undo_redo_action(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.size() < 2);
	inspector->add_undo_redo_action(p_data);
}

void ScriptEditorDebugger::_msg_performance_profile_names(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.size() != 2);
	Array name_data = p_data[0];
	Array type_data = p_data[1];

	Vector<StringName> monitors;
	monitors.resize(name_data.size());
	for (int i = 0; i < name_data.size(); i++) {
		ERR_FAIL_COND(name_data[i].get_type() != Variant::STRING_NAME);
		monitors.set(i, name_data[i]);
	}

	PackedInt32Array types;
	types.resize(type_data.size());
	for (int i = 0; i < type_data.size(); i++) {
		ERR_FAIL_COND(type_data[i].get_type() != Variant::INT);
		types.set(i, type_data[i]);
	}

	performance_profiler->update_monitors(monitors, types);
}

void ScriptEditorDebugger::_msg_filesystem_update_file(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.is_empty());
	if (EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->update_file(p_data[0]);
	}
}

void ScriptEditorDebugger::_msg_window_title(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.size() != 1);
	emit_signal(SNAME("remote_window_title_changed"), p_data[0]);
}

void ScriptEditorDebugger::_msg_editor_shortcut_pressed(uint64_t p_thread_id, const Array &p_data) {
	ERR_FAIL_COND(p_data.size() != 1);
	Ref<Shortcut> shortcut = EditorSettings::get_singleton()->get_shortcut(p_data[0]);
	if (shortcut.is_valid() && shortcut->has_valid_event()) {
		Ref<InputEventShortcut> s;
		s.instantiate();
		s->set_shortcut(shortcut);
		Input::get_singleton()->parse_input_event(s);
	}
}

void ScriptEditorDebugger::_parse_message(const String &p_msg, uint64_t p_thread_id, const Array &p_data) {
	emit_signal(SNAME("debug_data"), p_msg, p_data);

	ParseMessageFunc *fn_ptr = parse_message_handlers.getptr(p_msg);
	if (fn_ptr) {
		(this->**fn_ptr)(p_thread_id, p_data);
	} else {
		int colon_index = p_msg.find_char(':');
		ERR_FAIL_COND_MSG(colon_index < 1, "Invalid message received");

		bool parsed = EditorDebuggerNode::get_singleton()->plugins_capture(this, p_msg, p_data);
		if (!parsed) {
			WARN_PRINT("Unknown message: " + p_msg);
		}
	}
}

HashMap<String, ScriptEditorDebugger::ParseMessageFunc> ScriptEditorDebugger::parse_message_handlers;

void ScriptEditorDebugger::_init_parse_message_handlers() {
	parse_message_handlers["set_pid"] = &ScriptEditorDebugger::_msg_set_pid;
	parse_message_handlers["scene:click_ctrl"] = &ScriptEditorDebugger::_msg_scene_click_ctrl;
	parse_message_handlers["scene:scene_tree"] = &ScriptEditorDebugger::_msg_scene_scene_tree;
	parse_message_handlers["scene:inspect_objects"] = &ScriptEditorDebugger::_msg_scene_inspect_objects;
#ifndef DISABLE_DEPRECATED
	parse_message_handlers["scene:inspect_object"] = &ScriptEditorDebugger::_msg_scene_inspect_object;
#endif // DISABLE_DEPRECATED
	parse_message_handlers["scene:debug_mute_audio"] = &ScriptEditorDebugger::_msg_scene_debug_mute_audio;
	parse_message_handlers["servers:memory_usage"] = &ScriptEditorDebugger::_msg_servers_memory_usage;
	parse_message_handlers["output"] = &ScriptEditorDebugger::_msg_output;
	parse_message_handlers["performance:profile_frame"] = &ScriptEditorDebugger::_msg_performance_profile_frame;
	parse_message_handlers["visual:hardware_info"] = &ScriptEditorDebugger::_msg_visual_hardware_info;
	parse_message_handlers["visual:profile_frame"] = &ScriptEditorDebugger::_msg_visual_profile_frame;
	parse_message_handlers["error"] = &ScriptEditorDebugger::_msg_error;
	parse_message_handlers["servers:profile_frame"] = &ScriptEditorDebugger::_msg_servers_profile_frame;
	parse_message_handlers["servers:profile_total"] = &ScriptEditorDebugger::_msg_servers_profile_total;
	parse_message_handlers["request_quit"] = &ScriptEditorDebugger::_msg_request_quit;
	parse_message_handlers["remote_objects_selected"] = &ScriptEditorDebugger::_msg_remote_objects_selected;
	parse_message_handlers["remote_nothing_selected"] = &ScriptEditorDebugger::_msg_remote_nothing_selected;
	parse_message_handlers["remote_selection_invalidated"] = &ScriptEditorDebugger::_msg_remote_selection_invalidated;
	parse_message_handlers["remote_undo_redo_action"] = &ScriptEditorDebugger::_msg_remote_undo_redo_action;
	parse_message_handlers["performance:profile_names"] = &ScriptEditorDebugger::_msg_performance_profile_names;
	parse_message_handlers["filesystem:update_file"] = &ScriptEditorDebugger::_msg_filesystem_update_file;
	parse_message_handlers["window:title"] = &ScriptEditorDebugger::_msg_window_title;
	parse_message_handlers["editor_shortcut_pressed"] = &ScriptEditorDebugger::_msg_editor_shortcut_pressed;
}

void ScriptEditorDebugger::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_TRANSLATION_CHANGED: {
			if (is_ready()) {
				update_tabs();
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			vmem_notice_icon->set_texture(get_editor_theme_icon(SNAME("NodeInfo")));
			vmem_refresh->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
			vmem_export->set_button_icon(get_editor_theme_icon(SNAME("Save")));
			vmem_item_menu->set_item_icon(VMEM_MENU_SHOW_IN_FILESYSTEM, get_editor_theme_icon(SNAME("ShowInFileSystem")));
			vmem_item_menu->set_item_icon(VMEM_MENU_SHOW_IN_EXPLORER, get_editor_theme_icon(SNAME("Filesystem")));

			const Ref<Font> source_font = get_theme_font(SNAME("output_source"), EditorStringName(EditorFonts));
			if (source_font.is_valid()) {
				error_tree->add_theme_font_override("font", source_font);
			}
			const int font_size = get_theme_font_size(SNAME("output_source_size"), EditorStringName(EditorFonts));
			error_tree->add_theme_font_size_override("font_size", font_size);

			TreeItem *error_root = error_tree->get_root();
			if (error_root) {
				TreeItem *error = error_root->get_first_child();
				while (error) {
					if (error->has_meta("_is_warning")) {
						error->set_icon(0, get_editor_theme_icon(SNAME("Warning")));
						error->set_custom_color(0, get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
						error->set_custom_color(1, get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
					} else if (error->has_meta("_is_error")) {
						error->set_icon(0, get_editor_theme_icon(SNAME("Error")));
						error->set_custom_color(0, get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
						error->set_custom_color(1, get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
					}

					error = error->get_next();
				}
			}
		} break;

		case NOTIFICATION_PROCESS: {
			if (is_session_active()) {
				peer->poll();

				if (camera_override == CameraOverride::OVERRIDE_EDITORS) {
					// CanvasItem Editor
					{
						Dictionary state = CanvasItemEditor::get_singleton()->get_state();
						float zoom = state["zoom"];
						Point2 offset = state["ofs"];
						Transform2D transform;

						transform.scale_basis(Size2(zoom, zoom));
						transform.columns[2] = -offset * zoom;

						Array msg = { transform };
						_put_msg("scene:transform_camera_2d", msg);
					}

					// Node3D Editor
					{
						Node3DEditorViewport *viewport = Node3DEditor::get_singleton()->get_last_used_viewport();
						const Camera3D *cam = viewport->get_camera_3d();

						Array msg = { cam->get_camera_transform() };
						if (cam->get_projection() == Camera3D::PROJECTION_ORTHOGONAL) {
							msg.push_back(false);
							msg.push_back(cam->get_size());
						} else {
							msg.push_back(true);
							msg.push_back(cam->get_fov());
						}
						msg.push_back(cam->get_near());
						msg.push_back(cam->get_far());
						_put_msg("scene:transform_camera_3d", msg);
					}
				}
			}

			const uint64_t until = OS::get_singleton()->get_ticks_msec() + 20;

			while (peer.is_valid() && peer->has_message()) {
				Array arr = peer->get_message();
				if (arr.size() != 3 || arr[0].get_type() != Variant::STRING || arr[1].get_type() != Variant::INT || arr[2].get_type() != Variant::ARRAY) {
					_stop_and_notify();
					ERR_FAIL_MSG("Invalid message format received from peer");
				}

				_parse_message(arr[0], arr[1], arr[2]);

				if (OS::get_singleton()->get_ticks_msec() > until) {
					break;
				}
			}
			if (!is_session_active()) {
				_stop_and_notify();
				break;
			};
		} break;
	}
}

void ScriptEditorDebugger::start(Ref<RemoteDebuggerPeer> p_peer) {
	_clear_errors_list();
	stop();

	profiler->set_enabled(true, true);
	visual_profiler->set_enabled(true);

	peer = p_peer;
	ERR_FAIL_COND(p_peer.is_null());

	performance_profiler->reset();

	set_process(true);
	camera_override = CameraOverride::OVERRIDE_NONE;

	_update_buttons_state();

	Array quit_keys = DebuggerMarshalls::serialize_key_shortcut(ED_GET_SHORTCUT("editor/stop_running_project"));
	_put_msg("scene:setup_scene", quit_keys);

	if (EditorSettings::get_singleton()->get_project_metadata("debug_options", "autostart_profiler", false)) {
		profiler->set_profiling(true);
	}

	if (EditorSettings::get_singleton()->get_project_metadata("debug_options", "autostart_visual_profiler", false)) {
		visual_profiler->set_profiling(true);
	}
}

void ScriptEditorDebugger::_update_buttons_state() {
	const bool active = is_session_active();
	const bool has_editor_tree = active && editor_remote_tree && editor_remote_tree->get_selected();
	vmem_refresh->set_disabled(!active);
	le_clear->set_disabled(!active);
	le_set->set_disabled(!has_editor_tree);
}

void ScriptEditorDebugger::_stop_and_notify() {
	stop();
	emit_signal(SNAME("stopped"));
}

void ScriptEditorDebugger::stop() {
	set_process(false);
	remote_pid = 0;

	inspector->clear_cache();

	if (peer.is_valid()) {
		peer->close();
		peer.unref();
	}

	node_path_cache.clear();
	res_path_cache.clear();

	profiler->set_enabled(false, false);
	profiler->set_profiling(false);

	visual_profiler->set_enabled(false);
	visual_profiler->set_profiling(false);

	_update_buttons_state();
}

void ScriptEditorDebugger::_profiler_activate(bool p_enable, int p_type) {
	Array msg_data = { p_enable };
	switch (p_type) {
		case PROFILER_VISUAL:
			_put_msg("profiler:visual", msg_data);
			break;
		case PROFILER_SERVERS:
			_put_msg("profiler:servers", msg_data);
			break;
		default:
			ERR_FAIL_MSG("Invalid profiler type");
	}
}

void ScriptEditorDebugger::_export_csv() {
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	file_dialog_purpose = SAVE_MONITORS_CSV;
	file_dialog->popup_file_dialog();
}

void ScriptEditorDebugger::_resources_reimported(const PackedStringArray &p_resources) {
	Array msg = { p_resources };
	_put_msg("scene:reload_cached_files", msg);
}

int ScriptEditorDebugger::_get_node_path_cache(const NodePath &p_path) {
	const int *r = node_path_cache.getptr(p_path);
	if (r) {
		return *r;
	}

	last_path_id++;

	node_path_cache[p_path] = last_path_id;
	Array msg = { p_path, last_path_id };
	_put_msg("scene:live_node_path", msg);

	return last_path_id;
}

int ScriptEditorDebugger::_get_res_path_cache(const String &p_path) {
	HashMap<String, int>::Iterator E = res_path_cache.find(p_path);

	if (E) {
		return E->value;
	}

	last_path_id++;

	res_path_cache[p_path] = last_path_id;
	Array msg = { p_path, last_path_id };
	_put_msg("scene:live_res_path", msg);

	return last_path_id;
}

void ScriptEditorDebugger::_method_changed(Object *p_base, const StringName &p_name, const Variant **p_args, int p_argcount) {
	if (!p_base || !live_debug || !is_session_active() || !EditorNode::get_singleton()->get_edited_scene()) {
		return;
	}

	Node *node = Object::cast_to<Node>(p_base);

	for (int i = 0; i < p_argcount; i++) {
		//no pointers, sorry
		if (p_args[i]->get_type() == Variant::OBJECT || p_args[i]->get_type() == Variant::RID) {
			return;
		}
	}

	if (node) {
		NodePath path = EditorNode::get_singleton()->get_edited_scene()->get_path_to(node);
		int pathid = _get_node_path_cache(path);

		Array msg = { pathid, p_name };
		for (int i = 0; i < p_argcount; i++) {
			//no pointers, sorry
			msg.push_back(*p_args[i]);
		}
		_put_msg("scene:live_node_call", msg);

		return;
	}

	Resource *res = Object::cast_to<Resource>(p_base);

	if (res && !res->get_path().is_empty()) {
		String respath = res->get_path();
		int pathid = _get_res_path_cache(respath);

		Array msg = { pathid, p_name };
		for (int i = 0; i < p_argcount; i++) {
			//no pointers, sorry
			msg.push_back(*p_args[i]);
		}
		_put_msg("scene:live_res_call", msg);

		return;
	}
}

void ScriptEditorDebugger::_property_changed(Object *p_base, const StringName &p_property, const Variant &p_value) {
	if (!p_base || !live_debug || !EditorNode::get_singleton()->get_edited_scene()) {
		return;
	}

	Node *node = Object::cast_to<Node>(p_base);

	if (node) {
		NodePath path = EditorNode::get_singleton()->get_edited_scene()->get_path_to(node);
		int pathid = _get_node_path_cache(path);

		if (p_value.is_ref_counted()) {
			Ref<Resource> res = p_value;
			if (res.is_valid() && !res->get_path().is_empty()) {
				Array msg = { pathid, p_property, res->get_path() };
				_put_msg("scene:live_node_prop_res", msg);
			}
		} else {
			Array msg = { pathid, p_property, p_value };
			_put_msg("scene:live_node_prop", msg);
		}

		return;
	}

	Resource *res = Object::cast_to<Resource>(p_base);

	if (res && !res->get_path().is_empty()) {
		String respath = res->get_path();
		int pathid = _get_res_path_cache(respath);

		if (p_value.is_ref_counted()) {
			Ref<Resource> res2 = p_value;
			if (res2.is_valid() && !res2->get_path().is_empty()) {
				Array msg = { pathid, p_property, res2->get_path() };
				_put_msg("scene:live_res_prop_res", msg);
			}
		} else {
			Array msg = { pathid, p_property, p_value };
			_put_msg("scene:live_res_prop", msg);
		}

		return;
	}
}

void ScriptEditorDebugger::set_live_debugging(bool p_enable) {
	live_debug = p_enable;
}

void ScriptEditorDebugger::_live_edit_set() {
	if (!is_session_active() || !editor_remote_tree) {
		return;
	}

	TreeItem *ti = editor_remote_tree->get_selected();
	if (!ti) {
		return;
	}

	String path;

	while (ti) {
		String lp = ti->get_text(0);
		path = "/" + lp + path;
		ti = ti->get_parent();
	}

	NodePath np = path;

	EditorNode::get_editor_data().set_edited_scene_live_edit_root(np);

	update_live_edit_root();
}

void ScriptEditorDebugger::_live_edit_clear() {
	NodePath np = NodePath("/root");
	EditorNode::get_editor_data().set_edited_scene_live_edit_root(np);

	update_live_edit_root();
}

void ScriptEditorDebugger::update_live_edit_root() {
	NodePath np = EditorNode::get_editor_data().get_edited_scene_live_edit_root();

	Array msg = { np };
	if (EditorNode::get_singleton()->get_edited_scene()) {
		msg.push_back(EditorNode::get_singleton()->get_edited_scene()->get_scene_file_path());
	} else {
		msg.push_back("");
	}
	_put_msg("scene:live_set_root", msg);
	live_edit_root->set_text(String(np));
}

void ScriptEditorDebugger::live_debug_create_node(const NodePath &p_parent, const String &p_type, const String &p_name) {
	if (live_debug) {
		Array msg = { p_parent, p_type, p_name };
		_put_msg("scene:live_create_node", msg);
	}
}

void ScriptEditorDebugger::live_debug_instantiate_node(const NodePath &p_parent, const String &p_path, const String &p_name) {
	if (live_debug) {
		Array msg = { p_parent, p_path, p_name };
		_put_msg("scene:live_instantiate_node", msg);
	}
}

void ScriptEditorDebugger::live_debug_remove_node(const NodePath &p_at) {
	if (live_debug) {
		Array msg = { p_at };
		_put_msg("scene:live_remove_node", msg);
	}
}

void ScriptEditorDebugger::live_debug_remove_and_keep_node(const NodePath &p_at, ObjectID p_keep_id) {
	if (live_debug) {
		Array msg = { p_at, p_keep_id };
		_put_msg("scene:live_remove_and_keep_node", msg);
	}
}

void ScriptEditorDebugger::live_debug_restore_node(ObjectID p_id, const NodePath &p_at, int p_at_pos) {
	if (live_debug) {
		Array msg = { p_id, p_at, p_at_pos };
		_put_msg("scene:live_restore_node", msg);
	}
}

void ScriptEditorDebugger::live_debug_duplicate_node(const NodePath &p_at, const String &p_new_name) {
	if (live_debug) {
		Array msg = { p_at, p_new_name };
		_put_msg("scene:live_duplicate_node", msg);
	}
}

void ScriptEditorDebugger::live_debug_reparent_node(const NodePath &p_at, const NodePath &p_new_place, const String &p_new_name, int p_at_pos) {
	if (live_debug) {
		Array msg = { p_at, p_new_place, p_new_name, p_at_pos };
		_put_msg("scene:live_reparent_node", msg);
	}
}

bool ScriptEditorDebugger::get_debug_mute_audio() const {
	return debug_mute_audio;
}

void ScriptEditorDebugger::set_debug_mute_audio(bool p_mute) {
	Array msg = { p_mute };
	_put_msg("scene:debug_mute_audio", msg);
	debug_mute_audio = p_mute;
}

void ScriptEditorDebugger::set_debug_collisions(bool p_enable) {
	Array msg = { p_enable };
	_put_msg("scene:set_debug_collisions", msg);
}

CameraOverride ScriptEditorDebugger::get_camera_override() const {
	return camera_override;
}

void ScriptEditorDebugger::set_camera_override(CameraOverride p_override) {
	Array msg = {
		p_override != CameraOverride::OVERRIDE_NONE,
		p_override == CameraOverride::OVERRIDE_EDITORS
	};
	_put_msg("scene:override_cameras", msg);

	camera_override = p_override;
}

void ScriptEditorDebugger::_error_activated() {
	TreeItem *selected = error_tree->get_selected();

	if (!selected) {
		return;
	}

	TreeItem *ci = selected->get_first_child();
	if (ci) {
		selected->set_collapsed(!selected->is_collapsed());
	}
}

void ScriptEditorDebugger::_expand_errors_list() {
	TreeItem *root = error_tree->get_root();
	if (!root) {
		return;
	}

	TreeItem *item = root->get_first_child();
	while (item) {
		item->set_collapsed(false);
		item = item->get_next();
	}
}

void ScriptEditorDebugger::_collapse_errors_list() {
	TreeItem *root = error_tree->get_root();
	if (!root) {
		return;
	}

	TreeItem *item = root->get_first_child();
	while (item) {
		item->set_collapsed(true);
		item = item->get_next();
	}
}

void ScriptEditorDebugger::_vmem_item_activated() {
	TreeItem *selected = vmem_tree->get_selected();
	if (!selected) {
		return;
	}
	const String path = selected->get_text(0);
	if (path.is_empty() || !FileAccess::exists(path)) {
		return;
	}
	FileSystemDock::get_singleton()->navigate_to_path(path);
}

void ScriptEditorDebugger::_vmem_tree_rmb_selected(const Vector2 &p_pos, MouseButton p_button) {
	if (p_button != MouseButton::RIGHT) {
		return;
	}

	TreeItem *item = vmem_tree->get_selected();
	if (!item) {
		return;
	}

	String path = item->get_text(0);
	if (path.is_empty() || !FileAccess::exists(path)) {
		return;
	}

	vmem_item_menu->set_position(vmem_tree->get_screen_position() + p_pos);
	vmem_item_menu->popup();
}

void ScriptEditorDebugger::_vmem_item_menu_id_pressed(int p_option) {
	TreeItem *item = vmem_tree->get_selected();
	if (!item) {
		return;
	}

	String path = item->get_text(0);
	switch (p_option) {
		case VMEM_MENU_SHOW_IN_FILESYSTEM: {
			FileSystemDock::get_singleton()->navigate_to_path(path);
		} break;
		case VMEM_MENU_SHOW_IN_EXPLORER: {
			OS::get_singleton()->shell_show_in_file_manager(ProjectSettings::get_singleton()->globalize_path(path), true);
		} break;
		case VMEM_MENU_OWNERS: {
			FileSystemDock::get_owners_dialog()->show(path);
		} break;
	}
}

void ScriptEditorDebugger::_clear_errors_list() {
	error_tree->clear();
	error_count = 0;
	warning_count = 0;
	emit_signal(SNAME("errors_cleared"));
	update_tabs();

	expand_all_button->set_disabled(true);
	collapse_all_button->set_disabled(true);
	clear_button->set_disabled(true);
}

// Right click on specific file(s) or folder(s).
void ScriptEditorDebugger::_error_tree_item_rmb_selected(const Vector2 &p_pos, MouseButton p_button) {
	if (p_button != MouseButton::RIGHT) {
		return;
	}

	item_menu->clear();
	item_menu->reset_size();

	if (error_tree->is_anything_selected()) {
		item_menu->add_icon_item(get_editor_theme_icon(SNAME("ActionCopy")), TTRC("Copy Error"), ACTION_COPY_ERROR);
		item_menu->add_icon_item(get_editor_theme_icon(SNAME("ExternalLink")), TTRC("Open C++ Source on GitHub"), ACTION_OPEN_SOURCE);
	}

	if (item_menu->get_item_count() > 0) {
		item_menu->set_position(error_tree->get_screen_position() + p_pos);
		item_menu->popup();
	}
}

void ScriptEditorDebugger::_item_menu_id_pressed(int p_option) {
	switch (p_option) {
		case ACTION_COPY_ERROR: {
			TreeItem *ti = error_tree->get_selected();
			while (ti->get_parent() != error_tree->get_root()) {
				ti = ti->get_parent();
			}

			String type;

			if (ti->has_meta("_is_warning")) {
				type = "W ";
			} else if (ti->has_meta("_is_error")) {
				type = "E ";
			}

			String text = ti->get_text(0) + "   ";
			int rpad_len = text.length();

			text = type + text + ti->get_text(1) + "\n";
			TreeItem *ci = ti->get_first_child();
			while (ci) {
				text += "  " + ci->get_text(0).rpad(rpad_len) + ci->get_text(1) + "\n";
				ci = ci->get_next();
			}

			DisplayServer::get_singleton()->clipboard_set(text);
		} break;

		case ACTION_OPEN_SOURCE: {
			TreeItem *ti = error_tree->get_selected();
			while (ti->get_parent() != error_tree->get_root()) {
				ti = ti->get_parent();
			}

			// Find the child with the "C++ Source".
			// It's not at a fixed position as "C++ Error" may come first.
			TreeItem *ci = ti->get_first_child();
			const String cpp_source = "<" + TTR("C++ Source") + ">";
			while (ci) {
				if (ci->get_text(0) == cpp_source) {
					break;
				}
				ci = ci->get_next();
			}

			if (!ci) {
				WARN_PRINT_ED("No C++ source reference is available for this error.");
				return;
			}

			// Parse back the `file:line @ method()` string.
			const Vector<String> file_line_number = ci->get_text(1).get_slicec('@', 0).strip_edges().split(":");
			ERR_FAIL_COND_MSG(file_line_number.size() < 2, "Incorrect C++ source stack trace file:line format (please report).");
			const String &file = file_line_number[0];
			const int line_number = file_line_number[1].to_int();

			// Construct a GitHub repository URL and open it in the user's default web browser.
			// If the commit hash is available, use it for greater accuracy. Otherwise fall back to tagged release.
			String git_ref = String(GODOT_VERSION_HASH).is_empty() ? String(GODOT_VERSION_NUMBER) + "-stable" : String(GODOT_VERSION_HASH);
			OS::get_singleton()->shell_open(vformat("https://github.com/godotengine/godot/blob/%s/%s#L%d",
					git_ref, file, line_number));
		} break;
	}
}

void ScriptEditorDebugger::_tab_changed(int p_tab) {
	if (tabs->get_tab_title(p_tab) == "Video RAM") {
		// "Video RAM" tab was clicked, refresh the data it's displaying when entering the tab.
		_video_mem_request();
	}
}

void ScriptEditorDebugger::_bind_methods() {
	ClassDB::bind_method(D_METHOD("live_debug_create_node"), &ScriptEditorDebugger::live_debug_create_node);
	ClassDB::bind_method(D_METHOD("live_debug_instantiate_node"), &ScriptEditorDebugger::live_debug_instantiate_node);
	ClassDB::bind_method(D_METHOD("live_debug_remove_node"), &ScriptEditorDebugger::live_debug_remove_node);
	ClassDB::bind_method(D_METHOD("live_debug_remove_and_keep_node"), &ScriptEditorDebugger::live_debug_remove_and_keep_node);
	ClassDB::bind_method(D_METHOD("live_debug_restore_node"), &ScriptEditorDebugger::live_debug_restore_node);
	ClassDB::bind_method(D_METHOD("live_debug_duplicate_node"), &ScriptEditorDebugger::live_debug_duplicate_node);
	ClassDB::bind_method(D_METHOD("live_debug_reparent_node"), &ScriptEditorDebugger::live_debug_reparent_node);
	ClassDB::bind_method(D_METHOD("update_remote_object", "id", "property", "value", "field"), &ScriptEditorDebugger::update_remote_object);

	ADD_SIGNAL(MethodInfo("started"));
	ADD_SIGNAL(MethodInfo("stopped"));
	ADD_SIGNAL(MethodInfo("stop_requested"));
	ADD_SIGNAL(MethodInfo("remote_objects_requested", PropertyInfo(Variant::ARRAY, "ids")));
	ADD_SIGNAL(MethodInfo("remote_objects_updated", PropertyInfo(Variant::OBJECT, "remote_objects")));
	ADD_SIGNAL(MethodInfo("remote_object_property_updated", PropertyInfo(Variant::INT, "id"), PropertyInfo(Variant::STRING, "property")));
	ADD_SIGNAL(MethodInfo("remote_window_title_changed", PropertyInfo(Variant::STRING, "title")));
	ADD_SIGNAL(MethodInfo("remote_tree_updated"));
	ADD_SIGNAL(MethodInfo("remote_tree_select_requested", PropertyInfo(Variant::ARRAY, "ids")));
	ADD_SIGNAL(MethodInfo("remote_tree_clear_selection_requested"));
	ADD_SIGNAL(MethodInfo("output", PropertyInfo(Variant::STRING, "msg"), PropertyInfo(Variant::INT, "level")));
	ADD_SIGNAL(MethodInfo("debug_data", PropertyInfo(Variant::STRING, "msg"), PropertyInfo(Variant::ARRAY, "data")));
	ADD_SIGNAL(MethodInfo("errors_cleared"));
}

void ScriptEditorDebugger::add_debugger_tab(Control *p_control) {
	tabs->add_child(p_control);
}

void ScriptEditorDebugger::remove_debugger_tab(Control *p_control) {
	int idx = tabs->get_tab_idx_from_control(p_control);
	ERR_FAIL_COND(idx < 0);
	p_control->queue_free();
}

int ScriptEditorDebugger::get_current_debugger_tab() const {
	return tabs->get_current_tab();
}

void ScriptEditorDebugger::switch_to_debugger(int p_debugger_tab_idx) {
	tabs->set_current_tab(p_debugger_tab_idx);
}

void ScriptEditorDebugger::send_message(const String &p_message, const Array &p_args) {
	_put_msg(p_message, p_args);
}

void ScriptEditorDebugger::toggle_profiler(const String &p_profiler, bool p_enable, const Array &p_data) {
	Array msg_data = { p_enable, p_data };
	_put_msg("profiler:" + p_profiler, msg_data);
}

void ScriptEditorDebugger::update_layout(EditorDock::DockLayout p_layout, int p_slot) {
	if (p_slot != EditorDock::DOCK_SLOT_BOTTOM) {
		vmem_mc->set_theme_type_variation("NoBorderHorizontalBottom");
		vmem_tree->set_scroll_hint_mode(Tree::SCROLL_HINT_MODE_DISABLED);
	} else {
		vmem_mc->set_theme_type_variation("NoBorderHorizontal");
		vmem_tree->set_scroll_hint_mode(Tree::SCROLL_HINT_MODE_BOTTOM);
	}
}

ScriptEditorDebugger::ScriptEditorDebugger() {
	if (unlikely(parse_message_handlers.is_empty())) {
		_init_parse_message_handlers();
	}

	tabs = memnew(TabContainer);
	add_child(tabs);
	tabs->connect("tab_changed", callable_mp(this, &ScriptEditorDebugger::_tab_changed));

	InspectorDock::get_inspector_singleton()->connect("object_id_selected", callable_mp(this, &ScriptEditorDebugger::_remote_object_selected));
	EditorFileSystem::get_singleton()->connect("resources_reimported", callable_mp(this, &ScriptEditorDebugger::_resources_reimported));

	inspector = memnew(EditorDebuggerInspector);
	inspector->connect("objects_edited", callable_mp(this, &ScriptEditorDebugger::_remote_objects_edited));
	inspector->connect("object_property_updated", callable_mp(this, &ScriptEditorDebugger::_remote_object_property_updated));
	inspector->connect("canvas_item_objects_changed", callable_mp(this, &ScriptEditorDebugger::change_canvas_item_objects));

	{ // Errors.
		errors_tab = memnew(VBoxContainer);
		errors_tab->set_name(TTRC("Errors"));

		HBoxContainer *error_hbox = memnew(HBoxContainer);
		errors_tab->add_child(error_hbox);

		expand_all_button = memnew(Button);
		expand_all_button->set_text(TTRC("Expand All"));
		expand_all_button->set_disabled(true);
		expand_all_button->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorDebugger::_expand_errors_list));
		error_hbox->add_child(expand_all_button);

		collapse_all_button = memnew(Button);
		collapse_all_button->set_text(TTRC("Collapse All"));
		collapse_all_button->set_disabled(true);
		collapse_all_button->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorDebugger::_collapse_errors_list));
		error_hbox->add_child(collapse_all_button);

		Control *space = memnew(Control);
		space->set_h_size_flags(SIZE_EXPAND_FILL);
		error_hbox->add_child(space);

		clear_button = memnew(Button);
		clear_button->set_text(TTRC("Clear"));
		clear_button->set_h_size_flags(SIZE_SHRINK_BEGIN);
		clear_button->set_disabled(true);
		clear_button->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorDebugger::_clear_errors_list));
		error_hbox->add_child(clear_button);

		error_tree = memnew(Tree);
		error_tree->set_columns(2);

		error_tree->set_column_expand(0, false);
		error_tree->set_column_custom_minimum_width(0, 140);
		error_tree->set_column_clip_content(0, false);

		error_tree->set_column_expand(1, true);
		error_tree->set_column_clip_content(1, true);

		error_tree->set_select_mode(Tree::SELECT_ROW);
		error_tree->set_hide_root(true);
		error_tree->set_v_size_flags(SIZE_EXPAND_FILL);
		error_tree->set_allow_rmb_select(true);
		error_tree->set_allow_reselect(true);
		error_tree->set_theme_type_variation("TreeSecondary");
		error_tree->connect("item_activated", callable_mp(this, &ScriptEditorDebugger::_error_activated));
		error_tree->connect("item_mouse_selected", callable_mp(this, &ScriptEditorDebugger::_error_tree_item_rmb_selected));
		errors_tab->add_child(error_tree);

		item_menu = memnew(PopupMenu);
		item_menu->connect(SceneStringName(id_pressed), callable_mp(this, &ScriptEditorDebugger::_item_menu_id_pressed));
		error_tree->add_child(item_menu);

		tabs->add_child(errors_tab);
	}

	{ // File dialog
		file_dialog = memnew(EditorFileDialog);
		file_dialog->connect("file_selected", callable_mp(this, &ScriptEditorDebugger::_file_selected));
		add_child(file_dialog);
	}

	{ //profiler
		profiler = memnew(EditorProfiler);
		profiler->set_name(TTRC("Profiler"));
		tabs->add_child(profiler);
		profiler->connect("enable_profiling", callable_mp(this, &ScriptEditorDebugger::_profiler_activate).bind(PROFILER_SERVERS));
	}

	{ //frame profiler
		visual_profiler = memnew(EditorVisualProfiler);
		visual_profiler->set_name(TTRC("Visual Profiler"));
		tabs->add_child(visual_profiler);
		visual_profiler->connect("enable_profiling", callable_mp(this, &ScriptEditorDebugger::_profiler_activate).bind(PROFILER_VISUAL));
	}

	{ //monitors
		performance_profiler = memnew(EditorPerformanceProfiler);
		tabs->add_child(performance_profiler);
	}

	{ //vmem inspect
		VBoxContainer *vmem_vb = memnew(VBoxContainer);
		HBoxContainer *vmem_hb = memnew(HBoxContainer);

		Label *vmlb = memnew(Label(TTRC("List of Video Memory Usage by Resource:")));
		vmlb->set_theme_type_variation("HeaderSmall");
		vmem_hb->add_child(vmlb);

		{ // Add notice icon.
			vmem_notice_icon = memnew(TextureRect);
			vmem_notice_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
			vmem_notice_icon->set_h_size_flags(SIZE_SHRINK_CENTER);
			vmem_notice_icon->set_visible(true);
			vmem_notice_icon->set_tooltip_text(TTRC(R"(Notice:
This tool only reports memory allocations tracked by the engine.
Therefore, total VRAM usage is inaccurate compared to what the Monitors tab or external tools can report.
Instead, use the monitors tab to obtain more precise VRAM usage.

- Buffer Memory (e.g. GPUParticles) is not tracked.
- Meshes are not tracked in the Compatibility renderer.)"));
			vmem_hb->add_child(vmem_notice_icon);
		}

		{ // Add some space to move the rest of the controls to the right.
			Control *space = memnew(Control);
			space->set_h_size_flags(SIZE_EXPAND_FILL);
			vmem_hb->add_child(space);
		}

		vmem_hb->add_child(memnew(Label(TTRC("Total:"))));
		vmem_total = memnew(LineEdit);
		vmem_total->set_editable(false);
		vmem_total->set_accessibility_name(TTRC("Video RAM Total"));
		vmem_total->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
		vmem_hb->add_child(vmem_total);
		vmem_refresh = memnew(Button);
		vmem_refresh->set_accessibility_name(TTRC("Refresh Video RAM"));
		vmem_refresh->set_theme_type_variation(SceneStringName(FlatButton));
		vmem_hb->add_child(vmem_refresh);
		vmem_export = memnew(Button);
		vmem_export->set_theme_type_variation(SceneStringName(FlatButton));
		vmem_export->set_tooltip_text(TTRC("Export list to a CSV file"));
		vmem_hb->add_child(vmem_export);
		vmem_vb->add_child(vmem_hb);
		vmem_refresh->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorDebugger::_video_mem_request));
		vmem_export->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorDebugger::_video_mem_export));

		vmem_mc = memnew(MarginContainer);
		vmem_mc->set_theme_type_variation("NoBorderHorizontal");
		vmem_mc->set_v_size_flags(SIZE_EXPAND_FILL);
		vmem_vb->add_child(vmem_mc);

		vmem_tree = memnew(Tree);
		vmem_vb->set_name(TTRC("Video RAM"));
		vmem_tree->set_columns(4);
		vmem_tree->set_column_titles_visible(true);
		vmem_tree->set_column_title(0, TTRC("Resource Path"));
		vmem_tree->set_column_expand(0, true);
		vmem_tree->set_column_expand(1, false);
		vmem_tree->set_column_title(1, TTRC("Type"));
		vmem_tree->set_column_custom_minimum_width(1, 100 * EDSCALE);
		vmem_tree->set_column_expand(2, false);
		vmem_tree->set_column_title(2, TTRC("Format"));
		vmem_tree->set_column_custom_minimum_width(2, 150 * EDSCALE);
		vmem_tree->set_column_expand(3, false);
		vmem_tree->set_column_title(3, TTRC("Usage"));
		vmem_tree->set_column_custom_minimum_width(3, 80 * EDSCALE);
		vmem_tree->set_hide_root(true);
		vmem_tree->set_scroll_hint_mode(Tree::SCROLL_HINT_MODE_BOTTOM);
		vmem_mc->add_child(vmem_tree);
		vmem_tree->set_allow_rmb_select(true);
		vmem_tree->connect("item_activated", callable_mp(this, &ScriptEditorDebugger::_vmem_item_activated));
		vmem_tree->connect("item_mouse_selected", callable_mp(this, &ScriptEditorDebugger::_vmem_tree_rmb_selected));
		tabs->add_child(vmem_vb);

		vmem_item_menu = memnew(PopupMenu);
		vmem_item_menu->connect(SceneStringName(id_pressed), callable_mp(this, &ScriptEditorDebugger::_vmem_item_menu_id_pressed));
		vmem_item_menu->add_item(TTRC("Show in FileSystem"), VMEM_MENU_SHOW_IN_FILESYSTEM);
		vmem_item_menu->add_item(OS::get_singleton()->get_platform_string(OS::PLATFORM_STRING_FILE_MANAGER_SHOW), VMEM_MENU_SHOW_IN_EXPLORER);
		vmem_item_menu->add_item(TTRC("View Owners..."), VMEM_MENU_OWNERS);
		add_child(vmem_item_menu);
	}

	{ // misc
		VBoxContainer *misc = memnew(VBoxContainer);
		misc->set_name(TTRC("Misc"));
		tabs->add_child(misc);

		GridContainer *info_left = memnew(GridContainer);
		info_left->set_columns(2);
		misc->add_child(info_left);
		clicked_ctrl = memnew(LineEdit);
		clicked_ctrl->set_editable(false);
		clicked_ctrl->set_accessibility_name(TTRC("Clicked Control:"));
		clicked_ctrl->set_h_size_flags(SIZE_EXPAND_FILL);
		info_left->add_child(memnew(Label(TTRC("Clicked Control:"))));
		info_left->add_child(clicked_ctrl);
		clicked_ctrl_type = memnew(LineEdit);
		clicked_ctrl_type->set_editable(false);
		clicked_ctrl_type->set_accessibility_name(TTRC("Clicked Control Type:"));
		info_left->add_child(memnew(Label(TTRC("Clicked Control Type:"))));
		info_left->add_child(clicked_ctrl_type);

		scene_tree = memnew(SceneDebuggerTree);
		live_edit_root = memnew(LineEdit);
		live_edit_root->set_editable(false);
		live_edit_root->set_h_size_flags(SIZE_EXPAND_FILL);
		live_edit_root->set_accessibility_name(TTRC("Live Edit Root:"));

		{
			HBoxContainer *lehb = memnew(HBoxContainer);
			Label *l = memnew(Label(TTRC("Live Edit Root:")));
			info_left->add_child(l);
			lehb->add_child(live_edit_root);
			le_set = memnew(Button(TTRC("Set From Tree")));
			le_set->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorDebugger::_live_edit_set));
			lehb->add_child(le_set);
			le_clear = memnew(Button(TTRC("Clear")));
			le_clear->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorDebugger::_live_edit_clear));
			lehb->add_child(le_clear);
			info_left->add_child(lehb);
		}

		misc->add_child(memnew(HSeparator));

		HBoxContainer *buttons = memnew(HBoxContainer);

		export_csv = memnew(Button(TTRC("Export measures as CSV")));
		export_csv->connect(SceneStringName(pressed), callable_mp(this, &ScriptEditorDebugger::_export_csv));
		buttons->add_child(export_csv);

		misc->add_child(buttons);
	}

	_update_buttons_state();
}

ScriptEditorDebugger::~ScriptEditorDebugger() {
	if (peer.is_valid()) {
		peer->close();
		peer.unref();
	}
	memdelete(inspector);
	memdelete(scene_tree);
}
