// ============================================================================
// LG HT306 Audio Bridge - Windows Audio Policy & Endpoint Manager
// Manages virtual audio driver lifecycle, COM default endpoint switching,
// and 5.1 surround 96kHz format configuration on Windows 10 & 11
// ============================================================================

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app_state.h"
#include <windows.h>
#include <string>
#include <mutex>

namespace ht306::audio {

struct AudioDriverStatus {
    bool installed = false;
    bool is_default = false;
    std::string endpoint_name;
    std::wstring endpoint_id;
};

class AudioPolicyManager {
public:
    static AudioPolicyManager& Instance();

    // Query whether VB-CABLE or dedicated HT306 virtual endpoint exists (runs COM scan and updates cache)
    AudioDriverStatus CheckDriverStatus();

    // Fast, non-blocking getter returning cached status without COM I/O storm
    AudioDriverStatus GetCachedDriverStatus();

    // Execute unattended setup with UAC elevation
    bool InstallDriver(HWND parent_hwnd);

    // Sync and auto-rename audio endpoints in Windows registry and hide 16ch
    bool SyncEndpointNames();

    // Switch default audio playback device in Windows taskbar
    bool SetDefaultEndpoint(const std::wstring& device_id);

    // Auto-switch to LG HT306 when hardware is connected
    bool SwitchToHT306(AppState& state);

    // Restore previous playback device when hardware is disconnected
    bool RestorePreviousDefault(AppState& state);

    // Get current Windows default audio endpoint ID
    std::wstring GetCurrentDefaultEndpointId();

    // Configure 5.1 surround (6 channels, 96 kHz, 24/32-bit)
    bool ConfigureEndpointSurround96k(const std::wstring& device_id);

    // Rename endpoint friendly name in Windows Audio Property Store
    bool RenameEndpoint(const std::wstring& device_id, const std::wstring& new_name);

private:
    AudioPolicyManager() = default;
    ~AudioPolicyManager() = default;
    AudioPolicyManager(const AudioPolicyManager&) = delete;
    AudioPolicyManager& operator=(const AudioPolicyManager&) = delete;

    std::recursive_mutex m_mutex;
    std::wstring m_saved_previous_device_id;
    bool m_switched_to_ht306 = false;
    AudioDriverStatus m_cached_status;
    uint64_t m_last_check_tick = 0;
};

} // namespace ht306::audio
