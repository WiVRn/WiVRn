#include "hid.h"

#include "scene.h"
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <linux/input.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace linux_hid::detail
{

constexpr size_t bits_per_word = std::numeric_limits<unsigned long>::digits;

template <size_t N>
bool has_bit(const std::array<unsigned long, N> & bits, unsigned int code)
{
	return code < N * bits_per_word and (bits[code / bits_per_word] & (1UL << (code % bits_per_word))) != 0;
}

struct input_device
{
	int fd;
	bool keyboard;
	bool mouse;
	bool discarded = false;
	int32_t dx = 0;
	int32_t dy = 0;
	int32_t wheel = 0;
	int32_t hwheel = 0;
	int32_t wheel_hi = 0;
	int32_t hwheel_hi = 0;
	int32_t wheel_remainder = 0;
	int32_t hwheel_remainder = 0;
	bool wheel_has_hi = false;
	bool hwheel_has_hi = false;

	~input_device()
	{
		::close(fd);
	}

	void flush(scene * current_scene, bool mouse_enabled)
	{
		if (current_scene and mouse_enabled and mouse)
		{
			if (dx or dy)
				current_scene->on_input_mouse_move(dx, dy);

			int32_t vertical = wheel;
			int32_t horizontal = hwheel;
			if (wheel_has_hi)
			{
				wheel_remainder += wheel_hi;
				vertical = wheel_remainder / 120;
				wheel_remainder %= 120;
			}
			if (hwheel_has_hi)
			{
				hwheel_remainder += hwheel_hi;
				horizontal = hwheel_remainder / 120;
				hwheel_remainder %= 120;
			}
			if (vertical or horizontal)
				current_scene->on_input_scroll(horizontal, vertical);
		}

		if (not current_scene or not mouse_enabled)
			wheel_remainder = hwheel_remainder = 0;
		dx = dy = wheel = hwheel = wheel_hi = hwheel_hi = 0;
		wheel_has_hi = hwheel_has_hi = false;
	}

	void handle(scene * current_scene, bool keyboard_enabled, bool mouse_enabled, const input_event & event)
	{
		if (event.type == EV_SYN)
		{
			if (event.code == SYN_DROPPED)
			{
				discarded = true;
				flush(nullptr, false);
			}
			else if (event.code == SYN_REPORT)
			{
				if (discarded)
					discarded = false;
				else
					flush(current_scene, mouse_enabled);
			}
			return;
		}

		if (discarded)
			return;

		if (event.type == EV_KEY and (event.value == 0 or event.value == 1))
		{
			if (keyboard and keyboard_enabled and current_scene and event.code > 0 and event.code < BTN_MISC and event.code <= UINT8_MAX and
			    event.code != KEY_VOLUMEUP and event.code != KEY_VOLUMEDOWN and event.code != KEY_POWER)
			{
				if (event.value)
					current_scene->on_input_key_down(event.code);
				else
					current_scene->on_input_key_up(event.code);
			}
			if (mouse and mouse_enabled and current_scene and event.code >= BTN_LEFT and event.code <= BTN_MIDDLE)
			{
				uint8_t button = event.code - BTN_LEFT;
				if (event.value)
					current_scene->on_input_button_down(button);
				else
					current_scene->on_input_button_up(button);
			}
		}
		else if (event.type == EV_REL and mouse and mouse_enabled)
		{
			switch (event.code)
			{
				case REL_X:
					dx += event.value;
					break;
				case REL_Y:
					dy += event.value;
					break;
				case REL_WHEEL:
					wheel += event.value;
					break;
				case REL_HWHEEL:
					hwheel += event.value;
					break;
				case REL_WHEEL_HI_RES:
					wheel_hi += event.value;
					wheel_has_hi = true;
					break;
				case REL_HWHEEL_HI_RES:
					hwheel_hi += event.value;
					hwheel_has_hi = true;
					break;
			}
		}
	}
};

std::unique_ptr<input_device> open_device(const std::filesystem::path & path)
{
	int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return {};

	std::array<unsigned long, (KEY_CNT + bits_per_word - 1) / bits_per_word> keys{};
	std::array<unsigned long, (REL_CNT + bits_per_word - 1) / bits_per_word> relative{};
	bool has_keys = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) >= 0;
	bool has_relative = ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relative)), relative.data()) >= 0;

	bool keyboard = has_keys and has_bit(keys, KEY_A) and has_bit(keys, KEY_SPACE);
	bool mouse = (has_relative and has_bit(relative, REL_X) and has_bit(relative, REL_Y)) or (has_keys and has_bit(keys, BTN_LEFT));
	if (not keyboard and not mouse)
	{
		::close(fd);
		return {};
	}

	return std::unique_ptr<input_device>(new input_device{
	        .fd = fd,
	        .keyboard = keyboard,
	        .mouse = mouse,
	});
}

template <typename Fn>
void for_each_evdev(Fn && fn)
{
	std::error_code ec;
	std::filesystem::directory_iterator it("/dev/input", ec);
	for (const std::filesystem::directory_iterator end; not ec and it != end; it.increment(ec))
	{
		if (it->path().filename().string().starts_with("event"))
			fn(it->path());
	}
}

} // namespace linux_hid::detail

using linux_hid::detail::for_each_evdev;
using linux_hid::detail::open_device;

struct linux_hid::input_handler::impl
{
	std::map<std::filesystem::path, std::unique_ptr<detail::input_device>> devices;
	std::chrono::steady_clock::time_point next_scan{};
	bool keyboard_enabled = false;
	bool mouse_enabled = false;
};

bool linux_hid::evdev_accessible(device_type type)
{
	static auto last_check = std::chrono::steady_clock::time_point{};
	static std::array<bool, 2> available{};
	const auto now = std::chrono::steady_clock::now();
	if (now - last_check >= 1s)
	{
		available = {};
		for_each_evdev([&](const auto & path) {
			if (auto device = open_device(path))
			{
				available[0] |= device->keyboard;
				available[1] |= device->mouse;
			}
		});
		last_check = now;
	}
	return available[type == device_type::mouse ? 1 : 0];
}

linux_hid::input_handler::input_handler() :
        state(std::make_unique<impl>())
{}

linux_hid::input_handler::~input_handler() = default;

void linux_hid::input_handler::poll(scene * current_scene, bool keyboard_enabled, bool mouse_enabled)
{
	if (not current_scene or (not keyboard_enabled and not mouse_enabled))
	{
		state->devices.clear();
		state->next_scan = {};
		state->keyboard_enabled = false;
		state->mouse_enabled = false;
		return;
	}

	if (state->keyboard_enabled != keyboard_enabled or state->mouse_enabled != mouse_enabled)
	{
		state->keyboard_enabled = keyboard_enabled;
		state->mouse_enabled = mouse_enabled;
		state->next_scan = {};
	}

	std::erase_if(state->devices, [&](const auto & entry) {
		const auto & device = *entry.second;
		return not((device.keyboard and keyboard_enabled) or (device.mouse and mouse_enabled));
	});

	const auto now = std::chrono::steady_clock::now();
	if (now >= state->next_scan)
	{
		std::set<std::filesystem::path> found;
		for_each_evdev([&](const auto & path) {
			found.insert(path);
			if (not state->devices.contains(path))
			{
				if (auto device = open_device(path); device and ((device->keyboard and keyboard_enabled) or (device->mouse and mouse_enabled)))
					state->devices.emplace(path, std::move(device));
			}
		});
		std::erase_if(state->devices, [&](const auto & entry) { return not found.contains(entry.first); });
		state->next_scan = now + 1s;
	}

	for (auto it = state->devices.begin(); it != state->devices.end();)
	{
		auto & device = *it->second;
		input_event event;
		ssize_t count;
		while ((count = ::read(device.fd, &event, sizeof(event))) == sizeof(event))
			device.handle(current_scene, keyboard_enabled, mouse_enabled, event);

		if (count == 0 or (count < 0 and errno != EAGAIN and errno != EWOULDBLOCK and errno != EINTR) or (count > 0 and count != sizeof(event)))
			it = state->devices.erase(it);
		else
			++it;
	}
}
