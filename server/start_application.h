#pragma once

#include <sys/types.h>

struct configuration;

pid_t fork_application();
int exec_application(configuration);
