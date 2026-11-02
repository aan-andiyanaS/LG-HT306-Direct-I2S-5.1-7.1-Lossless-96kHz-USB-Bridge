# Rencana Implementasi Teknis: Aplikasi PC Companion (HT306 Audio Bridge)

## Ringkasan Proyek
Membangun aplikasi perantara di PC (**`HT306 Audio Bridge`**) untuk Windows 10/11 yang menangkap audio Windows secara real-time melalui WASAPI (Loopback atau Virtual Audio Sink Device), memadatkannya secara lossless menggunakan algoritma Delta-Rice 96 kHz 24-bit 5.1/Stereo, dan mentransfernya ke mikrokontroler Waveshare RP2350-Zero melalui pipa WinUSB Bulk (`EP 0x03 OUT`) tanpa menyebabkan lag pada PC.

## Keputusan Arsitektur
- **ADR-001:** Menggunakan Vendor Bulk + PC Companion Bridge daripada UAC2 native untuk menjamin 0% risiko kernel deadlock dan 100% bit-perfect transmission.
- **Native WinUSB:** Memanggil `winusb.dll` dan `setupapi.dll` langsung bawaan Windows (tanpa memerlukan instalasi driver Zadig atau runtime eksternal).
- **WASAPI Event-Driven Capture:** Buffer latensi rendah (< 5 ms) untuk audio sinkronisasi lip-sync film dan game.

---

## Tahapan Implementasi (Vertical Slices)

### Fase 1: Fondasi Transport USB & Encoder Lossless
- [ ] **Task 1.1: Wrapper Native WinUSB (`pc_streamer/ht306_bridge/winusb_transport.py`)**
  - Mengimplementasikan deteksi otomatis perangkat RP2350-Zero (`VID: 0x2E8A`, `PID: 0x4306`).
  - Membuka handle WinUSB ke Interface 2 (Vendor Class) dan Endpoint `0x03` OUT (Bulk).
  - Mengimplementasikan pengiriman blok 64-byte asinkron dengan penanganan auto-reconnect saat kabel dicabut/dipasang.
  - Verifikasi: Script deteksi mendeteksi RP2350-Zero yang terhubung dan sukses mengirim paket dummy.
- [ ] **Task 1.2: Modul Encoder Delta-Rice Real-Time (`pc_streamer/ht306_bridge/lossless_encoder.py`)**
  - Mengintegrasikan encoder FLAC Order-4 + Golomb-Rice residual yang menghasilkan format paket `HT306_LosslessHeader_t` (Sync Word `0xA55A`, CRC-8, 14-byte header + anchor + payload).
  - Verifikasi: Hasil enkripsi identik secara bit-perfect dengan `verify_end_to_end.py`.

### Checkpoint 1: Fondasi Transport & Codec
- [ ] Unit test codec lulus 100% bit-perfect (0 bit error).
- [ ] RP2350-Zero merespons penerimaan paket tanpa error buffer.

---

### Fase 2: Mesin Penangkapan Audio Windows (WASAPI Capture Engine)
- [ ] **Task 2.1: Modul WASAPI Audio Capture (`pc_streamer/ht306_bridge/audio_capture.py`)**
  - Mengimplementasikan penangkapan audio Windows via WASAPI Loopback Capture (`AUDCLNT_STREAMFLAGS_LOOPBACK`) dan Virtual Audio Device Sink (VB-Audio Cable).
  - Mendukung sample rate 48.000 Hz dan 96.000 Hz, stereo 2.0 dan surround 5.1/7.1.
  - Normalisasi sampel audio menjadi PCM 24-bit signed integer dalam format interleaved untuk diserahkan ke encoder.
  - Verifikasi: Menangkap audio YouTube/Spotify secara real-time dan memverifikasi level amplitudo suara.

### Checkpoint 2: Audio Capture
- [ ] Stream audio dari Windows berhasil tertangkap secara kontinyu tanpa buffer underrun.

---

### Fase 3: Integrasi Pipeline End-to-End & Monitor CDC Serial
- [ ] **Task 3.1: Pipeline Orkestrasi (`pc_streamer/ht306_bridge/app.py`)**
  - Menghubungkan aliran `audio_capture` -> `lossless_encoder` -> `winusb_transport` secara streaming real-time dengan latensi < 10 ms.
  - Menangani dynamic sample rate switching (48 kHz vs 96 kHz).
- [ ] **Task 3.2: Klien Diagnostik Serial CDC (`pc_streamer/ht306_bridge/cdc_monitor.py`)**
  - Terhubung ke port serial CDC ACM (`COM4`) secara paralel di thread terpisah.
  - Membaca detak jantung, uptime, watermark buffer DMA, dan status underrun RP2350-Zero.
  - Verifikasi: Serial monitor menampilkan statistik real-time sinkron dengan audio yang diputar.

### Checkpoint 3: End-to-End Streaming
- [ ] Audio Windows mengalir mulus ke speaker LG HT306 via RP2350.
- [ ] Komputer Windows 100% bebas lag dan CPU usage < 1%.

---

### Fase 4: Antarmuka Pengguna & Paket Rilis Mandiri
- [ ] **Task 4.1: Antarmuka System Tray & Console HUD (`pc_streamer/ht306_bridge/ui_tray.py`)**
  - Menyediakan icon status di System Tray Windows (warna hijau saat streaming, abu-abu saat standby).
  - Menu konteks klik kanan: Pilih Audio Device, Mode Bit-Perfect vs Desktop, Buka Monitor Diagnostik, Exit.
- [ ] **Task 4.2: Skrip Otomasi Peluncuran & Build Standalone Executable**
  - Membuat `run_bridge.bat` untuk peluncuran instan satu klik.
  - Menyusun konfigurasi PyInstaller untuk menghasilkan file `.exe` mandiri tanpa dependensi Python.
  - Verifikasi: Aplikasi `.exe` dapat dijalankan langsung di Windows.

---

## Matriks Risiko & Mitigasi

| Risiko | Dampak | Mitigasi |
| :--- | :---: | :--- |
| **Kabel USB longgar/tercabut saat lagu diputar** | Sedang | Thread transport mendeteksi `ERROR_DEVICE_NOT_CONNECTED` dan beralih ke state auto-reconnect setiap 500 ms tanpa crash. RP2350 otomatis memainkan silence block. |
| **Peralihan sample rate mendadak (misal 44.1k ke 96k)** | Rendah | Audio capture me-restart client WASAPI secara mulus dan mengupdate flag `HT306_FLAG_RATE_96KHZ` di header paket. |
| **Latensi lip-sync saat menonton video/film** | Sedang | Menggunakan WASAPI event-driven buffer ukuran kecil (< 5 ms) dan sub-frame encoder 48 sampel (0,5 ms). Total latensi tetap di bawah 10 ms. |
