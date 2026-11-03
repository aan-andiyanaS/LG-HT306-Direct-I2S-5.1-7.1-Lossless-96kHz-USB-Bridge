# Daftar Tugas Implementasi (Task Breakdown)

- [x] **Slice 1: Implementasi & Verifikasi Codec Lossless Delta-Rice 96 kHz (6-Channel 24-Bit)**
  - Acceptance: Encoder dan Decoder berhasil mengompres dan merekonstruksi 6-channel 96 kHz 24-bit PCM dengan `0 bit error` (`Bit-Perfect`) dan throughput `< 880 KB/s` (Tercapai: **`703.3 KB/detik` / `40.7%`**).
  - Verify: Lulus verifikasi `tests/verify_end_to_end.py`.
  - Files: `firmware/include/lossless_codec.h`, `firmware/src/lossless_codec.c`, `firmware/tests/test_lossless_codec.c`

- [x] **Slice 2: Mesin PIO Multi-Lane I2S & Ring Buffer DMA untuk `RP2350-Zero`**
  - Acceptance: Program assembly PIO dan driver DMA mendukung frekuensi 48 kHz (`BCK = 3.072 MHz`) dan 96 kHz (`BCK = 6.144 MHz`) pada pin `GP2`–`GP7`.
  - Verify: Lulus simulasi shift register PIO 4-lane pada `tests/verify_end_to_end.py`.
  - Files: `firmware/src/i2s_multi.pio`, `firmware/include/i2s_engine.h`, `firmware/src/i2s_engine.c`

- [x] **Slice 3: Firmware Utama `RP2350-Zero` (USB Descriptors, Dual-Core Orchestration, WS2812 & HT306 Control)**
  - Acceptance: Core 0 menangani USB/DMA, Core 1 menangani *Lossless Unpacking*, `GP8`/`GP9`/`GP16` menangani saklar, remote LG, dan indikator LED RGB `RP2350-Zero`.
  - Verify: Semua modul terintegrasi di `firmware/CMakeLists.txt`.
  - Files: `firmware/src/usb_descriptors.c`, `firmware/include/ht306_control.h`, `firmware/src/ht306_control.c`, `firmware/src/main.c`, `firmware/CMakeLists.txt`

- [x] **Slice 4: Modul PC Streamer 96 kHz untuk JRiver Media Center**
  - Acceptance: Modul pengirim PC dapat mengemas blok 6-channel 96 kHz/24-bit PCM menjadi paket USB Lossless dan melewati benchmark performa.
  - Verify: Terverifikasi end-to-end dengan `tests/verify_end_to_end.py`.
  - Files: `pc_streamer/src/main.c`, `pc_streamer/CMakeLists.txt`, `tests/verify_end_to_end.py`

- [x] **Slice 5: Audit Kesiapan Kompilasi & Flash ke Waveshare `RP2350-Zero` (Pico SDK & TinyUSB)**
  - Acceptance: Seluruh konfigurasi `tusb_config.h`, callback TinyUSB, `tusb_init()`/`ht306_usb_task()`, auto-switching clock PIO (`48 kHz` vs `96 kHz`), memory barrier acquire/release (`__dmb()`), dan penyatuan fragmen paket USB Bulk 64-byte (`packet_len`) lengkap tanpa simbol atau header yang hilang serta dipangkas dengan prinsip `ponytail`.
  - Verify: Lulus `[TEST 1]` s/d `[TEST 4]` pada `tests/verify_end_to_end.py`.
  - Files: `firmware/include/tusb_config.h`, `firmware/include/ht306_usb.h`, `firmware/src/usb_descriptors.c`, `firmware/src/main.c`, `firmware/src/i2s_engine.c`, `firmware/include/lossless_codec.h`, `firmware/src/lossless_codec.c`, `firmware/CMakeLists.txt`

- [x] **Slice 6: Dokumentasi Profesional `README.md` & `LICENSE` (MIT License)**
  - Acceptance: `README.md` profesional mencakup latar belakang, bedah arsitektur LG HT306 (`MT1389MH` + `PS9850` + `TAS5352`), diagram arsitektur Mermaid, spesifikasi matematis FLAC Order-4 Golomb-Rice, diagram sekuens PIO 4-Lane I2S, tabel pinout hardware, hasil benchmark, panduan build `.uf2`, konfigurasi JRiver DSP Studio, dan MIT License.
  - Files: `README.md`, `LICENSE`

- [x] **Slice 7: Struktur Dokumentasi Bilingual (English Default + Bahasa Indonesia) & Penghapusan PRD**
  - Acceptance: Menghapus file PRD, menjadikan Bahasa Inggris sebagai bahasa default di seluruh dokumentasi (`README.md` dan `docs/*.md`), menyediakan versi Bahasa Indonesia lengkap (`README.id.md` dan `docs/*.id.md`) dengan tombol peralihan bahasa di setiap file, dan melakukan push ke repositori GitHub.
  - Files: `README.md`, `README.id.md`, `docs/*.md`, `docs/*.id.md`

- [x] **Slice 8: Integrasi USB CDC ACM Serial Monitor, Detak Jantung Diagnostik & Kompatibilitas Build Pico SDK 2.3.1**
  - Acceptance: Mengaktifkan USB CDC ACM Interface 0/1 (`COM4`) bersamaan dengan Vendor Bulk Interface 2 melalui IAD descriptor, memperbaiki dependensi DTR pada `tud_cdc_connected()`, menambahkan detak jantung periodik 1 detik, echo respon interaktif serial, perbaikan header `pico.h`, integrasi konfigurasi Pico VS Code Extension (`pico-vscode.cmake`), dan berhasil kompilasi file tunggal `ht306_rp2350_zero.uf2`.
  - Verify: Serial Monitor VS Code pada COM4 menampilkan banner boot dan heartbeat real-time setiap 1 detik.
  - Files: `firmware/include/tusb_config.h`, `firmware/include/ht306_usb.h`, `firmware/src/usb_descriptors.c`, `firmware/src/main.c`, `firmware/src/lossless_codec.c`, `firmware/CMakeLists.txt`

- [x] **Slice 9: Arsitektur Pivot — Restorasi Vendor Bulk Lossless Audio Bridge & Eliminasi Total Lag Kernel PC**
  - Acceptance:
    1. Mengembalikan TinyUSB Device Descriptor ke konfigurasi stabil PRD awal: Vendor Bulk (`CFG_TUD_VENDOR = 1`) + CDC ACM Serial Monitor (`CFG_TUD_CDC = 1`), mematikan UAC2 (`CFG_TUD_AUDIO = 0`) untuk mengeliminasi ketergantungan pada kernel driver `usbaudio2.sys` yang memicu lag/freeze PC.
    2. Membawa seluruh hal plus stabilitas:
       - Buffer bounds check pada `ht306_usb_process_vendor_stream` untuk proteksi memori.
       - Core 1 queue idle sleep (`sleep_us(50)`) untuk mengeliminasi saturasi crossbar AHB SRAM 150 MHz.
       - Caching nominal clkdiv pada DMA IRQ untuk zero runtime float division.
       - Guard `tud_cdc_connected()` pada pengiriman banner dan heartbeat serial monitor.
       - Seluruh alokasi buffer audio ditempatkan secara `static` di SRAM untuk proteksi stack overflow batas 2 KB Core 0 & Core 1.
       - Pembersihan dead code dan dead buffer (hemat 1.152 Byte SRAM).
       - Perbaikan `.gitignore` untuk file PDF manual 13 MB.
    3. Kompilasi firmware sukses menghasilkan `ht306_rp2350_zero.uf2` (69.120 Byte) dengan status 100% lulus bit-perfect pada `tests/verify_end_to_end.py`.
  - Files: `firmware/CMakeLists.txt`, `firmware/include/tusb_config.h`, `firmware/include/ht306_usb.h`, `firmware/include/i2s_engine.h`, `firmware/pico_sdk_import.cmake`, `firmware/src/i2s_engine.c`, `firmware/src/lossless_codec.c`, `firmware/src/main.c`, `firmware/src/usb_descriptors.c`, `.gitignore`

- [x] **Slice 10: Dukungan Native 32-bit Lossless Codec (5.1 Surround 96 kHz True Studio Master)**
  - Acceptance: Menambahkan dukungan resolusi 32-bit (`HT306_FLAG_BITDEPTH_32`) pada encoder dan decoder Delta-Rice di `lossless_codec.h` dan `lossless_codec.c`. Sinyal 32-bit penuh dialirkan langsung tanpa bit-truncation ke slot 32-bit I2S Pulsus PS9850 di LG HT306.
  - Verify: Lulus verifikasi `[TEST 1A]` (24-bit) dan `[TEST 1B]` (32-bit) dengan 0 bit error dari 115.200 sampel pada `tests/verify_end_to_end.py`. Kompilasi firmware `ht306_rp2350_zero.uf2` sukses (70.144 Byte).
  - Files: `firmware/include/lossless_codec.h`, `firmware/src/lossless_codec.c`, `tests/verify_end_to_end.py`

- [x] **Slice 11: Audit Komprehensif Adversarial, Optimasi Ponytail, Penguatan Ketahanan Hardware & Zero-Error Approval**
  - Acceptance:
    1. Memperbaiki DMA ping-pong lockup dengan reload `trans_count` (`dma_channel_set_trans_count`) sebelum `read_addr`.
    2. Mengubah penulisan buffer I2S menjadi All-or-Nothing Atomic Subframe Check untuk mencegah phase shift antar-kanal.
    3. Mengimplementasikan driver LED RGB WS2812 fisik pada pin GP16 dengan cache guard non-blocking (menghemat >6.2% CPU Core 1).
    4. Mengganti signed integer overflow UB pada prediktor 32-bit dengan modular arithmetic `uint32_t` ISO C murni.
    5. Mengisolasi encoder khusus untuk target PC/test harness sehingga memangkas 2.3 KB SRAM di BSS dan mengurangi biner hingga ~52 KB.
    6. Mencegah race condition SPSC Wait-Free ring buffer dengan menjaga invarian Producer-only write counter dan Consumer-only read counter.
    7. Menambahkan DMA partner channel kick-start auto-recovery untuk mencegah freezing jika terjadi stalling eksternal > 1 ms.
    8. Mengganti polling busy-wait Core 1 dengan ARM Cortex-M event handling (`__wfe()` & `__sev()`).
    9. Memperbaiki USB bulk reassembler alignment guard saat buffer overflow atau desinkronisasi framing.
    10. Lulus 100% verifikasi adversarial subagent Senior Code Reviewer dengan verdict **APPROVE**.
  - Verify: Lulus `tests/verify_end_to_end.py` (24-bit & 32-bit 100% bit-exact) dan kompilasi Pico SDK 2.3.1 sukses menghasilkan `ht306_rp2350_zero.uf2` (70.656 Byte).
  - Files: `firmware/include/ht306_usb.h`, `firmware/src/i2s_engine.c`, `firmware/src/lossless_codec.c`, `firmware/src/ht306_control.c`, `firmware/src/main.c`, `firmware/src/usb_descriptors.c`, `firmware/tests/test_lossless_codec.c`

- [x] **Slice 12: Pembuatan Aplikasi PC Companion (HT306 Audio Bridge) untuk Windows Audio Loopback & Virtual Device Sink**
  - Acceptance: Membangun aplikasi PC Companion yang menangkap audio Windows via WASAPI Loopback / Virtual Device Sink, mengemasnya secara real-time ke format Lossless Delta-Rice 96 kHz 24-bit/32-bit 5.1, dan mengirimkannya ke RP2350-Zero melalui WinUSB Bulk Pipe tanpa menyentuh kernel subsystem audio Windows.
  - Hasil & Verifikasi:
    1. **Arsitektur Memori Ultra-Efisien**: Pengukuran runtime membuktikan penggunaan RAM Python Engine murni hanya **~32-34 MB**, jauh di bawah batas maksimal 50 MB yang ditetapkan user.
    2. **Identifikasi Perangkat Anti-Salah Tebak (4-Factor Authentication)**: Berhasil mengisolasi RP2350-Zero secara deterministik dari ESP32, Pico standar, CH340, CP2102, dll. via 4 lapis validasi (VID/PID, Manufacturer/Product/Serial strings, WinUSB Bulk Endpoint 0x03 OUT, dan CDC Handshake marker `[HT306]`).
    3. **UI Desktop Modern & Responsive (Edge App Mode)**: Mengintegrasikan seluruh 7 antarmuka Stitch (Dashboard, Mixer, Routing Matrix, Profiles, 5.1 Acoustic Sweep Test, Log Diagnostics, Settings) ke dalam Single Page Application yang disajikan via Python Standard Library `ThreadingHTTPServer` (tanpa dependensi framework pihak ketiga).
    4. **Dukungan Microsoft OS 2.0 WinUSB Descriptors**: Menambahkan BOS & MS OS 2.0 descriptors di firmware agar Windows 10/11 otomatis memasang `winusb.sys` tanpa perlu Zadig manual.
    5. **Test Suite Komprehensif**: Sebanyak 64 unit test unit & integration di `pc_streamer/tests/` lulus 100% (termasuk verifikasi isolasi RAM subprocess < 45 MB). Ditambahkan hardware test harness di `pc_streamer/tests_hw/`.
  - Files: `pc_streamer/ht306bridge/`, `pc_streamer/tests/`, `pc_streamer/tests_hw/`, `firmware/src/usb_descriptors.c`

- [x] **Slice 13: Perbaikan Tampilan 7-Halaman UI & Resolusi Deteksi Hardware RP2350-Zero pada Windows 11**
  - Acceptance:
    1. Menyelesaikan masalah "kenapa hanya ada 2 page":
       - Mendiagnosis dan memperbaiki tag nesting HTML: Menghapus 5 ekstra tag `</div>` penutup pada `tab-mixer`, `tab-routing`, `tab-profiles`, `tab-test`, dan `tab-settings` yang sebelumnya menutup kontainer `<main>` secara prematur.
       - Mengubah tag inner `<section>` di dalam `tab-test` menjadi `<div>` untuk mencegah browser menutup outer section lebih awal.
       - Menstandarkan seluruh 7 tab menjadi wrapper `<div id="tab-..." class="tab-panel...">` yang terbukti berada rapi di dalam `html > body > div.pl-56 > main`.
       - Diverifikasi otomatis dengan Playwright MCP: Ke-7 tab (Dashboard, Mixer, Routing, Profiles, Test, Log, Settings) berhasil diklik dan merender seluruh kontrol interaktifnya secara sempurna.
    2. Menyelesaikan masalah "RP2350 tidak terdeteksi":
       - Mendiagnosis pembatasan libusb Endpoint 0 pada Windows composite parent driver (`usbccgp.sys`) yang menyebabkan `util.get_string` gagal dengan permission issue (`mfr=None, prod=None, sn=None`).
       - Menambahkan fallback PnP di `discovery.py`: jika string USB tidak dapat diakses via Endpoint 0, serial number diambil dari Windows PnP registry cache (`list_ports.comports()`).
       - Menoleransi `PermissionError: Access is denied` pada handshake serial jika `COM4` sedang dimonitor oleh VS Code Serial Monitor, sehingga streaming audio tidak terblokir.
       - Menambahkan `MS_OS_20_FEATURE_REG_PROPERTY` dengan GUID `{CDB3B5AD-293B-4663-AA36-1AAE46463776}` ke Microsoft OS 2.0 Descriptors di `firmware/src/usb_descriptors.c` agar Windows membuat symlink antarmuka device path untuk WinUSB.
       - Menambahkan perintah bootloader `reset_usb_boot()` pada serial CDC monitor untuk kemudahan reboot ke BOOTSEL mode via software.
       - Kompilasi ulang firmware sukses menghasilkan `firmware/build/ht306_rp2350_zero.uf2` (71.168 Byte).
       - Menghapus panggilan ilegal `dev.set_configuration()` pada Windows di `transport.py`.
    3. Verifikasi Penuh:
       - 64/64 Unit Test lulus 100% (`pytest pc_streamer/tests -v`).
       - Hardware Test nyata terverifikasi lulus (`pytest pc_streamer/tests_hw --hardware`): RP2350-Zero fisik terdeteksi seketika pada `COM4` dengan VID/PID `0x2E8A:0x4306` dan serial `HT306-RP2350Z-96K`.
  - Files: `pc_streamer/ht306bridge/discovery.py`, `pc_streamer/ht306bridge/transport.py`, `pc_streamer/ht306bridge/web/index.html`, `firmware/src/usb_descriptors.c`, `firmware/src/main.c`, `firmware/build/ht306_rp2350_zero.uf2`

- [x] **Slice 14: Perbaikan Dual-Access Enum State pada UI Server & Stabilisasi Koneksi Real-Time**
  - Acceptance:
    1. Menyelesaikan TypeError: 'State' object is not callable pada endpoint GET /api/status di ui_server.py:296 dan cli.py:181.
    2. Menambahkan implementasi method __call__(self) pada enum State di 	ransport.py sehingga mendukung dual-access (sebagai property 	ransport.state maupun pemanggilan fungsi 	ransport.state()).
    3. Memperbaiki pemanggilan aman di ui_server.py dan cli.py (
aw_st = transport.state; st = raw_st() if callable(raw_st) else raw_st).
    4. Menyesuaikan assertion di 	est_hw_stream.py.
    5. Verifikasi: 64/64 unit tests lulus 100%, user mengonfirmasi aplikasi berjalan normal ('oke, bisa') dan status di UI terhubung/aktif secara lancar.
  - Files: pc_streamer/ht306bridge/transport.py, pc_streamer/ht306bridge/ui_server.py, pc_streamer/ht306bridge/cli.py, pc_streamer/tests_hw/test_hw_stream.py

- [x] **Slice 15: Pembangunan Native C/C++ Dear ImGui Desktop App (\pc_stream_app\) Identik dengan Stitch Design System**
  - Acceptance:
    1. Menyiapkan toolchain MinGW-w64 GCC 16.2.0 portabel (\w64devkit\) dan library Dear ImGui v1.91.8.
    2. Mengimplementasikan antarmuka Dear ImGui DirectX 11 native murni yang sama persis dengan Stitch Design System:
       - Sidebar kiri 200px (7 tab: Dashboard, Mixer, Routing, Profiles, Test, Log, Settings).
       - Header bar (Status pill, Profile selector, Quick stream toggle).
       - Hardware Status Card (Uptime, Heartbeat, PIO I2S, GP8 MUX, DSP Sync Lock).
       - Stream & Format Engine (4 parameter boxes 96kHz, 32-bit, 5.1, Delta-Rice + Capture Device bar).
       - Link Budget & Buffer (Gradient bar 68% safe envelope + 4 micro telemetry tiles).
       - 6-Channel Live VU Meters (Strips horizontal FL, FR, C, LFE, SL, SR dengan skala dBFS, peak hold line, dan gradien warna halus).
       - 5.1 Acoustic Sweep Speaker Test (Diagram ruangan surround dengan animasi pulse ring pada speaker aktif).
       - Halaman Mixer (6 fader gain trim) & Routing (Matrix 6x6).
    3. Mengintegrasikan WinUSB Bulk transport (EP 0x03 OUT) dan WASAPI Loopback capture engine.
    4. Mengunduh dan menyiapkan paket driver virtual audio resmi (VB-Audio Virtual Cable) di \pc_stream_app/tools/vbcable\ agar dapat memunculkan endpoint audio di taskbar Windows.
    5. Sukses kompilasi biner mandiri \pc_stream_app/build/ht306_stream_app.exe\ (2.22 MB, zero runtime dependencies, ultra-ringan ~10-12 MB RAM).
  - Files: \pc_stream_app/CMakeLists.txt\, \pc_stream_app/src/\ (main.cpp, ui_theme.h/cpp, ui_sidebar.h/cpp, ui_dashboard.h/cpp, ui_sweep_test.h/cpp, usb_transport.h/cpp, audio_capture.h/cpp, audio_endpoint_helper.h/cpp, app_state.h)
