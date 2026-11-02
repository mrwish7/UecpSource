#include "aac_encoder.h"

#include <stdexcept>

#include "aacenc_lib.h"

namespace ucs {

namespace {
HANDLE_AACENCODER Handle(void* h) { return static_cast<HANDLE_AACENCODER>(h); }
} // namespace

AacEncoder::AacEncoder(int sampleRate, unsigned channels, int bitrateBps,
                       AacObjectType aot, AacTransport transport) :
    sample_rate_(sampleRate), channels_(channels), bitrate_bps_(bitrateBps),
    aot_(aot), transport_(transport) {}

AacEncoder::~AacEncoder() {
    if (handle_) {
        HANDLE_AACENCODER h = Handle(handle_);
        aacEncClose(&h);
    }
}

void AacEncoder::open() {
    if (channels_ != 1 && channels_ != 2)
        throw std::runtime_error("AacEncoder: only 1 or 2 channels supported");

    HANDLE_AACENCODER h = nullptr;
    if (aacEncOpen(&h, 0, channels_) != AACENC_OK)
        throw std::runtime_error("aacEncOpen failed");
    handle_ = h;

    CHANNEL_MODE mode = (channels_ == 1) ? MODE_1 : MODE_2;

    if (aacEncoder_SetParam(h, AACENC_AOT, static_cast<int>(aot_)) != AACENC_OK)
        throw std::runtime_error("aacEncoder_SetParam(AOT) failed");
    if (aacEncoder_SetParam(h, AACENC_SAMPLERATE, sample_rate_) != AACENC_OK)
        throw std::runtime_error("aacEncoder_SetParam(SAMPLERATE) failed");
    if (aacEncoder_SetParam(h, AACENC_CHANNELMODE, mode) != AACENC_OK)
        throw std::runtime_error("aacEncoder_SetParam(CHANNELMODE) failed");
    if (aacEncoder_SetParam(h, AACENC_BITRATE, bitrate_bps_) != AACENC_OK)
        throw std::runtime_error("aacEncoder_SetParam(BITRATE) failed");

    // LOAS, not raw LATM -- see this class's header comment.
    TRANSPORT_TYPE tt = (transport_ == AacTransport::LATM) ? TT_MP4_LOAS : TT_MP4_ADTS;
    if (aacEncoder_SetParam(h, AACENC_TRANSMUX, tt) != AACENC_OK)
        throw std::runtime_error("aacEncoder_SetParam(TRANSMUX) failed");
    if (aacEncoder_SetParam(h, AACENC_AFTERBURNER, 1) != AACENC_OK)
        throw std::runtime_error("aacEncoder_SetParam(AFTERBURNER) failed");

    // NULL/NULL/NULL/NULL triggers the deferred (re)initialization.
    if (aacEncEncode(h, NULL, NULL, NULL, NULL) != AACENC_OK)
        throw std::runtime_error("aacEncEncode (init) failed");

    AACENC_InfoStruct info{};
    if (aacEncInfo(h, &info) != AACENC_OK)
        throw std::runtime_error("aacEncInfo failed");

    frame_length_ = static_cast<int>(info.frameLength);
}

int AacEncoder::encodeFrame(const int16_t* pcm, int numSamples,
                             const uint8_t* ancData, int ancLen,
                             uint8_t* outBuf, int outBufSize) {
    void* inBufs[2];
    INT   inIds[2];
    INT   inSizes[2];
    INT   inElSizes[2];
    int   nInBufs = 0;

    inBufs[nInBufs]    = const_cast<int16_t*>(pcm);
    inIds[nInBufs]     = IN_AUDIO_DATA;
    inSizes[nInBufs]   = numSamples * static_cast<int>(sizeof(int16_t));
    inElSizes[nInBufs] = sizeof(int16_t);
    nInBufs++;

    if (ancLen > 0) {
        inBufs[nInBufs]    = const_cast<uint8_t*>(ancData);
        inIds[nInBufs]     = IN_ANCILLRY_DATA;
        inSizes[nInBufs]   = ancLen;
        inElSizes[nInBufs] = 1;
        nInBufs++;
    }

    AACENC_BufDesc inBufDesc{};
    inBufDesc.numBufs           = nInBufs;
    inBufDesc.bufs              = inBufs;
    inBufDesc.bufferIdentifiers = inIds;
    inBufDesc.bufSizes          = inSizes;
    inBufDesc.bufElSizes        = inElSizes;

    void* outPtr = outBuf;
    INT   outSize = outBufSize;
    INT   outElSize = 1;
    INT   outId = OUT_BITSTREAM_DATA;

    AACENC_BufDesc outBufDesc{};
    outBufDesc.numBufs           = 1;
    outBufDesc.bufs              = &outPtr;
    outBufDesc.bufferIdentifiers = &outId;
    outBufDesc.bufSizes          = &outSize;
    outBufDesc.bufElSizes        = &outElSize;

    AACENC_InArgs inArgs{};
    inArgs.numInSamples = numSamples;
    inArgs.numAncBytes  = ancLen;

    AACENC_OutArgs outArgs{};

    AACENC_ERROR err = aacEncEncode(Handle(handle_), &inBufDesc, &outBufDesc, &inArgs, &outArgs);
    if (err != AACENC_OK) return -1;

    return static_cast<int>(outArgs.numOutBytes);
}

int AacEncoder::flush(uint8_t* outBuf, int outBufSize) {
    void* outPtr = outBuf;
    INT   outSize = outBufSize;
    INT   outElSize = 1;
    INT   outId = OUT_BITSTREAM_DATA;

    AACENC_BufDesc outBufDesc{};
    outBufDesc.numBufs           = 1;
    outBufDesc.bufs              = &outPtr;
    outBufDesc.bufferIdentifiers = &outId;
    outBufDesc.bufSizes          = &outSize;
    outBufDesc.bufElSizes        = &outElSize;

    AACENC_InArgs inArgs{};
    inArgs.numInSamples = -1; // signal end-of-stream, drain internal delay

    AACENC_OutArgs outArgs{};

    AACENC_ERROR err = aacEncEncode(Handle(handle_), NULL, &outBufDesc, &inArgs, &outArgs);
    if (err == AACENC_ENCODE_EOF) return -1;
    if (err != AACENC_OK) return -2;

    return static_cast<int>(outArgs.numOutBytes);
}

} // namespace ucs
