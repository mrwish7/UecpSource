#include "net_common.h"

#include <cstring>

#if defined(_WIN32)
#pragma comment(lib, "ws2_32.lib")
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace ucs {

NetworkInit::NetworkInit() {
#if defined(_WIN32)
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
}

NetworkInit::~NetworkInit() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

void CloseSocket(socket_t s) {
    if (s == kInvalidSocket) return;
#if defined(_WIN32)
    closesocket(s);
#else
    close(s);
#endif
}

std::string LastSocketError() {
#if defined(_WIN32)
    int err = WSAGetLastError();
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    return std::string(buf) + " (" + std::to_string(err) + ")";
#else
    return std::string(strerror(errno)) + " (" + std::to_string(errno) + ")";
#endif
}

bool SetSocketReceiveTimeout(socket_t s, int timeout_ms) {
#if defined(_WIN32)
    DWORD timeout = static_cast<DWORD>(timeout_ms);
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
#else
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool SetSocketNonBlocking(socket_t s, bool non_blocking) {
#if defined(_WIN32)
    u_long mode = non_blocking ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    flags = non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(s, F_SETFL, flags) == 0;
#endif
}

} // namespace ucs
