#include "wivrn_uinput.h"

#include "utils/overloaded.h"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <system_error>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <linux/uinput.h>

namespace
{
wivrn::fd_base open_uinput_or_throw()
{
	constexpr std::array paths = {"/dev/uinput", "/dev/input/uinput"};
	for (const char * p: paths)
	{
		int fd = ::open(p, O_WRONLY | O_NONBLOCK);
		if (fd >= 0)
			return fd;
		if (errno != ENOENT) // exists but inaccessbile
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

void fill_ids(uinput_user_dev & uidev, uint16_t product) noexcept
{
	std::memset(&uidev, 0, sizeof(uidev));
	uidev.id.bustype = 0x03;
	uidev.id.vendor = 0x4711;
	uidev.id.product = product;
	uidev.id.version = 5;
}

void emit_ev(int fd, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event ev;
	std::memset(&ev, 0, sizeof(ev));
	gettimeofday(&ev.time, nullptr);
	ev.type = type;
	ev.code = code;
	ev.value = value;
	write_or_throw(fd, &ev, sizeof(ev));
}

void syn(int fd)
{
	emit_ev(fd, EV_SYN, SYN_REPORT, 0);
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

} // namespace

wivrn_uinput::wivrn_uinput()
{
	init_keyboard();
	init_mouse();
};

void wivrn_uinput::handle_input(wivrn::from_headset::hid::input & e)
{
	std::visit(utils::overloaded{
	                   [this](const wivrn::from_headset::hid::key_down & e) {
		                   send_key(e.key, true);
	                   },
	                   [this](const wivrn::from_headset::hid::key_up & e) {
		                   send_key(e.key, false);
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
}

void wivrn_uinput::send_key(uint16_t key, bool down)
{
	emit_ev(kbd_fd, EV_KEY, key, down ? 1 : 0);
	syn(kbd_fd);
}

/// 0: left, 1: right, 2: middle
void wivrn_uinput::send_button(uint16_t mouse_button, bool down)
{
	mouse_button += BTN_MOUSE;
	emit_ev(mouse_fd, EV_KEY, mouse_button, down ? 1 : 0);
	syn(mouse_fd);
}

void wivrn_uinput::mouse_move_relative(int16_t x, int16_t y)
{
	if (x)
		emit_ev(mouse_fd, EV_REL, REL_X, x);
	if (y)
		emit_ev(mouse_fd, EV_REL, REL_Y, y);
	syn(mouse_fd);
}

void wivrn_uinput::mouse_scroll(int16_t vertical, int16_t horizontal)
{
	if (vertical)
		emit_ev(mouse_fd, EV_REL, REL_WHEEL, vertical);
	if (horizontal)
		emit_ev(mouse_fd, EV_REL, REL_HWHEEL, horizontal);
	syn(mouse_fd);
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

	uinput_user_dev uidev;
	fill_ids(uidev, 0x0840);
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

	uinput_user_dev uidev;
	fill_ids(uidev, 0x0839);
	std::snprintf(uidev.name, sizeof(uidev.name), "WiVRn Mouse");
	write_or_throw(fd, &uidev, sizeof(uidev));
	ioctl_or_throw(fd, UI_DEV_CREATE);
}
