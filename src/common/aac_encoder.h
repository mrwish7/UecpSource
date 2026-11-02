#pragma once

#include <cstdint>

#include "app_config.h"

namespace ucs {

// Thin wrapper around libfdk-aac's aacEncEncode() API. Ported from mp2anc's
// AACEncoder with no functional changes.
//
// Ancillary data (used to carry UECP bytes) is passed straight through as a
// raw AAC data_stream_element() (DSE): libfdk-aac wraps whatever buffer is
// supplied via IN_ANCILLRY_DATA/numAncBytes in a DSE alongside the frame's
// audio payload. No extra length/marker framing is added on top -- the DSE's
// own `count` field already tells a bitstream-aware decoder how many bytes
// are in this frame's chunk, unlike the raw trailing ancillary bytes used in
// the MP2 path (which have no such built-in framing and need one).
//
// AacTransport (app_config.h) selects how each access unit is framed:
//   ADTS -- self-contained per-frame header. What generic tools (ffmpeg,
//           VLC, browsers) expect from a raw "audio/aac" byte-stream.
//   LATM -- LOAS-framed Audio Mux Elements (an 11-bit sync word + 13-bit
//           length prefix ahead of each element), which is what a decoder
//           needs to find frame boundaries in a continuous, unframed byte
//           stream like an Icecast mount. This is what real-world
//           UECP-over-DSE broadcast streams actually mean by "LATM" when
//           delivered this way.
//
//           Note: fdk-aac's raw TT_MP4_LATM_MCP1/MCP0 transport types are
//           deliberately NOT used here -- FDK_audio.h's own TT_IS_PACKET()
//           macro classifies them as packet-based, "obviously no sync
//           layer", meant for a transport (RTP, a custom packetized API)
//           that already delineates access units externally. Dumped into a
//           raw byte-stream they give a decoder nothing to synchronize on
//           at all. LOAS is used instead.
class AacEncoder
{
public:
    // sampleRate in Hz, channels 1 or 2, bitrateBps in bits/second.
    AacEncoder(int sampleRate, unsigned channels, int bitrateBps,
               AacObjectType aot = AacObjectType::AacLc,
               AacTransport transport = AacTransport::LATM);

    AacEncoder(const AacEncoder&) = delete;
    AacEncoder& operator=(const AacEncoder&) = delete;
    ~AacEncoder();

    // Opens and configures the encoder (selected transport, afterburner on).
    // Throws std::runtime_error on failure.
    void open();

    // PCM samples expected per channel for each encodeFrame() call.
    int frameLength() const { return frame_length_; }

    // Encode one frame of interleaved 16-bit PCM. numSamples is the total
    // sample count (frameLength()*channels). ancData/ancLen (0..N bytes) are
    // embedded as a raw DSE alongside the audio payload; pass ancLen=0 to
    // encode a frame with no ancillary data.
    //
    // libfdk-aac doesn't always embed what it's given: a frame's ancillary
    // data is dropped whole if it exceeds the encoder's per-frame limit
    // (roughly the frame's bit budget less 8 kbps per channel), and isn't
    // consumed on a call that doesn't reach the encode step. If
    // ancBytesWritten is non-null it receives how many of the ancLen bytes
    // were actually embedded (0 on error) -- the rest are the caller's to
    // resend.
    //
    // Returns the number of bitstream bytes written to outBuf (may be 0 --
    // the encoder buffers internally before the first frame is emitted), or
    // -1 on error.
    int encodeFrame(const int16_t* pcm, int numSamples,
                     const uint8_t* ancData, int ancLen,
                     uint8_t* outBuf, int outBufSize,
                     int* ancBytesWritten = nullptr);

    // Drain remaining buffered frames at end of stream. Call repeatedly;
    // each call may produce 0 or more output bytes. Returns -1 once the
    // encoder has nothing left to flush (AACENC_ENCODE_EOF), 0 or more
    // while flushing continues, -2 on a hard error.
    int flush(uint8_t* outBuf, int outBufSize);

private:
    int          sample_rate_;
    unsigned     channels_;
    int          bitrate_bps_;
    AacObjectType aot_;
    AacTransport transport_;
    int          frame_length_ = 0;
    void*        handle_ = nullptr; // HANDLE_AACENCODER, kept opaque here
};

} // namespace ucs
