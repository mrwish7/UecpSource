#include "source_app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace ucs {

namespace {
constexpr int kEncodeReadTimeoutMs = 200;
constexpr int kNetworkPopTimeoutMs = 200;
constexpr int kReconnectBackoffStartMs = 1000;
constexpr int kReconnectBackoffMaxMs = 30000;
constexpr double kCaptureRingSeconds = 1.0; // soundcard capture has no network jitter to
                                             // absorb like mp2anc's old libVLC path did --
                                             // this only needs to cover encode-thread/OS
                                             // scheduling jitter, not multi-second stalls.
} // namespace

SourceApp::SourceApp() = default;

SourceApp::~SourceApp() {
    stop();
}

std::vector<AudioDeviceInfo> SourceApp::listInputDevices() {
    return AudioDevice::enumerate(AudioDeviceKind::Capture);
}

bool SourceApp::inputSupportsFormatNatively(const std::string& device_id, int sample_rate, int channels) {
    return AudioDevice::supportsFormatNatively(AudioDeviceKind::Capture, device_id, sample_rate, channels);
}

void SourceApp::setState(const std::string& state, const std::string& error) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = state;
    if (!error.empty()) status_.last_error = error;
}

SourceApp::Status SourceApp::status() const {
    Status s;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        s = status_;
    }
    s.frames_encoded = frames_encoded_.load();
    if (uecp_) {
        s.uecp_client_connected = uecp_->clientConnected();
        s.uecp_packets_seen = uecp_->packetsReceived();
    }
    return s;
}

std::string SourceApp::contentType() const {
    if (cfg_.codec == Codec::MP2) return "audio/mpeg";
    // audio/aac is the conventional MIME type for a raw ADTS byte-stream.
    // There's no equally-conventional HTTP content type for LATM; this is
    // the identifier RFC 6416 registers for MP4A-LATM RTP payloads, at
    // least recognisable to anything transport-aware.
    return (cfg_.aac_transport == AacTransport::LATM) ? "audio/MP4A-LATM" : "audio/aac";
}

bool SourceApp::start(const SourceConfig& cfg) {
    if (running_.load()) return false;
    cfg_ = cfg;
    frames_encoded_ = 0;

    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = Status{};
        status_.state = "Connecting";
    }

    // Open the encoder first: AAC's real frameLength() (1024 for AAC-LC,
    // independent of sample rate) isn't known until after open(), and both
    // the capture ring and block queue need to be sized around whichever
    // encoder's frame duration is actually in play.
    mp2_encoder_.reset();
    aac_encoder_.reset();
    try {
        if (cfg_.codec == Codec::MP2) {
            mp2_encoder_ = std::make_unique<Mp2Encoder>(
                cfg_.sample_rate, cfg_.channels, cfg_.bitrate_kbps, cfg_.mp2_joint_stereo,
                cfg_.mp2_psy_model);
            mp2_encoder_->open(kMp2AncTotalBytes);
            frame_samples_ = Mp2Encoder::frameSamples();
        } else {
            aac_encoder_ = std::make_unique<AacEncoder>(
                cfg_.sample_rate, static_cast<unsigned>(cfg_.channels),
                cfg_.bitrate_kbps * 1000, cfg_.aac_aot, cfg_.aac_transport);
            aac_encoder_->open();
            frame_samples_ = aac_encoder_->frameLength();
        }
    } catch (const std::exception& e) {
        setState("Error", std::string("encoder: ") + e.what());
        mp2_encoder_.reset();
        aac_encoder_.reset();
        return false;
    }

    const size_t raw_bytes_per_sec = static_cast<size_t>(cfg_.sample_rate) *
                                      static_cast<size_t>(cfg_.channels) * sizeof(int16_t);
    capture_ring_ = std::make_unique<ByteRingBuffer>(std::max<size_t>(
        static_cast<size_t>(raw_bytes_per_sec * kCaptureRingSeconds), raw_bytes_per_sec / 10));

    // Both codecs run at a constant bitrate, so bytes map directly to duration.
    const size_t encoded_bytes_per_sec = static_cast<size_t>(cfg_.bitrate_kbps) * 1000 / 8;
    block_queue_ = std::make_unique<BlockQueue>(
        static_cast<size_t>(encoded_bytes_per_sec * kCaptureRingSeconds));

    uecp_ = std::make_unique<UecpServer>(cfg_.uecp_port);
    uecp_->start();

    bool capture_opened = audio_.openCapture(
        cfg_.input_device_id, cfg_.sample_rate, cfg_.channels,
        [this](const int16_t* pcm, size_t frame_count) {
            capture_ring_->push(reinterpret_cast<const uint8_t*>(pcm),
                                 frame_count * static_cast<size_t>(cfg_.channels) * sizeof(int16_t));
            updateLevel(pcm, frame_count * static_cast<size_t>(cfg_.channels));
        });
    if (!capture_opened) {
        setState("Error", "audio device: " + audio_.lastError());
        uecp_->stop();
        uecp_.reset();
        mp2_encoder_.reset();
        aac_encoder_.reset();
        return false;
    }

    if (!audio_.start()) {
        setState("Error", "audio device: " + audio_.lastError());
        audio_.close();
        uecp_->stop();
        uecp_.reset();
        mp2_encoder_.reset();
        aac_encoder_.reset();
        return false;
    }

    running_ = true;
    encode_thread_ = std::thread(&SourceApp::encodeThreadFunc, this);
    network_thread_ = std::thread(&SourceApp::networkThreadFunc, this);

    return true;
}

void SourceApp::stop() {
    if (!running_.load()) return;
    running_ = false;

    audio_.stop();
    audio_.close();

    if (capture_ring_) capture_ring_->requestStop();
    if (block_queue_) block_queue_->requestStop();

    if (encode_thread_.joinable()) encode_thread_.join();
    if (network_thread_.joinable()) network_thread_.join();

    if (uecp_) { uecp_->stop(); uecp_.reset(); }

    // No final flush() here -- BlockQueue::popBlocking() unconditionally
    // returns empty once requestStop() has been called, regardless of
    // what's still queued, so a flush pushed after that point could never
    // reach the network thread anyway (same as MPXpack's SenderApp::stop()).
    // Losing the last frame or two of audio (a few tens of ms) on Stop is an
    // accepted trade-off for a live stream, not something worth a more
    // complex drain-then-stop queue semantic.
    mp2_encoder_.reset();
    aac_encoder_.reset();

    setState("Idle");
}

void SourceApp::updateLevel(const int16_t* pcm, size_t n) {
    if (n == 0) return;
    double peak = 0.0, sum_sq = 0.0;
    for (size_t i = 0; i < n; i++) {
        double v = std::abs(static_cast<int>(pcm[i])) / 32768.0;
        peak = std::max(peak, v);
        sum_sq += v * v;
    }
    double rms = std::sqrt(sum_sq / static_cast<double>(n));

    std::lock_guard<std::mutex> lock(status_mutex_);
    // Simple decay so the meter doesn't look frozen between GUI polls.
    status_.input_peak = std::max(peak, status_.input_peak * 0.7);
    status_.input_rms = rms;
}

void SourceApp::encodeThreadFunc() {
    const size_t frame_bytes = static_cast<size_t>(frame_samples_) *
                                static_cast<size_t>(cfg_.channels) * sizeof(int16_t);
    std::vector<uint8_t> raw_chunk(frame_bytes);
    std::vector<uint8_t> encoded_buf(4096);
    short mp2_pcm[2][1152]; // only touched when cfg_.codec == Codec::MP2

    constexpr int kAncBufSize = kMp2AncPayloadBytes; // >= kAacAncPayloadBytes too
    uint8_t anc_chunk[kAncBufSize];
    const int anc_max = (cfg_.codec == Codec::MP2) ? kMp2AncPayloadBytes : kAacAncPayloadBytes;

    uint64_t underrun_count = 0;

    while (running_.load()) {
        size_t got = capture_ring_->popBlocking(raw_chunk.data(), frame_bytes, kEncodeReadTimeoutMs);
        if (got < frame_bytes) {
            std::memset(raw_chunk.data() + got, 0, frame_bytes - got);
            underrun_count++;
            std::fprintf(stderr, "Capture underrun #%llu: got %zu/%zu bytes\n",
                         static_cast<unsigned long long>(underrun_count), got, frame_bytes);
        }

        size_t anc_len = uecp_->popChunk(anc_chunk, static_cast<size_t>(anc_max));
        const int16_t* interleaved = reinterpret_cast<const int16_t*>(raw_chunk.data());

        int out_bytes;
        if (cfg_.codec == Codec::MP2) {
            if (cfg_.channels == 2) {
                for (int i = 0; i < frame_samples_; i++) {
                    mp2_pcm[0][i] = interleaved[2 * i];
                    mp2_pcm[1][i] = interleaved[2 * i + 1];
                }
            } else {
                for (int i = 0; i < frame_samples_; i++)
                    mp2_pcm[0][i] = interleaved[i];
            }
            out_bytes = mp2_encoder_->encodeFrame(mp2_pcm, anc_chunk, static_cast<int>(anc_len),
                                                   encoded_buf.data(), static_cast<int>(encoded_buf.size()));
        } else {
            out_bytes = aac_encoder_->encodeFrame(
                interleaved, frame_samples_ * cfg_.channels,
                anc_chunk, static_cast<int>(anc_len),
                encoded_buf.data(), static_cast<int>(encoded_buf.size()));
        }

        if (out_bytes < 0) {
            // Matches mp2anc's original behavior: log and keep going --
            // an isolated bad frame isn't worth tearing down a live stream.
            std::fprintf(stderr, "Encoder error on frame %llu\n",
                         static_cast<unsigned long long>(frames_encoded_.load()));
            continue;
        }
        // Counted per input frame, not per output block: libtoolame-dab only
        // hands back output every ~4 KB (several MP2 frames), and fdk-aac
        // returns nothing for its first few frames.
        frames_encoded_.fetch_add(1, std::memory_order_relaxed);
        if (out_bytes == 0) continue; // encoder still buffering internally

        block_queue_->push(std::vector<uint8_t>(encoded_buf.begin(), encoded_buf.begin() + out_bytes));
    }
}

void SourceApp::networkThreadFunc() {
    IcecastSourceClient client;
    int backoff_ms = kReconnectBackoffStartMs;
    const std::string content_type = contentType();

    auto window_start = std::chrono::steady_clock::now();
    uint64_t window_bytes = 0;

    while (running_.load()) {
        if (!client.isOpen()) {
            setState(backoff_ms == kReconnectBackoffStartMs ? "Connecting" : "Reconnecting");
            if (!client.connect(cfg_.icecast, content_type)) {
                setState("Reconnecting", "Icecast: " + client.lastError());
                for (int waited = 0; waited < backoff_ms && running_.load(); waited += 100)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                backoff_ms = std::min(backoff_ms * 2, kReconnectBackoffMaxMs);
                continue;
            }
            backoff_ms = kReconnectBackoffStartMs;
            setState("Streaming");
        }

        auto block = block_queue_->popBlocking(kNetworkPopTimeoutMs);
        if (block.empty()) continue;

        if (!client.sendBlock(block.data(), block.size())) {
            setState("Reconnecting", "Icecast: " + client.lastError());
            client.close();
            continue;
        }
        window_bytes += block.size();

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - window_start).count();
        if (elapsed >= 1.0) {
            std::lock_guard<std::mutex> lock(status_mutex_);
            status_.outgoing_kbps = window_bytes * 8.0 / 1000.0 / elapsed;
            // kbps == bits per millisecond.
            status_.queue_depth_ms = static_cast<int>(block_queue_->bytes() * 8 / cfg_.bitrate_kbps);
            window_bytes = 0;
            window_start = now;
        }
    }

    client.close();
}

} // namespace ucs
