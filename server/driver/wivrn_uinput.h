#include "wivrn_packets.h"
#include "wivrn_sockets.h"

#include <cstdint>
#include <inplace_vector.hpp>
#include <map>
#include <vector>

#include <linux/uinput.h>

class wivrn_uinput
{
public:
	// Devices are created lazily on first use, so only forwarded classes are exposed.
	wivrn_uinput() = default;

	void handle_input(wivrn::from_headset::hid::input &);

	// The gamepad device is created on the first packet with gamepad inputs
	void handle_gamepad(const wivrn::from_headset::inputs &);
	// Remove the virtual gamepad, when the headset stops mirroring or the gamepad disconnects
	void destroy_gamepad();

	// Drains pending force-feedback requests from the virtual gamepad and returns the
	// resulting haptics packets to forward to the headset, if any changed.
	std::vector<wivrn::to_headset::haptics> read_rumble();

private:
	void init_keyboard();
	void init_mouse();
	void init_gamepad();

	// Add more space if needed
	using ev_inplace_vector = beman::inplace_vector::inplace_vector<input_event, 3>;
	using hid_ev = std::pair<ev_inplace_vector, int>;

	hid_ev key_ev(uint16_t key, bool down);
	hid_ev button_ev(uint16_t mouse_button, bool down);
	hid_ev mouse_move_relative_ev(int16_t x, int16_t y);
	hid_ev mouse_scroll_ev(int16_t vertical, int16_t horizontal);

	static constexpr int ff_effects_max = 16;

	wivrn::fd_base kbd_fd;
	wivrn::fd_base mouse_fd;
	wivrn::fd_base gamepad_fd;
	std::map<int16_t, ff_effect> ff_effects;
};
