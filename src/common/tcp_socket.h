#pragma once

#include <cstdint>
#include <string>

#include "net_common.h"

namespace ucs {

// Minimal blocking TCP client socket, shared by the Icecast PUT (sender) and
// GET (receiver) clients — neither needs anything beyond connect/send/recv.
class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    bool connectTo(const std::string& host, int port, int connect_timeout_ms = 5000);

    bool sendAll(const uint8_t* data, size_t len);
    bool sendAll(const std::string& s);

    // Returns bytes read (0 = graceful close), or -1 on error/timeout.
    int recvSome(uint8_t* buf, size_t len, int timeout_ms);

    void close();
    bool isOpen() const { return sock_ != kInvalidSocket; }
    const std::string& lastError() const { return last_error_; }

private:
    socket_t sock_ = kInvalidSocket;
    std::string last_error_;
};

} // namespace ucs
