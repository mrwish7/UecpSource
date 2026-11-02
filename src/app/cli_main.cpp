// Headless front end for SourceApp: loads the same config file the GUI
// writes, starts streaming immediately, logs status to stdout, and stops
// cleanly on Ctrl+C / SIGTERM (or console close on Windows). Never writes
// the config file -- it's read-only here, so a running CLI instance can't
// clobber edits made from the GUI.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

#include "app_paths.h"
#include "net_common.h" // also pulls in <windows.h> on Windows (for SetConsoleCtrlHandler)
#include "source_app.h"
#include "source_settings.h"

namespace {

constexpr int kPollIntervalMs = 200;
constexpr int kStatusIntervalSec = 10;

std::atomic<bool> g_stop_requested{false};
std::atomic<bool> g_shutdown_complete{false};

#if defined(_WIN32)
BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
    g_stop_requested = true;
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        // Windows terminates the process as soon as this handler returns for
        // these events, so hold it here (within the OS's ~5 s grace period)
        // until main() has stopped the pipeline and closed Icecast cleanly.
        for (int waited = 0; waited < 4500 && !g_shutdown_complete.load(); waited += 50)
            Sleep(50);
    }
    return TRUE;
}
#else
void SignalHandler(int) {
    g_stop_requested = true;
}
#endif

void InstallStopHandlers() {
#if defined(_WIN32)
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    // (SIGPIPE needs no handling here -- TcpSocket::sendAll() passes
    // MSG_NOSIGNAL, so a dropped Icecast connection is just a send() error.)
#endif
}

std::string Timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// printf-style, prefixed with a timestamp and flushed immediately so output
// shows up promptly when redirected to a log file or a service manager.
template <typename... Args>
void Log(const char* fmt, Args... args) {
    std::printf("[%s] ", Timestamp().c_str());
    std::printf(fmt, args...);
    std::printf("\n");
    std::fflush(stdout);
}

void PrintUsage(const char* argv0) {
    std::printf(
        "Usage: %s [--config <path>] [--list-devices] [--help]\n"
        "\n"
        "Headless UecpSource: streams the configured capture device to Icecast as\n"
        "MP2 or AAC with UECP from the TCP input embedded, using the same config\n"
        "file the GUI (uecp_source) writes. Runs until Ctrl+C / SIGTERM.\n"
        "\n"
        "  --config <path>   Config file to use (default: source_config.ini next to\n"
        "                    the executable). Never modified by this program.\n"
        "  --list-devices    List capture devices and the input_device_name value\n"
        "                    that selects each one, then exit.\n"
        "  --help            Show this help.\n"
        "\n"
        "If input_device_name is empty or missing, the system default capture\n"
        "device is used. If it names a device that isn't present, startup fails\n"
        "rather than silently streaming from a different device.\n",
        argv0);
}

int ListDevices() {
    auto devices = ucs::SourceApp::listInputDevices();
    if (devices.empty()) {
        std::printf("No capture devices found.\n");
        return 0;
    }
    std::printf("Capture devices (* = system default):\n");
    for (const auto& d : devices) {
        std::printf("  %s %s\n", d.is_default ? "*" : " ", d.displayLabel().c_str());
        std::printf("      input_device_name=%s\n", ucs::PersistedIdentity(d).c_str());
    }
    return 0;
}

const char* CodecLabel(const ucs::SourceConfig& cfg) {
    if (cfg.codec == ucs::Codec::MP2) return "MP2";
    switch (cfg.aac_aot) {
        case ucs::AacObjectType::HeAac: return "HE-AAC";
        case ucs::AacObjectType::HeAacV2: return "HE-AACv2";
        default: return "AAC-LC";
    }
}

void LogStatus(const ucs::SourceApp::Status& s) {
    Log("%s | %.0f kbps out | queue %d ms | %llu frames | peak %.0f%% | UECP %s, %llu packets",
        s.state.c_str(), s.outgoing_kbps, s.queue_depth_ms,
        static_cast<unsigned long long>(s.frames_encoded), s.input_peak * 100.0,
        s.uecp_client_connected ? "connected" : "not connected",
        static_cast<unsigned long long>(s.uecp_packets_seen));
}

} // namespace

int main(int argc, char** argv) {
    bool list_devices = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "--list-devices") == 0) {
            list_devices = true;
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            ++i; // value consumed by ResolveConfigPath()
        } else {
            std::fprintf(stderr, "Unknown or incomplete argument: %s\n\n", argv[i]);
            PrintUsage(argv[0]);
            return 2;
        }
    }

    if (list_devices) return ListDevices();

    ucs::NetworkInit net_init;

    std::string config_path = ucs::ResolveConfigPath(argc, argv, "source_config.ini");
    ucs::SourceSettings settings;
    if (!settings.load(config_path)) {
        Log("Error: can't read config file %s (create one with the GUI, or pass --config)",
            config_path.c_str());
        return 1;
    }
    if (settings.icecast_host.empty()) {
        Log("Error: icecast_host is not set in %s", config_path.c_str());
        return 1;
    }

    auto devices = ucs::SourceApp::listInputDevices();
    int device_index = -1;
    if (settings.input_device_name.empty()) {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].is_default) { device_index = static_cast<int>(i); break; }
        }
        if (device_index < 0 && !devices.empty()) device_index = 0;
        if (device_index < 0) {
            Log("Error: no capture devices found");
            return 1;
        }
    } else {
        device_index = ucs::FindDevice(devices, settings.input_device_name);
        if (device_index < 0) {
            Log("Error: capture device \"%s\" not found (run with --list-devices)",
                settings.input_device_name.c_str());
            return 1;
        }
    }
    const ucs::AudioDeviceInfo& device = devices[device_index];

    ucs::SourceConfig cfg = settings.toSourceConfig(device.id);

    Log("Config: %s", config_path.c_str());
    Log("Input: %s, %d Hz, %s", device.displayLabel().c_str(), cfg.sample_rate,
        cfg.channels == 1 ? "mono" : "stereo");
    if (!ucs::SourceApp::inputSupportsFormatNatively(device.id, cfg.sample_rate, cfg.channels))
        Log("Warning: %d Hz not natively supported by this device -- the audio backend will resample",
            cfg.sample_rate);
    if (cfg.codec == ucs::Codec::MP2) {
        Log("Encoder: MP2 %d kbps, %s, psy model %d", cfg.bitrate_kbps,
            cfg.channels == 1 ? "mono" : (cfg.mp2_joint_stereo ? "joint stereo" : "stereo"),
            cfg.mp2_psy_model);
    } else {
        Log("Encoder: %s %d kbps, %s", CodecLabel(cfg), cfg.bitrate_kbps,
            cfg.aac_transport == ucs::AacTransport::LATM ? "LATM (LOAS)" : "ADTS");
    }
    Log("Output: icecast://%s:%d%s%s", cfg.icecast.host.c_str(), cfg.icecast.port,
        (cfg.icecast.mount.empty() || cfg.icecast.mount[0] != '/') ? "/" : "", cfg.icecast.mount.c_str());
    Log("UECP input: TCP port %d", cfg.uecp_port);

    InstallStopHandlers();

    ucs::SourceApp app;
    if (!app.start(cfg)) {
        Log("Error: failed to start: %s", app.status().last_error.c_str());
        g_shutdown_complete = true;
        return 1;
    }
    Log("Started -- press Ctrl+C to stop");

    std::string last_state, last_error;
    auto last_report = std::chrono::steady_clock::now();
    while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));

        ucs::SourceApp::Status s = app.status();
        if (s.last_error != last_error) {
            Log("Error: %s", s.last_error.c_str());
            last_error = s.last_error;
        }
        auto now = std::chrono::steady_clock::now();
        if (s.state != last_state ||
            now - last_report >= std::chrono::seconds(kStatusIntervalSec)) {
            LogStatus(s);
            last_state = s.state;
            last_report = now;
        }
    }

    Log("Stopping...");
    app.stop();
    Log("Stopped");
    g_shutdown_complete = true;
    return 0;
}
