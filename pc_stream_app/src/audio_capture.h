#pragma once

#include "app_state.h"
#include "usb_transport.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <atomic>

namespace ht306::audio {

class AudioCapture {
public:
    AudioCapture(AppState& state, usb::UsbTransport& transport);
    ~AudioCapture();

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    static DWORD WINAPI CaptureThreadProc(LPVOID param);
    void CaptureLoop();

    AppState& m_state;
    usb::UsbTransport& m_transport;

    std::atomic<bool> m_running{false};
    HANDLE m_thread = NULL;
    HANDLE m_stop_event = NULL;

    uint8_t m_seq = 0;
};

} // namespace ht306::audio
