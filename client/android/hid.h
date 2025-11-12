#pragma once

#include "scene.h"
#include <android_native_app_glue.h>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <mutex>
#include <vector>

namespace android_hid
{

void request_pointer_capture(ANativeActivity * activity);
void release_pointer_capture(ANativeActivity * activity);

class input_handler
{
	uint32_t buttons_before = 0;

public:
	bool handle_input(scene * current_scene, AInputEvent * event);
};

} // namespace android_hid
