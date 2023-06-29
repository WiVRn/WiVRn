#include "audio_setup.h"

#include "audio_pulse.h"

std::shared_ptr<audio_device> audio_device::create(
        const std::string & source_name,
        const std::string & source_description,
        const std::string & sink_name,
        const std::string & sink_description,
        const xrt::drivers::wivrn::from_headset::headset_info_packet & info,
        xrt::drivers::wivrn::wivrn_session& session)
{
	// TODO: other backends
	return create_pulse_handle(source_name, source_description, sink_name, sink_description, info, session);
}
