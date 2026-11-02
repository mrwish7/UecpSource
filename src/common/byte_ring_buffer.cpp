#include "byte_ring_buffer.h"

#include <algorithm>
#include <chrono>

namespace ucs {

ByteRingBuffer::ByteRingBuffer(size_t capacity_bytes)
    : buffer_(capacity_bytes), capacity_(capacity_bytes) {}

void ByteRingBuffer::push(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (len >= capacity_) {
        // Pathological single push larger than the whole buffer: keep only
        // the tail end of it.
        data += (len - capacity_);
        len = capacity_;
        head_ = 0;
        size_ = 0;
    }

    // Drop the oldest bytes to make room, rather than blocking the caller.
    if (size_ + len > capacity_) {
        size_t drop = size_ + len - capacity_;
        head_ = (head_ + drop) % capacity_;
        size_ -= drop;
    }

    size_t tail = (head_ + size_) % capacity_;
    size_t first_chunk = std::min(len, capacity_ - tail);
    std::copy(data, data + first_chunk, buffer_.begin() + static_cast<ptrdiff_t>(tail));
    if (first_chunk < len) {
        std::copy(data + first_chunk, data + len, buffer_.begin());
    }
    size_ += len;

    cv_.notify_all();
}

size_t ByteRingBuffer::popBlocking(uint8_t* out, size_t want, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    cv_.wait_until(lock, deadline, [this, want] { return stop_ || size_ >= want; });

    if (stop_) return 0;

    size_t n = std::min(want, size_);
    size_t first_chunk = std::min(n, capacity_ - head_);
    std::copy(buffer_.begin() + static_cast<ptrdiff_t>(head_),
              buffer_.begin() + static_cast<ptrdiff_t>(head_ + first_chunk), out);
    if (first_chunk < n) {
        std::copy(buffer_.begin(), buffer_.begin() + static_cast<ptrdiff_t>(n - first_chunk),
                  out + first_chunk);
    }

    head_ = (head_ + n) % capacity_;
    size_ -= n;
    return n;
}

void ByteRingBuffer::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = 0;
    size_ = 0;
    stop_ = false;
}

void ByteRingBuffer::requestStop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
}

size_t ByteRingBuffer::availableBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

} // namespace ucs
