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
    state.available_devices.clear();

    // Always provide Default option
    AudioEndpointInfo default_item;
    default_item.id = L"";
    default_item.name = "Default Windows Playback Device";
    default_item.is_default = true;
    state.available_devices.push_back(default_item);

    IMMDeviceEnumerator* enumerator = NULL;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (FAILED(hr) || !enumerator) return;

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
                        
                        // Check if this is a dedicated virtual cable or HT306 device
                        if (info.name.find("CABLE") != std::string::npos || 
                            info.name.find("HT306") != std::string::npos ||
                            info.name.find("Virtual") != std::string::npos) {
                            // Suggest selecting this virtual endpoint
                            if (state.active_device_index <= 0) {
                                state.active_device_index = (int)state.available_devices.size();
                            }
                        }

                        state.available_devices.push_back(info);
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

    if (state.active_device_index < 0 && !state.available_devices.empty()) {
        state.active_device_index = 0;
    }
}

} // namespace ht306::audio
