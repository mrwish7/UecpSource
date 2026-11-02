#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aac_encoder.h"
#include "app_config.h"
#include "audio_device.h"
#include "block_queue.h"
#include "byte_ring_buffer.h"
#include "icecast_source_client.h"
#include "mp2_encoder.h"
#include "uecp_server.h"

namespace ucs {

// Orchestrates the whole pipeline: miniaudio capture -> encode thread ->
// network thread, mirroring MPXpack's SenderApp shape almost exactly (see
// MPXpack/src/sender/sender_app.h/.cpp) -- swap WavPack for MP2/AAC and add
// the UecpServer, and it's the same architecture. A UecpServer runs
// independently alongside the pipeline, feeding pending UECP bytes to the
// encode thread each frame.
//
// Threading/lifetime invariant (same as BridgeApp/SenderApp): start(),
// stop(), and status() are only ever called from one thread (the GUI
// thread) -- never concurrently with each other. That's what makes it safe
// for status() to read uecp_/encoder members without its own locking; only
// the fields touched by the real-time capture callback and the encode/
// network threads (state, last_error, level meter) go through status_mutex_.
class SourceApp {
public:
    struct Status {
        std::string state = "Idle"; // Idle, Connecting, Streaming, Reconnecting, Error
        std::string last_error;
        double input_peak = 0.0;   // 0..1
        double input_rms = 0.0;    // 0..1
        double outgoing_kbps = 0.0;
        int queue_depth_ms = 0;
        uint64_t frames_encoded = 0;
        bool uecp_client_connected = false;
        uint64_t uecp_packets_seen = 0;
    };

    SourceApp();
    ~SourceApp();

    static std::vector<AudioDeviceInfo> listInputDevices();
    static bool inputSupportsFormatNatively(const std::string& device_id, int sample_rate, int channels);

    bool start(const SourceConfig& cfg);
    void stop();
    bool isRunning() const { return running_.load(); }

    Status status() const;

private:
    void encodeThreadFunc();
    void networkThreadFunc();
    void updateLevel(const int16_t* pcm, size_t n);
    void setState(const std::string& state, const std::string& error = "");
    std::string contentType() const;

    SourceConfig cfg_;
    AudioDevice audio_;
    std::unique_ptr<UecpServer> uecp_;

    std::unique_ptr<Mp2Encoder> mp2_encoder_;
    std::unique_ptr<AacEncoder> aac_encoder_;
    int frame_samples_ = 0; // samples/channel per encode call; known once the encoder is open

    std::unique_ptr<ByteRingBuffer> capture_ring_;
    std::unique_ptr<BlockQueue> block_queue_;

    std::atomic<bool> running_{false};
    std::thread encode_thread_;
    std::thread network_thread_;

    mutable std::mutex status_mutex_;
    Status status_;

    std::atomic<uint64_t> frames_encoded_{0};
};

} // namespace ucs
