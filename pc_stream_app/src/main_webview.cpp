// ============================================================================
// LG HT306 Audio Bridge - Direct I2S 96kHz Lossless
// WebView2 Desktop Application Host & Native IPC Bridge
// ============================================================================

#define WEBVIEW_BUILD 1
#define WEBVIEW_MSWEBVIEW2_BUILTIN_IMPL 1

#include <windows.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <shellapi.h>
#include "WebView2.h"
#include "webview/webview.h"
#include "app_state.h"
#include "audio_capture.h"
#include "usb_transport.h"
#include "audio_endpoint_helper.h"
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
        std::string sw_action = webview::detail::json_parse(json, "action", 0);
        if (sw_action.empty()) sw_action = webview::detail::json_parse(json, "cmd", 0);
        std::string ch_str = webview::detail::json_parse(json, "channel", 0);
        int ch_idx = ParseChannelIndex(ch_str);
        float lvl = -20.0f;
        JsonGetFloat(json, "level_db", lvl);

        if (sw_action == "stop" || sw_action == "pause") {
            g_State.sweep_running.store(false);
            g_State.sweep_active_channel.store(-1);
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "SweepTest", "5.1 acoustic sweep paused/idle");
        } else {
            if (ch_idx < 0) ch_idx = 0;
            g_State.sweep_active_channel.store(ch_idx);
            g_State.sweep_running.store(true);
            g_State.test_signal_type = 1; // Pink Noise generator
            g_State.test_level_dbfs = lvl;
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "SweepTest", "Pink noise acoustic test active on channel " + std::to_string(ch_idx));
        }
    }
    else if (action == "open_vbcable" || action == "open_vbcable_installer" || action == "install_driver") {
        std::string vbcable_path = FindAssetPath("pc_stream_app\\tools\\vbcable");
        if (GetFileAttributesA(vbcable_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            vbcable_path = FindAssetPath("tools\\vbcable");
        }
        ShellExecuteA(NULL, "open", vbcable_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        g_State.AddLog(ht306::LogLevel::LOG_INFO, "Driver", "Opened VB-Cable directory: " + vbcable_path);
    }
    else if (action == "reboot_bootsel") {
        std::string port_str = "\\\\.\\" + g_State.hw_info.com_port;
        HANDLE hPort = CreateFileA(port_str.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPort != INVALID_HANDLE_VALUE) {
            DCB dcb = { 0 };
            dcb.DCBlength = sizeof(DCB);
            if (GetCommState(hPort, &dcb)) {
                dcb.BaudRate = CBR_1200;
                dcb.ByteSize = 8;
                dcb.Parity = NOPARITY;
                dcb.StopBits = ONESTOPBIT;
                SetCommState(hPort, &dcb);
            }
            CloseHandle(hPort);
        }
        g_State.AddLog(ht306::LogLevel::LOG_WARN, "RP2350", "Requested BOOTSEL reset via CDC-ACM 1200 baud touch on " + g_State.hw_info.com_port);
    }
    else if (action == "set_profile") {
        std::string prof = webview::detail::json_parse(json, "profile", 0);
        if (!prof.empty()) {
            g_State.settings.active_profile = prof;
            g_State.AddLog(ht306::LogLevel::LOG_INFO, "Profiles", "Activated profile: " + prof);
        }
    }
    else if (action == "reset_peaks") {
        for (int i = 0; i < 6; ++i) {
            g_State.meters[i].peak_hold_db = -60.0f;
        }
    }
    else if (action == "clear_logs") {
        g_State.logs.clear();
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
        // Instant post-boot trim after Blink DOM and layout finish rendering (~800ms)
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            TrimWorkingSetTree();
        }).detach();
    }
}

// ----------------------------------------------------------------------------
// Background Telemetry Thread (~60 Hz / 16ms)
// ----------------------------------------------------------------------------
void TelemetryLoop() {
    uint32_t counter = 0;
    while (g_Running.load()) {
        // Sleep dynamically: 250ms when idle (0% CPU/GPU), 35ms when streaming audio
        int sleep_ms = g_State.is_streaming.load() ? 35 : 250;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        if (!g_Running.load() || !g_pWebview) continue;

        counter++;
        // Staged cold-start trimming:
        // counter == 5  (~1.25s): crushes initial DOM & Blink bootstrap spike
        // counter == 12 (~3.00s): crushes Google Fonts & DirectWrite glyph decode cache
        // counter == 20 (~5.00s): crushes CSS rasterization & network service buffers
        if (counter == 5 || counter == 12 || counter == 20) {
            TrimWorkingSetTree();
        }
        // Periodic active working set trimming every ~10s when idle
        if (!g_State.is_streaming.load() && counter % 40 == 0) {
            TrimWorkingSetTree();
        }
        if (counter % 30 == 0) {
            g_State.hw_info.uptime_sec++;
        }

        std::string state_str = "READY";
        if (g_State.is_streaming.load()) {
            state_str = "STREAMING";
        } else if (!g_UsbTransport.IsOpen()) {
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
        ss << "],"
           << "\"device\":{"
           << "\"com_port\":\"" << g_State.hw_info.com_port << "\","
           << "\"vid\":" << g_State.hw_info.vid << ","
           << "\"pid\":" << g_State.hw_info.pid << ","
           << "\"serial\":\"" << g_State.hw_info.serial_number << "\","
           << "\"firmware\":\"" << g_State.hw_info.firmware_version << "\""
           << "},"
           << "\"hw_info\":{"
           << "\"uptime_sec\":" << g_State.hw_info.uptime_sec << ","
           << "\"com_port\":\"" << g_State.hw_info.com_port << "\","
           << "\"vid\":" << g_State.hw_info.vid << ","
           << "\"pid\":" << g_State.hw_info.pid
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
           << "\"config\":{"
           << "\"mode\":\"" << (g_State.settings.bit_perfect_mode ? "bitperfect" : "desktop") << "\","
           << "\"profile\":\"" << g_State.settings.active_profile << "\","
           << "\"volume_db\":" << g_State.settings.master_volume_db << ","
           << "\"trims_db\":[";
        for (int i = 0; i < 6; ++i) {
            ss << g_State.settings.trims[i] << (i < 5 ? "," : "");
        }
        ss << "]}";
        ss << "}";

        std::string json_str = ss.str();
        if (g_Running.load()) {
            if (g_pCoreWebView2) {
                int len = MultiByteToWideChar(CP_UTF8, 0, json_str.c_str(), -1, NULL, 0);
                if (len > 0) {
                    std::wstring wjson(len, 0);
                    MultiByteToWideChar(CP_UTF8, 0, json_str.c_str(), -1, &wjson[0], len);
                    g_pCoreWebView2->PostWebMessageAsJson(wjson.c_str());
                }
            } else if (g_pWebview) {
                g_pWebview->dispatch([json_str]() {
                    if (g_pWebview) {
                        std::string js = "if(window.handleNativeTelemetry) window.handleNativeTelemetry(" + json_str + ");";
                        g_pWebview->eval(js);
                    }
                });
            }
        }
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

    // 1. Initialize audio endpoint helper and detect device
    ht306::audio::AudioEndpointHelper::EnumerateDevices(g_State);
    g_UsbTransport.StartWorker();

    // 2. Start background telemetry thread
    std::thread telemetry_thread(TelemetryLoop);

    // 3. Initialize WebView2 with low-RAM parameters
    try {
        // Enforce lowest possible memory constraints on Chromium WebView2
        SetEnvironmentVariableA("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
            "--disable-gpu "
            "--disable-gpu-compositing "
            "--disk-cache-size=1 --media-cache-size=1 --js-flags=\"--expose-gc --max-old-space-size=16 --optimize_for_size --lite-mode\" "
            "--renderer-process-limit=1 "
            "--disable-background-networking "
            "--disable-component-update "
            "--disable-sync "
            "--disable-features=TranslateUI,AutofillServerCommunication,CalculateNativeWinOcclusion,SpareRendererForSitePerProcess "
            "--disable-default-apps "
            "--disable-extensions"
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
    } catch (const webview::exception& e) {
        std::cerr << "WebView error: " << e.what() << std::endl;
        MessageBoxA(NULL, e.what(), "WebView2 Initialization Error", MB_ICONERROR | MB_OK);
    } catch (const std::exception& e) {
        std::cerr << "Standard error: " << e.what() << std::endl;
        MessageBoxA(NULL, e.what(), "Application Error", MB_ICONERROR | MB_OK);
    }

    // 4. Resource cleanup upon exit
    g_Running.store(false);
    g_pWebview = nullptr;

    if (telemetry_thread.joinable()) {
        telemetry_thread.join();
    }

    g_AudioCapture.Stop();
    g_UsbTransport.StopWorker();
    g_UsbTransport.Close();

    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
}
