// ============================================================================
// LG HT306 Audio Bridge - Windows Audio Policy & Endpoint Manager
// ============================================================================

#include "audio_policy_manager.h"
#include "policy_config.h"
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <iostream>
#include <algorithm>
#include <vector>

#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
static const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_GUID = {
    0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_GUID
#endif

namespace ht306::audio {

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

[[maybe_unused]] static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

AudioPolicyManager& AudioPolicyManager::Instance() {
    static AudioPolicyManager instance;
    return instance;
}

std::wstring AudioPolicyManager::GetCurrentDefaultEndpointId() {
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    std::wstring default_id;

    IMMDeviceEnumerator* enumerator = NULL;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (SUCCEEDED(hr) && enumerator) {
        IMMDevice* default_device = NULL;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &default_device)) && default_device) {
            LPWSTR dev_id = NULL;
            if (SUCCEEDED(default_device->GetId(&dev_id)) && dev_id) {
                default_id = dev_id;
                CoTaskMemFree(dev_id);
            }
            default_device->Release();
        }
        enumerator->Release();
    }

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
    return default_id;
}

AudioDriverStatus AudioPolicyManager::CheckDriverStatus() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // Automatically ensure VB-Cable endpoints are renamed to LG HT306 Home Theater and 16ch is disabled
    SyncEndpointNames();

    AudioDriverStatus status;
    status.installed = false;
    status.endpoint_visible = false;
    status.is_default = false;

    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    std::wstring current_default = GetCurrentDefaultEndpointId();

    IMMDeviceEnumerator* enumerator = NULL;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (SUCCEEDED(hr) && enumerator) {
        // 1. Direct O(1) lookup by resolved VB-Cable GUID if available from SyncEndpointNames()
        if (!m_ht306_guid.empty()) {
            std::wstring target_id = L"{0.0.0.00000000}." + m_ht306_guid;
            IMMDevice* device = NULL;
            if (SUCCEEDED(enumerator->GetDevice(target_id.c_str(), &device)) && device) {
                DWORD dwState = 0;
                device->GetState(&dwState);
                status.installed = true;
                status.endpoint_visible = (dwState == DEVICE_STATE_ACTIVE);
                status.endpoint_id = target_id;
                status.endpoint_name = "LG HT306 Home Theater (5.1 Surround)";

                IPropertyStore* props = NULL;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
                    PROPVARIANT var_name;
                    PropVariantInit(&var_name);
                    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var_name)) && var_name.pwszVal) {
                        std::string name = WideToUtf8(var_name.pwszVal);
                        if (!name.empty()) {
                            status.endpoint_name = name;
                        }
                    }
                    PropVariantClear(&var_name);
                    props->Release();
                }

                if (status.endpoint_visible && !current_default.empty() && current_default == target_id) {
                    status.is_default = true;
                }
                device->Release();
            }
        }

        // 2. Fallback enumeration if direct GUID lookup did not resolve
        if (!status.installed) {
            IMMDeviceCollection* collection = NULL;
            hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL, &collection);
            if (SUCCEEDED(hr) && collection) {
                UINT count = 0;
                collection->GetCount(&count);

                for (UINT i = 0; i < count; ++i) {
                    IMMDevice* device = NULL;
                    if (SUCCEEDED(collection->Item(i, &device)) && device) {
                        LPWSTR dev_id = NULL;
                        device->GetId(&dev_id);

                        DWORD dwState = 0;
                        device->GetState(&dwState);

                        IPropertyStore* props = NULL;
                        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
                            PROPVARIANT var_name;
                            PropVariantInit(&var_name);
                            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var_name)) && var_name.pwszVal) {
                                std::string name = WideToUtf8(var_name.pwszVal);
                                
                                // Check specifically for LG HT306 or VB-CABLE Input (exclude 16ch sub-endpoint)
                                if ((name.find("HT306") != std::string::npos ||
                                     name.find("CABLE Input") != std::string::npos) &&
                                    name.find("16ch") == std::string::npos &&
                                    (dwState == DEVICE_STATE_ACTIVE || dwState == DEVICE_STATE_DISABLED)) {
                                    
                                    status.installed = true;
                                    status.endpoint_visible = (dwState == DEVICE_STATE_ACTIVE);
                                    status.endpoint_name = name;
                                    if (dev_id) status.endpoint_id = dev_id;
                                    if (status.endpoint_visible && !current_default.empty() && dev_id && current_default == dev_id) {
                                        status.is_default = true;
                                    }
                                    PropVariantClear(&var_name);
                                    props->Release();
                                    if (dev_id) CoTaskMemFree(dev_id);
                                    device->Release();
                                    break;
                                }
                            }
                            PropVariantClear(&var_name);
                            props->Release();
                        }
                        if (dev_id) CoTaskMemFree(dev_id);
                        device->Release();
                    }
                }
                collection->Release();
            }
        }
        enumerator->Release();
    }

    // Fallback to Registry scan result from SyncEndpointNames() when endpoint is hidden
    if (!status.installed && !m_ht306_guid.empty()) {
        status.installed = true;
        status.endpoint_visible = (m_ht306_reg_state == 0x00000001);
        status.endpoint_id = L"{0.0.0.00000000}." + m_ht306_guid;
        status.endpoint_name = "LG HT306 Home Theater (5.1 Surround)";
        if (status.endpoint_visible && !current_default.empty() && current_default == status.endpoint_id) {
            status.is_default = true;
        }
    }

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }

    m_cached_status = status;
    m_last_check_tick = GetTickCount64();
    return status;
}

AudioDriverStatus AudioPolicyManager::GetCachedDriverStatus() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    uint64_t now = GetTickCount64();
    if (m_last_check_tick == 0 || (now - m_last_check_tick > 4000)) {
        return CheckDriverStatus();
    }
    return m_cached_status;
}

std::wstring AudioPolicyManager::FindFallbackNonHT306EndpointId() {
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    std::wstring fallback_id;

    IMMDeviceEnumerator* enumerator = NULL;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (SUCCEEDED(hr) && enumerator) {
        IMMDeviceCollection* collection = NULL;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)) && collection) {
            UINT count = 0;
            collection->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                IMMDevice* device = NULL;
                if (SUCCEEDED(collection->Item(i, &device)) && device) {
                    LPWSTR dev_id = NULL;
                    device->GetId(&dev_id);

                    IPropertyStore* props = NULL;
                    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
                        PROPVARIANT var_name;
                        PropVariantInit(&var_name);
                        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var_name)) && var_name.pwszVal) {
                            std::string name = WideToUtf8(var_name.pwszVal);
                            if (name.find("HT306") == std::string::npos &&
                                name.find("CABLE") == std::string::npos &&
                                name.find("VB-Audio") == std::string::npos) {
                                if (dev_id) fallback_id = dev_id;
                            }
                        }
                        PropVariantClear(&var_name);
                        props->Release();
                    }
                    if (dev_id) CoTaskMemFree(dev_id);
                    device->Release();
                    if (!fallback_id.empty()) break;
                }
            }
            collection->Release();
        }
        enumerator->Release();
    }

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
    return fallback_id;
}

bool AudioPolicyManager::SetEndpointVisibility(bool visible) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_ht306_guid.empty()) {
        SyncEndpointNames();
    }
    if (m_ht306_guid.empty()) {
        return false;
    }

    std::wstring endpoint_id = L"{0.0.0.00000000}." + m_ht306_guid;
    std::wstring subPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render\\" + m_ht306_guid;

    // 1. If surfacing, ensure low nibble of DeviceState in Registry is 0x01 (DEVICE_STATE_ACTIVE)
    // in case it was previously set to 0x10000002 (DEVICE_STATE_DISABLED).
    if (visible) {
        HKEY hSubKeyWrite = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath.c_str(), 0, KEY_SET_VALUE, &hSubKeyWrite) == ERROR_SUCCESS) {
            DWORD activeState = 0x00000001;
            RegSetValueExW(hSubKeyWrite, L"DeviceState", 0, REG_DWORD, (const BYTE*)&activeState, sizeof(activeState));
            RegCloseKey(hSubKeyWrite);
        }
    }

    // 2. Notify Audiosrv in real-time via IPolicyConfig::SetEndpointVisibility so Windows 11 Taskbar
    // immediately shows or hides the endpoint without needing an Audiosrv restart.
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool ok = false;

    IPolicyConfig* pPolicyConfig = NULL;
    HRESULT hr = CoCreateInstance(
        CLSID_CPolicyConfigClient,
        NULL,
        CLSCTX_ALL,
        IID_IPolicyConfig,
        (void**)&pPolicyConfig
    );

    if (SUCCEEDED(hr) && pPolicyConfig) {
        if (visible) {
            // Toggle 0 -> 1 to force Audiosrv to reload Registry DeviceState and broadcast OnDeviceStateChanged
            pPolicyConfig->SetEndpointVisibility(endpoint_id.c_str(), 0);
            HRESULT hrVis = pPolicyConfig->SetEndpointVisibility(endpoint_id.c_str(), 1);
            ok = SUCCEEDED(hrVis);
        } else {
            HRESULT hrVis = pPolicyConfig->SetEndpointVisibility(endpoint_id.c_str(), 0);
            ok = SUCCEEDED(hrVis);
        }
        pPolicyConfig->Release();
    }

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }

    m_ht306_reg_state = visible ? 0x00000001 : 0x10000001;
    m_last_check_tick = 0; // Invalidate cache
    return ok;
}

bool AudioPolicyManager::SetDefaultEndpoint(const std::wstring& device_id) {
    if (device_id.empty()) return false;

    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool success = false;

    IPolicyConfig* pPolicyConfig = NULL;
    HRESULT hr = CoCreateInstance(
        CLSID_CPolicyConfigClient,
        NULL,
        CLSCTX_ALL,
        IID_IPolicyConfig,
        (void**)&pPolicyConfig
    );

    if (SUCCEEDED(hr) && pPolicyConfig) {
        // Set default for Multimedia (music, videos, browser, youtube)
        HRESULT hr1 = pPolicyConfig->SetDefaultEndpoint(device_id.c_str(), eMultimedia);
        // Set default for Console (games, system notifications)
        HRESULT hr2 = pPolicyConfig->SetDefaultEndpoint(device_id.c_str(), eConsole);
        
        success = SUCCEEDED(hr1) || SUCCEEDED(hr2);
        pPolicyConfig->Release();
    }

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
    return success;
}

bool AudioPolicyManager::SwitchToHT306(AppState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    AudioDriverStatus status = CheckDriverStatus();
    if (!status.installed || status.endpoint_id.empty()) {
        state.AddLog(LogLevel::LOG_WARN, "AudioPolicy", 
            "Virtual Audio Driver (HT306/VB-Cable) not installed. Please click 'Install Driver' in Settings.");
        return false;
    }

    // Save current non-HT306 default playback endpoint before surfacing HT306
    std::wstring current_def = GetCurrentDefaultEndpointId();
    if (!current_def.empty() && current_def != status.endpoint_id) {
        m_saved_previous_device_id = current_def;
    }

    // 1. Ensure the LG HT306 Home Theater endpoint is visible in Windows Taskbar (DeviceState = 1)
    if (!status.endpoint_visible) {
        SetEndpointVisibility(true);
        // Poll up to 400ms (10 x 40ms) for Audiosrv to register the newly activated endpoint
        for (int attempt = 0; attempt < 10; ++attempt) {
            Sleep(40);
            status = CheckDriverStatus();
            if (status.endpoint_visible) break;
        }
    }

    // 2. Switch default audio output to LG HT306 Home Theater
    current_def = GetCurrentDefaultEndpointId();
    if (current_def != status.endpoint_id) {
        bool ok = SetDefaultEndpoint(status.endpoint_id);
        if (ok) {
            m_switched_to_ht306 = true;
            state.AddLog(LogLevel::LOG_INFO, "AudioPolicy", 
                "RP2350 connected: Surfaced & switched Windows Taskbar audio to " + status.endpoint_name);
        } else {
            state.AddLog(LogLevel::LOG_ERROR, "AudioPolicy", "Failed to switch default audio device via IPolicyConfig");
            return false;
        }
    } else {
        m_switched_to_ht306 = true;
    }

    // 3. Configure 5.1 surround 96kHz format
    ConfigureEndpointSurround96k(status.endpoint_id);
    CheckDriverStatus(); // Refresh cached status

    // 4. Sync active_device_index in state to the HT306 endpoint
    for (size_t i = 0; i < state.available_devices.size(); ++i) {
        if (!state.available_devices[i].id.empty() && state.available_devices[i].id == status.endpoint_id) {
            state.active_device_index = (int)i;
            break;
        }
    }

    return true;
}

bool AudioPolicyManager::RestorePreviousDefault(AppState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    AudioDriverStatus status = CheckDriverStatus();
    if (!status.installed) {
        return false;
    }

    // 1. Determine fallback speaker if saved previous ID is empty or points to HT306
    if (m_saved_previous_device_id.empty() || m_saved_previous_device_id == status.endpoint_id) {
        m_saved_previous_device_id = FindFallbackNonHT306EndpointId();
    }

    // 2. Switch default playback back to PC speaker before hiding HT306
    if (!m_saved_previous_device_id.empty()) {
        SetDefaultEndpoint(m_saved_previous_device_id);
    }
    m_switched_to_ht306 = false;

    // 3. Hide LG HT306 Home Theater from Windows Taskbar unless manual override is active
    if (!m_manual_visibility_override) {
        if (SetEndpointVisibility(false) && status.endpoint_visible) {
            state.AddLog(LogLevel::LOG_INFO, "AudioPolicy", 
                "RP2350 disconnected: Restored PC speaker and hid LG HT306 Home Theater from Windows Taskbar");
        }
    }

    CheckDriverStatus(); // Refresh cached status
    state.active_device_index = 0; // Default endpoint
    return true;
}

bool AudioPolicyManager::ConfigureEndpointSurround96k(const std::wstring& device_id) {
    if (device_id.empty()) return false;

    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool success = false;

    IPolicyConfig* pPolicyConfig = NULL;
    HRESULT hr = CoCreateInstance(
        CLSID_CPolicyConfigClient,
        NULL,
        CLSCTX_ALL,
        IID_IPolicyConfig,
        (void**)&pPolicyConfig
    );

    if (SUCCEEDED(hr) && pPolicyConfig) {
        // Prepare 5.1 Surround 96 kHz 32-bit Float format
        WAVEFORMATEXTENSIBLE wfx{};
        wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfx.Format.nChannels = 6;
        wfx.Format.nSamplesPerSec = 96000;
        wfx.Format.wBitsPerSample = 32;
        wfx.Format.nBlockAlign = 6 * sizeof(float); // 24 bytes per 6-channel frame
        wfx.Format.nAvgBytesPerSec = 96000 * wfx.Format.nBlockAlign;
        wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX); // 22 bytes
        wfx.Samples.wValidBitsPerSample = 32;
        wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
                            SPEAKER_LOW_FREQUENCY | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
        wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        HRESULT hrSet = pPolicyConfig->SetDeviceFormat(device_id.c_str(), (WAVEFORMATEX*)&wfx, (WAVEFORMATEX*)&wfx);
        success = SUCCEEDED(hrSet);
        pPolicyConfig->Release();
    }

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
    return success;
}

bool AudioPolicyManager::RenameEndpoint(const std::wstring& device_id, const std::wstring& new_name) {
    if (device_id.empty() || new_name.empty()) return false;

    // Extract GUID from endpoint ID "{0.0.0.00000000}.{GUID}"
    size_t pos = device_id.rfind(L".{");
    std::wstring guid = (pos != std::wstring::npos) ? device_id.substr(pos + 1) : device_id;

    std::wstring propPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render\\" + guid + L"\\Properties";
    HKEY hPropWrite = NULL;
    // IMPORTANT: Open with ONLY KEY_SET_VALUE (0x0002), NOT KEY_WRITE, because MMDevices ACL grants
    // BUILTIN\Users and BUILTIN\Administrators only KEY_QUERY_VALUE | KEY_SET_VALUE (not KEY_CREATE_SUB_KEY).
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, propPath.c_str(), 0, KEY_SET_VALUE, &hPropWrite) == ERROR_SUCCESS) {
        const wchar_t* newIfName = L"5.1 Surround";
        RegSetValueExW(hPropWrite, L"{a45c254e-df1c-4efd-8020-67d146a850e0},2", 0, REG_SZ,
                       (const BYTE*)new_name.c_str(), (DWORD)((new_name.length() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hPropWrite, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6", 0, REG_SZ,
                       (const BYTE*)newIfName, (DWORD)((wcslen(newIfName) + 1) * sizeof(wchar_t)));
        RegCloseKey(hPropWrite);
        return true;
    }
    return false;
}

bool AudioPolicyManager::SyncEndpointNames() {
    HKEY hRenderKey = NULL;
    const wchar_t* renderPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, renderPath, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &hRenderKey) != ERROR_SUCCESS) {
        return false;
    }

    m_ht306_guid.clear();
    wchar_t subKeyName[256];
    DWORD index = 0;
    DWORD nameLen = 256;

    while (RegEnumKeyExW(hRenderKey, index, subKeyName, &nameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        index++;
        nameLen = 256;

        std::wstring subPath = std::wstring(renderPath) + L"\\" + subKeyName;
        std::wstring propPath = subPath + L"\\Properties";

        HKEY hSubKeyRead = NULL;
        DWORD currentState = 0;
        DWORD cbState = sizeof(currentState);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath.c_str(), 0, KEY_QUERY_VALUE, &hSubKeyRead) == ERROR_SUCCESS) {
            RegQueryValueExW(hSubKeyRead, L"DeviceState", NULL, NULL, (LPBYTE)&currentState, &cbState);
            RegCloseKey(hSubKeyRead);
        }

        // Skip uninstalled / non-present ghost endpoints (low nibble 0x04 = DEVICE_STATE_NOTPRESENT)
        DWORD stateLow = currentState & 0x0F;
        if (stateLow != DEVICE_STATE_ACTIVE && stateLow != DEVICE_STATE_DISABLED) {
            continue;
        }

        HKEY hPropKey = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, propPath.c_str(), 0, KEY_QUERY_VALUE, &hPropKey) == ERROR_SUCCESS) {
            wchar_t desc[256] = { 0 };
            wchar_t ifname[256] = { 0 };
            wchar_t hwId[256] = { 0 };
            wchar_t subType[256] = { 0 };
            DWORD cbDesc = sizeof(desc);
            DWORD cbIfName = sizeof(ifname);
            DWORD cbHwId = sizeof(hwId);
            DWORD cbSubType = sizeof(subType);

            RegQueryValueExW(hPropKey, L"{a45c254e-df1c-4efd-8020-67d146a850e0},2", NULL, NULL, (LPBYTE)desc, &cbDesc);
            RegQueryValueExW(hPropKey, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6", NULL, NULL, (LPBYTE)ifname, &cbIfName);
            RegQueryValueExW(hPropKey, L"{a8b865dd-2e3d-4094-ad97-e593a70c75d6},8", NULL, NULL, (LPBYTE)hwId, &cbHwId);
            RegQueryValueExW(hPropKey, L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},8", NULL, NULL, (LPBYTE)subType, &cbSubType);
            RegCloseKey(hPropKey);

            std::wstring wDesc(desc);
            std::wstring wIfName(ifname);
            std::wstring wHwId(hwId);
            std::wstring wSubType(subType);

            bool isVbCable = (wHwId.find(L"VBAudioVACWDM") != std::wstring::npos) ||
                             (wDesc.find(L"CABLE") != std::wstring::npos) ||
                             (wDesc == L"LG HT306 Home Theater") ||
                             (wIfName.find(L"VB-Audio") != std::wstring::npos);

            if (isVbCable) {
                bool is16ch = (wDesc.find(L"16ch") != std::wstring::npos) ||
                              (wIfName.find(L"16ch") != std::wstring::npos) ||
                              (wSubType.find(L"DFF21FE3") != std::wstring::npos) ||
                              (wSubType.find(L"dff21fe3") != std::wstring::npos);

                if (is16ch) {
                    if (currentState != 0x10000002) {
                        // Hide 16ch endpoint live via IPolicyConfig::SetEndpointVisibility(0) and lock Registry to 0x10000002
                        HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);
                        IPolicyConfig* pPC = NULL;
                        if (SUCCEEDED(CoCreateInstance(CLSID_CPolicyConfigClient, NULL, CLSCTX_ALL, IID_IPolicyConfig, (void**)&pPC)) && pPC) {
                            std::wstring id16ch = std::wstring(L"{0.0.0.00000000}.") + subKeyName;
                            pPC->SetEndpointVisibility(id16ch.c_str(), 0);
                            pPC->Release();
                        }
                        if (SUCCEEDED(hrCom)) CoUninitialize();

                        HKEY hSubKeyWrite = NULL;
                        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath.c_str(), 0, KEY_SET_VALUE, &hSubKeyWrite) == ERROR_SUCCESS) {
                            DWORD disabledState = 0x10000002;
                            RegSetValueExW(hSubKeyWrite, L"DeviceState", 0, REG_DWORD, (const BYTE*)&disabledState, sizeof(disabledState));
                            RegCloseKey(hSubKeyWrite);
                        }
                    }
                } else {
                    m_ht306_guid = subKeyName;
                    m_ht306_reg_state = currentState;

                    if (wDesc != L"LG HT306 Home Theater" || wIfName != L"5.1 Surround") {
                        HKEY hPropWrite = NULL;
                        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, propPath.c_str(), 0, KEY_SET_VALUE, &hPropWrite) == ERROR_SUCCESS) {
                            const wchar_t* newDesc = L"LG HT306 Home Theater";
                            const wchar_t* newIfName = L"5.1 Surround";
                            const wchar_t* newFriendly = L"LG HT306 Home Theater (5.1 Surround)";
                            const wchar_t* speakerIcon = L"%windir%\\system32\\mmres.dll,-3010";
                            RegSetValueExW(hPropWrite, L"{a45c254e-df1c-4efd-8020-67d146a850e0},2", 0, REG_SZ,
                                           (const BYTE*)newDesc, (DWORD)((wcslen(newDesc) + 1) * sizeof(wchar_t)));
                            RegSetValueExW(hPropWrite, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6", 0, REG_SZ,
                                           (const BYTE*)newIfName, (DWORD)((wcslen(newIfName) + 1) * sizeof(wchar_t)));
                            RegSetValueExW(hPropWrite, L"{a45c254e-df1c-4efd-8020-67d146a850e0},14", 0, REG_SZ,
                                           (const BYTE*)newFriendly, (DWORD)((wcslen(newFriendly) + 1) * sizeof(wchar_t)));
                            RegSetValueExW(hPropWrite, L"{259abffc-50a7-47ce-af08-68c9a7d73366},12", 0, REG_SZ,
                                           (const BYTE*)speakerIcon, (DWORD)((wcslen(speakerIcon) + 1) * sizeof(wchar_t)));
                            RegCloseKey(hPropWrite);
                        }
                    }
                }
            }
        }
    }

    RegCloseKey(hRenderKey);
    m_last_check_tick = 0; // Invalidate cache so next check reads updated names
    return !m_ht306_guid.empty();
}

static std::string FindVBCableSetupExe() {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecA(exePath);

    char candidate[MAX_PATH] = { 0 };
    char canonical[MAX_PATH] = { 0 };

    // 1. In <exe_dir>\tools\vbcable\VBCABLE_Setup_x64.exe
    PathCombineA(candidate, exePath, "tools\\vbcable\\VBCABLE_Setup_x64.exe");
    if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        if (GetFullPathNameA(candidate, MAX_PATH, canonical, NULL)) return canonical;
        return candidate;
    }

    // 2. In <exe_dir>\..\tools\vbcable\VBCABLE_Setup_x64.exe
    PathCombineA(candidate, exePath, "..\\tools\\vbcable\\VBCABLE_Setup_x64.exe");
    if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        if (GetFullPathNameA(candidate, MAX_PATH, canonical, NULL)) return canonical;
        return candidate;
    }

    // 3. In <exe_dir>\..\..\tools\vbcable\VBCABLE_Setup_x64.exe
    PathCombineA(candidate, exePath, "..\\..\\tools\\vbcable\\VBCABLE_Setup_x64.exe");
    if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        if (GetFullPathNameA(candidate, MAX_PATH, canonical, NULL)) return canonical;
        return candidate;
    }

    // 4. Relative to current working directory
    if (GetFileAttributesA("tools\\vbcable\\VBCABLE_Setup_x64.exe") != INVALID_FILE_ATTRIBUTES) {
        if (GetFullPathNameA("tools\\vbcable\\VBCABLE_Setup_x64.exe", MAX_PATH, canonical, NULL)) return canonical;
        return "tools\\vbcable\\VBCABLE_Setup_x64.exe";
    }
    if (GetFileAttributesA("pc_stream_app\\tools\\vbcable\\VBCABLE_Setup_x64.exe") != INVALID_FILE_ATTRIBUTES) {
        if (GetFullPathNameA("pc_stream_app\\tools\\vbcable\\VBCABLE_Setup_x64.exe", MAX_PATH, canonical, NULL)) return canonical;
        return "pc_stream_app\\tools\\vbcable\\VBCABLE_Setup_x64.exe";
    }

    return "";
}

bool AudioPolicyManager::InstallDriver(HWND parent_hwnd) {
    std::string setup_exe = FindVBCableSetupExe();
    if (setup_exe.empty()) {
        MessageBoxA(parent_hwnd, 
            "File installer driver 'VBCABLE_Setup_x64.exe' tidak ditemukan dalam direktori tools/vbcable.",
            "LG HT306 Driver Setup", MB_ICONERROR | MB_OK);
        return false;
    }

    char setup_dir[MAX_PATH] = { 0 };
    strcpy_s(setup_dir, sizeof(setup_dir), setup_exe.c_str());
    PathRemoveFileSpecA(setup_dir);

    // Initialize COM for ShellExecuteEx (STA is recommended for shell operations)
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Launch VBCABLE_Setup_x64.exe with administrator elevation
    SHELLEXECUTEINFOA sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = NULL; // Safe for worker thread elevation
    sei.lpVerb = "runas"; // Elevate with Windows UAC prompt
    sei.lpFile = setup_exe.c_str();
    sei.lpParameters = NULL; // Interactive GUI installer
    sei.lpDirectory = setup_dir;
    sei.nShow = SW_SHOWNORMAL;

    BOOL shellRet = ShellExecuteExA(&sei);

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }

    if (!shellRet) {
        // If UAC was cancelled or rejected by user, cleanly exit
        return false;
    }

    if (sei.hProcess) {
        // Wait for setup process (user clicking 'Install Driver' dialog, up to 120s)
        WaitForSingleObject(sei.hProcess, 120000);
        CloseHandle(sei.hProcess);
    }

    // Windows PnP and AudioEndpointBuilder automatically discover the new WHQL endpoint
    Sleep(1500);

    // Run automated script to set friendly name to 'LG HT306 Home Theater' and disable 16ch
    SyncEndpointNames();

    // Check if installation succeeded
    AudioDriverStatus st = CheckDriverStatus();
    if (st.installed) {
        RenameEndpoint(st.endpoint_id, L"LG HT306 Home Theater");
        ConfigureEndpointSurround96k(st.endpoint_id);
    }

    return st.installed;
}

} // namespace ht306::audio
