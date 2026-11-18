#include "wivrn_uinput.h"

#include "utils/overloaded.h"
#include "wivrn_gamepad_map.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <inplace_vector.hpp>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <system_error>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <linux/uinput.h>

using namespace beman::inplace_vector;

namespace
{
constexpr uinput_user_dev fill_ids(uint16_t vendor, uint16_t product) noexcept
{
	return {
	        .id = {
	                .bustype = 0x03,
	                .vendor = vendor,
	                .product = product,
	                .version = 5,
	        },
	};
}

constexpr uinput_user_dev fill_ids(uint16_t product) noexcept
{
	return fill_ids(0x4711, product);
}

constexpr input_event ev(uint16_t type, uint16_t code, int32_t value) noexcept
{
	return {
	        .type = type,
	        .code = code,
	        .value = value,
	};
}

constexpr input_event syn() noexcept
{
	return ev(EV_SYN, SYN_REPORT, 0);
}

constexpr int16_t scale_stick(float v) noexcept
{
	v = std::clamp(v, -1.f, 1.f);
	return std::lround(v * 32767.f);
}

constexpr int16_t scale_trigger(float v) noexcept
{
	v = std::clamp(v, 0.f, 1.f);
	return std::lround(v * 255.f);
}
} // namespace

wivrn::fd_base open_uinput_or_throw(int flags = O_WRONLY)
{
	constexpr std::array paths = {"/dev/uinput", "/dev/input/uinput"};
	for (const char * p: paths)
	{
		int fd = ::open(p, flags | O_NONBLOCK);
		if (fd >= 0)
			return fd;
		if (errno != ENOENT) // exists but inaccessible
			throw std::system_error(errno, std::generic_category(), "error while opening uinput");
	}
	throw std::runtime_error("no uinput device found");
}

void write_or_throw(int fd, const void * buf, size_t n)
{
	auto res = ::write(fd, buf, n);
	if (res < 0)
		throw std::system_error(errno, std::generic_category(), "error during write");
	if (res != n)
		throw std::runtime_error("byte count mismatch while writing to uinput");
}

void ioctl_or_throw(int fd, unsigned long op)
{
	auto n = ioctl(fd, op);
	if (n < 0)
		throw std::system_error(errno, std::generic_category(), "ioctl error");
}

void ioctl_or_throw(int fd, unsigned long op, unsigned long int arg)
{
	auto n = ioctl(fd, op, arg);
	if (n < 0)
		throw std::system_error(errno, std::generic_category(), "ioctl error");
}

void wivrn_uinput::handle_input(wivrn::from_headset::hid::input & e)
{
	if (kbd_fd.get_fd() < 0)
	{
		init_keyboard();
		init_mouse();
	}

	std::visit(utils::overloaded{
	                   [this](const wivrn::from_headset::hid::key_down & e) {
		                   send_key(e.key, true);
	                   },
	                   [this](const wivrn::from_headset::hid::key_up & e) {
		                   send_button(e.key, false);
	                   },
	                   [this](const wivrn::from_headset::hid::button_down & e) {
		                   send_button(e.button, true);
	                   },
	                   [this](const wivrn::from_headset::hid::button_up & e) {
		                   send_button(e.button, false);
	                   },
	                   [this](const wivrn::from_headset::hid::mouse_scroll & e) {
		                   mouse_scroll(e.v, e.h);
	                   },
	                   [this](const wivrn::from_headset::hid::mouse_move & e) {
		                   mouse_move_relative(e.x, e.y);
	                   },
	           },
	           e.input_data);

	// write_or_throw(fd, events.data(), events.size() * sizeof(decltype(events)::value_type));
}

void wivrn_uinput::handle_gamepad(const wivrn::from_headset::inputs & inputs)
{
	using namespace wivrn;
	if (gamepad_fd.get_fd() < 0)
	{
		// Inputs packets are mostly controller inputs: don't create the device
		// until the headset actually sends gamepad inputs
		if (std::ranges::none_of(inputs.values, [](const auto & value) { return map_gamepad(value.id).slot != gp_count; }))
			return;
		init_gamepad();
	}
	int fd = gamepad_fd.get_fd();

	bool dpad_left = false, dpad_right = false, dpad_up = false, dpad_down = false;
	std::vector<input_event> events;
	// Reserve dynamic memory so a reallocation doesn't happen.
	// 1 index for the syn report, 2 for the dpad events and the remainder for all other gamepad events.
	events.reserve(3 + inputs.values.size());

	for (const auto & value: inputs.values)
	{
		auto m = map_gamepad(value.id);
		if (m.slot == gp_count)
			continue;
		switch (m.kind)
		{
			case gp_value::button:
				events.push_back(ev(EV_KEY, m.ev_code, value.value != 0));
				break;
			case gp_value::trigger:
				events.push_back(ev(EV_ABS, m.ev_code, scale_trigger(value.value)));
				break;
			case gp_value::stick_x:
			case gp_value::stick_y:
				events.push_back(ev(EV_ABS, m.ev_code, scale_stick(value.value)));
				break;
			case gp_value::dpad:
				switch (m.slot)
				{
					case gp_dpad_left:
						dpad_left = value.value != 0;
						break;
					case gp_dpad_right:
						dpad_right = value.value != 0;
						break;
					case gp_dpad_up:
						dpad_up = value.value != 0;
						break;
					case gp_dpad_down:
						dpad_down = value.value != 0;
						break;
					default:
						break;
				}
				break;
		}
	}

	const auto axis = [](bool positive, bool negative) {
		return static_cast<int32_t>(positive) - static_cast<int32_t>(negative);
	};

	events.push_back(ev(EV_ABS, ABS_HAT0X, axis(dpad_right, dpad_left)));
	events.push_back(ev(EV_ABS, ABS_HAT0Y, axis(dpad_down, dpad_up)));
	events.push_back(syn());

	write_or_throw(fd, events.data(), events.size() * sizeof(decltype(events)::value_type));
}

void wivrn_uinput::destroy_gamepad()
{
	if (gamepad_fd.get_fd() < 0)
		return;
	ioctl(gamepad_fd.get_fd(), UI_DEV_DESTROY);
	gamepad_fd = {};
	ff_effects.clear();
}

std::vector<wivrn::to_headset::haptics> wivrn_uinput::read_rumble()
{
	int fd = gamepad_fd.get_fd();
	if (fd < 0)
		return {};

	std::vector<wivrn::to_headset::haptics> result;
	auto motors = [&](float strong, float weak, uint16_t length_ms) {
		// 0 length is a continuous effect; send a finite burst instead
		const std::chrono::nanoseconds duration{int64_t(length_ms ? length_ms : 100) * 1'000'000};
		// strong/weak are motor weights, not locations, so drive both outputs at the combined intensity
		const float amplitude = std::max(strong, weak);
		result = {
		        {wivrn::device_id::GAMEPAD_HAPTIC_LEFT, duration, 0.f, amplitude},
		        {wivrn::device_id::GAMEPAD_HAPTIC_RIGHT, duration, 0.f, amplitude},
		};
	};

	input_event ev;
	while (::read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
	{
		if (ev.type == EV_UINPUT and ev.code == UI_FF_UPLOAD)
		{
			uinput_ff_upload upload{
			        .request_id = (uint32_t)ev.value,
			};
			if (ioctl(fd, UI_BEGIN_FF_UPLOAD, &upload) == 0)
			{
				ff_effects[upload.effect.id] = upload.effect;
				upload.retval = 0;
				ioctl(fd, UI_END_FF_UPLOAD, &upload);
			}
		}
		else if (ev.type == EV_UINPUT and ev.code == UI_FF_ERASE)
		{
			uinput_ff_erase erase{
			        .request_id = (uint32_t)ev.value,
			};
			if (ioctl(fd, UI_BEGIN_FF_ERASE, &erase) == 0)
			{
				ff_effects.erase(erase.effect_id);
				erase.retval = 0;
				ioctl(fd, UI_END_FF_ERASE, &erase);
			}
		}
		else if (ev.type == EV_FF)
		{
			// ev.code is the effect id, ev.value != 0 starts it, 0 stops it.
			if (ev.value == 0)
			{
				motors(0.f, 0.f, 0);
			}
			else if (auto it = ff_effects.find(ev.code); it != ff_effects.end() and it->second.type == FF_RUMBLE)
			{
				const auto & rumble = it->second.u.rumble;
				motors(rumble.strong_magnitude / 65535.f, rumble.weak_magnitude / 65535.f, (uint16_t)it->second.replay.length);
			}
		}
	}

	return result;
}

void wivrn_uinput::send_key(uint16_t key, bool down)
{
	std::array<input_event, 2> events = {
	        ev(EV_KEY, key, static_cast<int32_t>(down)),
	        syn()};
	write_or_throw(kbd_fd.get_fd(), events.data(), events.size() * sizeof(decltype(events)::value_type));
}

/// 0: left, 1: right, 2: middle
void wivrn_uinput::send_button(uint16_t mouse_button, bool down)
{
	std::array<input_event, 2> events = {
	        ev(EV_KEY, mouse_button + BTN_MOUSE, static_cast<int32_t>(down)),
	        syn()};
	write_or_throw(kbd_fd.get_fd(), events.data(), events.size() * sizeof(decltype(events)::value_type));
}

void wivrn_uinput::mouse_move_relative(int16_t x, int16_t y)
{
	inplace_vector<input_event, 3> events;
	if (x)
		events.push_back(ev(EV_REL, REL_X, x));
	if (y)
		events.push_back(ev(EV_REL, REL_Y, y));
	events.push_back(syn());
	write_or_throw(mouse_fd.get_fd(), events.data(), events.size() * sizeof(decltype(events)::value_type));
}

void wivrn_uinput::mouse_scroll(int16_t vertical, int16_t horizontal)
{
	inplace_vector<input_event, 3> events;
	if (vertical)
		events.push_back(ev(EV_REL, REL_WHEEL, vertical));
	if (horizontal)
		events.push_back(ev(EV_REL, REL_HWHEEL, horizontal));
	events.push_back(syn());
	write_or_throw(mouse_fd.get_fd(), events.data(), events.size() * sizeof(decltype(events)::value_type));
}

void wivrn_uinput::init_keyboard()
{
	kbd_fd = open_uinput_or_throw();
	int fd = kbd_fd.get_fd();

	ioctl_or_throw(fd, UI_SET_EVBIT, EV_KEY);
	ioctl_or_throw(fd, UI_SET_EVBIT, EV_REP);

	// enable all KEY_* to allow arbitrary key codes
	for (int code = 1; code <= KEY_MAX; ++code)
		ioctl_or_throw(fd, UI_SET_KEYBIT, code);

	uinput_user_dev uidev = fill_ids(0x0840);
	std::snprintf(uidev.name, sizeof(uidev.name), "WiVRn Keyboard");
	write_or_throw(fd, &uidev, sizeof(uidev));
	ioctl_or_throw(fd, UI_DEV_CREATE);
}

void wivrn_uinput::init_mouse()
{
	mouse_fd = open_uinput_or_throw();
	int fd = mouse_fd.get_fd();

	ioctl_or_throw(fd, UI_SET_EVBIT, EV_KEY);
	ioctl_or_throw(fd, UI_SET_EVBIT, EV_REL);

	// buttons
	ioctl_or_throw(fd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl_or_throw(fd, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl_or_throw(fd, UI_SET_KEYBIT, BTN_MIDDLE);

	// relative motion + wheels
	ioctl_or_throw(fd, UI_SET_RELBIT, REL_X);
	ioctl_or_throw(fd, UI_SET_RELBIT, REL_Y);
	ioctl_or_throw(fd, UI_SET_RELBIT, REL_WHEEL);
	ioctl_or_throw(fd, UI_SET_RELBIT, REL_HWHEEL);

	uinput_user_dev uidev = fill_ids(0x0839);
	std::snprintf(uidev.name, sizeof(uidev.name), "WiVRn Mouse");
	write_or_throw(fd, &uidev, sizeof(uidev));
	ioctl_or_throw(fd, UI_DEV_CREATE);
}

void wivrn_uinput::init_gamepad()
{
	// O_RDWR to read force-feedback (rumble) back from the device
	gamepad_fd = open_uinput_or_throw(O_RDWR);
	int fd = gamepad_fd.get_fd();

	ioctl_or_throw(fd, UI_SET_EVBIT, EV_KEY);
	ioctl_or_throw(fd, UI_SET_EVBIT, EV_ABS);
	ioctl_or_throw(fd, UI_SET_EVBIT, EV_FF);
	ioctl_or_throw(fd, UI_SET_FFBIT, FF_RUMBLE);

	// BTN_GAMEPAD (== BTN_SOUTH) is required or SDL ignores the device
	for (int code: {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR})
		ioctl_or_throw(fd, UI_SET_KEYBIT, code);

	for (int code: {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y})
		ioctl_or_throw(fd, UI_SET_ABSBIT, code);

	// Microsoft X-Box vendor/product so Steam Input also recognizes it by GUID
	uinput_user_dev uidev = fill_ids(0x045e, 0x028e);
	uidev.ff_effects_max = ff_effects_max;
	std::snprintf(uidev.name, sizeof(uidev.name), "WiVRn Gamepad");

	const auto set_abs = [&](int code, int32_t min, int32_t max, int32_t fuzz, int32_t flat) {
		uidev.absmin[code] = min;
		uidev.absmax[code] = max;
		uidev.absfuzz[code] = fuzz;
		uidev.absflat[code] = flat;
	};
	for (int code: {ABS_X, ABS_Y, ABS_RX, ABS_RY})
		set_abs(code, -32768, 32767, 16, 128);
	set_abs(ABS_Z, 0, 255, 0, 0);
	set_abs(ABS_RZ, 0, 255, 0, 0);
	set_abs(ABS_HAT0X, -1, 1, 0, 0);
	set_abs(ABS_HAT0Y, -1, 1, 0, 0);

	write_or_throw(fd, &uidev, sizeof(uidev));
	ioctl_or_throw(fd, UI_DEV_CREATE);
}
