/**************************************************************************/
/*  scene_debugger_object.cpp                                             */
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

#ifdef DEBUG_ENABLED

#include "scene_debugger_object.h"

#include "core/debugger/debugger_marshalls.h"
#include "core/io/marshalls.h"
#include "scene/main/node.h"

SceneDebuggerObject::SceneDebuggerObject(Object *p_obj) {
	if (!p_obj) {
		return;
	}

	id = p_obj->get_instance_id();
	class_name = p_obj->get_class();

	if (Node *node = Object::cast_to<Node>(p_obj)) {
		{
			PropertyInfo pi(Variant::STRING_NAME, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE);
			properties.push_back(SceneDebuggerProperty(pi, node->get_name()));
		}

		// For debugging multiplayer.
		{
			PropertyInfo pi(Variant::INT, String("Node/multiplayer_authority"), PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_READ_ONLY);
			properties.push_back(SceneDebuggerProperty(pi, node->get_multiplayer_authority()));
		}

		// Add specialized NodePath info (if inside tree).
		if (node->is_inside_tree()) {
			PropertyInfo pi(Variant::NODE_PATH, String("Node/path"));
			properties.push_back(SceneDebuggerProperty(pi, node->get_path()));
		} else { // Can't ask for path if a node is not in tree.
			PropertyInfo pi(Variant::STRING, String("Node/path"));
			properties.push_back(SceneDebuggerProperty(pi, "[Orphan]"));
		}
	}

	// Add base object properties.
	List<PropertyInfo> pinfo;
	p_obj->get_property_list(&pinfo, true);
	for (PropertyInfo &E : pinfo) {
		const Variant &m = p_obj->get(E.name);

		if (!m.is_null() && E.type == Variant::OBJECT && E.hint == PROPERTY_HINT_NODE_TYPE && E.usage & PROPERTY_USAGE_EDITOR) {
			E.hint_string = DebuggerMarshalls::parse_type_from_variant(m);
		}

		if (E.usage & (PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) {
			properties.push_back(SceneDebuggerProperty(E, m));
		}
	}
}

void SceneDebuggerObject::serialize(Array &r_arr, int p_max_size) {
	Array send_props;
	for (SceneDebuggerProperty &property : properties) {
		const PropertyInfo &pi = property.first;
		Variant &var = property.second;

		Ref<Resource> res = var;

		Array prop = { pi.name, pi.type };
		PropertyHint hint = pi.hint;
		String hint_string = pi.hint_string;

		if (res.is_valid()) {
			hint_string = res->get_class();
		}

		if (res.is_valid() && !res->get_path().is_empty()) {
			// HACK: Overwrite `PropertyInfo` with the current runtime type.
			// This allows untyped variables to be displayed correctly.
			prop[1] = Variant::OBJECT;

			var = res->get_path();
		} else { //only send information that can be sent..
			int len = 0; //test how big is this to encode
			encode_variant(var, nullptr, len);
			if (len > p_max_size) { //limit to max size
				hint = PROPERTY_HINT_OBJECT_TOO_BIG;
				hint_string = "";
				var = Variant();
			}
		}
		prop.push_back(hint);
		prop.push_back(hint_string);
		prop.push_back(pi.usage);
		prop.push_back(var);
		send_props.push_back(prop);
	}
	r_arr.push_back(uint64_t(id));
	r_arr.push_back(class_name);
	r_arr.push_back(send_props);
}

#define CHECK_TYPE(p_what, p_type) ERR_FAIL_COND(p_what.get_type() != Variant::p_type)

void SceneDebuggerObject::deserialize(const Array &p_arr) {
	ERR_FAIL_COND(p_arr.size() < 3);
	CHECK_TYPE(p_arr[0], INT);
	CHECK_TYPE(p_arr[1], STRING);
	CHECK_TYPE(p_arr[2], ARRAY);

	deserialize(uint64_t(p_arr[0]), p_arr[1], p_arr[2]);
}

void SceneDebuggerObject::deserialize(uint64_t p_id, const String &p_class_name, const Array &p_props) {
	id = p_id;
	class_name = p_class_name;

	for (int i = 0; i < p_props.size(); i++) {
		CHECK_TYPE(p_props[i], ARRAY);
		Array prop = p_props[i];

		ERR_FAIL_COND(prop.size() != 6);
		CHECK_TYPE(prop[0], STRING);
		CHECK_TYPE(prop[1], INT);
		CHECK_TYPE(prop[2], INT);
		CHECK_TYPE(prop[3], STRING);
		CHECK_TYPE(prop[4], INT);

		PropertyInfo pinfo;
		pinfo.name = prop[0];
		pinfo.type = Variant::Type(int(prop[1]));
		pinfo.hint = PropertyHint(int(prop[2]));
		pinfo.hint_string = prop[3];
		pinfo.usage = PropertyUsageFlags(int(prop[4]));
		Variant var = prop[5];

		if (pinfo.type == Variant::OBJECT) {
			if (var.is_zero()) {
				var = Ref<Resource>();
			} else if (var.get_type() == Variant::OBJECT) {
				if (((Object *)var)->is_class("EncodedObjectAsID")) {
					var = Object::cast_to<EncodedObjectAsID>(var)->get_object_id();
					pinfo.type = var.get_type();
					pinfo.hint = PROPERTY_HINT_OBJECT_ID;
					if (pinfo.hint_string.is_empty()) {
						pinfo.hint_string = "Object";
					}
				}
			}
		}
		properties.push_back(SceneDebuggerProperty(pinfo, var));
	}
}

SceneDebuggerTree::SceneDebuggerTree(Node *p_root) {
	// Flatten tree into list, depth first, use stack to avoid recursion.
	List<Node *> stack;
	stack.push_back(p_root);
	bool is_root = true;
	const StringName &is_visible_in_tree_sn = SNAME("is_visible_in_tree");
	while (stack.size()) {
		Node *n = stack.front()->get();
		stack.pop_front();

		int count = n->get_child_count();
		for (int i = 0; i < count; i++) {
			stack.push_front(n->get_child(count - i - 1));
		}

		int view_flags = 0;
		if (is_root) {
			// Prevent root window visibility from being changed.
			is_root = false;
		} else if (n->has_method(SceneStringName(is_visible))) {
			const Variant visible = n->call(SceneStringName(is_visible));
			if (visible.get_type() == Variant::BOOL) {
				view_flags = RemoteNode::VIEW_HAS_VISIBLE_METHOD;
				view_flags |= uint8_t(visible) * RemoteNode::VIEW_VISIBLE;
			}
			if (n->has_method(is_visible_in_tree_sn)) {
				const Variant visible_in_tree = n->call(is_visible_in_tree_sn);
				if (visible_in_tree.get_type() == Variant::BOOL) {
					view_flags |= uint8_t(visible_in_tree) * RemoteNode::VIEW_VISIBLE_IN_TREE;
				}
			}
		}

		nodes.push_back(RemoteNode(count, n->get_name(), n->get_class(), n->get_instance_id(), n->get_scene_file_path(), view_flags));
	}
}

void SceneDebuggerTree::serialize(Array &p_arr) {
	for (const RemoteNode &n : nodes) {
		p_arr.push_back(n.child_count);
		p_arr.push_back(n.name);
		p_arr.push_back(n.type_name);
		p_arr.push_back(n.id);
		p_arr.push_back(n.scene_file_path);
		p_arr.push_back(n.view_flags);
	}
}

void SceneDebuggerTree::deserialize(const Array &p_arr) {
	int idx = 0;
	while (p_arr.size() > idx) {
		ERR_FAIL_COND(p_arr.size() < 6);
		CHECK_TYPE(p_arr[idx], INT); // child_count.
		CHECK_TYPE(p_arr[idx + 1], STRING); // name.
		CHECK_TYPE(p_arr[idx + 2], STRING); // type_name.
		CHECK_TYPE(p_arr[idx + 3], INT); // id.
		CHECK_TYPE(p_arr[idx + 4], STRING); // scene_file_path.
		CHECK_TYPE(p_arr[idx + 5], INT); // view_flags.
		nodes.push_back(RemoteNode(p_arr[idx], p_arr[idx + 1], p_arr[idx + 2], p_arr[idx + 3], p_arr[idx + 4], p_arr[idx + 5]));
		idx += 6;
	}
}

#undef CHECK_TYPE

#endif // DEBUG_ENABLED
