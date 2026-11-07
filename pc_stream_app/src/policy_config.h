// ============================================================================
// LG HT306 Audio Bridge - Windows COM PolicyConfig Interface
// Compatible with Windows 10 & Windows 11 (Undocumented Core Audio API)
// Modeled after production-grade implementations in EarTrumpet & Sunshine
// ============================================================================

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmdeviceapi.h>

// CLSID: {870af99c-171d-4f9e-af0d-e63df40c2bc9}
EXTERN_C const GUID DECLSPEC_SELECTANY CLSID_CPolicyConfigClient =
    { 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };

// IID: {f8679f50-850a-41cf-9c72-430f290290c8}
EXTERN_C const GUID DECLSPEC_SELECTANY IID_IPolicyConfig =
    { 0xf8679f50, 0x850a, 0x41cf, { 0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8 } };

// VTable definition for Windows 10 & 11
interface IPolicyConfig : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(
        PCWSTR pszDeviceName,
        WAVEFORMATEX** ppFormat) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(
        PCWSTR pszDeviceName,
        INT bDefault,
        WAVEFORMATEX** ppFormat) = 0;

    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(
        PCWSTR pszDeviceName) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(
        PCWSTR pszDeviceName,
        WAVEFORMATEX* pEndpointFormat,
        WAVEFORMATEX* pMixFormat) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(
        PCWSTR pszDeviceName,
        INT bDefault,
        PINT64 pmftDefaultPeriod,
        PINT64 pmftMinimumPeriod) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(
        PCWSTR pszDeviceName,
        PINT64 pmftPeriod) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetShareMode(
        PCWSTR pszDeviceName,
        struct DeviceShareMode* pMode) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetShareMode(
        PCWSTR pszDeviceName,
        struct DeviceShareMode* pMode) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(
        PCWSTR pszDeviceName,
        const PROPERTYKEY& Key,
        PROPVARIANT* pv) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(
        PCWSTR pszDeviceName,
        const PROPERTYKEY& Key,
        PROPVARIANT* pv) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(
        PCWSTR wszDeviceId,
        ERole eRole) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(
        PCWSTR wszDeviceId,
        INT bVisible) = 0;
};
