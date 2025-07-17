#pragma once

#include "wivrn_config.h"

#include <string>
#include <sys/types.h>
#include <vector>

namespace wivrn
{

pid_t fork_application(const std::vector<std::string> & args);

#if WIVRN_USE_SYSTEMD
pid_t start_unit_file(const std::vector<std::string> & args);
#endif
} // namespace wivrn
