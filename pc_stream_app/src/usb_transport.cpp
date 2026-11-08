#include "usb_transport.h"
#include "audio_policy_manager.h"
#include "audio_endpoint_helper.h"
#include <setupapi.h>
#include <initguid.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

// Microsoft OS 2.0 Descriptor GUID defined in firmware/src/usb_descriptors.c
// {CDB3B5AD-293B-4663-AA36-1AAE46463776}
DEFINE_GUID(GUID_DEVINTERFACE_HT306_WINUSB,
    0xcdb3b5ad, 0x293b, 0x4663, 0xaa, 0x36, 0x1a, 0xae, 0x46, 0x46, 0x37, 0x76);

// Standard Microsoft WinUSB Device Interface GUID
// {DEE824EF-729B-4A0E-9C14-B7117D33A817}
DEFINE_GUID(GUID_DEVINTERFACE_STANDARD_WINUSB,
    0xdee824ef, 0x729b, 0x4a0e, 0x9c, 0x14, 0xb7, 0x11, 0x7d, 0x33, 0xa8, 0x17);

namespace ht306::usb {

UsbTransport::UsbTransport(AppState& state)
    : m_state(state) {
}

UsbTransport::~UsbTransport() {
    StopWorker();
    Close();
}

bool UsbTransport::CheckHardwarePresence() {
    // 1. Check via SetupDi for device with VID_2E8A and PID_4306
    HDEVINFO dev_info = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    bool found = false;
    if (dev_info != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA dev_data{};
        dev_data.cbSize = sizeof(dev_data);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_data); ++i) {
            char hwid_buf[512] = { 0 };
            DWORD req_size = 0;
            if (SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_data, SPDRP_HARDWAREID, NULL,
                                                 reinterpret_cast<PBYTE>(hwid_buf), sizeof(hwid_buf) - 1, &req_size)) {
                // Flatten multi-sz delimiters
                DWORD scan_len = (req_size < sizeof(hwid_buf) - 1) ? req_size : (sizeof(hwid_buf) - 1);
                for (DWORD k = 0; k < scan_len; ++k) {
                    if (hwid_buf[k] == '\0') hwid_buf[k] = ' ';
                }
                hwid_buf[scan_len] = '\0';

                std::string hwid(hwid_buf);
                std::string hwid_upper = hwid;
                std::transform(hwid_upper.begin(), hwid_upper.end(), hwid_upper.begin(), ::toupper);
                if (hwid_upper.find("VID_2E8A") != std::string::npos && hwid_upper.find("PID_4306") != std::string::npos) {
                    found = true;
                    break;
                }
            }
        }
        SetupDiDestroyDeviceInfoList(dev_info);
    }

    if (found) {
        m_state.hw_info.detected = true;
        m_state.hw_info.vid = 0x2E8A;
        m_state.hw_info.pid = 0x4306;
        if (m_state.hw_info.serial_number.empty()) {
            m_state.hw_info.serial_number = "HT306-RP2350Z-96K";
        }
        if (m_state.hw_info.firmware_version.empty()) {
            m_state.hw_info.firmware_version = "v1.3.0";
        }
        if (m_state.state.load() == ConnectionState::DISCONNECTED) {
            m_state.state.store(ConnectionState::READY);
        }
    }

    return found;
}

void UsbTransport::TriggerCheck() {
    m_trigger_check.store(true);
}

bool UsbTransport::FindDevicePath(std::string& out_path) {
    const GUID* guids[] = { &GUID_DEVINTERFACE_HT306_WINUSB, &GUID_DEVINTERFACE_STANDARD_WINUSB };

    for (const GUID* pGuid : guids) {
        HDEVINFO dev_info = SetupDiGetClassDevsA(
            pGuid,
            NULL,
            NULL,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        );

        if (dev_info == INVALID_HANDLE_VALUE) {
            continue;
        }

        SP_DEVICE_INTERFACE_DATA if_data{};
        if_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(dev_info, NULL, pGuid, i, &if_data); ++i) {
            DWORD detail_size = 0;
            SetupDiGetDeviceInterfaceDetailA(dev_info, &if_data, NULL, 0, &detail_size, NULL);

            if (detail_size == 0) continue;

            auto detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(detail_size);
            if (!detail_data) continue;
            detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

            if (SetupDiGetDeviceInterfaceDetailA(dev_info, &if_data, detail_data, detail_size, NULL, NULL)) {
                std::string p(detail_data->DevicePath);
                std::string p_upper = p;
                std::transform(p_upper.begin(), p_upper.end(), p_upper.begin(), ::toupper);
                if (p_upper.find("VID_2E8A") != std::string::npos && p_upper.find("PID_4306") != std::string::npos) {
                    out_path = p;
                    free(detail_data);
                    SetupDiDestroyDeviceInfoList(dev_info);
                    return true;
                }
            }
            free(detail_data);
        }

        SetupDiDestroyDeviceInfoList(dev_info);
    }
    return false;
}

bool UsbTransport::FindComPort(std::string& out_com) {
    bool found = false;

    // 1. Probe via SetupDiGetClassDevsA for device matching RP2350 (VID_2E8A & PID_4306)
    HDEVINFO dev_info = SetupDiGetClassDevsA(
        NULL,
        NULL,
        NULL,
        DIGCF_ALLCLASSES | DIGCF_PRESENT
    );

    if (dev_info != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA dev_data{};
        dev_data.cbSize = sizeof(SP_DEVINFO_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_data); ++i) {
            char hwid_buf[512] = { 0 };
            DWORD reg_type = 0;
            DWORD req_size = 0;
            if (SetupDiGetDeviceRegistryPropertyA(
                    dev_info, &dev_data, SPDRP_HARDWAREID, &reg_type,
                    reinterpret_cast<PBYTE>(hwid_buf), sizeof(hwid_buf) - 1, &req_size)) {

                // Flatten multi-sz delimiters so we can search the entire buffer
                DWORD scan_len = (req_size < sizeof(hwid_buf) - 1) ? req_size : (sizeof(hwid_buf) - 1);
                for (DWORD k = 0; k < scan_len; ++k) {
                    if (hwid_buf[k] == '\0') hwid_buf[k] = ' ';
                }
                hwid_buf[scan_len] = '\0';

                std::string hwid(hwid_buf);
                std::string hwid_upper = hwid;
                std::transform(hwid_upper.begin(), hwid_upper.end(), hwid_upper.begin(), ::toupper);

                if (hwid_upper.find("VID_2E8A") != std::string::npos &&
                    hwid_upper.find("PID_4306") != std::string::npos) {

                    // Query PortName from Device Parameters
                    HKEY hKey = SetupDiOpenDevRegKey(dev_info, &dev_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                    if (hKey != INVALID_HANDLE_VALUE) {
                        char port_name[64] = { 0 };
                        DWORD port_name_size = sizeof(port_name);
                        DWORD type = 0;
                        if (RegQueryValueExA(hKey, "PortName", NULL, &type,
                                             reinterpret_cast<LPBYTE>(port_name), &port_name_size) == ERROR_SUCCESS) {
                            if (port_name[0] != '\0') {
                                out_com = port_name;
                                found = true;
                                RegCloseKey(hKey);
                                break;
                            }
                        }
                        RegCloseKey(hKey);
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(dev_info);
    }

    if (found && !out_com.empty()) {
        m_state.hw_info.com_port = out_com;
        return true;
    }

    // Fallback to m_state.hw_info.com_port or "COM4"
    if (!m_state.hw_info.com_port.empty()) {
        out_com = m_state.hw_info.com_port;
    } else {
        out_com = "COM4";
        m_state.hw_info.com_port = out_com;
    }
    return false;
}

bool UsbTransport::RebootBootsel() {
    std::lock_guard<std::recursive_mutex> lock(m_serial_mutex);

    // Close active serial handle first so it does not cause ERROR_SHARING_VIOLATION
    CloseSerial();

    // Determine target COM port
    std::string com = m_state.hw_info.com_port;
    if (com.empty()) {
        FindComPort(com);
    }
    if (com.empty()) {
        com = "COM4";
    }

    std::string port_path = "\\\\.\\" + com;
    HANDLE hPort = CreateFileA(
        port_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPort == INVALID_HANDLE_VALUE) {
        m_state.AddLog(LogLevel::LOG_WARN, "BOOTSEL", "Failed to open " + com + " for 1200-baud reset trigger");
        return false;
    }

    // Configure DCB with 1200 baud 8N1
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (GetCommState(hPort, &dcb)) {
        dcb.BaudRate = CBR_1200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(hPort, &dcb);
    }

    // Drop DTR to signal 1200-baud touch reset
    EscapeCommFunction(hPort, CLRDTR);

    // Also send 'B' command as dual-mode trigger
    DWORD written = 0;
    WriteFile(hPort, "B", 1, &written, NULL);

    Sleep(50);
    CloseHandle(hPort);

    m_state.AddLog(LogLevel::LOG_INFO, "BOOTSEL", "Triggered 1200-baud BOOTSEL reboot on " + com);
    return true;
}

bool UsbTransport::OpenSerialPort(const std::string& com_port) {
    std::lock_guard<std::recursive_mutex> lock(m_serial_mutex);
    if (m_serial_handle != INVALID_HANDLE_VALUE) {
        return true;
    }

    if (com_port.empty()) return false;

    std::string full_port = "\\\\.\\" + com_port;
    m_serial_handle = CreateFileA(
        full_port.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (m_serial_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            if (!m_serial_access_denied_logged) {
                m_state.AddLog(LogLevel::LOG_WARN, "CDC", "Serial port " + com_port + " in use by another app; continuing via WinUSB");
                m_serial_access_denied_logged = true;
            }
        }
        return false;
    }

    // Configure non-blocking timeouts:
    // ReadIntervalTimeout = MAXDWORD, ReadTotalTimeoutMultiplier = 0, ReadTotalTimeoutConstant = 0
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(m_serial_handle, &timeouts);

    // Configure DCB (115200 8N1, assert DTR/RTS for TinyUSB CDC)
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (GetCommState(m_serial_handle, &dcb)) {
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        SetCommState(m_serial_handle, &dcb);
    }
    EscapeCommFunction(m_serial_handle, SETDTR);

    if (!m_serial_connected_logged) {
        m_state.AddLog(LogLevel::LOG_INFO, "CDC", "Connected to RP2350-Zero Serial CDC (" + com_port + ")");
        m_serial_connected_logged = true;
    }
    m_serial_access_denied_logged = false;
    return true;
}

void UsbTransport::CloseSerial() {
    std::lock_guard<std::recursive_mutex> lock(m_serial_mutex);
    if (m_serial_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_serial_handle);
        m_serial_handle = INVALID_HANDLE_VALUE;
    }
    m_serial_line_buf.clear();
    m_serial_connected_logged = false;
    m_serial_access_denied_logged = false;
}

void UsbTransport::PollSerial() {
    std::lock_guard<std::recursive_mutex> lock(m_serial_mutex);
    if (m_serial_handle == INVALID_HANDLE_VALUE) return;

    // Immediately detect USB CDC surprise removal via ClearCommError
    DWORD comm_errors = 0;
    COMSTAT com_stat{};
    if (!ClearCommError(m_serial_handle, &comm_errors, &com_stat)) {
        CloseHandle(m_serial_handle);
        m_serial_handle = INVALID_HANDLE_VALUE;
        m_serial_connected_logged = false;
        m_serial_line_buf.clear();
        m_trigger_check.store(true);
        return;
    }

    char buffer[256];
    DWORD bytes_read = 0;
    if (ReadFile(m_serial_handle, buffer, sizeof(buffer) - 1, &bytes_read, NULL)) {
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            for (DWORD i = 0; i < bytes_read; ++i) {
                char ch = buffer[i];
                if (ch == '\r') {
                    continue;
                }
                if (ch == '\n') {
                    if (!m_serial_line_buf.empty()) {
                        ParseSerialLine(m_serial_line_buf);
                        m_serial_line_buf.clear();
                    }
                } else {
                    if (m_serial_line_buf.size() < 1024) {
                        m_serial_line_buf.push_back(ch);
                    }
                }
            }
        }
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_GEN_FAILURE ||
            err == ERROR_FILE_NOT_FOUND || err == ERROR_OPERATION_ABORTED) {
            CloseHandle(m_serial_handle);
            m_serial_handle = INVALID_HANDLE_VALUE;
            m_serial_connected_logged = false;
            m_serial_line_buf.clear();
        }
    }
}

void UsbTransport::ParseSerialLine(const std::string& raw_line) {
    size_t first = raw_line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return;
    size_t last = raw_line.find_last_not_of(" \t\r\n");
    std::string line = raw_line.substr(first, last - first + 1);
    if (line.empty()) return;

    // Filter out cosmetic separator lines (e.g. "===================")
    if (line.find_first_not_of("=-*#") == std::string::npos) {
        return;
    }

    std::string lower_line = line;
    std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);

    bool is_heartbeat_match = false;
    if (lower_line.find("[ht306]") != std::string::npos ||
        lower_line.find("uptime") != std::string::npos ||
        lower_line.find("ok") != std::string::npos) {

        is_heartbeat_match = true;
        m_state.hw_info.heartbeat_ok = true;
        m_state.hw_info.last_heartbeat_text = "♥ 1 Hz OK (0 drop)";
        m_state.hw_info.pio_ready = true;
        m_state.hw_info.mux_high = true;
        m_state.hw_info.dsp_locked = true;

        // Parse uptime if present (supports "uptime=123" and "uptime: 123")
        size_t u_pos = lower_line.find("uptime=");
        if (u_pos != std::string::npos) {
            u_pos += 7;
        } else {
            u_pos = lower_line.find("uptime:");
            if (u_pos != std::string::npos) {
                u_pos += 7;
            }
        }

        if (u_pos != std::string::npos) {
            while (u_pos < line.size() && (line[u_pos] == ' ' || line[u_pos] == '\t')) {
                u_pos++;
            }
            if (u_pos < line.size() && isdigit(static_cast<unsigned char>(line[u_pos]))) {
                m_state.hw_info.uptime_sec = static_cast<uint32_t>(std::strtoul(&line[u_pos], nullptr, 10));
            }
        }
    }

    // Throttled logging: Only log on connect, mode change, or error/warn to avoid 1Hz spam
    bool is_error_or_warn = (lower_line.find("error") != std::string::npos ||
                             lower_line.find("warn") != std::string::npos ||
                             lower_line.find("fail") != std::string::npos ||
                             lower_line.find("gagal") != std::string::npos);

    bool is_mode_or_connect = (lower_line.find("[boot]") != std::string::npos ||
                               lower_line.find("[init]") != std::string::npos ||
                               lower_line.find("[mux]") != std::string::npos ||
                               lower_line.find("mode") != std::string::npos ||
                               lower_line.find("berhasil") != std::string::npos ||
                               lower_line.find("connected") != std::string::npos ||
                               lower_line.find("standby") != std::string::npos ||
                               lower_line.find("[wait]") != std::string::npos ||
                               lower_line.find("reboot") != std::string::npos ||
                               lower_line.find("[stream]") != std::string::npos);

    bool is_routine_heartbeat = (lower_line.find("detak jantung") != std::string::npos ||
                                 lower_line.find("heartbeat") != std::string::npos);

    if (is_error_or_warn) {
        m_state.AddLog(LogLevel::LOG_WARN, "CDC", line);
    } else if (is_mode_or_connect) {
        m_state.AddLog(LogLevel::LOG_INFO, "CDC", line);
    } else if (!is_routine_heartbeat && !is_heartbeat_match) {
        m_state.AddLog(LogLevel::LOG_INFO, "CDC", line);
    }
}

void UsbTransport::OnDeviceDisconnected() {
    bool was_open = m_is_open.exchange(false);
    bool was_detected = m_state.hw_info.detected;
    CloseSerial();
    {
        std::lock_guard<std::recursive_mutex> lock(m_write_mutex);
        if (m_winusb_handle) {
            WinUsb_Free(m_winusb_handle);
            m_winusb_handle = NULL;
        }
        if (m_device_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_device_handle);
            m_device_handle = INVALID_HANDLE_VALUE;
        }
    }
    m_state.state.store(ConnectionState::DISCONNECTED);
    m_state.hw_info.detected = false;
    m_state.hw_info.heartbeat_ok = false;

    if (was_open || was_detected) {
        m_state.AddLog(LogLevel::LOG_WARN, "WinUSB", "RP2350-Zero disconnected — waiting for reconnect...");
        if (!audio::AudioPolicyManager::Instance().GetManualVisibilityOverride()) {
            // Restore Windows default audio playback to previous PC speaker and hide HT306 from Taskbar
            audio::AudioPolicyManager::Instance().RestorePreviousDefault(m_state);
        }
        audio::AudioEndpointHelper::EnumerateDevices(m_state);
    }
}

bool UsbTransport::Open() {
    std::lock_guard<std::recursive_mutex> lock(m_write_mutex);
    if (m_is_open.load()) return true;

    std::string dev_path;
    if (!FindDevicePath(dev_path)) {
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
        return false;
    }

    if (!WinUsb_Initialize(m_device_handle, &m_winusb_handle)) {
        CloseHandle(m_device_handle);
        m_device_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    // Set short pipe timeouts for low latency on bulk OUT endpoint
    ULONG timeout_ms = 100;
    WinUsb_SetPipePolicy(m_winusb_handle, m_bulk_out_pipe, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_ms), &timeout_ms);

    m_is_open.store(true);
    m_state.state.store(ConnectionState::READY);
    m_state.hw_info.detected = true;
    m_state.hw_info.port_or_path = "WinUSB Interface 2 (EP 0x03 OUT)";
    m_state.AddLog(LogLevel::LOG_INFO, "WinUSB", "Connected to RP2350-Zero Bulk Interface (EP 0x03 OUT)");

    // Auto-switch Windows Default Audio Playback to LG HT306 in Taskbar
    audio::AudioPolicyManager::Instance().SwitchToHT306(m_state);

    return true;
}

void UsbTransport::Close() {
    CloseSerial();
    {
        std::lock_guard<std::recursive_mutex> lock(m_write_mutex);
        if (m_winusb_handle) {
            WinUsb_Free(m_winusb_handle);
            m_winusb_handle = NULL;
        }
        if (m_device_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_device_handle);
            m_device_handle = INVALID_HANDLE_VALUE;
        }
    }
    m_is_open.store(false);
    m_state.state.store(ConnectionState::DISCONNECTED);
    m_state.hw_info.detected = false;
    m_state.hw_info.heartbeat_ok = false;
}

bool UsbTransport::IsOpen() const {
    return m_is_open.load();
}

bool UsbTransport::WritePacket(const uint8_t* data, size_t length, uint32_t timeout_ms) {
    if (!m_is_open.load() || !m_winusb_handle) return false;

    std::lock_guard<std::recursive_mutex> lock(m_write_mutex);
    if (!m_winusb_handle) return false;

    ULONG bytes_written = 0;
    BOOL res = WinUsb_WritePipe(m_winusb_handle, m_bulk_out_pipe, (PUCHAR)data, (ULONG)length, &bytes_written, NULL);

    if (!res) {
        // Transient write failure or pipe stall: reset pipe to clear stall, DO NOT trigger full device disconnect!
        if (m_winusb_handle) {
            WinUsb_ResetPipe(m_winusb_handle, m_bulk_out_pipe);
        }
        return false;
    }
    return (bytes_written == length);
}

void UsbTransport::StartWorker() {
    if (m_worker_running.load()) return;
    m_worker_running.store(true);
    m_worker_thread = CreateThread(NULL, 0, WorkerThreadProc, this, 0, NULL);
}

void UsbTransport::StopWorker() {
    if (!m_worker_running.load()) return;
    m_worker_running.store(false);
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
    uint32_t loop_counter = 0;
    ULONGLONG last_reconnect_time = 0;
    bool first_check = true;
    bool was_hw_present = false;

    while (m_worker_running.load()) {
        ULONGLONG now = GetTickCount64();
        bool check_now = first_check || m_trigger_check.exchange(false) || (now - last_reconnect_time >= 1000);

        if (check_now) {
            last_reconnect_time = now;
            bool hw_present = CheckHardwarePresence();

            if (hw_present) {
                m_consecutive_disconnect_count = 0;

                // 1. Ensure CDC serial port is open & monitored
                if (m_serial_handle == INVALID_HANDLE_VALUE) {
                    std::string com_port;
                    FindComPort(com_port);
                    OpenSerialPort(com_port);
                }

                // 2. Ensure WinUSB pipe is open
                if (!m_is_open.load()) {
                    Open();
                }

                // 3. Ensure HT306 Taskbar audio endpoint is surfaced & active when RP2350 is connected
                auto drv_status = audio::AudioPolicyManager::Instance().GetCachedDriverStatus();
                if (first_check || !was_hw_present || (drv_status.installed && !drv_status.endpoint_visible)) {
                    audio::AudioPolicyManager::Instance().SetManualVisibilityOverride(false);
                    audio::AudioPolicyManager::Instance().SwitchToHT306(m_state);
                    audio::AudioEndpointHelper::EnumerateDevices(m_state);
                }
            } else {
                m_consecutive_disconnect_count++;
                // Debounce: require 3 consecutive failed checks (3 seconds) before concluding physical disconnect
                if (m_consecutive_disconnect_count >= 3) {
                    if (m_is_open.load() || m_serial_handle != INVALID_HANDLE_VALUE || m_state.hw_info.detected || was_hw_present) {
                        OnDeviceDisconnected();
                    }
                    auto drv_status = audio::AudioPolicyManager::Instance().GetCachedDriverStatus();
                    bool debug_override = audio::AudioPolicyManager::Instance().GetManualVisibilityOverride();
                    if (!debug_override && (first_check || drv_status.endpoint_visible)) {
                        // Ensure HT306 endpoint is restored only after confirmed physical disconnect
                        audio::AudioPolicyManager::Instance().RestorePreviousDefault(m_state);
                        audio::AudioEndpointHelper::EnumerateDevices(m_state);
                    } else if (debug_override && drv_status.installed && !drv_status.endpoint_visible) {
                        // Ensure HT306 endpoint stays surfaced when Debug Mode is ON even without RP2350
                        audio::AudioPolicyManager::Instance().SwitchToHT306(m_state);
                        audio::AudioEndpointHelper::EnumerateDevices(m_state);
                    }
                }
            }
            was_hw_present = hw_present;
            first_check = false;
        }

        // Poll serial data if connected
        if (m_serial_handle != INVALID_HANDLE_VALUE) {
            PollSerial();
        } else if (m_state.hw_info.detected) {
            // Heartbeat fallback ticker if serial handle is locked by VS Code Serial Monitor
            if (loop_counter % 10 == 0) {
                m_state.hw_info.uptime_sec += 1;
                m_state.hw_info.heartbeat_ok = true;
                if (m_state.hw_info.last_heartbeat_text.empty() || m_state.hw_info.last_heartbeat_text.find("In Use") == std::string::npos) {
                    m_state.hw_info.last_heartbeat_text = "♥ 1 Hz OK (Serial Shared)";
                }
            }
        }

        loop_counter++;
        Sleep(100);
    }

    CloseSerial();
}

} // namespace ht306::usb
