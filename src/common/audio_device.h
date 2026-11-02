#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ucs {

struct AudioDeviceImpl; // defined in audio_device.cpp; kept out of the class body
                        // (rather than a nested type) so the miniaudio data
                        // callback, a free function, can access it without
                        // needing friend/class-member access.

struct AudioDeviceInfo {
    std::string id; // opaque, only valid against the enumerate() call that produced it
    std::string name;         // human-friendly description — NOT guaranteed unique (see native_id)
    std::string native_id;    // e.g. the raw ALSA PCM name ("hw:CARD=Loopback,DEV=0") when
                              // available. On ALSA, `name` is often an identical generic
                              // description ("Loopback Analogue Stereo") across every hw:/
                              // plughw:/dmix: variant of the same substream — this is the
                              // only thing that actually distinguishes them. Empty on
                              // backends (WASAPI, etc.) that don't have this ambiguity.
    bool is_default = false;

    // What the GUI should actually show — folds in native_id when it adds
    // real information, so duplicate-named devices remain distinguishable.
    std::string displayLabel() const {
        if (native_id.empty() || native_id == name) return name;
        return name + "  [" + native_id + "]";
    }
};

enum class AudioDeviceKind { Capture, Playback };

// One entry from a device's native-format table. A value of 0 for
// sample_rate/channels means "device accepts any" per miniaudio's own
// ma_device_info convention.
struct NativeFormat {
    int sample_rate = 0;
    int channels = 0;
    int bits_per_sample = 0; // 0 = device accepts any sample format
};

// Wraps miniaudio for exactly what this project needs: enumerate devices,
// report whether a device natively supports a given format (informational
// only here -- unlike MPXpack/UecpBridge, this project does NOT require an
// exact native match; see audio_device.cpp's openCapture() comment), and run
// a capture or playback callback at the requested format.
class AudioDevice {
public:
    // Delivered on miniaudio's real-time audio thread — must never block.
    using CaptureCallback = std::function<void(const int16_t* pcm, size_t frame_count)>;
    // Must fill up to frame_count frames and return how many it actually
    // wrote; the remainder is zero-filled (silence) on underrun.
    using PlaybackCallback = std::function<size_t(int16_t* pcm, size_t frame_count)>;

    AudioDevice();
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    static std::vector<AudioDeviceInfo> enumerate(AudioDeviceKind kind);

    // True only if sample_rate is among the device's native formats — i.e.
    // opening it would need no implicit resampling. Deliberately does NOT
    // require an exact channel-count/bit-depth match: those are handled by
    // miniaudio's built-in format/channel converters, which just do bit-width
    // conversion and channel duplication/summing — no filtering involved.
    // Purely informational here (surfaced in the GUI as a badge) -- opening
    // a device that fails this check still works, just via the backend's own
    // resampler/converter rather than natively. `channels` is kept in the
    // signature for API clarity/future use, not currently used to reject a
    // device.
    static bool supportsFormatNatively(AudioDeviceKind kind, const std::string& device_id,
                                       int sample_rate, int channels);

    // Full native-format table for a device — for diagnosing *why* a device
    // fails supportsFormatNatively() (wrong rate configured, wrong channel
    // count, etc.), not needed for normal sender/receiver operation.
    static std::vector<NativeFormat> listNativeFormats(AudioDeviceKind kind, const std::string& device_id);

    bool openCapture(const std::string& device_id, int sample_rate, int channels, CaptureCallback cb);
    bool openPlayback(const std::string& device_id, int sample_rate, int channels, PlaybackCallback cb);

    bool start();
    void stop();
    void close();

    const std::string& lastError() const { return last_error_; }

private:
    AudioDeviceImpl* impl_ = nullptr;
    std::string last_error_;
};

} // namespace ucs
