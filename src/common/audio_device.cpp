#include "audio_device.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cstring>
#include <vector>

namespace ucs {

namespace {
// Cached ma_device_id list from the most recent enumerate() call for each
// kind — AudioDeviceInfo::id is just a stringified index into this cache
// (see audio_device.h: "opaque, only valid against the enumerate() call
// that produced it").
std::vector<ma_device_id> g_capture_ids;
std::vector<ma_device_id> g_playback_ids;

// By default, miniaudio's ALSA backend collapses every "hw:"/"plughw:"/
// "dmix:"/"dsnoop:" alias for a given card/device pair into a single
// enumerated entry and silently picks one internally at open time. That's
// fine for a normal soundcard, but it means the caller has no way to
// specifically pick, say, "hw:CARD=Loopback,DEV=0" (direct hardware
// access, no ALSA-side conversion) versus "plughw:CARD=Loopback,DEV=0"
// (goes through ALSA's software "plug" layer, which can silently resample/
// convert). Setting useVerboseDeviceEnumeration makes ALSA enumerate every
// registered PCM name separately instead, so both variants show up as
// distinct, individually selectable devices. This is a real, documented
// miniaudio option — not a workaround — and it's a complete no-op on
// non-ALSA backends (WASAPI, CoreAudio, etc.), so it's always safe to set.
ma_context_config MakeContextConfig() {
    ma_context_config cfg = ma_context_config_init();
    cfg.alsa.useVerboseDeviceEnumeration = MA_TRUE;
    return cfg;
}

// Unlike MPXpack/UecpBridge (which force ma_backend_alsa specifically, no
// fallback, because MPX's 192kHz composite signal needs guaranteed exact
// native hardware access with zero silent resampling), this project has no
// such requirement -- see the "no forced native-format matching" note in
// openCapture()/openPlayback() below. So: use miniaudio's normal default
// backend priority (PulseAudio/PipeWire first on most desktop Linux, ALSA
// fallback; WASAPI on Windows), which is what a general "pick my soundcard"
// tool's users actually expect -- PulseAudio-routed virtual sinks/monitors
// included, not just raw ALSA hw: devices.
ma_result InitContext(ma_context* context) {
    ma_context_config cfg = MakeContextConfig();
    return ma_context_init(nullptr, 0, &cfg, context);
}
} // namespace

struct AudioDeviceImpl {
    ma_context context{};
    ma_device device{};
    bool context_initialized = false;
    bool device_initialized = false;
    int channels = 0;
    AudioDevice::CaptureCallback capture_cb;
    AudioDevice::PlaybackCallback playback_cb;
};

static void DataCallback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
    auto* self = static_cast<AudioDeviceImpl*>(device->pUserData);

    if (input && self->capture_cb) {
        self->capture_cb(static_cast<const int16_t*>(input), frame_count);
    }

    if (output && self->playback_cb) {
        size_t written = self->playback_cb(static_cast<int16_t*>(output), frame_count);
        if (written < frame_count) {
            size_t remaining_samples = (frame_count - written) * static_cast<size_t>(self->channels);
            std::memset(static_cast<int16_t*>(output) + written * self->channels, 0,
                        remaining_samples * sizeof(int16_t));
        }
    }
}

AudioDevice::AudioDevice() : impl_(new AudioDeviceImpl()) {}

AudioDevice::~AudioDevice() {
    close();
    delete impl_;
}

std::vector<AudioDeviceInfo> AudioDevice::enumerate(AudioDeviceKind kind) {
    std::vector<AudioDeviceInfo> result;

    ma_context context;
    if (InitContext(&context) != MA_SUCCESS) {
        return result;
    }

    ma_device_info* playback_infos = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_infos = nullptr;
    ma_uint32 capture_count = 0;

    if (ma_context_get_devices(&context, &playback_infos, &playback_count,
                                &capture_infos, &capture_count) == MA_SUCCESS) {
        auto& cache = (kind == AudioDeviceKind::Capture) ? g_capture_ids : g_playback_ids;
        cache.clear();

        ma_device_info* infos = (kind == AudioDeviceKind::Capture) ? capture_infos : playback_infos;
        ma_uint32 count = (kind == AudioDeviceKind::Capture) ? capture_count : playback_count;

        // ma_device_id is a UNION: unlike MPXpack/UecpBridge (which force
        // ma_backend_alsa, so .alsa is unconditionally the populated member
        // on Linux), this project lets miniaudio pick its normal default
        // backend (see InitContext()), which on Linux is commonly PulseAudio/
        // PipeWire, not ALSA. Reading .alsa when some other backend actually
        // wrote the ID would reinterpret those bytes as a narrow-char PCM
        // name -- garbage, not an empty string -- so which member is valid
        // has to be a genuine runtime check against the context's *actual*
        // resolved backend, not just "are we compiled for Linux".
        bool alsa_active = (context.backend == ma_backend_alsa);
        for (ma_uint32 i = 0; i < count; ++i) {
            AudioDeviceInfo info;
            info.id = std::to_string(cache.size());
            info.name = infos[i].name;
            if (alsa_active) {
                // Plain char[256] PCM name, e.g. "hw:CARD=Loopback,DEV=0".
                info.native_id = infos[i].id.alsa;
            }
            info.is_default = infos[i].isDefault != 0;
            cache.push_back(infos[i].id);
            result.push_back(std::move(info));
        }
    }

    ma_context_uninit(&context);
    return result;
}

bool AudioDevice::supportsFormatNatively(AudioDeviceKind kind, const std::string& device_id,
                                          int sample_rate, int /*channels*/) {
    auto& cache = (kind == AudioDeviceKind::Capture) ? g_capture_ids : g_playback_ids;
    int idx = -1;
    try {
        idx = std::stoi(device_id);
    } catch (...) {
        return false;
    }
    if (idx < 0 || static_cast<size_t>(idx) >= cache.size()) return false;

    ma_context context;
    if (InitContext(&context) != MA_SUCCESS) return false;

    ma_device_info detailed{};
    ma_device_type dtype = (kind == AudioDeviceKind::Capture) ? ma_device_type_capture : ma_device_type_playback;
    ma_result r = ma_context_get_device_info(&context, dtype, &cache[idx], &detailed);
    ma_context_uninit(&context);
    if (r != MA_SUCCESS) return false;

    // Only sample rate is checked — see the header doc comment for why
    // channel count and bit depth are deliberately not required to match.
    for (ma_uint32 i = 0; i < detailed.nativeDataFormatCount; ++i) {
        const auto& f = detailed.nativeDataFormats[i];
        if (f.sampleRate == static_cast<ma_uint32>(sample_rate) || f.sampleRate == 0) return true;
    }
    return false;
}

std::vector<NativeFormat> AudioDevice::listNativeFormats(AudioDeviceKind kind, const std::string& device_id) {
    std::vector<NativeFormat> result;

    auto& cache = (kind == AudioDeviceKind::Capture) ? g_capture_ids : g_playback_ids;
    int idx = -1;
    try {
        idx = std::stoi(device_id);
    } catch (...) {
        return result;
    }
    if (idx < 0 || static_cast<size_t>(idx) >= cache.size()) return result;

    ma_context context;
    if (InitContext(&context) != MA_SUCCESS) return result;

    ma_device_info detailed{};
    ma_device_type dtype = (kind == AudioDeviceKind::Capture) ? ma_device_type_capture : ma_device_type_playback;
    ma_result r = ma_context_get_device_info(&context, dtype, &cache[idx], &detailed);
    ma_context_uninit(&context);
    if (r != MA_SUCCESS) return result;

    for (ma_uint32 i = 0; i < detailed.nativeDataFormatCount; ++i) {
        const auto& f = detailed.nativeDataFormats[i];
        NativeFormat nf;
        nf.sample_rate = static_cast<int>(f.sampleRate);
        nf.channels = static_cast<int>(f.channels);
        nf.bits_per_sample = (f.format == ma_format_unknown) ? 0 : static_cast<int>(ma_get_bytes_per_sample(f.format) * 8);
        result.push_back(nf);
    }
    return result;
}

bool AudioDevice::openCapture(const std::string& device_id, int sample_rate, int channels, CaptureCallback cb) {
    close();

    int idx = -1;
    try { idx = std::stoi(device_id); } catch (...) {}
    if (idx < 0 || static_cast<size_t>(idx) >= g_capture_ids.size()) {
        last_error_ = "invalid capture device id";
        return false;
    }

    if (InitContext(&impl_->context) != MA_SUCCESS) {
        last_error_ = "ma_context_init failed";
        return false;
    }
    impl_->context_initialized = true;

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.pDeviceID = &g_capture_ids[idx];
    config.capture.format = ma_format_s16;
    config.capture.channels = static_cast<ma_uint32>(channels);
    config.capture.shareMode = ma_share_mode_shared;
    config.sampleRate = static_cast<ma_uint32>(sample_rate);
    config.dataCallback = &DataCallback;
    config.pUserData = impl_;
    // Unlike MPXpack/UecpBridge, deliberately leave backend-level auto-
    // resampling/conversion enabled (no noAutoResample/noAutoConvertSRC
    // here): regular program audio isn't filter-sensitive the way MPX's
    // composite signal is, so letting WASAPI shared mode or ALSA's plug/
    // dmix/pulse layer silently convert an unsupported rate/channel count
    // is the right default for a general "pick my soundcard" capture tool
    // -- it means far more devices just work without the user needing to
    // find one that natively matches the selected encode format exactly.

    if (ma_device_init(&impl_->context, &config, &impl_->device) != MA_SUCCESS) {
        last_error_ = "ma_device_init failed";
        ma_context_uninit(&impl_->context);
        impl_->context_initialized = false;
        return false;
    }

    impl_->device_initialized = true;
    impl_->channels = channels;
    impl_->capture_cb = std::move(cb);
    return true;
}

bool AudioDevice::openPlayback(const std::string& device_id, int sample_rate, int channels, PlaybackCallback cb) {
    close();

    int idx = -1;
    try { idx = std::stoi(device_id); } catch (...) {}
    if (idx < 0 || static_cast<size_t>(idx) >= g_playback_ids.size()) {
        last_error_ = "invalid playback device id";
        return false;
    }

    if (InitContext(&impl_->context) != MA_SUCCESS) {
        last_error_ = "ma_context_init failed";
        return false;
    }
    impl_->context_initialized = true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.pDeviceID = &g_playback_ids[idx];
    config.playback.format = ma_format_s16;
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.playback.shareMode = ma_share_mode_shared;
    config.sampleRate = static_cast<ma_uint32>(sample_rate);
    config.dataCallback = &DataCallback;
    config.pUserData = impl_;
    // See openCapture()'s comment -- auto-resampling/conversion deliberately
    // left enabled here too.

    if (ma_device_init(&impl_->context, &config, &impl_->device) != MA_SUCCESS) {
        last_error_ = "ma_device_init failed";
        ma_context_uninit(&impl_->context);
        impl_->context_initialized = false;
        return false;
    }

    impl_->device_initialized = true;
    impl_->channels = channels;
    impl_->playback_cb = std::move(cb);
    return true;
}

bool AudioDevice::start() {
    if (!impl_->device_initialized) {
        last_error_ = "device not opened";
        return false;
    }
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        last_error_ = "ma_device_start failed";
        return false;
    }
    return true;
}

void AudioDevice::stop() {
    if (impl_->device_initialized) {
        ma_device_stop(&impl_->device);
    }
}

void AudioDevice::close() {
    if (impl_->device_initialized) {
        ma_device_uninit(&impl_->device);
        impl_->device_initialized = false;
    }
    if (impl_->context_initialized) {
        ma_context_uninit(&impl_->context);
        impl_->context_initialized = false;
    }
    impl_->capture_cb = nullptr;
    impl_->playback_cb = nullptr;
}

} // namespace ucs
