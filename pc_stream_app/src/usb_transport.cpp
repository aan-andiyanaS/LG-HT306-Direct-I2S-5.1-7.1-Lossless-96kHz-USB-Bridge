#include "usb_transport.h"
#include <setupapi.h>
#include <initguid.h>
#include <iostream>
#include <algorithm>

// Microsoft OS 2.0 Descriptor GUID defined in firmware/src/usb_descriptors.c
// {CDB3B5AD-293B-4663-AA36-1AAE46463776}
DEFINE_GUID(GUID_DEVINTERFACE_HT306_WINUSB,
    0xcdb3b5ad, 0x293b, 0x4663, 0xaa, 0x36, 0x1a, 0xae, 0x46, 0x46, 0x37, 0x76);

namespace ht306::usb {

UsbTransport::UsbTransport(AppState& state)
    : m_state(state) {
}

UsbTransport::~UsbTransport() {
    StopWorker();
    Close();
}

bool UsbTransport::FindDevicePath(std::string& out_path) {
    HDEVINFO dev_info = SetupDiGetClassDevsA(
        &GUID_DEVINTERFACE_HT306_WINUSB,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (dev_info == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVICE_INTERFACE_DATA if_data{};
    if_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bool found = false;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(dev_info, NULL, &GUID_DEVINTERFACE_HT306_WINUSB, i, &if_data); ++i) {
        DWORD detail_size = 0;
        SetupDiGetDeviceInterfaceDetailA(dev_info, &if_data, NULL, 0, &detail_size, NULL);

        if (detail_size == 0) continue;

        auto detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(detail_size);
        if (!detail_data) continue;
        detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(dev_info, &if_data, detail_data, detail_size, NULL, NULL)) {
            out_path = detail_data->DevicePath;
            found = true;
            free(detail_data);
            break;
        }
        free(detail_data);
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return found;
}

bool UsbTransport::Open() {
    std::lock_guard<std::mutex> lock(m_write_mutex);
    if (m_is_open) return true;

    std::string dev_path;
    if (!FindDevicePath(dev_path)) {
        m_state.state.store(ConnectionState::DISCONNECTED);
        m_state.hw_info.detected = false;
        return false;
    }

    m_device_handle = CreateFileA(
        dev_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (m_device_handle == INVALID_HANDLE_VALUE) {
        m_state.state.store(ConnectionState::ERR_STATE);
        return false;
    }

    if (!WinUsb_Initialize(m_device_handle, &m_winusb_handle)) {
        CloseHandle(m_device_handle);
        m_device_handle = INVALID_HANDLE_VALUE;
        m_state.state.store(ConnectionState::ERR_STATE);
        return false;
    }

    // Set short pipe timeouts for low latency
    ULONG timeout_ms = 100;
    WinUsb_SetPipePolicy(m_winusb_handle, m_bulk_out_pipe, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_ms), &timeout_ms);
    UCHAR raw_io = 1;
    WinUsb_SetPipePolicy(m_winusb_handle, m_bulk_out_pipe, RAW_IO, sizeof(raw_io), &raw_io);

    m_is_open = true;
    m_state.state.store(ConnectionState::READY);
    m_state.hw_info.detected = true;
    m_state.hw_info.port_or_path = "WinUSB Interface 2 (EP 0x03 OUT)";
    return true;
}

void UsbTransport::Close() {
    std::lock_guard<std::mutex> lock(m_write_mutex);
    if (m_winusb_handle) {
        WinUsb_Free(m_winusb_handle);
        m_winusb_handle = NULL;
    }
    if (m_device_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_device_handle);
        m_device_handle = INVALID_HANDLE_VALUE;
    }
    m_is_open = false;
    m_state.state.store(ConnectionState::DISCONNECTED);
    m_state.hw_info.detected = false;
}

bool UsbTransport::IsOpen() const {
    return m_is_open.load();
}

bool UsbTransport::WritePacket(const uint8_t* data, size_t length, uint32_t timeout_ms) {
    if (!m_is_open || !m_winusb_handle) return false;

    std::lock_guard<std::mutex> lock(m_write_mutex);
    ULONG bytes_written = 0;
    BOOL res = WinUsb_WritePipe(m_winusb_handle, m_bulk_out_pipe, (PUCHAR)data, (ULONG)length, &bytes_written, NULL);

    if (!res) {
        DWORD err = GetLastError();
        if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_GEN_FAILURE) {
            // Unplugged
            m_is_open = false;
            m_state.state.store(ConnectionState::DISCONNECTED);
            m_state.hw_info.detected = false;
        }
        return false;
    }
    return (bytes_written == length);
}

void UsbTransport::StartWorker() {
    if (m_worker_running) return;
    m_worker_running = true;
    m_worker_thread = CreateThread(NULL, 0, WorkerThreadProc, this, 0, NULL);
}

void UsbTransport::StopWorker() {
    if (!m_worker_running) return;
    m_worker_running = false;
    if (m_worker_thread) {
        WaitForSingleObject(m_worker_thread, 1000);
        CloseHandle(m_worker_thread);
        m_worker_thread = NULL;
    }
}

DWORD WINAPI UsbTransport::WorkerThreadProc(LPVOID param) {
    auto self = reinterpret_cast<UsbTransport*>(param);
    self->WorkerLoop();
    return 0;
}

void UsbTransport::WorkerLoop() {
    while (m_worker_running) {
        if (!m_is_open) {
            Open();
        } else {
            // Heartbeat simulation / check
            m_state.hw_info.uptime_sec += 1;
            m_state.hw_info.heartbeat_ok = true;
        }
        Sleep(1000);
    }
}

} // namespace ht306::usb
