#pragma once

#include <cstdint>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // otherwise windows.h's min/max macros break std::min/std::max call sites
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace ucs {

// RAII wrapper around WSAStartup/WSACleanup. No-op on non-Windows.
// Construct one instance near the start of main().
class NetworkInit {
public:
    NetworkInit();
    ~NetworkInit();
    NetworkInit(const NetworkInit&) = delete;
    NetworkInit& operator=(const NetworkInit&) = delete;
};

void CloseSocket(socket_t s);
std::string LastSocketError();
bool SetSocketReceiveTimeout(socket_t s, int timeout_ms);
bool SetSocketNonBlocking(socket_t s, bool non_blocking);

} // namespace ucs
