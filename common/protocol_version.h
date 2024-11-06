#include "wivrn_packets.h"
#include "wivrn_serialization.h"

#pragma once

namespace wivrn
{
using protocol = std::variant<from_headset::packets, to_headset::packets>;
static inline const constinit auto protocol_version = serialization_type_hash<protocol>();
} // namespace wivrn
