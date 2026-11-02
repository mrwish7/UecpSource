#pragma once

#include <string>
#include <vector>

#include "app_config.h"
#include "audio_device.h"

namespace ucs {

// The persisted (config-file) form of the app's settings, shared by the GUI
// and the headless CLI so both read/write exactly the same keys. Fields are
// stored the way the GUI's widgets want them (radio/combo indices, strings)
// rather than as a SourceConfig directly -- that's also what's on disk, so
// existing config files keep loading unchanged.
struct SourceSettings {
    std::string input_device_name; // persisted identity, see PersistedIdentity()

    int format_index = 1;          // 0 = MP2, 1 = AAC
    int sample_rate_index = 1;     // 0 = 44100, 1 = 48000
    int channels_index = 1;        // 0 = mono, 1 = stereo
    int bitrate_kbps = kDefaultBitrateKbps;
    bool mp2_joint_stereo = true;
    int mp2_psy_model = kDefaultMp2PsyModel;
    int aac_aot_index = 0;         // 0 = LC, 1 = HE-AAC, 2 = HE-AACv2
    int aac_transport_index = 1;   // 0 = ADTS, 1 = LATM

    std::string icecast_host;
    int icecast_port = 8000;
    std::string icecast_mount = "/stream.aac";
    std::string icecast_user = "source";
    std::string icecast_password;
    std::string icecast_name = "UecpSource";
    int icecast_buffer_kb = kDefaultIcecastBufferKb;

    int uecp_port = kDefaultUecpPort;

    // Returns false if the file doesn't exist or can't be read, leaving
    // every field at its current (default) value.
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    Codec codec() const { return (format_index == 0) ? Codec::MP2 : Codec::AAC; }
    int sampleRate() const { return (sample_rate_index == 0) ? 44100 : 48000; }
    int channels() const { return (channels_index == 0) ? 1 : 2; }

    // device_id is the opaque AudioDeviceInfo::id from the current
    // enumerate() call -- resolve it with FindDevice() first.
    SourceConfig toSourceConfig(const std::string& device_id) const;
};

// The identity used for matching/persisting device selection across runs.
// Prefer native_id (e.g. the raw ALSA PCM name) since `name` alone can be
// identical across several distinct devices (see audio_device.h).
std::string PersistedIdentity(const AudioDeviceInfo& d);

// Index into devices whose PersistedIdentity() matches name, or -1.
int FindDevice(const std::vector<AudioDeviceInfo>& devices, const std::string& name);

} // namespace ucs
