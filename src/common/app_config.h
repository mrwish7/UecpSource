#pragma once

#include <string>

namespace ucs {

// Audio format encoded and streamed. See mp2_encoder.h / aac_encoder.h.
enum class Codec { MP2, AAC };

// AAC access-unit framing. See aac_encoder.h's AACTransport for the full
// rationale (raw LATM has no sync layer and is unusable over a plain byte
// stream; LOAS is what "LATM over Icecast" actually means in practice).
enum class AacTransport { ADTS, LATM };

// MPEG-4 Audio Object Type, for the handful AAC decoders in the wild
// actually encounter: AAC-LC covers the common case, HE-AAC/HE-AACv2 trade
// audio bandwidth for bitrate at low rates via SBR/PS.
enum class AacObjectType {
    AacLc = 2,
    HeAac = 5,
    HeAacV2 = 29,
};

// Ancillary-data payload limits per frame, matching real-world UECP-over-DSE/
// MP2-ancillary streams (see mp2_encoder.h / aac_encoder.h for the wire
// formats these bound).
constexpr int kMp2AncPayloadBytes = 8;  // + 2 bytes of length/marker framing
constexpr int kMp2AncTotalBytes = kMp2AncPayloadBytes + 2;
constexpr int kAacAncPayloadBytes = 4;  // raw, no extra framing (DSE's own count field)

constexpr int kDefaultUecpPort = 9000;
constexpr int kDefaultIcecastBufferKb = 16;

constexpr int kMinBitrateKbps = 32;
constexpr int kMaxBitrateKbps = 384;
constexpr int kDefaultBitrateKbps = 192;

// libtoolame-dab psychoacoustic model (toolame_set_psy_model() accepts 0..3).
// Model 2 (ISO "AT&T" FFT-based model) gives noticeably better quality than
// the library's own default of model 1, at a modest CPU cost.
constexpr int kMinMp2PsyModel = 0;
constexpr int kMaxMp2PsyModel = 3;
constexpr int kDefaultMp2PsyModel = 2;

struct IcecastConfig {
    std::string host;
    int port = 8000;
    std::string mount;       // e.g. "/stream.aac"
    std::string password;
    std::string user = "source";
    std::string name = "UecpSource";
    int buffer_kb = kDefaultIcecastBufferKb; // 0 disables write-coalescing
};

struct SourceConfig {
    std::string input_device_id;
    int sample_rate = 48000;
    int channels = 2;

    Codec codec = Codec::AAC;

    int bitrate_kbps = kDefaultBitrateKbps;

    // MP2-specific: joint stereo (true) vs plain stereo (false). Meaningless
    // (forced mono) when channels == 1.
    bool mp2_joint_stereo = true;
    int mp2_psy_model = kDefaultMp2PsyModel;

    // AAC-specific.
    AacObjectType aac_aot = AacObjectType::AacLc;
    AacTransport aac_transport = AacTransport::LATM;

    IcecastConfig icecast;
    int uecp_port = kDefaultUecpPort;
};

} // namespace ucs
