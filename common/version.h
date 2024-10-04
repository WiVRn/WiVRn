#pragma once

#include "wivrn_packets.h"
#include "wivrn_serialization.h"

namespace wivrn
{
using protocol = std::variant<from_headset::packets, to_headset::packets>;

extern const char git_version[];
extern const char git_commit[];
static inline const constinit auto protocol_version = serialization_type_hash<protocol>();
} // namespace wivrn
