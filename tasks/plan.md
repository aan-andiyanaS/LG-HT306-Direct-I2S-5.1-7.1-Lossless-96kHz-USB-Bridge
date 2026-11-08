# Implementation Plan: Dynamic Audio Endpoint Presence (Show/Hide on RP2350 Connection)

## Overview
Mengimplementasikan manajemen visibilitas dinamis (*Dynamic Endpoint Presence*) untuk perangkat audio Windows **`LG HT306 Home Theater (5.1 Surround)`** sehingga perangkat tersebut **hanya muncul di Windows Taskbar Sound Output ketika hardware RP2350-Zero benar-benar terhubung ke USB**, dan **otomatis disembunyikan (serta mengembalikan output ke speaker bawaan PC)** ketika RP2350-Zero dicabut atau belum dihubungkan.

## Architecture Decisions
1. **Zero-UAC Real-Time `DeviceState` Switching via `KEY_SET_VALUE` (`0x0002`):**
   - Menggunakan Win32 API `RegOpenKeyExW(HKEY_LOCAL_MACHINE, ..., 0, KEY_SET_VALUE, &hKey)` pada `SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{GUID}` untuk mengubah nilai `DeviceState`:
     - **RP2350 Terhubung (`Active`):** `DeviceState = 0x00000001` (`DEVICE_STATE_ACTIVE`) → perangkat langsung muncul di Windows Taskbar Sound Output.
     - **RP2350 Tidak Terhubung / Dicabut (`Hidden/Standby`):** `DeviceState = 0x10000002` (`DEVICE_STATE_DISABLED`) → perangkat langsung menghilang dari Windows Taskbar Sound Output, sementara `CABLE In 16ch` tetap dikunci pada `0x10000002`.
2. **Pemisahan Status `installed` vs `endpoint_visible` pada `AudioDriverStatus`:**
   - Saat `DeviceState = 0x10000002`, fungsi `IMMDeviceEnumerator::EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE)` tidak lagi mengembalikan endpoint HT306.
   - Oleh karena itu, `AudioPolicyManager::CheckDriverStatus()` akan menggunakan mask `DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED` (serta verifikasi registry `VBAudioVACWDM` non-16ch) dan mengisi struktur `AudioDriverStatus`:
     - `installed = true` (driver sudah terpasang di PC).
     - `endpoint_visible = (dwState == DEVICE_STATE_ACTIVE)` (apakah saat ini sedang dimunculkan di Taskbar).
     - `is_default = (endpoint_visible && current_default == dev_id)`.
3. **Fallback Default Speaker Otomatis sebelum Menyembunyikan Endpoint:**
   - Sebelum menyembunyikan `LG HT306 Home Theater` (`SetEndpointVisibility(false)`), jika `m_saved_previous_device_id` kosong (misal aplikasi baru dibuka saat RP2350 tidak terhubung namun HT306 masih menjadi default dari sesi sebelumnya), `RestorePreviousDefault()` otomatis mencari endpoint `DEVICE_STATE_ACTIVE` pertama yang bukan HT306/VB-Cable (contoh: `Speakers (Realtek(R) Audio)`) dan memindahkannya via `IPolicyConfig::SetDefaultEndpoint`.
4. **Sinkronisasi Siklus Hidup (`UsbTransport` & `main_webview.cpp`):**
   - **Saat Startup (`WinMain`):** Mengecek `g_UsbTransport.CheckHardwarePresence()`. Jika RP2350 belum dicolok, memanggil `RestorePreviousDefault(g_State)` (yang sekaligus menyembunyikan endpoint dari Taskbar). Jika RP2350 sudah dicolok, memanggil `SwitchToHT306(g_State)` (yang memunculkan endpoint di Taskbar dan menjadikannya default).
   - **Saat Hot-Plug Masuk (`CheckHardwarePresence()` / `Open()`):** Memanggil `SwitchToHT306(m_state)` → `SetEndpointVisibility(true)` → tunggu hingga `Audiosrv` mendaftarkan `DEVICE_STATE_ACTIVE` (polling cepat $\le 400\text{ ms}$) → `SetDefaultEndpoint` + `ConfigureEndpointSurround96k`.
   - **Saat Hot-Plug Cabut (`OnDeviceDisconnected()`) & Saat Aplikasi Ditutup (`WM_CLOSE` / `WM_DESTROY`):** Memanggil `RestorePreviousDefault(m_state)` → kembalikan speaker PC → `SetEndpointVisibility(false)`.

## Task List

### Phase 1: Backend AudioPolicyManager (`audio_policy_manager.h/cpp`)
- [x] **Task 1: Perluas `AudioDriverStatus` & `CheckDriverStatus()` untuk Mendukung Status `Standby / Hidden`**
  - **Acceptance:** `AudioDriverStatus` memiliki field `bool endpoint_visible = false;`. `CheckDriverStatus()` memindai `DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED` (mengecualikan `16ch`) sehingga ketika endpoint disembunyikan (`0x10000002`), `installed` tetap `true` dan `endpoint_visible` bernilai `false`.
  - **Verify:** Kompilasi bersih via Ninja.
  - **Files:** `pc_stream_app/src/audio_policy_manager.h`, `pc_stream_app/src/audio_policy_manager.cpp`

- [x] **Task 2: Implementasi `SetEndpointVisibility(bool visible)` & Integrasi ke `SwitchToHT306()` / `RestorePreviousDefault()`**
  - **Acceptance:**
    - `SetEndpointVisibility(bool visible)` mengubah `DeviceState` endpoint utama HT306 (`0x00000001` saat `true`, `0x10000002` saat `false`) via `RegOpenKeyExW(..., KEY_SET_VALUE)` tanpa menyentuh `16ch` (yang selalu `0x10000002`).
    - `SwitchToHT306(AppState&)` memanggil `SetEndpointVisibility(true)`, mem-polling `CheckDriverStatus()` hingga `endpoint_visible == true` (maks 10x40ms), menyimpan endpoint non-HT306 sebelumnya, lalu menyetel default endpoint + format 5.1 96 kHz.
    - `RestorePreviousDefault(AppState&)` memindahkan default playback ke speaker PC (menggunakan `m_saved_previous_device_id` atau fallback speaker non-HT306 aktif pertama), lalu memanggil `SetEndpointVisibility(false)`.
  - **Verify:** Kompilasi bersih via Ninja.
  - **Files:** `pc_stream_app/src/audio_policy_manager.h`, `pc_stream_app/src/audio_policy_manager.cpp`

### Checkpoint 1: Backend AudioPolicyManager
- [x] Build C++ berhasil tanpa warning/error dan transisi `SetEndpointVisibility(true/false)` teruji.

### Phase 2: Integrasi USB Lifecycle & Frontend UI (`usb_transport.cpp`, `main_webview.cpp`, `ui/index.html`)
- [x] **Task 3: Integrasi State Kehadiran Hardware RP2350 (`usb_transport.cpp` & `main_webview.cpp`)**
  - **Acceptance:**
    - Saat startup di `WinMain`, visibilitas endpoint langsung disinkronkan dengan `g_UsbTransport.CheckHardwarePresence()`.
    - Di `UsbTransport::WorkerLoop()` dan `OnDeviceDisconnected()`, transisi `hw_present` (`false -> true` maupun `true -> false`) selalu memicu `SwitchToHT306(m_state)` / `RestorePreviousDefault(m_state)` dan memperbarui `AudioEndpointHelper::EnumerateDevices(m_state)`.
    - JSON telemetri `audio_driver` menyertakan `"visible": true/false`.
  - **Verify:** Kompilasi bersih via Ninja.
  - **Files:** `pc_stream_app/src/usb_transport.cpp`, `pc_stream_app/src/main_webview.cpp`

- [x] **Task 4: Pembaruan Indikator Status UI (`pc_stream_app/ui/index.html`)**
  - **Acceptance:**
    - Saat `installed == true` dan `visible == false` (RP2350 belum dicolok):
      - Badge Header menampilkan **`Taskbar: Standby (RP2350 Off)`** (bukan `+ Pasang Driver Taskbar`).
      - Kartu Settings menampilkan **`Terpasang — Siaga (Muncul Saat RP2350 Terhubung)`** beserta tombol manual **`Tampilkan Manual`** jika pengguna ingin menguji tanpa RP2350.
    - Saat `installed == true` dan `visible == true` (RP2350 terhubung):
      - Badge Header menampilkan **`Taskbar: LG HT306 Home Theater (5.1 Surround)`** berwarna hijau aktif.
  - **Verify:** Verifikasi visual UI dan daftar Sound Output di Windows Taskbar.
  - **Files:** `pc_stream_app/ui/index.html`, `pc_stream_app/src/main_webview.cpp`

### Checkpoint 2: End-to-End Verification
- [x] Saat RP2350 tidak dicolok: `LG HT306 Home Theater` tidak muncul di Windows Taskbar Sound Output, audio PC keluar lewat speaker bawaan.
- [x] Saat RP2350 dicolok (atau disimulasikan aktif): `LG HT306 Home Theater (5.1 Surround)` muncul di Windows Taskbar dalam `< 200 ms` dan otomatis menjadi default speaker.

## Risks and Mitigations
| Risk | Impact | Mitigation |
|------|--------|------------|
| Jeda propagasi `Audiosrv` (~20–80 ms) setelah `DeviceState` diubah ke `1` sebelum `IPolicyConfig::SetDefaultEndpoint` dipanggil | Medium | Menambahkan *short readiness polling* (maks 10 iterasi $\times$ 40 ms) di dalam `SwitchToHT306()` hingga `IMMDeviceEnumerator` melaporkan endpoint aktif. |
| Aplikasi dimulai saat RP2350 tidak terhubung, tetapi dari sesi sebelumnya `LG HT306` masih berstatus default speaker (`m_saved_previous_device_id` kosong) | Medium | Menambahkan *Fallback Non-HT306 Speaker Finder* di `RestorePreviousDefault()` yang mencari endpoint aktif pertama selain VB-Cable/HT306 sebelum menyembunyikan endpoint HT306. |
| Pengguna ingin melakukan pengujian *5.1 Acoustic Sweep Test* tanpa mencolokkan RP2350 | Low | Menyediakan tombol manual *"Tampilkan di Taskbar"* di tab Settings (dan generator Sweep Test tetap berjalan menggunakan QPC clock internal meskipun endpoint disembunyikan). |
