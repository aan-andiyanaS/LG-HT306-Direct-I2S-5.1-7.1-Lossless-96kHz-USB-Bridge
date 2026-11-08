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

    // COM Port auto-discovery for RP2350-Zero CDC ACM
    bool FindComPort(std::string& out_com);

    // Trigger 1200-baud reset into RP2350 USB BOOTSEL mode
    bool RebootBootsel();

    bool WritePacket(const uint8_t* data, size_t length, uint32_t timeout_ms = 100);

    // Background monitoring thread (heartbeat / connection watchdog)
    void StartWorker();
    void StopWorker();

    // Flexible hardware presence check & instant hotplug trigger
    bool CheckHardwarePresence();
    void TriggerCheck();

private:
    bool FindDevicePath(std::string& out_path);
    bool OpenSerialPort(const std::string& com_port);
    void CloseSerial();
    void PollSerial();
    void ParseSerialLine(const std::string& raw_line);
    void OnDeviceDisconnected();

    AppState& m_state;
    HANDLE m_device_handle = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE m_winusb_handle = NULL;
    UCHAR m_bulk_out_pipe = 0x03;

    // CDC ACM Serial telemetry handle and buffers
    HANDLE m_serial_handle = INVALID_HANDLE_VALUE;
    std::recursive_mutex m_serial_mutex;
    std::string m_serial_line_buf;
    bool m_serial_connected_logged = false;
    bool m_serial_access_denied_logged = false;

    // Concurrency and thread management
    std::recursive_mutex m_write_mutex;
    std::atomic<bool> m_is_open{false};
    std::atomic<bool> m_worker_running{false};
    std::atomic<bool> m_trigger_check{true};
    HANDLE m_worker_thread = NULL;
    int m_consecutive_disconnect_count = 0;

    static DWORD WINAPI WorkerThreadProc(LPVOID param);
    void WorkerLoop();
};

} // namespace ht306::usb
