#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace ucs {

// Bounded, thread-safe byte queue used as the handoff between the real-time
// audio capture callback (producer) and the encode thread (consumer, via
// popBlocking). On overflow, push() drops the OLDEST bytes already queued
// rather than blocking the producer or growing unboundedly -- this bounds
// live latency at the cost of an audible glitch under a sustained encoder
// stall, rather than an ever-growing backlog.
class ByteRingBuffer {
public:
    explicit ByteRingBuffer(size_t capacity_bytes);

    void push(const uint8_t* data, size_t len);

    // Blocks until `want` bytes are available, stop() is called, or the
    // timeout elapses. Returns the number of bytes actually written to `out`
    // (may be less than `want` on timeout/stop).
    size_t popBlocking(uint8_t* out, size_t want, int timeout_ms);

    // Drops all queued bytes and clears the stop flag. Call when
    // (re)opening a decoder against a fresh connection.
    void reset();

    // Makes any in-progress or future popBlocking() calls return immediately.
    void requestStop();

    size_t availableBytes() const;
    size_t capacityBytes() const { return capacity_; }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<uint8_t> buffer_;
    size_t head_ = 0; // next byte to read
    size_t size_ = 0; // bytes currently queued
    size_t capacity_;
    bool stop_ = false;
};

} // namespace ucs
