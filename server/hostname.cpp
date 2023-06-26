#include "hostname.h"
#include <string>
#include <unistd.h>

std::string hostname()
{
  char buf[256];
  int code = gethostname(buf, 256);
  if (code == 0)
    return std::string(buf);
  return "unknown";
}

