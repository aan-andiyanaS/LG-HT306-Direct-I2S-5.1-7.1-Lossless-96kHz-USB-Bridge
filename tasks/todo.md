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

- [x] **Slice 9: Implementasi Native USB Audio Class 2.0 (UAC2) Desktop Speaker untuk Deteksi Soundcard Windows**
  - Acceptance: Mengimplementasikan UAC2 Desktop Speaker (`usbaudio2.sys` bawaan Windows 10/11) dengan mode **Isochronous Adaptive** (tanpa endpoint feedback eksplisit untuk mencegah kernel spinlock/lag Windows), mendukung sample rate 44.1k/48k/96kHz 16-bit PCM, nama perangkat `Speakers (LG HT306 Home Theater)`, otomatis mengalirkan sampel audio PCM Windows ke PIO0 4-Lane I2S secara real-time, dan port CDC Serial Monitor tetap aktif berdampingan (PID `0x430A`).
  - Verify: Kompilasi sukses ke `ht306_rp2350_zero.uf2` dan tercatat pada GitHub Issue #9 & PR #8.
  - Files: `firmware/include/usb_descriptors.h`, `firmware/include/tusb_config.h`, `firmware/src/usb_descriptors.c`

- [x] **Slice 10: Upgrade Standar Industri Audiophile — Dual Resolution 16/24-Bit 96 kHz & Fractional PIO Drift Tracking**
  - Acceptance: Menambahkan Alternate Setting 2 untuk resolusi 24-bit Studio Master PCM (32-bit slot container), mendukung auto-detect resolusi di `tud_audio_set_itf_cb()`, pemetaan direct lossless pass-through bit-perfect (tanpa audio mixing kotor), serta algoritma *Fractional PIO PLL Clock-Drift Tracking* ($\pm 0.02\%$ dynamic adjustment) pada ISR DMA I2S untuk sinkronisasi zero-click/zero-pop jangka panjang (PID `0x430B`).
  - Verify: Kompilasi sukses ke `ht306_rp2350_zero.uf2` (72.192 byte) dan lulus `tests/verify_end_to_end.py`.
  - Files: `firmware/include/usb_descriptors.h`, `firmware/include/tusb_config.h`, `firmware/src/usb_descriptors.c`, `firmware/src/i2s_engine.c`

- [x] **Slice 11: Audit Bug Menyeluruh, Ponytail Optimization & Clean-Code Refactoring**
  - Acceptance:
    1. Memperbaiki attenuasi volume hardware slider Windows via Q16 linear gain precalculation (`db_to_linear_gain_q16` & `scale_sample_q16`) saat menerima event Feature Unit `0x02`.
    2. Menambahkan channel boundary guard (`< 3U`) pada Feature Unit GET/SET requests dan buffer guard pada vendor stream reassembler untuk mencegah buffer overflow.
    3. Mengeliminasi saturasi memory bus crossbar 150 MHz dengan menambahkan `sleep_us(50)` pada loop idle Core 1.
    4. Mengoptimalkan ISR DMA dengan caching `g_nominal_clkdiv` untuk zero runtime float division di interrupt.
    5. Menghapus dead code `ht306_usb_receive_raw_48k_pcm24` dan buffer `g_core0_raw48k_i2s32` (menghemat 1.152 byte SRAM).
    6. Menerapkan Clean-Code & Ponytail: Modularisasi `stream_uac2_pcm16_frames()` dan `stream_uac2_pcm24_frames()` dengan direct bit-perfect pass-through.
    7. Menambahkan logging stream audio aktif real-time di CDC serial monitor via `ht306_i2s_get_stats()`.
  - Verify: Kompilasi sukses ke `ht306_rp2350_zero.uf2`, 100% lulus `tests/verify_end_to_end.py`, commit `04d9edb` ter-push ke `feat/cdc-serial-monitor-heartbeat` dan tercatat pada GitHub Issue #9.
  - Files: `firmware/include/ht306_usb.h`, `firmware/include/i2s_engine.h`, `firmware/src/i2s_engine.c`, `firmware/src/main.c`, `firmware/src/usb_descriptors.c`

- [x] **Slice 12: Resolusi Tuntas Freeze / Lag PC Pasca Flash Firmware — Explicit Feedback Endpoint (UAC2 Asynchronous) & Static Buffer Allocation**
  - Acceptance:
    1. Mengimplementasikan UAC2 Asynchronous with Explicit Feedback (`TUD_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR`) dengan EP 0x01 (OUT) dan EP 0x81 (Feedback IN 4-Byte 16.16) untuk menghentikan kernel DPC hyper-polling IRP spinlock pada driver Windows `usbaudio2.sys`.
    2. Mengimplementasikan `tud_audio_feedback_params_cb()` dengan metode `AUDIO_FEEDBACK_METHOD_FIFO_COUNT` bawaan TinyUSB audio driver.
    3. Memindahkan alokasi buffer audio `samples_6ch` dari call stack Core 0 ke static memory SRAM (`s_samples_6ch`) untuk mencegah overflow batas stack 2 KB Core 0.
    4. Memperbarui USB PID ke `0x4320U` (`HT306_USB_PID`) untuk melewati cache registry Windows lama yang korup.
    5. Menjaga prinsip bit-perfect lossless pass-through murni tanpa upmixing / audio mixing.
  - Verify: Kompilasi sukses ke `ht306_rp2350_zero.uf2` (81.920 Byte), lulus 100% `tests/verify_end_to_end.py`, commit `47db13f` dipush ke remote branch `feat/cdc-serial-monitor-heartbeat`, dan update terbit di GitHub Issue #10.
  - Files: `firmware/include/tusb_config.h`, `firmware/include/usb_descriptors.h`, `firmware/src/usb_descriptors.c`
