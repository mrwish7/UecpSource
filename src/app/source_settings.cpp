#include "source_settings.h"

#include "config_store.h"

namespace ucs {

namespace {
AacObjectType AacAotFor(int index) {
    switch (index) {
        case 1: return AacObjectType::HeAac;
        case 2: return AacObjectType::HeAacV2;
        default: return AacObjectType::AacLc;
    }
}
} // namespace

bool SourceSettings::load(const std::string& path) {
    ConfigStore cs;
    if (!cs.load(path)) return false; // no saved config yet (first run) -- keep built-in defaults

    input_device_name = cs.getString("input_device_name", "");

    format_index = cs.getInt("format_index", format_index);
    sample_rate_index = cs.getInt("sample_rate_index", sample_rate_index);
    channels_index = cs.getInt("channels_index", channels_index);
    bitrate_kbps = cs.getInt("bitrate_kbps", bitrate_kbps);
    mp2_joint_stereo = cs.getBool("mp2_joint_stereo", mp2_joint_stereo);
    mp2_psy_model = cs.getInt("mp2_psy_model", mp2_psy_model);
    if (mp2_psy_model < kMinMp2PsyModel || mp2_psy_model > kMaxMp2PsyModel)
        mp2_psy_model = kDefaultMp2PsyModel; // hand-edited/garbage value -- toolame would reject it
    aac_aot_index = cs.getInt("aac_aot_index", aac_aot_index);
    aac_transport_index = cs.getInt("aac_transport_index", aac_transport_index);

    icecast_host = cs.getString("icecast_host", "");
    icecast_port = cs.getInt("icecast_port", icecast_port);
    icecast_mount = cs.getString("icecast_mount", icecast_mount);
    icecast_user = cs.getString("icecast_user", icecast_user);
    icecast_password = cs.getString("icecast_password", "");
    icecast_name = cs.getString("icecast_name", icecast_name);
    icecast_buffer_kb = cs.getInt("icecast_buffer_kb", icecast_buffer_kb);

    uecp_port = cs.getInt("uecp_port", uecp_port);
    return true;
}

bool SourceSettings::save(const std::string& path) const {
    ConfigStore cs;

    cs.setString("input_device_name", input_device_name);

    cs.setInt("format_index", format_index);
    cs.setInt("sample_rate_index", sample_rate_index);
    cs.setInt("channels_index", channels_index);
    cs.setInt("bitrate_kbps", bitrate_kbps);
    cs.setBool("mp2_joint_stereo", mp2_joint_stereo);
    cs.setInt("mp2_psy_model", mp2_psy_model);
    cs.setInt("aac_aot_index", aac_aot_index);
    cs.setInt("aac_transport_index", aac_transport_index);

    cs.setString("icecast_host", icecast_host);
    cs.setInt("icecast_port", icecast_port);
    cs.setString("icecast_mount", icecast_mount);
    cs.setString("icecast_user", icecast_user);
    cs.setString("icecast_password", icecast_password);
    cs.setString("icecast_name", icecast_name);
    cs.setInt("icecast_buffer_kb", icecast_buffer_kb);

    cs.setInt("uecp_port", uecp_port);

    return cs.save(path);
}

SourceConfig SourceSettings::toSourceConfig(const std::string& device_id) const {
    SourceConfig cfg;
    cfg.input_device_id = device_id;
    cfg.sample_rate = sampleRate();
    cfg.channels = channels();
    cfg.codec = codec();
    cfg.bitrate_kbps = bitrate_kbps;
    cfg.mp2_joint_stereo = mp2_joint_stereo;
    cfg.mp2_psy_model = mp2_psy_model;
    cfg.aac_aot = AacAotFor(aac_aot_index);
    cfg.aac_transport = (aac_transport_index == 0) ? AacTransport::ADTS : AacTransport::LATM;
    cfg.icecast.host = icecast_host;
    cfg.icecast.port = icecast_port;
    cfg.icecast.mount = icecast_mount;
    cfg.icecast.user = icecast_user;
    cfg.icecast.password = icecast_password;
    cfg.icecast.name = icecast_name;
    cfg.icecast.buffer_kb = icecast_buffer_kb;
    cfg.uecp_port = uecp_port;
    return cfg;
}

std::string PersistedIdentity(const AudioDeviceInfo& d) {
    return d.native_id.empty() ? d.name : d.native_id;
}

int FindDevice(const std::vector<AudioDeviceInfo>& devices, const std::string& name) {
    for (size_t i = 0; i < devices.size(); ++i) {
        if (PersistedIdentity(devices[i]) == name) return static_cast<int>(i);
    }
    return -1;
}

} // namespace ucs
