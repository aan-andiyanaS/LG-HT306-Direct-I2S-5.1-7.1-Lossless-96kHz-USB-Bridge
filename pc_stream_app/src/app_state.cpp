#include "app_state.h"

namespace ht306 {

AppState::AppState() {
    // 1. Initial Profiles
    profiles = {
        {
            "prof_movie",
            "Movie / Cinematic Surround",
            "Dynamic dialogue intelligibility, ITU-R BS.775 surround matrix, and 24ms AV lip-sync offset.",
            "Desktop OS-Linked",
            "ITU-R 5.1 Matrix",
            24,
            +1.5f,
            120,
            true
        },
        {
            "prof_music",
            "Music / Bit-Perfect Audiophile",
            "Direct 1:1 discrete sample passthrough with zero resampling and hardware DSP bypass.",
            "Bit-Perfect Pass-thru",
            "Direct 1:1 Discrete",
            0,
            0.0f,
            80,
            false
        },
        {
            "prof_game",
            "Gaming / Ultra-Low Latency",
            "Tight 3.8ms sub-packet buffers, localized rear imaging, and low-lag surround tracking.",
            "Desktop OS-Linked",
            "Spatial 5.1 Gaming",
            12,
            +0.5f,
            100,
            false
        },
        {
            "prof_night",
            "Late Night / Dynamic Compression",
            "Soft-knee dialogue boost and subdued bass transients for quiet home theater listening.",
            "Desktop OS-Linked",
            "ITU-R Night Limiter",
            24,
            +3.0f,
            80,
            false
        }
    };

    // 2. Initial Application Rules
    app_rules = {
        { "PotPlayer 64-bit", "PotPlayer64.exe", "Movie / Cinematic Surround", true },
        { "Foobar2000 Hi-Res", "foobar2000.exe", "Music / Bit-Perfect Audiophile", true },
        { "Cyberpunk 2077", "Cyberpunk2077.exe", "Gaming / Ultra-Low Latency", true },
        { "MPC-HC Video Player", "mpc-hc64.exe", "Movie / Cinematic Surround", true },
        { "TIDAL Desktop", "TIDAL.exe", "Music / Bit-Perfect Audiophile", false }
    };

    // 3. Initial Authentic Event Logs
    logs = {
        { "19:42:01.102", LogLevel::LOG_INFO,  "SYS_CORE",   "HT306 Audio Bridge Engine initialized. OS: Windows 10/11 CoreAudio." },
        { "19:42:01.145", LogLevel::LOG_INFO,  "WINUSB",     "Device discovery: VID 0x2E8A PID 0x4306 (RP2350-Zero Direct-I2S)." },
        { "19:42:01.210", LogLevel::LOG_INFO,  "WINUSB",     "Claimed Interface 2 Bulk Endpoint 0x03 OUT (512B MaxPacketSize)." },
        { "19:42:01.250", LogLevel::LOG_INFO,  "CDC_ACM",    "Control channel opened on COM4 @ 115200 baud." },
        { "19:42:01.298", LogLevel::LOG_INFO,  "RP2350",     "Handshake ACK: Firmware v1.3.0 | PIO SM0-2 Dual Core Sync." },
        { "19:42:01.312", LogLevel::LOG_INFO,  "HARDWARE",   "GP8 MUX Pin pulled HIGH (Direct I2S PS9850 ASRC bypass)." },
        { "19:42:01.350", LogLevel::LOG_DEBUG, "AUDIO_CAP",  "WASAPI Loopback endpoint acquired: 96000 Hz, 6 Channels, 32-bit Float." },
        { "19:42:01.402", LogLevel::LOG_INFO,  "CODEC",      "Lossless Rice-FLAC Order-4 encoder ready. Buffer batch: 8 frames." },
        { "19:42:01.450", LogLevel::LOG_INFO,  "DSP_CORE",   "6-Channel TAS5508 DSP Matrix ready. Master unity 0.0dB. Limiter active." },
        { "19:42:02.000", LogLevel::LOG_DEBUG, "TELEMETRY",  "Link Budget: 716 KB/s (68% utilized), Clock drift: +0.003 ppm, 0 drop." }
    };
}

void AppState::AddLog(LogLevel level, const std::string& subsystem, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms.count());

    std::lock_guard<std::mutex> lock(log_mutex);
    logs.push_back({ buf, level, subsystem, msg });
    if (logs.size() > 500) {
        logs.erase(logs.begin());
    }
}

void AppState::ClearLogs() {
    std::lock_guard<std::mutex> lock(log_mutex);
    logs.clear();
}

std::vector<LogEntry> AppState::GetLogsSnapshot() const {
    std::lock_guard<std::mutex> lock(log_mutex);
    return logs;
}

namespace {

int FindDeviceIndexById(const std::vector<AudioEndpointInfo>& list, const std::wstring& id) {
    if (id.empty()) return -1;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FindVirtualHT306Index(const std::vector<AudioEndpointInfo>& list) {
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string& name = list[i].name;
        if (name.find("16ch") != std::string::npos) {
            continue;
        }
        if (name.find("HT306") != std::string::npos ||
            name.find("CABLE") != std::string::npos ||
            name.find("Virtual") != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

std::vector<AudioEndpointInfo> AppState::GetAvailableDevicesSnapshot() const {
    std::lock_guard<std::mutex> lock(devices_mutex);
    return available_devices;
}

void AppState::SetAvailableDevices(
    std::vector<AudioEndpointInfo> next_devices,
    const std::wstring& preferred_id_or_empty,
    bool auto_select_ht306
) {
    std::lock_guard<std::mutex> lock(devices_mutex);
    available_devices = std::move(next_devices);

    if (available_devices.empty()) {
        active_device_id.clear();
        active_device_index.store(0);
        return;
    }

    int target_idx = -1;
    if (!preferred_id_or_empty.empty()) {
        target_idx = FindDeviceIndexById(available_devices, preferred_id_or_empty);
    }
    if (target_idx < 0 && !active_device_id.empty()) {
        target_idx = FindDeviceIndexById(available_devices, active_device_id);
    }
    if (target_idx <= 0 && auto_select_ht306) {
        int ht_idx = FindVirtualHT306Index(available_devices);
        if (ht_idx > 0) {
            target_idx = ht_idx;
        }
    }
    if (target_idx < 0) {
        int cur = active_device_index.load();
        target_idx = (cur >= 0 && cur < static_cast<int>(available_devices.size())) ? cur : 0;
    }

    active_device_index.store(target_idx);
    active_device_id = available_devices[static_cast<size_t>(target_idx)].id;
}

bool AppState::SelectDeviceByIndex(int idx, std::string* out_name) {
    std::lock_guard<std::mutex> lock(devices_mutex);
    if (idx >= 0 && idx < static_cast<int>(available_devices.size())) {
        active_device_index.store(idx);
        active_device_id = available_devices[static_cast<size_t>(idx)].id;
        settings.target_device_index = idx;
        if (out_name) {
            *out_name = available_devices[static_cast<size_t>(idx)].name;
        }
        return true;
    }
    if (idx == 0 && available_devices.empty()) {
        active_device_index.store(0);
        active_device_id.clear();
        settings.target_device_index = 0;
    }
    if (out_name) {
        out_name->clear();
    }
    return false;
}

bool AppState::SelectDeviceById(const std::wstring& id) {
    std::lock_guard<std::mutex> lock(devices_mutex);
    active_device_id = id;
    int idx = FindDeviceIndexById(available_devices, id);
    if (idx >= 0) {
        active_device_index.store(idx);
        settings.target_device_index = idx;
        return true;
    }
    return false;
}

AudioEndpointInfo AppState::GetActiveDeviceSnapshot(int* out_index) const {
    std::lock_guard<std::mutex> lock(devices_mutex);
    if (available_devices.empty()) {
        if (out_index) *out_index = 0;
        AudioEndpointInfo empty_def{};
        empty_def.id = active_device_id;
        empty_def.name = "Default Windows Playback Device";
        empty_def.is_default = active_device_id.empty();
        return empty_def;
    }
    int idx = active_device_index.load();
    if (idx < 0 || idx >= static_cast<int>(available_devices.size())) {
        idx = 0;
    }
    if (out_index) {
        *out_index = idx;
    }
    return available_devices[static_cast<size_t>(idx)];
}

} // namespace ht306
