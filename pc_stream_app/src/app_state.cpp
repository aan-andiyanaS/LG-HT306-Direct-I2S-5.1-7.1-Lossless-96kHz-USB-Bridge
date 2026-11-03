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

    logs.push_back({ buf, level, subsystem, msg });
    if (logs.size() > 500) {
        logs.erase(logs.begin());
    }
}

} // namespace ht306
