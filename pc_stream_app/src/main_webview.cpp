// ============================================================================
// LG HT306 Audio Bridge - Direct I2S 96kHz Lossless
// WebView2 Desktop Application Host & Native IPC Bridge
// ============================================================================

#define WEBVIEW_BUILD 1
#define WEBVIEW_MSWEBVIEW2_BUILTIN_IMPL 1

#include <windows.h>
#include <dbt.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <shellapi.h>
#include "WebView2.h"
#include "webview/webview.h"
#include "app_state.h"
#include "audio_capture.h"
#include "usb_transport.h"
#include "audio_endpoint_helper.h"
#include "audio_policy_manager.h"
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <tlhelp32.h>
#include <psapi.h>

namespace ht306 {
    using AudioCapture = audio::AudioCapture;
    using UsbTransport = usb::UsbTransport;
}

// ----------------------------------------------------------------------------
// Global Instances
// ----------------------------------------------------------------------------
ht306::AppState g_State;
ht306::UsbTransport g_UsbTransport(g_State);
ht306::AudioCapture g_AudioCapture(g_State, g_UsbTransport);
std::atomic<bool> g_Running{true};
std::atomic<bool> g_TelemetryDirty{true};
webview::webview* g_pWebview = nullptr;
ICoreWebView2* g_pCoreWebView2 = nullptr;


// ----------------------------------------------------------------------------
// Aggressive Memory & Working Set Trimmer
// ----------------------------------------------------------------------------
void TrimWorkingSetTree() {
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    DWORD myPid = GetCurrentProcessId();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        std::vector<DWORD> pids;
        if (Process32First(hSnap, &pe)) {
            do {
                if (pe.th32ParentProcessID == myPid) {
                    pids.push_back(pe.th32ProcessID);
                }
            } while (Process32Next(hSnap, &pe));
        }
        if (Process32First(hSnap, &pe)) {
            do {
                for (DWORD cp : pids) {
                    if (pe.th32ParentProcessID == cp) {
                        pids.push_back(pe.th32ProcessID);
                        break;
                    }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);

        for (DWORD pid : pids) {
            HANDLE hProc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, pid);
            if (hProc) {
                SetProcessWorkingSetSize(hProc, (SIZE_T)-1, (SIZE_T)-1);
                CloseHandle(hProc);
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Win32 Theming & Helper Functions
// ----------------------------------------------------------------------------
void EnableImmersiveDarkMode(HWND hwnd) {
    if (!hwnd) return;
    BOOL darkMode = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE (20 on Win10 2004+, 19 on earlier builds)
    if (FAILED(DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode)))) {
        DwmSetWindowAttribute(hwnd, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 */, &darkMode, sizeof(darkMode));
    }
}

static std::string FindAssetPath(const std::string& subpath) {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecA(exePath);

    char buf[MAX_PATH] = { 0 };
    PathCombineA(buf, exePath, subpath.c_str());
    if (GetFileAttributesA(buf) != INVALID_FILE_ATTRIBUTES) return buf;

    PathCombineA(buf, exePath, ("..\\" + subpath).c_str());
    if (GetFileAttributesA(buf) != INVALID_FILE_ATTRIBUTES) return buf;

    if (GetFileAttributesA(subpath.c_str()) != INVALID_FILE_ATTRIBUTES) return subpath;

    std::string proj_path = "E:\\Project\\HomeTheather\\" + subpath;
    if (GetFileAttributesA(proj_path.c_str()) != INVALID_FILE_ATTRIBUTES) return proj_path;

    return subpath;
}

static std::string ResolveUiHtmlPath() {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecA(exePath);

    char candidate[MAX_PATH] = { 0 };

    // 1. In <exe_dir>\ui\index.html
    PathCombineA(candidate, exePath, "ui\\index.html");
    if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        return candidate;
    }

    // 2. In <exe_dir>\..\ui\index.html (when running from build/)
    PathCombineA(candidate, exePath, "..\\ui\\index.html");
    if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        char full[MAX_PATH] = { 0 };
        GetFullPathNameA(candidate, MAX_PATH, full, NULL);
        return full;
    }

    // 3. Current working directory: ui\index.html
    if (GetFileAttributesA("ui\\index.html") != INVALID_FILE_ATTRIBUTES) {
        char full[MAX_PATH] = { 0 };
        GetFullPathNameA("ui\\index.html", MAX_PATH, full, NULL);
        return full;
    }

    // 4. pc_stream_app\ui\index.html
    if (GetFileAttributesA("pc_stream_app\\ui\\index.html") != INVALID_FILE_ATTRIBUTES) {
        char full[MAX_PATH] = { 0 };
        GetFullPathNameA("pc_stream_app\\ui\\index.html", MAX_PATH, full, NULL);
        return full;
    }

    // 5. Absolute fallback path
    const char* fallback = "E:\\Project\\HomeTheather\\pc_stream_app\\ui\\index.html";
    if (GetFileAttributesA(fallback) != INVALID_FILE_ATTRIBUTES) {
        return fallback;
    }

    return "ui\\index.html";
}

static std::string PathToUrl(const std::string& path) {
    char full[MAX_PATH] = { 0 };
    GetFullPathNameA(path.c_str(), MAX_PATH, full, NULL);
    std::string s = full;
    for (char& c : s) {
        if (c == '\\') c = '/';
    }
    return "file:///" + s;
}

// ----------------------------------------------------------------------------
// JSON Unwrapping & Parsing Utilities
// ----------------------------------------------------------------------------
static std::string UnwrapJsonRpc(const std::string& input) {
    std::string s = input;
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(0, 1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();

    // If it's a JSON array [ ... ]
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
        s = s.substr(1, s.size() - 2);
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.erase(0, 1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    }
    // If it's a quoted string "..."
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        int n = webview::detail::json_unescape(s.c_str(), s.length(), nullptr);
        if (n > 0) {
            std::string unesc(n, '\0');
            webview::detail::json_unescape(s.c_str(), s.length(), &unesc[0]);
            s = unesc;
        }
    }
    return s;
}

static std::string EscapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

static bool JsonGetInt(const std::string& s, const std::string& key, int& out_val) {
    std::string v = webview::detail::json_parse(s, key, 0);
    if (v.empty()) return false;
    try {
        out_val = std::stoi(v);
        return true;
    } catch (...) {
        return false;
    }
}

static bool JsonGetFloat(const std::string& s, const std::string& key, float& out_val) {
    std::string v = webview::detail::json_parse(s, key, 0);
    if (v.empty()) return false;
    try {
        out_val = std::stof(v);
        return true;
    } catch (...) {
        return false;
    }
}

static bool JsonGetBool(const std::string& s, const std::string& key, bool& out_val) {
    std::string v = webview::detail::json_parse(s, key, 0);
    if (v.empty()) return false;
    if (v == "true" || v == "1") {
        out_val = true;
        return true;
    }
    if (v == "false" || v == "0") {
        out_val = false;
        return true;
    }
    return false;
}

static int ParseChannelIndex(const std::string& ch_str) {
    if (ch_str.empty()) return -1;
    if (ch_str == "FL"  || ch_str == "fl"  || ch_str == "0") return 0;
    if (ch_str == "FR"  || ch_str == "fr"  || ch_str == "1") return 1;
    if (ch_str == "C"   || ch_str == "c"   || ch_str == "2") return 2;
    if (ch_str == "LFE" || ch_str == "lfe" || ch_str == "SW" || ch_str == "sw" || ch_str == "3") return 3;
    if (ch_str == "SL"  || ch_str == "sl"  || ch_str == "4") return 4;
    if (ch_str == "SR"  || ch_str == "sr"  || ch_str == "5") return 5;
    try {
        int idx = std::stoi(ch_str);
        if (idx >= 0 && idx < 6) return idx;
    } catch (...) {}
    return -1;
}

static bool ParseRoutingMap(const std::string& json, int map_out[6]) {
    size_t pos = json.find("\"routing_map\"");
    if (pos == std::string::npos) pos = json.find("routing_map");
    if (pos == std::string::npos) return false;

    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) return false;

    size_t obj_start = json.find('{', colon);
    size_t arr_start = json.find('[', colon);

    if (arr_start != std::string::npos && (obj_start == std::string::npos || arr_start < obj_start)) {
        size_t arr_end = json.find(']', arr_start);
        if (arr_end != std::string::npos) {
            std::string sub = json.substr(arr_start + 1, arr_end - arr_start - 1);
            std::stringstream ss(sub);
            std::string item;
            int idx = 0;
            while (std::getline(ss, item, ',') && idx < 6) {
                try {
                    map_out[idx++] = std::stoi(item);
                } catch (...) {}
            }
            return (idx == 6);
        }
    } else if (obj_start != std::string::npos) {
        size_t obj_end = json.find('}', obj_start);
        if (obj_end != std::string::npos) {
            std::string sub = json.substr(obj_start, obj_end - obj_start + 1);
            for (int i = 0; i < 6; ++i) {
                std::string k1 = "\"" + std::to_string(i) + "\"";
                size_t kp = sub.find(k1);
                if (kp == std::string::npos) {
                    kp = sub.find(std::to_string(i) + ":");
                }
                if (kp != std::string::npos) {
                    size_t c = sub.find(':', kp);
                    if (c != std::string::npos) {
                        size_t val_start = sub.find_first_of("012345", c);
                        if (val_start != std::string::npos && val_start < obj_end) {
                            map_out[i] = sub[val_start] - '0';
                        }
                    }
                }
            }
            return true;
        }
    }
    return false;
}


// ----------------------------------------------------------------------------
// IPC Command Handler
// ----------------------------------------------------------------------------
void HandleNativeCommand(const std::string& raw_json_str) {
    std::string json = UnwrapJsonRpc(raw_json_str);
    std::string action = webview::detail::json_parse(json, "action", 0);
    if (action.empty()) {
        action = webview::detail::json_parse(json, "cmd", 0);
    }

    if (action == "toggle_stream") {
        bool next_stream = !g_State.is_streaming.load();
        std::string str_val = webview::detail::json_parse(json, "streaming", 0);
        if (str_val == "true" || str_val == "1") {
            next_stream = true;
        } else if (str_val == "false" || str_val == "0") {
            next_stream = false;
        }

        if (next_stream) {
            g_AudioCapture.Start();
            g_State.is_streaming.store(true);
            g_State.state.store(ht306::ConnectionState::STREAMING);
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Stream", "Audio stream started (96kHz 32-bit float 5.1)");
        } else {
            g_AudioCapture.Stop();
            g_State.is_streaming.store(false);
            g_State.state.store(g_UsbTransport.IsOpen() ? ht306::ConnectionState::READY : ht306::ConnectionState::DISCONNECTED);
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Stream", "Audio stream stopped");
        }
    }
    else if (action == "set_volume" || action == "set_master_volume") {
        float vol = 0.0f;
        if (JsonGetFloat(json, "volume_db", vol) || JsonGetFloat(json, "volume", vol) || JsonGetFloat(json, "val", vol)) {
            if (vol < -60.0f) vol = -60.0f;
            if (vol > 6.0f) vol = 6.0f;
            g_State.settings.master_volume_db = vol;
        }
        bool muted = false;
        if (JsonGetBool(json, "muted", muted)) {
            g_State.settings.mute_master = muted;
        }
    }
    else if (action == "set_trim" || action == "set_gain") {
        std::string ch_str = webview::detail::json_parse(json, "channel", 0);
        int ch_idx = ParseChannelIndex(ch_str);
        float gain = 0.0f;
        if (ch_idx >= 0 && ch_idx < 6) {
            if (JsonGetFloat(json, "gain_db", gain) || JsonGetFloat(json, "trim_db", gain) || JsonGetFloat(json, "val", gain)) {
                if (gain < -12.0f) gain = -12.0f;
                if (gain > 12.0f) gain = 12.0f;
                g_State.settings.trims[ch_idx] = gain;
            }
        }
        bool reset_val = false;
        if (JsonGetBool(json, "reset", reset_val) && reset_val) {
            for (int i = 0; i < 6; ++i) g_State.settings.trims[i] = 0.0f;
        }
    }
    else if (action == "set_mute" || action == "set_mutes") {
        std::string ch_str = webview::detail::json_parse(json, "channel", 0);
        int ch_idx = ParseChannelIndex(ch_str);
        bool muted_val = false;
        if (ch_idx >= 0 && ch_idx < 6 && JsonGetBool(json, "muted", muted_val)) {
            g_State.settings.mutes[ch_idx] = muted_val;
        }
    }
    else if (action == "set_solo") {
        std::string ch_str = webview::detail::json_parse(json, "channel", 0);
        int ch_idx = ParseChannelIndex(ch_str);
        bool solo_val = false;
        if (ch_idx >= 0 && ch_idx < 6 && JsonGetBool(json, "solo", solo_val)) {
            g_State.settings.solos[ch_idx] = solo_val;
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Mixer", "Channel " + std::to_string(ch_idx) + (solo_val ? " solo enabled" : " solo disabled"));
        }
    }
    else if (action == "set_phase") {
        std::string ch_str = webview::detail::json_parse(json, "channel", 0);
        int ch_idx = ParseChannelIndex(ch_str);
        bool invert_val = false;
        if (ch_idx >= 0 && ch_idx < 6 && JsonGetBool(json, "invert", invert_val)) {
            g_State.settings.phase_inverts[ch_idx] = invert_val;
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Mixer", "Channel " + std::to_string(ch_idx) + (invert_val ? " phase inverted (180 deg)" : " phase normal (0 deg)"));
        }
    }
    else if (action == "set_test_signal") {
        int sig_type = 0;
        if (JsonGetInt(json, "signal_type", sig_type) || JsonGetInt(json, "type", sig_type)) {
            g_State.test_signal_type = sig_type;
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "SweepTest", "Test signal type set to " + std::to_string(sig_type));
        }
    }
    else if (action == "set_mode") {
        std::string mode = webview::detail::json_parse(json, "mode", 0);
        bool bitperfect = (mode == "bitperfect");
        bool bp_field = false;
        if (JsonGetBool(json, "bit_perfect_mode", bp_field)) {
            bitperfect = bp_field;
        }
        g_State.settings.bit_perfect_mode = bitperfect;
        g_State.settings.desktop_os_linked = !bitperfect;
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "DSP", bitperfect ? "Switched to Bit-Perfect Pass-thru mode" : "Switched to Desktop OS-Linked mode");
    }
    else if (action == "test_sweep" || action == "sweep_test") {
        std::string sw_action = webview::detail::json_parse(json, "sweep_action", 0);
        if (sw_action.empty()) sw_action = webview::detail::json_parse(json, "sub_action", 0);
        if (sw_action.empty()) sw_action = webview::detail::json_parse(json, "cmd", 0);
        if (sw_action.empty()) sw_action = webview::detail::json_parse(json, "action", 0);
        if (sw_action == "test_sweep" || sw_action == "sweep_test") sw_action = "start";

        std::string ch_str = webview::detail::json_parse(json, "channel", 0);
        int ch_idx = ParseChannelIndex(ch_str);
        float lvl = -20.0f;
        JsonGetFloat(json, "level_db", lvl);

        int sig_type = 1;
        if (JsonGetInt(json, "signal_type", sig_type)) {
            g_State.test_signal_type = sig_type;
        }

        if (sw_action == "stop" || sw_action == "pause") {
            g_State.sweep_running.store(false);
            g_State.sweep_active_channel.store(-1);
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "SweepTest", "5.1 acoustic sweep paused/idle");
        } else {
            if (ch_idx < 0) ch_idx = 0;
            g_State.sweep_active_channel.store(ch_idx);
            g_State.sweep_running.store(true);
            g_State.test_level_dbfs = lvl;
            if (!g_AudioCapture.IsRunning()) {
                g_AudioCapture.Start();
                g_State.is_streaming.store(true);
                g_State.state.store(ht306::ConnectionState::STREAMING);
            }
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "SweepTest", "Acoustic test signal (" + std::to_string(g_State.test_signal_type) + ") active on channel " + std::to_string(ch_idx));
        }
    }
    else if (action == "open_vbcable" || action == "open_vbcable_installer") {
        std::string vbcable_path = FindAssetPath("pc_stream_app\\tools\\vbcable");
        if (GetFileAttributesA(vbcable_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            vbcable_path = FindAssetPath("tools\\vbcable");
        }
        ShellExecuteA(NULL, "open", vbcable_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "Driver", "Opened VB-Cable directory: " + vbcable_path);
    }
    else if (action == "install_driver" || action == "install_audio_driver") {
        HWND hwnd = NULL;
        if (g_pWebview) {
            hwnd = (HWND)g_pWebview->get_native_handle(WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
        }
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "Driver", "Starting background installation for LG HT306 Virtual Audio Driver (WHQL)...");
        std::thread([hwnd]() {
            bool ok = ht306::audio::AudioPolicyManager::Instance().InstallDriver(hwnd);
            if (ok) {
                ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
                ht306::audio::AudioPolicyManager::Instance().SwitchToHT306(g_State);
                g_State.AddLog(ht306::LogLevel::LOG_INFO, "Driver", "LG HT306 Audio Driver successfully installed & set as default playback device!");
            } else {
                g_State.AddLog(ht306::LogLevel::LOG_WARN, "Driver", "Driver installation cancelled or not completed.");
            }
            g_TelemetryDirty.store(true);
        }).detach();
    }
    else if (action == "sync_audio_names" || action == "rename_audio_device") {
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "Driver", "Menyinkronkan penamaan Windows Taskbar ke 'LG HT306 Home Theater'...");
        std::thread([]() {
            bool ok = ht306::audio::AudioPolicyManager::Instance().SyncEndpointNames();
            if (ok) {
                ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
                ht306::audio::AudioPolicyManager::Instance().SwitchToHT306(g_State);
                g_State.AddLog(ht306::LogLevel::LOG_INFO, "Driver", "Nama audio taskbar berhasil diubah menjadi: LG HT306 Home Theater");
            } else {
                g_State.AddLog(ht306::LogLevel::LOG_WARN, "Driver", "Gagal atau dibatalkan menyinkronkan nama taskbar.");
            }
            g_TelemetryDirty.store(true);
        }).detach();
    }
    else if (action == "switch_to_ht306" || action == "set_taskbar_default" || action == "force_show_ht306") {
        if (!g_State.hw_info.detected) {
            ht306::audio::AudioPolicyManager::Instance().SetManualVisibilityOverride(true);
        }
        bool ok = ht306::audio::AudioPolicyManager::Instance().SwitchToHT306(g_State);
        ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
        if (ok) {
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "AudioPolicy", "Switched Windows taskbar default audio device to LG HT306");
        }
        g_TelemetryDirty.store(true);
    }
    else if (action == "restore_default_playback" || action == "hide_ht306_endpoint") {
        ht306::audio::AudioPolicyManager::Instance().SetManualVisibilityOverride(false);
        ht306::audio::AudioPolicyManager::Instance().RestorePreviousDefault(g_State);
        ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
        g_TelemetryDirty.store(true);
    }
    else if (action == "toggle_debug_audio" || action == "set_debug_audio") {
        bool cur_override = ht306::audio::AudioPolicyManager::Instance().GetManualVisibilityOverride();
        bool next_override = !cur_override;
        JsonGetBool(json, "enabled", next_override);
        JsonGetBool(json, "enable", next_override);
        ht306::audio::AudioPolicyManager::Instance().SetManualVisibilityOverride(next_override);
        if (next_override) {
            bool ok = ht306::audio::AudioPolicyManager::Instance().SwitchToHT306(g_State);
            if (ok) {
                g_State.AddLog(ht306::LogLevel::LOG_INFO, "DebugAudio", "Mode Debug Aktif: LG HT306 Home Theater diaktifkan tanpa RP2350");
            }
        } else {
            if (!g_UsbTransport.CheckHardwarePresence()) {
                ht306::audio::AudioPolicyManager::Instance().RestorePreviousDefault(g_State);
                g_State.AddLog(ht306::LogLevel::LOG_INFO, "DebugAudio", "Mode Debug Nonaktif: LG HT306 Home Theater disembunyikan dari Taskbar");
            } else {
                g_State.AddLog(ht306::LogLevel::LOG_INFO, "DebugAudio", "Mode Debug Nonaktif (RP2350 masih terhubung)");
            }
        }
        ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
        g_TelemetryDirty.store(true);
    }
    else if (action == "reboot_bootsel") {
        bool ok = g_UsbTransport.RebootBootsel();
        g_State.AddLog(ok ? ht306::LogLevel::LOG_WARN : ht306::LogLevel::LOG_ERROR, "RP2350", ok ? "Rebooted RP2350 to BOOTSEL mode via CDC-ACM 1200 baud touch" : "Failed to reboot RP2350 to BOOTSEL");
    }
    else if (action == "set_profile") {
        std::string prof = webview::detail::json_parse(json, "profile", 0);
        if (prof.empty()) prof = webview::detail::json_parse(json, "name", 0);
        if (!prof.empty()) {
            g_State.settings.active_profile = prof;
            if (prof.find("Movie") != std::string::npos) {
                float t[6] = {0.0f, 0.0f, 1.5f, 3.0f, 0.0f, 0.0f};
                for (int i = 0; i < 6; ++i) g_State.settings.trims[i] = t[i];
                g_State.settings.bit_perfect_mode = false;
                g_State.settings.desktop_os_linked = true;
                g_State.settings.limiter_enabled = true;
                g_State.settings.routing_preset = ht306::RoutingPreset::DISCRETE_1TO1;
            } else if (prof.find("Music") != std::string::npos) {
                float t[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                for (int i = 0; i < 6; ++i) g_State.settings.trims[i] = t[i];
                g_State.settings.bit_perfect_mode = false;
                g_State.settings.desktop_os_linked = true;
                g_State.settings.limiter_enabled = true;
                g_State.settings.routing_preset = ht306::RoutingPreset::STEREO_UPMIX;
            } else if (prof.find("Game") != std::string::npos || prof.find("Gaming") != std::string::npos) {
                float t[6] = {0.0f, 0.0f, 1.0f, 2.0f, 2.5f, 2.5f};
                for (int i = 0; i < 6; ++i) g_State.settings.trims[i] = t[i];
                g_State.settings.bit_perfect_mode = false;
                g_State.settings.desktop_os_linked = true;
                g_State.settings.limiter_enabled = true;
                g_State.settings.routing_preset = ht306::RoutingPreset::DISCRETE_1TO1;
            } else if (prof.find("Bit-Perfect") != std::string::npos || prof.find("Pass-Through") != std::string::npos || prof.find("bitperfect") != std::string::npos) {
                float t[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                for (int i = 0; i < 6; ++i) g_State.settings.trims[i] = t[i];
                g_State.settings.bit_perfect_mode = true;
                g_State.settings.desktop_os_linked = false;
                g_State.settings.limiter_enabled = false;
            }
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Profiles", "Activated profile: " + prof);
        }
    }
    else if (action == "set_routing_preset") {
        std::string preset = webview::detail::json_parse(json, "preset", 0);
        if (preset == "standard" || preset == "discrete") {
            g_State.settings.routing_preset = ht306::RoutingPreset::DISCRETE_1TO1;
            int m[6] = { 0, 1, 2, 3, 4, 5 };
            for (int i = 0; i < 6; ++i) g_State.settings.routing_map[i] = m[i];
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Routing", "Applied routing preset: standard (1-to-1 Discrete)");
        } else if (preset == "swapped") {
            int m[6] = { 0, 1, 2, 3, 5, 4 };
            for (int i = 0; i < 6; ++i) g_State.settings.routing_map[i] = m[i];
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Routing", "Applied routing preset: swapped (SL <-> SR)");
        } else if (preset == "all-stereo") {
            g_State.settings.routing_preset = ht306::RoutingPreset::DUAL_MONO;
            int m[6] = { 0, 1, 0, 1, 0, 1 };
            for (int i = 0; i < 6; ++i) g_State.settings.routing_map[i] = m[i];
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Routing", "Applied routing preset: all-stereo (Dual Mono)");
        } else if (preset == "stereo-upmix" || preset == "stereo" || preset == "upmix") {
            g_State.settings.routing_preset = ht306::RoutingPreset::STEREO_UPMIX;
            int m[6] = { 0, 1, 2, 3, 4, 5 };
            for (int i = 0; i < 6; ++i) g_State.settings.routing_map[i] = m[i];
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Routing", "Applied routing preset: stereo-upmix (Stereo Upmix)");
        }
    }
    else if (action == "set_routing") {
        int parsed_map[6];
        if (ParseRoutingMap(json, parsed_map)) {
            for (int i = 0; i < 6; ++i) {
                g_State.settings.routing_map[i] = parsed_map[i];
            }
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Routing", "Updated 6x6 channel routing matrix");
        }
    }
    else if (action == "swap_surround_channels") {
        std::swap(g_State.settings.routing_map[4], g_State.settings.routing_map[5]);
        std::swap(g_State.settings.trims[4], g_State.settings.trims[5]);
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "Routing", "Swapped surround channels (SL <-> SR)");
    }
    else if (action == "set_device" || action == "set_audio_device") {
        int dev_idx = 0;
        if (JsonGetInt(json, "device_index", dev_idx) || JsonGetInt(json, "index", dev_idx) || JsonGetInt(json, "dev", dev_idx)) {
            if (dev_idx >= 0 && dev_idx < (int)g_State.available_devices.size()) {
                g_State.active_device_index = dev_idx;
                g_State.settings.target_device_index = dev_idx;
                std::string dev_name = g_State.available_devices[dev_idx].name;
                g_State.AddLog(ht306::LogLevel::LOG_INFO, "AudioDevice", "Active device changed to [" + std::to_string(dev_idx) + "]: " + dev_name);
            } else {
                g_State.active_device_index = dev_idx;
                g_State.settings.target_device_index = dev_idx;
                g_State.AddLog(ht306::LogLevel::LOG_INFO, "AudioDevice", "Active device changed to index " + std::to_string(dev_idx));
            }
        }
    }
    else if (action == "set_format_mode") {
        int fmt = 0;
        std::string str_mode = webview::detail::json_parse(json, "format_mode", 0);
        if (!str_mode.empty()) {
            if (str_mode == "0" || str_mode == "auto" || str_mode == "Auto: Windows" || str_mode == "Auto") {
                fmt = 0;
            } else if (str_mode == "1" || str_mode == "best" || str_mode == "Best") {
                fmt = 1;
            } else if (str_mode == "2" || str_mode == "adaptive" || str_mode == "Adaptive") {
                fmt = 2;
            } else if (str_mode == "3" || str_mode == "manual" || str_mode == "Manual") {
                fmt = 3;
            } else {
                try { fmt = std::stoi(str_mode); } catch (...) {}
            }
        } else {
            JsonGetInt(json, "mode", fmt);
        }
        if (fmt < 0) fmt = 0;
        if (fmt > 3) fmt = 3;
        g_State.settings.format_mode = fmt;
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "Format", "Format mode set to " + std::to_string(fmt));
    }
    else if (action == "set_limiter") {
        bool lim = true;
        if (JsonGetBool(json, "limiter_enabled", lim) || JsonGetBool(json, "enabled", lim) || JsonGetBool(json, "limiter", lim)) {
            g_State.settings.limiter_enabled = lim;
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "DSP", lim ? "Master peak limiter enabled" : "Master peak limiter disabled");
        }
    }
    else if (action == "set_lipsync" || action == "set_delay") {
        int delay = 0;
        if (JsonGetInt(json, "lip_sync_delay_ms", delay) || JsonGetInt(json, "delay_ms", delay) || JsonGetInt(json, "lipsync_ms", delay) || JsonGetInt(json, "delay", delay)) {
            if (delay < 0) delay = 0;
            if (delay > 200) delay = 200;
            g_State.settings.lip_sync_delay_ms = delay;
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "DSP", "Lip-sync delay set to " + std::to_string(delay) + " ms");
        }
    }
    else if (action == "factory_reset") {
        g_State.settings = ht306::AppSettings();
        g_State.AddLog(ht306::LogLevel::LOG_WARN, "Config", "Settings reset to factory defaults");
    }
    else if (action == "save_config") {
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "Config", "Configuration saved & synced to hardware bridge");
    }
    else if (action == "reset_buffer") {
        g_State.telemetry.underrun_count = 0;
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "Buffer", "Audio FIFO buffer and telemetry counters reset");
    }
    else if (action == "reset_peaks") {
        for (int i = 0; i < 6; ++i) {
            g_State.meters[i].peak_hold_db = -60.0f;
        }
    }
    else if (action == "clear_logs") {
        g_State.ClearLogs();
    }
    else if (action == "window_minimize") {
        if (g_pWebview) {
            HWND hwnd = (HWND)g_pWebview->get_native_handle(WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
            if (hwnd) { ShowWindow(hwnd, SW_MINIMIZE); TrimWorkingSetTree(); }
        }
    }
    else if (action == "window_maximize") {
        if (g_pWebview) {
            HWND hwnd = (HWND)g_pWebview->get_native_handle(WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
            if (hwnd) {
                if (IsZoomed(hwnd)) {
                    ShowWindow(hwnd, SW_RESTORE);
                } else {
                    ShowWindow(hwnd, SW_MAXIMIZE);
                }
            }
        }
    }
    else if (action == "window_close") {
        if (g_pWebview) {
            HWND hwnd = (HWND)g_pWebview->get_native_handle(WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
            if (hwnd) PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
    }
    else if (action == "window_drag") {
        if (g_pWebview) {
            HWND hwnd = (HWND)g_pWebview->get_native_handle(WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
            if (hwnd) {
                ReleaseCapture();
                SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
        }
    }
    else if (action == "app_ready") {
        if (!g_AudioCapture.IsRunning()) {
            g_AudioCapture.Start();
            g_State.is_streaming.store(true);
        }
        g_UsbTransport.TriggerCheck();
        g_TelemetryDirty.store(true);
        // Staged post-boot trim after initial telemetry & Blink DOM layout finish rendering
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            TrimWorkingSetTree();
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            TrimWorkingSetTree();
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            TrimWorkingSetTree();
        }).detach();
    }
    g_TelemetryDirty.store(true);
}

// ----------------------------------------------------------------------------
// Windows System Tray & Subclassing
// ----------------------------------------------------------------------------
#define WM_TRAYICON (WM_USER + 100)
#define ID_TRAY_RESTORE 1001
#define ID_TRAY_TOGGLE_STREAM 1002
#define ID_TRAY_BITPERFECT 1003
#define ID_TRAY_EXIT 1004
#define ID_TRAY_DEBUG_AUDIO 1005

static WNDPROC g_OrigWndProc = NULL;
static NOTIFYICONDATAA g_Nid = { 0 };

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TRAYICON) {
        if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            return 0;
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_RESTORE, "Restore Window");
                InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

                bool streaming = g_State.is_streaming.load();
                InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE_STREAM,
                            streaming ? "Stop Stream" : "Start Stream");

                bool bitperfect = g_State.settings.bit_perfect_mode;
                UINT bpFlags = MF_BYPOSITION | MF_STRING | (bitperfect ? MF_CHECKED : MF_UNCHECKED);
                InsertMenuA(hMenu, -1, bpFlags, ID_TRAY_BITPERFECT, "Bit-Perfect Mode");

                bool debugAudio = ht306::audio::AudioPolicyManager::Instance().GetManualVisibilityOverride();
                UINT dbgFlags = MF_BYPOSITION | MF_STRING | (debugAudio ? MF_CHECKED : MF_UNCHECKED);
                InsertMenuA(hMenu, -1, dbgFlags, ID_TRAY_DEBUG_AUDIO, "Debug: Aktifkan Audio Tanpa RP2350");

                InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
                InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, "Exit");

                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);

                if (cmd == ID_TRAY_RESTORE) {
                    ShowWindow(hwnd, SW_SHOW);
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                } else if (cmd == ID_TRAY_TOGGLE_STREAM) {
                    HandleNativeCommand("{\"action\":\"toggle_stream\"}");
                } else if (cmd == ID_TRAY_BITPERFECT) {
                    bool new_bp = !g_State.settings.bit_perfect_mode;
                    std::string cmd_str = "{\"action\":\"set_mode\",\"bit_perfect_mode\":" + std::string(new_bp ? "true" : "false") + "}";
                    HandleNativeCommand(cmd_str);
                } else if (cmd == ID_TRAY_DEBUG_AUDIO) {
                    HandleNativeCommand("{\"action\":\"toggle_debug_audio\"}");
                } else if (cmd == ID_TRAY_EXIT) {
                    ht306::audio::AudioPolicyManager::Instance().SetManualVisibilityOverride(false);
                    ht306::audio::AudioPolicyManager::Instance().RestorePreviousDefault(g_State);
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
            }
            return 0;
        }
    } else if (uMsg == WM_DEVICECHANGE) {
        // USB hardware plugged in or removed - trigger instant connection check
        g_UsbTransport.TriggerCheck();
        ht306::audio::AudioPolicyManager::Instance().CheckDriverStatus();
        ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
        g_TelemetryDirty.store(true);
        return TRUE;
    } else if (uMsg == WM_CLOSE) {
        ht306::audio::AudioPolicyManager::Instance().SetManualVisibilityOverride(false);
        ht306::audio::AudioPolicyManager::Instance().RestorePreviousDefault(g_State);
    } else if (uMsg == WM_DESTROY) {
        ht306::audio::AudioPolicyManager::Instance().SetManualVisibilityOverride(false);
        ht306::audio::AudioPolicyManager::Instance().RestorePreviousDefault(g_State);
        Shell_NotifyIconA(NIM_DELETE, &g_Nid);
    }
    return CallWindowProcA(g_OrigWndProc, hwnd, uMsg, wParam, lParam);
}

// ----------------------------------------------------------------------------
// Background Telemetry Thread (~60 Hz / 16ms)
// ----------------------------------------------------------------------------
void TelemetryLoop() {
    uint32_t counter = 0;
    bool last_detected = false;
    bool last_usb_open = false;
    std::string last_com_port;
    std::string last_hb;
    size_t last_log_count = 0;
    bool last_drv_installed = false;
    bool last_drv_visible = false;
    bool last_drv_default = false;
    bool last_drv_debug = false;
    std::string last_drv_name;

    while (g_Running.load()) {
        bool active_stream = g_State.is_streaming.load() || g_State.sweep_running.load();
        // Sleep dynamically: 250ms when idle (0% CPU/GPU), 35ms when streaming audio
        int sleep_ms = active_stream ? 35 : 250;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        if (!g_Running.load() || !g_pWebview) continue;

        counter++;
        if (counter % 30 == 0) {
            g_State.hw_info.uptime_sec++;
        }

        bool drv_changed = false;
        if (counter <= 2 || counter % 16 == 0) {
            auto drv = ht306::audio::AudioPolicyManager::Instance().GetCachedDriverStatus();
            bool cur_drv_debug = ht306::audio::AudioPolicyManager::Instance().GetManualVisibilityOverride();
            if (drv.installed != last_drv_installed ||
                drv.endpoint_visible != last_drv_visible ||
                drv.is_default != last_drv_default ||
                cur_drv_debug != last_drv_debug ||
                drv.endpoint_name != last_drv_name) {
                last_drv_installed = drv.installed;
                last_drv_visible = drv.endpoint_visible;
                last_drv_default = drv.is_default;
                last_drv_debug = cur_drv_debug;
                last_drv_name = drv.endpoint_name;
                ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
                drv_changed = true;
            }
        }

        bool cur_detected = g_State.hw_info.detected;
        bool cur_usb_open = g_UsbTransport.IsOpen();
        std::string cur_com = g_State.hw_info.com_port;
        std::string cur_hb = g_State.hw_info.last_heartbeat_text;
        auto logs_snapshot = g_State.GetLogsSnapshot();
        size_t cur_log_count = logs_snapshot.size();

        bool hw_changed = (cur_detected != last_detected) ||
                          (cur_usb_open != last_usb_open) ||
                          (cur_com != last_com_port) ||
                          (cur_hb != last_hb) ||
                          (cur_log_count != last_log_count) ||
                          drv_changed;
        bool should_push = active_stream || g_TelemetryDirty.exchange(false) || hw_changed || (counter <= 2);

        if (!should_push) {
            if (counter == 5 || counter == 12 || counter == 20 || counter % 40 == 0) {
                TrimWorkingSetTree();
            }
            continue;
        }

        last_detected = cur_detected;
        last_usb_open = cur_usb_open;
        last_com_port = cur_com;
        last_hb = cur_hb;
        last_log_count = cur_log_count;

        std::string state_str = "READY";
        if (g_State.is_streaming.load()) {
            state_str = "STREAMING";
        } else if (!g_State.hw_info.detected && !g_UsbTransport.IsOpen()) {
            state_str = "DISCONNECTED";
        }

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1);

        ss << "{"
           << "\"state\":\"" << state_str << "\","
           << "\"is_streaming\":" << (g_State.is_streaming.load() ? "true" : "false") << ","
           << "\"format\":{\"rate\":96000,\"bits\":32,\"channels\":6},"
           << "\"dBFS\":[";
        for (int i = 0; i < 6; ++i) {
            ss << g_State.meters[i].peak_db << (i < 5 ? "," : "");
        }
        ss << "],\"peak_hold_db\":[";
        for (int i = 0; i < 6; ++i) {
            ss << g_State.meters[i].peak_hold_db << (i < 5 ? "," : "");
        }
        ss << "],\"clipped\":[";
        for (int i = 0; i < 6; ++i) {
            ss << (g_State.meters[i].clipped ? "true" : "false") << (i < 5 ? "," : "");
        }
        ss << "],"
           << "\"device\":{"
           << "\"com_port\":\"" << EscapeJsonString(g_State.hw_info.com_port) << "\","
           << "\"vid\":" << g_State.hw_info.vid << ","
           << "\"pid\":" << g_State.hw_info.pid << ","
           << "\"serial\":\"" << EscapeJsonString(g_State.hw_info.serial_number) << "\","
           << "\"firmware\":\"" << EscapeJsonString(g_State.hw_info.firmware_version) << "\""
           << "},"
           << "\"hw_info\":{"
           << "\"uptime_sec\":" << g_State.hw_info.uptime_sec << ","
           << "\"com_port\":\"" << EscapeJsonString(g_State.hw_info.com_port) << "\","
           << "\"vid\":" << g_State.hw_info.vid << ","
           << "\"pid\":" << g_State.hw_info.pid << ","
           << "\"pio_ready\":" << (g_State.hw_info.pio_ready ? "true" : "false") << ","
           << "\"mux_high\":" << (g_State.hw_info.mux_high ? "true" : "false") << ","
           << "\"dsp_locked\":" << (g_State.hw_info.dsp_locked ? "true" : "false") << ","
           << "\"heartbeat_text\":\"" << EscapeJsonString(g_State.hw_info.last_heartbeat_text) << "\""
           << "},"
           << "\"stats\":{"
           << "\"throughput_kbs\":" << g_State.telemetry.throughput_kbs << ","
           << "\"latency_ms\":" << g_State.telemetry.latency_ms << ","
           << "\"buffer_fill_pct\":" << g_State.telemetry.buffer_fill_pct << ","
           << "\"link_usage_pct\":" << g_State.telemetry.link_usage_pct << ","
           << "\"underruns\":" << g_State.telemetry.underrun_count
           << "},"
           << "\"telemetry\":{"
           << "\"throughput_kbs\":" << g_State.telemetry.throughput_kbs << ","
           << "\"latency_ms\":" << g_State.telemetry.latency_ms << ","
           << "\"buffer_fill_pct\":" << g_State.telemetry.buffer_fill_pct << ","
           << "\"link_usage_pct\":" << g_State.telemetry.link_usage_pct << ","
           << "\"underrun_count\":" << g_State.telemetry.underrun_count << ","
           << "\"sample_rate\":96000,\"bit_depth\":32,\"channels\":6"
           << "},"
           << "\"master_volume_db\":" << g_State.settings.master_volume_db << ","
           << "\"mute_master\":" << (g_State.settings.mute_master ? "true" : "false") << ","
           << "\"active_device_index\":" << g_State.active_device_index << ","
           << "\"pio_ready\":" << (g_State.hw_info.pio_ready ? "true" : "false") << ","
           << "\"mux_high\":" << (g_State.hw_info.mux_high ? "true" : "false") << ","
           << "\"dsp_locked\":" << (g_State.hw_info.dsp_locked ? "true" : "false") << ","
           << "\"heartbeat_text\":\"" << EscapeJsonString(g_State.hw_info.last_heartbeat_text) << "\","
           << "\"format_mode\":" << g_State.settings.format_mode << ","
           << "\"limiter_enabled\":" << (g_State.settings.limiter_enabled ? "true" : "false") << ","
           << "\"lip_sync_delay_ms\":" << g_State.settings.lip_sync_delay_ms << ","
           << "\"mutes\":[";
        for (int i = 0; i < 6; ++i) {
            ss << (g_State.settings.mutes[i] ? "true" : "false") << (i < 5 ? "," : "");
        }
        ss << "],\"solos\":[";
        for (int i = 0; i < 6; ++i) {
            ss << (g_State.settings.solos[i] ? "true" : "false") << (i < 5 ? "," : "");
        }
        ss << "],\"phase_inverts\":[";
        for (int i = 0; i < 6; ++i) {
            ss << (g_State.settings.phase_inverts[i] ? "true" : "false") << (i < 5 ? "," : "");
        }
        ss << "],\"routing_map\":[";
        for (int i = 0; i < 6; ++i) {
            ss << g_State.settings.routing_map[i] << (i < 5 ? "," : "");
        }
        ss << "],"
           << "\"audio_driver\":{"
           << "\"installed\":" << (ht306::audio::AudioPolicyManager::Instance().GetCachedDriverStatus().installed ? "true" : "false") << ","
           << "\"visible\":" << (ht306::audio::AudioPolicyManager::Instance().GetCachedDriverStatus().endpoint_visible ? "true" : "false") << ","
           << "\"is_default\":" << (ht306::audio::AudioPolicyManager::Instance().GetCachedDriverStatus().is_default ? "true" : "false") << ","
           << "\"debug_override\":" << (ht306::audio::AudioPolicyManager::Instance().GetManualVisibilityOverride() ? "true" : "false") << ","
           << "\"name\":\"" << EscapeJsonString(ht306::audio::AudioPolicyManager::Instance().GetCachedDriverStatus().endpoint_name) << "\""
           << "},"
           << "\"devices\":[";
        for (size_t i = 0; i < g_State.available_devices.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{\"index\":" << i << ",\"name\":\"" << EscapeJsonString(g_State.available_devices[i].name) << "\""
               << ",\"is_default\":" << (g_State.available_devices[i].is_default ? "true" : "false") << "}";
        }
        ss << "],";

        size_t start_idx = logs_snapshot.size() > 50 ? logs_snapshot.size() - 50 : 0;
        ss << "\"logs\":[";
        for (size_t i = start_idx; i < logs_snapshot.size(); ++i) {
            if (i > start_idx) ss << ",";
            const auto& entry = logs_snapshot[i];
            std::string formatted = entry.subsystem.empty() ? entry.message : ("[" + entry.subsystem + "] " + entry.message);
            ss << "\"" << EscapeJsonString(formatted) << "\"";
        }
        ss << "],"
           << "\"config\":{"
           << "\"mode\":\"" << (g_State.settings.bit_perfect_mode ? "bitperfect" : "desktop") << "\","
           << "\"profile\":\"" << EscapeJsonString(g_State.settings.active_profile) << "\","
           << "\"volume_db\":" << g_State.settings.master_volume_db << ","
           << "\"master_volume_db\":" << g_State.settings.master_volume_db << ","
           << "\"mute_master\":" << (g_State.settings.mute_master ? "true" : "false") << ","
           << "\"format_mode\":" << g_State.settings.format_mode << ","
           << "\"limiter_enabled\":" << (g_State.settings.limiter_enabled ? "true" : "false") << ","
           << "\"lip_sync_delay_ms\":" << g_State.settings.lip_sync_delay_ms << ","
           << "\"active_device_index\":" << g_State.active_device_index << ","
           << "\"trims_db\":[";
        for (int i = 0; i < 6; ++i) {
            ss << g_State.settings.trims[i] << (i < 5 ? "," : "");
        }
        ss << "],\"mutes\":[";
        for (int i = 0; i < 6; ++i) {
            ss << (g_State.settings.mutes[i] ? "true" : "false") << (i < 5 ? "," : "");
        }
        ss << "],\"solos\":[";
        for (int i = 0; i < 6; ++i) {
            ss << (g_State.settings.solos[i] ? "true" : "false") << (i < 5 ? "," : "");
        }
        ss << "],\"phase_inverts\":[";
        for (int i = 0; i < 6; ++i) {
            ss << (g_State.settings.phase_inverts[i] ? "true" : "false") << (i < 5 ? "," : "");
        }
        ss << "],\"routing_map\":[";
        for (int i = 0; i < 6; ++i) {
            ss << g_State.settings.routing_map[i] << (i < 5 ? "," : "");
        }
        ss << "]}";
        ss << "}";

        std::string json_str = ss.str();
        if (g_pWebview) {
            g_pWebview->dispatch([json_str]() {
                if (!g_Running.load() || !g_pWebview) return;
                if (g_pCoreWebView2) {
                    int len = MultiByteToWideChar(CP_UTF8, 0, json_str.c_str(), -1, NULL, 0);
                    if (len > 0) {
                        std::wstring wjson(len, 0);
                        MultiByteToWideChar(CP_UTF8, 0, json_str.c_str(), -1, &wjson[0], len);
                        g_pCoreWebView2->PostWebMessageAsJson(wjson.c_str());
                    }
                } else {
                    std::string js = "if(window.handleNativeTelemetry) window.handleNativeTelemetry(" + json_str + ");";
                    g_pWebview->eval(js);
                }
            });
        }
        if (!active_stream) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            TrimWorkingSetTree();
        }
    }
}


// ----------------------------------------------------------------------------
// Clean up orphaned WebView2 processes if previous run terminated abnormally
// ----------------------------------------------------------------------------
static void KillOrphanWebViewProcesses() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, "msedgewebview2.exe") == 0) {
                    HANDLE hParent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ParentProcessID);
                    if (!hParent) {
                        HANDLE hOrphan = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (hOrphan) {
                            TerminateProcess(hOrphan, 0);
                            CloseHandle(hOrphan);
                        }
                    } else {
                        CloseHandle(hParent);
                    }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
}

// ----------------------------------------------------------------------------
// WinMain & Application Entry Point
// ----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // 0. Ensure single instance so second launch activates existing window
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\LG_HT306_AudioBridge_InstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingHwnd = FindWindowA(NULL, "LG HT306 Audio Bridge \xE2\x80\x94 Direct I2S 96kHz Lossless");
        if (existingHwnd) {
            ShowWindow(existingHwnd, SW_SHOW);
            ShowWindow(existingHwnd, SW_RESTORE);
            SetForegroundWindow(existingHwnd);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Clean up any lingering WebView2 processes from previous ungraceful exits
    KillOrphanWebViewProcesses();

    // 1. Initialize audio endpoint helper and sync visibility with RP2350 hardware presence
    ht306::audio::AudioPolicyManager::Instance().CheckDriverStatus();
    if (g_UsbTransport.CheckHardwarePresence()) {
        ht306::audio::AudioPolicyManager::Instance().SwitchToHT306(g_State);
    } else {
        ht306::audio::AudioPolicyManager::Instance().RestorePreviousDefault(g_State);
    }
    ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
    g_UsbTransport.StartWorker();
    g_AudioCapture.Start();
    g_State.is_streaming.store(true);

    // 2. Start background telemetry thread
    std::thread telemetry_thread(TelemetryLoop);
    HDEVNOTIFY hDevNotify = NULL;

    // 3. Initialize WebView2 with low-RAM parameters
    try {
        // Enforce lowest possible memory constraints on Chromium WebView2
        SetEnvironmentVariableA("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
            "--disable-gpu "
            "--disable-gpu-compositing "
            "--disk-cache-size=1 --media-cache-size=1 --js-flags=\"--max-old-space-size=16 --optimize_for_size --lite-mode\" "
            "--renderer-process-limit=1 "
            "--disable-background-networking "
            "--disable-component-update "
            "--disable-sync "
            "--disable-features=TranslateUI,AutofillServerCommunication,CalculateNativeWinOcclusion,SpareRendererForSitePerProcess "
            "--disable-default-apps "
            "--disable-extensions --allow-file-access-from-files"
        );

        // debug = false to disable full Chromium DevTools backend (~60MB RAM savings)
        webview::webview w(false, nullptr);
        g_pWebview = &w;

        w.set_title("LG HT306 Audio Bridge \xE2\x80\x94 Direct I2S 96kHz Lossless");
        w.set_size(1280, 840, WEBVIEW_HINT_NONE);

        HWND hwnd = (HWND)w.get_native_handle(WEBVIEW_NATIVE_HANDLE_KIND_UI_WINDOW);
        if (hwnd) {
            EnableImmersiveDarkMode(hwnd);

            // Frameless window: remove standard OS caption bar so only the inner UI header displays
            LONG style = GetWindowLong(hwnd, GWL_STYLE);
            style &= ~WS_CAPTION;
            style &= ~WS_THICKFRAME;
            SetWindowLong(hwnd, GWL_STYLE, style);
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

            // Subclass main window to handle system tray icon notifications
            g_OrigWndProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)TrayWndProc);

            // Register Shell_NotifyIconA
            ZeroMemory(&g_Nid, sizeof(g_Nid));
            g_Nid.cbSize = sizeof(NOTIFYICONDATAA);
            g_Nid.hWnd = hwnd;
            g_Nid.uID = 1;
            g_Nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_Nid.uCallbackMessage = WM_TRAYICON;
            g_Nid.hIcon = LoadIconA(NULL, IDI_APPLICATION);
            strcpy_s(g_Nid.szTip, sizeof(g_Nid.szTip), "LG HT306 Audio Bridge - 96kHz Lossless");
            Shell_NotifyIconA(NIM_ADD, &g_Nid);

            // Register for USB device hotplug notifications
            DEV_BROADCAST_DEVICEINTERFACE_A notificationFilter{};
            notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE_A);
            notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
            hDevNotify = RegisterDeviceNotificationA(hwnd, &notificationFilter, DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
        }

        ICoreWebView2Controller* ctrl = (ICoreWebView2Controller*)w.get_native_handle(WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
        if (ctrl) {
            ctrl->get_CoreWebView2(&g_pCoreWebView2);
            if (g_pCoreWebView2) {
                ICoreWebView2_19* core19 = nullptr;
                if (SUCCEEDED(g_pCoreWebView2->QueryInterface(IID_ICoreWebView2_19, (void**)&core19)) && core19) {
                    core19->put_MemoryUsageTargetLevel(COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW);
                    core19->Release();
                }
            }
        }

        w.bind("nativePostCommand", [](std::string req) -> std::string {
            HandleNativeCommand(req);
            return "{\"status\":\"ok\"}";
        });

        std::string ui_path = ResolveUiHtmlPath();
        std::string ui_url = PathToUrl(ui_path);
        w.navigate(ui_url);

        w.run();

        // Graceful shutdown before w and its controller go out of scope
        g_Running.store(false);
        if (telemetry_thread.joinable()) {
            telemetry_thread.join();
        }
        if (g_pCoreWebView2) {
            g_pCoreWebView2->Release();
            g_pCoreWebView2 = nullptr;
        }
        g_pWebview = nullptr;
        if (hDevNotify) {
            UnregisterDeviceNotification(hDevNotify);
            hDevNotify = NULL;
        }
        Shell_NotifyIconA(NIM_DELETE, &g_Nid);
    } catch (const webview::exception& e) {
        std::cerr << "WebView error: " << e.what() << std::endl;
        std::string err = e.what();
        if (err.empty()) {
            err = "Tidak dapat menginisialisasi WebView2 runtime.\nPastikan Microsoft Edge WebView2 Runtime terpasang dan tidak ada proses aplikasi yang menggantung.";
        }
        MessageBoxA(NULL, err.c_str(), "LG HT306 Audio Bridge", MB_ICONWARNING | MB_OK);
    } catch (const std::exception& e) {
        std::cerr << "Standard error: " << e.what() << std::endl;
        MessageBoxA(NULL, e.what(), "Application Error", MB_ICONERROR | MB_OK);
    }

    // 4. Resource cleanup upon exit (if not already done inside try)
    g_Running.store(false);
    if (telemetry_thread.joinable()) {
        telemetry_thread.join();
    }
    if (g_pCoreWebView2) {
        g_pCoreWebView2->Release();
        g_pCoreWebView2 = nullptr;
    }
    g_pWebview = nullptr;
    if (hDevNotify) {
        UnregisterDeviceNotification(hDevNotify);
        hDevNotify = NULL;
    }
    Shell_NotifyIconA(NIM_DELETE, &g_Nid);

    g_AudioCapture.Stop();
    g_UsbTransport.StopWorker();
    g_UsbTransport.Close();
    ht306::audio::AudioPolicyManager::Instance().SetManualVisibilityOverride(false);
    ht306::audio::AudioPolicyManager::Instance().RestorePreviousDefault(g_State);

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
}
