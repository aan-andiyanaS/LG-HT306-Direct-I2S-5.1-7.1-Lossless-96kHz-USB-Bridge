#pragma once

#include "app_state.h"
#include <windows.h>
#include <winusb.h>
#include <string>
#include <mutex>
#include <atomic>

namespace ht306::usb {

class UsbTransport {
public:
    UsbTransport(AppState& state);
    ~UsbTransport();

    bool Open();
    void Close();
    bool IsOpen() const;

    bool WritePacket(const uint8_t* data, size_t length, uint32_t timeout_ms = 100);

    // Background monitoring thread (heartbeat / connection watchdog)
    void StartWorker();
    void StopWorker();

private:
    bool FindDevicePath(std::string& out_path);

    AppState& m_state;
    HANDLE m_device_handle = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE m_winusb_handle = NULL;
    UCHAR m_bulk_out_pipe = 0x03;

    std::mutex m_write_mutex;
    std::atomic<bool> m_is_open{false};
    std::atomic<bool> m_worker_running{false};
    HANDLE m_worker_thread = NULL;

    static DWORD WINAPI WorkerThreadProc(LPVOID param);
    void WorkerLoop();
};

} // namespace ht306::usb
