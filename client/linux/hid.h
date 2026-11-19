#pragma once

#include <memory>

class scene;

namespace linux_hid
{

enum class device_type
{
	keyboard,
	mouse,
};

bool evdev_accessible(device_type type);

class input_handler
{
	struct impl;
	std::unique_ptr<impl> state;

public:
	input_handler();
	~input_handler();

	input_handler(const input_handler &) = delete;
	input_handler & operator=(const input_handler &) = delete;

	void poll(scene * current_scene, bool keyboard_enabled, bool mouse_enabled);
};

} // namespace linux_hid
