#pragma once

#include "wivrn_config.h"

#include <sys/types.h>

struct configuration;

pid_t fork_application();
#ifdef WIVRN_USE_SYSTEMD
pid_t start_unit_file();
#endif
int exec_application(configuration);
