#include "audio_endpoint_helper.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <iostream>

namespace ht306::audio {

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

void AudioEndpointHelper::EnumerateDevices(AppState& state) {
    HRESULT hrCom = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    std::vector<AudioEndpointInfo> next_devices;

    // Always provide Default option at index 0
    AudioEndpointInfo default_item;
    default_item.id = L"";
    default_item.name = "Default Windows Playback Device";
    default_item.is_default = true;
    next_devices.push_back(default_item);

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
                        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var_name))) {
                            AudioEndpointInfo info;
                            if (dev_id) info.id = dev_id;
                            info.name = WideToUtf8(var_name.pwszVal ? var_name.pwszVal : L"Unknown Audio Device");
                            next_devices.push_back(info);
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

    state.SetAvailableDevices(std::move(next_devices), L"", true);

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
}

} // namespace ht306::audio
