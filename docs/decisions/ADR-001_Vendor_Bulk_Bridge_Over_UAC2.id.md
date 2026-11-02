# ADR-001: Transport Audio Vendor Bulk dengan PC Companion Bridge Dibandingkan UAC2 Native

## Status
Diterima (Accepted)

## Tanggal
2026-11-03

## Konteks
Percobaan implementasi awal difokuskan pada menghadirkan Waveshare RP2350-Zero sebagai perangkat *native USB Audio Class 2.0 (UAC2) Desktop Speaker* menggunakan driver bawaan Windows `usbaudio2.sys`. Meskipun di atas kertas UAC2 tidak memerlukan perangkat lunak perantara, pengujian riil pada perangkat keras RP2350 memicu kegagalan sistematis tingkat kernel:
1. **Deadlock Kernel Windows & Latensi DPC:** Driver `usbaudio2.sys` Windows secara kaku mengasumsikan topologi USB High-Speed (480 Mbps, interval mikroframe 125 µs). Pada Full-Speed (12 Mbps, frame 1 ms), keterlambatan interval *feedback isokronus* memicu transfer pemblokiran IRP/URB sinkron di tingkat kernel (timeout 5.000 ms), menyebabkan stuttering sistem, UI membeku (freeze/lag parah), dan crash pada Windows Audio Service (`Audiosrv` / `AudioEndpointBuilder`).
2. **Keterbatasan DPRAM Hardware RP2040/RP2350:** Pergantian Alternate Setting 0 (tanpa bandwidth) ke Alternate Setting 1 (aliran aktif) memicu race condition pada buffer DPRAM hardware USB Controller.
3. **Saturasi Bandwidth USB Full-Speed:** Audio 6-channel 96 kHz 24-bit uncompressed memerlukan bandwidth 1.728 KB/s, melebihi batas fisik USB Full-Speed (~1.023 KB/s). UAC2 standar tidak mendukung kompresi lossless FLAC/Delta-Rice secara dinamis.

## Keputusan
Memutar balik arsitektur aliran audio ke cetak biru **PRD Awal**:
1. **Lapisan Firmware (RP2350-Zero):** Beroperasi murni melalui **Vendor Bulk Endpoint** (Interface 2: EP 0x03 OUT / EP 0x83 IN) berdampingan dengan **CDC ACM Serial Monitor** (Interface 0 & 1: COM4).
2. **Lapisan Driver:** Menggunakan driver standar Microsoft `WinUSB` (`winusb.sys`), terisolasi sepenuhnya dari kernel audio Windows.
3. **Lapisan PC:** Menyediakan aplikasi perantara ringan **PC Companion Application (`HT306 Audio Bridge`)** yang menangkap audio Windows melalui WASAPI (Loopback atau Virtual Audio Device Sink), mengompresinya dengan Delta-Rice real-time (subframe 0,5 ms), dan mentransfernya melalui WinUSB Bulk.

## Alternatif yang Dipertimbangkan

### Alternatif 1: Melanjutkan Patch UAC2 Native via Linker Wraps
- **Kelebihan:** Tanpa instalasi aplikasi perantara di PC.
- **Kekurangan:** Sangat rapuh; membutuhkan monkey-patching biner TinyUSB; tidak dapat melewati batasan timeout `usbaudio2.sys` pada bus Full-Speed; tidak dapat melebihi 48 kHz stereo tanpa melebihi batas bandwidth.
- **Ditolak:** Menyebabkan lag PC berulang dan crash troubleshooter audio.

### Alternatif 2: Driver Kernel Audio Kustom Windows (WDM/KS)
- **Kelebihan:** Perangkat audio langsung terdaftar resmi di Device Manager.
- **Kekurangan:** Memerlukan sertifikasi Microsoft WHQL dan sertifikat EV code signing mahal; driver unsigned diblokir oleh Windows 10/11 Secure Boot; biaya pemeliharaan tinggi.
- **Ditolak:** Melanggar prinsip kesederhanaan dan deployment cepat.

### Alternatif 3: Vendor Bulk + PC Companion App dengan Virtual Audio Cable (Dipilih)
- **Kelebihan:**
  - 100% mengeliminasi freeze/lag kernel Windows (WinUSB beroperasi di user-space).
  - Hardware flow control ACK/NAK menjamin zero packet loss dan 100% bit-perfect audio.
  - Batasan bandwidth Full-Speed teratasi via kompresi lossless Delta-Rice (1.728 KB/s mentah dipadatkan ke ~716 KB/s).
  - Slider volume Windows, tombol media keyboard, dan per-app mixer bekerja normal via Virtual Audio Sink bersertifikat WHQL (misalnya VB-CABLE) atau WASAPI Loopback.
  - Firmware biner sangat ringkas (69 KB), deterministik, dan stabil.
- **Kekurangan:** Memerlukan aplikasi jembatan yang berjalan di latar belakang PC.
- **Diterima:** Menghasilkan audio surround 5.1 96 kHz murni bit-perfect dengan latensi di bawah 10 ms dan stabilitas 100%.

## Konsekuensi
- Firmware RP2350-Zero tetap ramping, efisien, dan bebas bug kernel (`firmware/`).
- Aplikasi PC Companion Bridge dikembangkan di `pc_streamer/` / `pc_bridge/`.
- Pengguna dapat mengontrol audio secara normal melalui antarmuka Windows tanpa risiko gangguan sistem.
