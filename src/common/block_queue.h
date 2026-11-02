#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace ucs {

// Bounded queue of discrete byte blocks (as opposed to ByteRingBuffer's flat
// stream) — used for the encoder->network handoff, where each encoded MP2/AAC
// frame needs to reach IcecastSourceClient as one intact block, not an
// arbitrary byte stream. Same drop-oldest-on-overflow bounded-latency policy
// as ByteRingBuffer: bounds live latency at the cost of a glitch under a
// sustained network stall, rather than an ever-growing backlog.
//
// Bounded by total queued bytes rather than block count: blocks aren't a
// fixed duration (libtoolame-dab hands back ~4 KB chunks spanning several
// MP2 frames, fdk-aac one access unit per block), whereas at a constant
// bitrate bytes are.
class BlockQueue {
public:
    // The newest block is always kept, even if it alone exceeds max_bytes.
    explicit BlockQueue(size_t max_bytes) : max_bytes_(max_bytes) {}

    void push(std::vector<uint8_t> block) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bytes_ += block.size();
            queue_.push_back(std::move(block));
            while (queue_.size() > 1 && bytes_ > max_bytes_) {
                bytes_ -= queue_.front().size();
                queue_.pop_front();
            }
        }
        cv_.notify_all();
    }

    // Returns an empty vector on timeout or stop.
    std::vector<uint8_t> popBlocking(int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        cv_.wait_until(lock, deadline, [this] { return stop_ || !queue_.empty(); });
        if (stop_ || queue_.empty()) return {};
        std::vector<uint8_t> block = std::move(queue_.front());
        queue_.pop_front();
        bytes_ -= block.size();
        return block;
    }

    void requestStop() {
        { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
        cv_.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        bytes_ = 0;
        stop_ = false;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // Total payload bytes currently queued.
    size_t bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::vector<uint8_t>> queue_;
    size_t max_bytes_;
    size_t bytes_ = 0;
    bool stop_ = false;
};

} // namespace ucs
