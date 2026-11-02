#include "source_gui.h"

#include <cstdio>

#include "imgui.h"

namespace ucs {

namespace {
void CopyToBuffer(char* buf, size_t buf_size, const std::string& s) {
    std::snprintf(buf, buf_size, "%s", s.c_str());
}
} // namespace

SourceGui::SourceGui() {
    CopyToBuffer(icecast_host_, sizeof(icecast_host_), settings_.icecast_host);
    CopyToBuffer(icecast_mount_, sizeof(icecast_mount_), settings_.icecast_mount);
    CopyToBuffer(icecast_user_, sizeof(icecast_user_), settings_.icecast_user);
    CopyToBuffer(icecast_password_, sizeof(icecast_password_), settings_.icecast_password);
    CopyToBuffer(icecast_name_, sizeof(icecast_name_), settings_.icecast_name);
    refreshDevices();
}

void SourceGui::refreshDevices() {
    devices_ = SourceApp::listInputDevices();
    selected_device_index_ = -1;
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].is_default) { selected_device_index_ = static_cast<int>(i); break; }
    }
    if (selected_device_index_ < 0 && !devices_.empty()) selected_device_index_ = 0;
    if (!settings_.input_device_name.empty()) resolveDeviceByName();
    device_checked_ = false;
    device_valid_ = false;
}

void SourceGui::resolveDeviceByName() {
    int i = FindDevice(devices_, settings_.input_device_name);
    if (i >= 0) selected_device_index_ = i;
    // Not found (unplugged, renamed, or first run with no saved config) --
    // leave whatever refreshDevices() already picked (default/first device).
}

void SourceGui::refreshValidation() {
    device_checked_ = true;
    device_valid_ = false;
    if (selected_device_index_ >= 0 && selected_device_index_ < static_cast<int>(devices_.size())) {
        device_valid_ = SourceApp::inputSupportsFormatNatively(
            devices_[selected_device_index_].id, settings_.sampleRate(), settings_.channels());
    }
}

void SourceGui::syncTextBuffersToSettings() {
    settings_.icecast_host = icecast_host_;
    settings_.icecast_mount = icecast_mount_;
    settings_.icecast_user = icecast_user_;
    settings_.icecast_password = icecast_password_;
    settings_.icecast_name = icecast_name_;
}

void SourceGui::draw(SourceApp& app) {
    ImGui::Begin("UecpSource");

    bool running = app.isRunning();
    ImGui::BeginDisabled(running);

    ImGui::SeparatorText("Input device");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) refreshDevices();

    std::string preview = (selected_device_index_ >= 0 && selected_device_index_ < static_cast<int>(devices_.size()))
        ? devices_[selected_device_index_].displayLabel() : "(no device selected)";
    if (ImGui::BeginCombo("Capture device", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(devices_.size()); ++i) {
            bool selected = (i == selected_device_index_);
            ImGui::PushID(i); // devices can share an identical display name -- without this,
                              // ImGui's label-derived IDs collide.
            if (ImGui::Selectable(devices_[i].displayLabel().c_str(), selected)) {
                selected_device_index_ = i;
                settings_.input_device_name = PersistedIdentity(devices_[i]);
                refreshValidation();
            }
            ImGui::PopID();
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!device_checked_ && selected_device_index_ >= 0) refreshValidation();
    if (device_checked_) {
        if (device_valid_) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Supports the selected rate/channels natively.");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
                                "Not natively supported -- will be converted by the audio backend.");
        }
    }

    ImGui::SeparatorText("Format");
    ImGui::RadioButton("MP2", &settings_.format_index, 0); ImGui::SameLine();
    ImGui::RadioButton("AAC", &settings_.format_index, 1);
    const Codec codec = settings_.codec();

    ImGui::RadioButton("44.1 kHz", &settings_.sample_rate_index, 0); ImGui::SameLine();
    ImGui::RadioButton("48 kHz", &settings_.sample_rate_index, 1);
    ImGui::RadioButton("Mono", &settings_.channels_index, 0); ImGui::SameLine();
    ImGui::RadioButton("Stereo", &settings_.channels_index, 1);
    if (device_checked_) refreshValidation(); // rate/channel choice affects the native-support check

    ImGui::SliderInt("Bitrate (kbps)", &settings_.bitrate_kbps, kMinBitrateKbps, kMaxBitrateKbps);

    if (codec == Codec::MP2) {
        ImGui::BeginDisabled(settings_.channels_index == 0);
        ImGui::Checkbox("Joint stereo", &settings_.mp2_joint_stereo);
        ImGui::EndDisabled();
        ImGui::Combo("Psy model", &settings_.mp2_psy_model,
                     "0 (fastest, lowest quality)\0"
                     "1 (ISO model 1)\0"
                     "2 (ISO model 2, default)\0"
                     "3 (modified model 1)\0\0");
    } else {
        ImGui::Combo("AAC profile", &settings_.aac_aot_index, "AAC-LC\0HE-AAC\0HE-AACv2\0\0");
        ImGui::RadioButton("ADTS", &settings_.aac_transport_index, 0);
        ImGui::SameLine();
        ImGui::RadioButton("LATM", &settings_.aac_transport_index, 1);
        ImGui::TextDisabled(settings_.aac_transport_index == 1
            ? "LATM (LOAS-framed): matches real-world UECP-over-DSE broadcast streams."
            : "ADTS: what generic players/tools expect from raw audio/aac.");
    }

    ImGui::SeparatorText("Icecast output");
    ImGui::InputText("Host", icecast_host_, sizeof(icecast_host_));
    ImGui::InputInt("Port", &settings_.icecast_port);
    ImGui::InputText("Mount", icecast_mount_, sizeof(icecast_mount_));
    ImGui::InputText("Source user", icecast_user_, sizeof(icecast_user_));
    ImGui::InputText("Source password", icecast_password_, sizeof(icecast_password_), ImGuiInputTextFlags_Password);
    ImGui::InputText("Stream name", icecast_name_, sizeof(icecast_name_));
    ImGui::SliderInt("Write buffer (KB)", &settings_.icecast_buffer_kb, 0, 64);
    ImGui::TextDisabled("0 disables write-coalescing and sends each frame immediately.");

    ImGui::SeparatorText("UECP input");
    ImGui::InputInt("TCP port", &settings_.uecp_port);

    ImGui::EndDisabled();

    ImGui::Spacing();
    bool can_start = !running && device_valid_ && selected_device_index_ >= 0 && icecast_host_[0] != '\0';

    if (!running) {
        ImGui::BeginDisabled(!can_start);
        if (ImGui::Button("Start", ImVec2(120, 0))) {
            syncTextBuffersToSettings();
            app.start(settings_.toSourceConfig(devices_[selected_device_index_].id));
            if (!config_path_.empty()) saveConfig(config_path_); // crash-safety: persist as soon as settings are used
        }
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Stop", ImVec2(120, 0))) {
            app.stop();
        }
    }

    ImGui::SeparatorText("Status");
    SourceApp::Status status = app.status();
    ImGui::Text("State: %s", status.state.c_str());
    if (!status.last_error.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Last error: %s", status.last_error.c_str());
    }
    ImGui::Text("Input level");
    ImGui::ProgressBar(static_cast<float>(status.input_peak), ImVec2(-1, 0));
    ImGui::Text("Outgoing bitrate: %.0f kbps", status.outgoing_kbps);
    ImGui::Text("Queue depth: %d ms", status.queue_depth_ms);
    ImGui::Text("Frames encoded: %llu", static_cast<unsigned long long>(status.frames_encoded));
    ImGui::Text("UECP client: %s (%llu packets embedded)",
                status.uecp_client_connected ? "connected" : "not connected",
                static_cast<unsigned long long>(status.uecp_packets_seen));

    ImGui::End();
}

void SourceGui::loadConfig(const std::string& path) {
    config_path_ = path; // remembered even on first run, so auto-save-on-Start still works

    if (!settings_.load(path)) return; // no saved config yet (first run) -- keep built-in defaults

    resolveDeviceByName();

    CopyToBuffer(icecast_host_, sizeof(icecast_host_), settings_.icecast_host);
    CopyToBuffer(icecast_mount_, sizeof(icecast_mount_), settings_.icecast_mount);
    CopyToBuffer(icecast_user_, sizeof(icecast_user_), settings_.icecast_user);
    CopyToBuffer(icecast_password_, sizeof(icecast_password_), settings_.icecast_password);
    CopyToBuffer(icecast_name_, sizeof(icecast_name_), settings_.icecast_name);
}

void SourceGui::saveConfig(const std::string& path) {
    if (selected_device_index_ >= 0 && selected_device_index_ < static_cast<int>(devices_.size()))
        settings_.input_device_name = PersistedIdentity(devices_[selected_device_index_]);
    syncTextBuffersToSettings();
    settings_.save(path);
}

} // namespace ucs
