#pragma once

#include <cstdint>
#include <atomic>
#include <string>
#include <vector>
#include <chrono>

namespace ht306 {

enum class ConnectionState {
    DISCONNECTED = 0,
    CONNECTING,
    READY,
    STREAMING,
    ERR_STATE
};

enum class NavTab {
    DASHBOARD = 0,
    MIXER,
    ROUTING,
    PROFILES,
    TEST,
    LOG,
    SETTINGS
};

enum class RoutingPreset {
    DISCRETE_1TO1 = 0,
    STEREO_UPMIX,
    DOWNMIX_71,
    DUAL_MONO
};

struct AudioEndpointInfo {
    std::wstring id;
    std::string name;
    bool is_default = false;
};

struct ChannelMeters {
    float peak_db = -60.0f;     // -60 dBFS to 0 dBFS
    float rms_db = -60.0f;
    float peak_hold_db = -60.0f;
    bool clipped = false;
};

struct StreamTelemetry {
    uint32_t sample_rate = 96000;
    uint32_t bit_depth = 32;
    uint32_t channels = 6;
    float throughput_kbs = 716.0f;
    float latency_ms = 3.8f;
    float buffer_fill_pct = 62.0f;
    uint64_t total_frames = 0;
    uint64_t total_packets = 0;
    uint32_t underrun_count = 0;
    float jitter_ms = 0.18f;
    int link_usage_pct = 68;
};

struct HardwareInfo {
    bool detected = true;
    std::string port_or_path = "WinUSB Bulk (EP 0x03 OUT)";
    std::string com_port = "COM4";
    uint16_t vid = 0x2E8A;
    uint16_t pid = 0x4306;
    std::string serial_number = "HT306-RP2350Z-96K";
    std::string firmware_version = "v1.3.0";
    uint32_t uptime_sec = 8049; // 02:14:09
    bool heartbeat_ok = true;
    std::string last_heartbeat_text = "♥ 1 Hz OK";
    bool pio_ready = true;
    bool mux_high = true;
    bool dsp_locked = true;
};

struct AppSettings {
    bool auto_reconnect = true;
    bool bit_perfect_mode = false;
    bool mute_master = false;
    float master_volume_db = 0.0f;      // -60.0 dB to +6.0 dB
    int buffer_batch_frames = 8;
    int target_device_index = 0;        // 0 = Default / Virtual Cable
    int format_mode = 0;                // 0: Auto Windows, 1: Best, 2: Adaptive, 3: Manual
    std::string active_profile = "Movie";

    // Operating mode & Limiter
    bool desktop_os_linked = true;
    bool limiter_enabled = true;
    int lip_sync_delay_ms = 24;         // 0 to 200 ms

    // Channel trims (-12dB to +12dB)
    float trims[6] = { 0.0f, 0.0f, +1.5f, -2.0f, 0.0f, 0.0f };
    bool mutes[6] = { false, false, false, false, false, false };
    bool solos[6] = { false, false, false, false, false, false };
    bool phase_inverts[6] = { false, false, false, false, false, false };

    // 6x6 routing matrix (output_ch -> input_ch, default identity 0->0, 1->1, ...)
    int routing_map[6] = { 0, 1, 2, 3, 4, 5 };
    RoutingPreset routing_preset = RoutingPreset::DISCRETE_1TO1;
    float downmix_attenuation_db = -3.0f;

    // Buffer latency profile (0: Auto, 1: Low Latency 3.8ms, 2: Safe Mode 10ms)
    int buffer_profile = 0;
    int resampling_quality = 0; // 0: Bit-Perfect, 1: Sinc, 2: Linear
    bool follow_default_device = true;
};

enum class LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

struct LogEntry {
    std::string timestamp;
    LogLevel level;
    std::string subsystem;
    std::string message;
};

struct ProfileItem {
    std::string id;
    std::string name;
    std::string description;
    std::string mode;
    std::string upmix;
    int lipsync_ms;
    float center_clarity_db;
    int sub_lpf_hz;
    bool is_active;
};

struct AppRuleItem {
    std::string app_name;
    std::string process_name;
    std::string target_profile;
    bool enabled;
};

struct AppState {
    std::atomic<ConnectionState> state{ConnectionState::READY};
    std::atomic<bool> is_streaming{false};
    std::atomic<bool> should_exit{false};
    std::atomic<NavTab> current_tab{NavTab::DASHBOARD};

    HardwareInfo hw_info;
    StreamTelemetry telemetry;
    ChannelMeters meters[6]; // FL, FR, C, LFE, SL, SR
    AppSettings settings;

    std::vector<AudioEndpointInfo> available_devices;
    int active_device_index = 0;

    // 5.1 Acoustic Sweep test state
    std::atomic<int> sweep_active_channel{-1}; // -1 = idle, 0..5 = FL, C, FR, SR, SL, LFE
    std::atomic<bool> sweep_running{false};
    int test_signal_type = 0;                  // 0: Chirp Sweep, 1: Pink Noise, 2: Sine 1kHz, 3: Dual-Tone, 4: Polarity Pulse
    float test_level_dbfs = -20.0f;

    // Profiles & Rules
    std::vector<ProfileItem> profiles;
    std::vector<AppRuleItem> app_rules;

    // Diagnostics / Log
    std::vector<LogEntry> logs;
    int log_filter_level = 0; // 0: ALL, 1: DEBUG, 2: INFO, 3: WARN, 4: ERROR
    char log_search[64] = { 0 };
    bool log_autoscroll = true;

    AppState();
    void AddLog(LogLevel level, const std::string& subsystem, const std::string& msg);
};

} // namespace ht306
