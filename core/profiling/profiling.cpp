/**************************************************************************/
/*  profiling.cpp                                                         */
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

#include "profiling.h"

#if defined(GODOT_USE_TRACY)
// Use the tracy profiler.

namespace tracy {
static bool configured = false;
} // namespace tracy

void godot_init_profiler() {
	ERR_FAIL_COND(tracy::configured);

	tracy::configured = true;

	// Send our first event to tracy; otherwise it doesn't start collecting data.
	// FrameMark is kind of fitting because it communicates "this is where we started tracing".
	FrameMark;
}

void godot_cleanup_profiler() {
	ERR_FAIL_COND(!tracy::configured);

	tracy::configured = false;
}

#elif defined(GODOT_USE_PERFETTO)
PERFETTO_TRACK_EVENT_STATIC_STORAGE();

void godot_init_profiler() {
	perfetto::TracingInitArgs args;

	args.backends |= perfetto::kSystemBackend;

	perfetto::Tracing::Initialize(args);
	perfetto::TrackEvent::Register();
}

void godot_cleanup_profiler() {
	// Stub
}

#elif defined(GODOT_USE_INSTRUMENTS)

namespace apple::instruments {

os_log_t LOG;
os_log_t LOG_TRACING;

} // namespace apple::instruments

void godot_init_profiler() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;
	apple::instruments::LOG = os_log_create("org.godotengine.godot", OS_LOG_CATEGORY_POINTS_OF_INTEREST);
#ifdef INSTRUMENTS_SAMPLE_CALLSTACKS
	apple::instruments::LOG_TRACING = os_log_create("org.godotengine.godot", OS_LOG_CATEGORY_DYNAMIC_STACK_TRACING);
#else
	apple::instruments::LOG_TRACING = os_log_create("org.godotengine.godot", "tracing");
#endif
}

void godot_cleanup_profiler() {
}

#else
void godot_init_profiler() {
	// Stub
}

void godot_cleanup_profiler() {
	// Stub
}
#endif
