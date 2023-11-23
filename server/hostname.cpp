#include "hostname.h"
#include <string>
#include <unistd.h>
#include <limits.h>

std::string hostname()
{
	char buf[HOST_NAME_MAX];
	int code = gethostname(buf, HOST_NAME_MAX);
	if (code == 0)
		return buf;
	return "unknown";
}
