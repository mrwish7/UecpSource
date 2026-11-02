#pragma once

#include <string>
#include <vector>

#include "audio_device.h"
#include "source_app.h"
#include "source_settings.h"

namespace ucs {

// ImGui panel for SourceApp, mirroring MPXpack's SenderGui shape.
class SourceGui {
public:
    SourceGui();

    void draw(SourceApp& app);

    void loadConfig(const std::string& path);
    void saveConfig(const std::string& path);

private:
    void refreshDevices();
    void resolveDeviceByName();
    void refreshValidation();
    void syncTextBuffersToSettings(); // ImGui edits the char buffers; settings_ holds std::strings

    std::vector<AudioDeviceInfo> devices_;
    int selected_device_index_ = -1;
    bool device_checked_ = false;
    bool device_valid_ = false;

    // Everything persisted, including settings_.input_device_name (the
    // device identity, see resolveDeviceByName()).
    SourceSettings settings_;

    char icecast_host_[128] = "";
    char icecast_mount_[128] = "";
    char icecast_user_[64] = "";
    char icecast_password_[128] = "";
    char icecast_name_[64] = "";

    std::string config_path_;
};

} // namespace ucs
