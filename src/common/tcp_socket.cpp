#include "tcp_socket.h"

#include <cstring>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ucs {

namespace {
#if defined(MSG_NOSIGNAL)
// Report a peer that has dropped the connection as an EPIPE error from
// send() instead of killing the process with SIGPIPE, so callers' normal
// error/reconnect handling gets a chance to run.
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0; // Windows: no SIGPIPE to suppress
#endif
} // namespace

TcpSocket::~TcpSocket() {
    close();
}

bool TcpSocket::connectTo(const std::string& host, int port, int connect_timeout_ms) {
    close();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || !result) {
        last_error_ = "could not resolve host: " + host;
        return false;
    }

    sock_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock_ == kInvalidSocket) {
        last_error_ = LastSocketError();
        freeaddrinfo(result);
        return false;
    }

    // Non-blocking connect with an explicit timeout so a dead/unreachable
    // Icecast host doesn't hang the caller indefinitely.
    SetSocketNonBlocking(sock_, true);
    int rc = ::connect(sock_, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    bool connected = (rc == 0);
    if (!connected) {
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(sock_, &write_set);
        timeval tv{};
        tv.tv_sec = connect_timeout_ms / 1000;
        tv.tv_usec = (connect_timeout_ms % 1000) * 1000;

        int sel = select(static_cast<int>(sock_ + 1), nullptr, &write_set, nullptr, &tv);
        if (sel > 0) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(sock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);
            connected = (so_error == 0);
        }
    }

    SetSocketNonBlocking(sock_, false);

    if (!connected) {
        last_error_ = "connect timed out or failed: " + host;
        close();
        return false;
    }
    return true;
}

bool TcpSocket::sendAll(const uint8_t* data, size_t len) {
    if (sock_ == kInvalidSocket) return false;
    size_t sent = 0;
    while (sent < len) {
        auto n = ::send(sock_, reinterpret_cast<const char*>(data + sent),
                         static_cast<int>(len - sent), kSendFlags);
        if (n <= 0) {
            last_error_ = LastSocketError();
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool TcpSocket::sendAll(const std::string& s) {
    return sendAll(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

int TcpSocket::recvSome(uint8_t* buf, size_t len, int timeout_ms) {
    if (sock_ == kInvalidSocket) return -1;
    SetSocketReceiveTimeout(sock_, timeout_ms);

    auto n = ::recv(sock_, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
    if (n < 0) {
#if defined(_WIN32)
        if (WSAGetLastError() == WSAETIMEDOUT) return -1;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
#endif
        last_error_ = LastSocketError();
        return -1;
    }
    return static_cast<int>(n); // 0 = peer closed gracefully
}

void TcpSocket::close() {
    if (sock_ != kInvalidSocket) {
        CloseSocket(sock_);
        sock_ = kInvalidSocket;
    }
}

} // namespace ucs
