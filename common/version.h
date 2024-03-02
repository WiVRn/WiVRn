#pragma once

#include "wivrn_serialization.h"
#include "wivrn_packets.h"


namespace xrt::drivers::wivrn
{
	using protocol = std::variant<from_headset::packets, to_headset::packets>;

	extern const char git_version[];
	static inline const constinit auto protocol_version = serialization_type_hash<protocol>();
}
