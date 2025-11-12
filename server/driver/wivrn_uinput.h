#include "wivrn_packets.h"
#include "wivrn_sockets.h"

#include <cstdint>

class wivrn_uinput
{
public:
	wivrn_uinput();

	void handle_input(wivrn::from_headset::hid::input &);

private:
	void init_keyboard();
	void init_mouse();

	void send_key(uint16_t key, bool down);
	void send_button(uint16_t mouse_button, bool down);
	void mouse_move_relative(int16_t x, int16_t y);
	void mouse_scroll(int16_t vertical, int16_t horizontal);

	wivrn::fd_base kbd_fd;
	wivrn::fd_base mouse_fd;
};
