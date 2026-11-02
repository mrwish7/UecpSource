#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app_config.h"
#include "tcp_socket.h"

namespace ucs {

// Icecast SOURCE-method client: opens one persistent TCP connection, sends
// the (legacy but universally-supported) SOURCE request with the stream's
// real Content-Type, then streams encoded bytes as the (unbounded,
// connection-close-delimited) request body.
//
// Unlike MPXpack's IcecastSourceClient -- which deliberately uses PUT +
// Content-Type: application/octet-stream to keep Icecast's codec-sniffers
// away from its custom WavPack payload -- real MP2/AAC decoders need the
// genuine MIME type advertised, so this is a separate, format-aware
// implementation, built on the same TcpSocket.
class IcecastSourceClient {
public:
    bool connect(const IcecastConfig& cfg, const std::string& content_type);

    // Buffers internally and only actually writes to the socket once
    // cfg.buffer_kb KB have accumulated (0 disables this and sends
    // immediately), coalescing many small per-frame sends into fewer,
    // larger writes -- matches common practice (e.g. ffmpeg's default
    // Icecast output buffering).
    bool sendBlock(const uint8_t* data, size_t len);

    // Force out anything still buffered (e.g. at end of stream -- without
    // this, up to buffer_kb of trailing audio would sit unsent).
    bool flush();

    void close();
    bool isOpen() const { return socket_.isOpen(); }
    const std::string& lastError() const { return last_error_; }

private:
    TcpSocket socket_;
    std::string last_error_;

    std::vector<uint8_t> send_buffer_;
    size_t send_buffer_threshold_ = 0;
};

} // namespace ucs
