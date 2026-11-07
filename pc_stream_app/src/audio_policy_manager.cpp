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
        IMMDeviceCollection* collection = NULL;
        hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
        if (SUCCEEDED(hr) && collection) {
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
                            
                            // Check specifically for LG HT306 or VB-CABLE Input (avoid matching VoiceMeeter / Hi-Fi cable)
                            if (name.find("HT306") != std::string::npos ||
                                name.find("CABLE Input") != std::string::npos) {
                                
                                status.installed = true;
                                status.endpoint_name = name;
                                if (dev_id) status.endpoint_id = dev_id;
                                if (!current_default.empty() && dev_id && current_default == dev_id) {
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
        enumerator->Release();
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

    std::wstring current_def = GetCurrentDefaultEndpointId();
    if (current_def != status.endpoint_id) {
        m_saved_previous_device_id = current_def;
        bool ok = SetDefaultEndpoint(status.endpoint_id);
        if (ok) {
            m_switched_to_ht306 = true;
            state.AddLog(LogLevel::LOG_INFO, "AudioPolicy", 
                "Windows default audio device automatically switched to: " + status.endpoint_name);
        } else {
            state.AddLog(LogLevel::LOG_ERROR, "AudioPolicy", "Failed to switch default audio device via IPolicyConfig");
            return false;
        }
    } else {
        m_switched_to_ht306 = true;
    }

    // Configure 5.1 surround 96kHz format
    ConfigureEndpointSurround96k(status.endpoint_id);

    // Sync active_device_index in state to the HT306 endpoint
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

    if (m_switched_to_ht306 && !m_saved_previous_device_id.empty()) {
        bool ok = SetDefaultEndpoint(m_saved_previous_device_id);
        if (ok) {
            state.AddLog(LogLevel::LOG_INFO, "AudioPolicy", 
                "Hardware disconnected: Restored Windows default audio playback to previous PC speaker");
        }
        m_switched_to_ht306 = false;
        m_saved_previous_device_id.clear();
    }
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

    bool modified = false;
    wchar_t subKeyName[256];
    DWORD index = 0;
    DWORD nameLen = 256;

    while (RegEnumKeyExW(hRenderKey, index, subKeyName, &nameLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        index++;
        nameLen = 256;

        std::wstring subPath = std::wstring(renderPath) + L"\\" + subKeyName;
        std::wstring propPath = subPath + L"\\Properties";

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
                             (wIfName.find(L"VB-Audio") != std::wstring::npos);

            if (isVbCable) {
                bool is16ch = (wDesc.find(L"16ch") != std::wstring::npos) ||
                              (wIfName.find(L"16ch") != std::wstring::npos) ||
                              (wSubType.find(L"DFF21FE3") != std::wstring::npos) ||
                              (wSubType.find(L"dff21fe3") != std::wstring::npos);

                if (is16ch) {
                    HKEY hSubKeyRead = NULL;
                    DWORD currentState = 1;
                    DWORD cbState = sizeof(currentState);
                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath.c_str(), 0, KEY_QUERY_VALUE, &hSubKeyRead) == ERROR_SUCCESS) {
                        RegQueryValueExW(hSubKeyRead, L"DeviceState", NULL, NULL, (LPBYTE)&currentState, &cbState);
                        RegCloseKey(hSubKeyRead);
                    }
                    if (currentState == 1) {
                        HKEY hSubKeyWrite = NULL;
                        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subPath.c_str(), 0, KEY_SET_VALUE, &hSubKeyWrite) == ERROR_SUCCESS) {
                            DWORD disabledState = 0x10000002;
                            if (RegSetValueExW(hSubKeyWrite, L"DeviceState", 0, REG_DWORD, (const BYTE*)&disabledState, sizeof(disabledState)) == ERROR_SUCCESS) {
                                modified = true;
                            }
                            RegCloseKey(hSubKeyWrite);
                        }
                    }
                } else {
                    if (wDesc != L"LG HT306 Home Theater" || wIfName != L"5.1 Surround") {
                        HKEY hPropWrite = NULL;
                        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, propPath.c_str(), 0, KEY_SET_VALUE, &hPropWrite) == ERROR_SUCCESS) {
                            const wchar_t* newDesc = L"LG HT306 Home Theater";
                            const wchar_t* newIfName = L"5.1 Surround";
                            const wchar_t* speakerIcon = L"%windir%\\system32\\mmres.dll,-3010";
                            RegSetValueExW(hPropWrite, L"{a45c254e-df1c-4efd-8020-67d146a850e0},2", 0, REG_SZ,
                                           (const BYTE*)newDesc, (DWORD)((wcslen(newDesc) + 1) * sizeof(wchar_t)));
                            RegSetValueExW(hPropWrite, L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6", 0, REG_SZ,
                                           (const BYTE*)newIfName, (DWORD)((wcslen(newIfName) + 1) * sizeof(wchar_t)));
                            RegSetValueExW(hPropWrite, L"{259abffc-50a7-47ce-af08-68c9a7d73366},12", 0, REG_SZ,
                                           (const BYTE*)speakerIcon, (DWORD)((wcslen(speakerIcon) + 1) * sizeof(wchar_t)));
                            RegCloseKey(hPropWrite);
                            modified = true;
                        }
                    }
                }
            }
        }
    }

    RegCloseKey(hRenderKey);
    m_last_check_tick = 0; // Invalidate cache so next check reads updated names
    return true;
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
