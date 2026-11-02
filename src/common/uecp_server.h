#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#include "net_common.h"

namespace ucs {

// Listens on a TCP port for UECP packets (0xFE...0xFF framed). Reassembles
// complete packets and queues their bytes for the encode thread, which
// drains up to N bytes per encoded frame via popChunk() -- spreading each
// packet across successive frames until it's fully embedded, the same way
// mp2anc's original UECPServer worked. Accepts one client connection at a
// time, matching a typical single RDS-encoder-to-encapsulator link.
//
// Unlike mp2anc's version (a one-shot CLI process where a hang on shutdown
// would go unnoticed), stop() has to be genuinely responsive here: the GUI's
// Stop button calls it on the UI thread, and it must not block waiting on an
// accept()/recv() call that may never return on its own. See stop()'s
// comment for how that's made safe.
class UecpServer
{
public:
    explicit UecpServer(int port);
    ~UecpServer();
    UecpServer(const UecpServer&) = delete;
    UecpServer& operator=(const UecpServer&) = delete;

    void start();
    void stop();

    // Take up to max_bytes from the front of the pending byte queue.
    // Returns the number of bytes written into buf.
    size_t popChunk(uint8_t* buf, size_t max_bytes);

    // Return bytes taken by popChunk() that the encoder didn't actually embed
    // to the front of the queue, so they go out with the next frame instead
    // of being lost from the middle of a UECP packet.
    void unpopChunk(const uint8_t* buf, size_t n);

    bool clientConnected() const { return client_connected_.load(); }
    uint64_t packetsReceived() const { return packets_received_.load(); }

private:
    void run();

    int port_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    // Tracked so stop() can force-close whichever socket the run() thread
    // might currently be blocked on (accept() or recv()).
    std::atomic<socket_t> listen_fd_{kInvalidSocket};
    std::atomic<socket_t> client_fd_{kInvalidSocket};

    std::atomic<bool> client_connected_{false};
    std::atomic<uint64_t> packets_received_{0};

    std::mutex mutex_;
    std::deque<uint8_t> pending_;
};

} // namespace ucs
