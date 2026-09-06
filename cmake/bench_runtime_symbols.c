/*
 * Symbols that patches/monado/0002 and 0008 move out of Monado and into wivrn-server.
 * The benchmark's client runtime is built from that same patched source without wivrn-server,
 * so it has to supply them. Values matter only to the IPC handshake, which compares the service
 * against the client library from this same build.
 */

#include <stdint.h>

int listen_socket = -1;

const char u_git_tag[] = "wivrn-bench-runtime";
const char u_runtime_description[] = "Monado";
const uint16_t u_version_major = 0;
const uint16_t u_version_minor = 0;
const uint16_t u_version_patch = 0;
