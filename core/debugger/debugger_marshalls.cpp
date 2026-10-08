/**************************************************************************/
/*  debugger_marshalls.cpp                                                */
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

#include "debugger_marshalls.h"

#include "core/io/marshalls.h"

#define CHECK_SIZE(arr, expected, what) ERR_FAIL_COND_V_MSG((uint32_t)arr.size() < (uint32_t)(expected), false, String("Malformed ") + what + " message from debugger, message too short. Expected size: " + itos(expected) + ", actual size: " + itos(arr.size()))
#define CHECK_END(arr, expected, what) ERR_FAIL_COND_V_MSG((uint32_t)arr.size() > (uint32_t)expected, false, String("Malformed ") + what + " message from debugger, message too long. Expected size: " + itos(expected) + ", actual size: " + itos(arr.size()))

Array DebuggerMarshalls::OutputError::serialize() {
	Array arr = {
		hr,
		min,
		sec, msec,
		source_file,
		source_func,
		source_line,
		error,
		error_descr,
		warning
	};
	return arr;
}

bool DebuggerMarshalls::OutputError::deserialize(const Array &p_arr) {
	CHECK_SIZE(p_arr, 10, "OutputError");
	hr = p_arr[0];
	min = p_arr[1];
	sec = p_arr[2];
	msec = p_arr[3];
	source_file = p_arr[4];
	source_func = p_arr[5];
	source_line = p_arr[6];
	error = p_arr[7];
	error_descr = p_arr[8];
	warning = p_arr[9];
	CHECK_END(p_arr, 10, "OutputError");
	return true;
}

Array DebuggerMarshalls::serialize_key_shortcut(const Ref<Shortcut> &p_shortcut) {
	ERR_FAIL_COND_V(p_shortcut.is_null(), Array());
	Array keys;
	for (const Ref<InputEvent> ev : p_shortcut->get_events()) {
		const Ref<InputEventKey> kev = ev;
		ERR_CONTINUE(kev.is_null());
		if (kev->get_physical_keycode() != Key::NONE) {
			keys.push_back(true);
			keys.push_back(kev->get_physical_keycode_with_modifiers());
		} else {
			keys.push_back(false);
			keys.push_back(kev->get_keycode_with_modifiers());
		}
	}
	return keys;
}

Ref<Shortcut> DebuggerMarshalls::deserialize_key_shortcut(const Array &p_keys) {
	Array key_events;
	ERR_FAIL_COND_V(p_keys.size() % 2 != 0, Ref<Shortcut>());
	for (int i = 0; i < p_keys.size(); i += 2) {
		ERR_CONTINUE(p_keys[i].get_type() != Variant::BOOL);
		ERR_CONTINUE(p_keys[i + 1].get_type() != Variant::INT);
		key_events.push_back(InputEventKey::create_reference((Key)p_keys[i + 1].operator int(), p_keys[i].operator bool()));
	}
	if (key_events.is_empty()) {
		return Ref<Shortcut>();
	}
	Ref<Shortcut> shortcut;
	shortcut.instantiate();
	shortcut->set_events(key_events);
	return shortcut;
}

String DebuggerMarshalls::parse_type_from_variant(const Variant &p_variant) {
	String name;

	if (p_variant.get_type() == Variant::OBJECT) {
		const Object *obj = p_variant.get_validated_object();
		if (obj) {
			return obj->get_class();
		}
	}

	return name;
}
