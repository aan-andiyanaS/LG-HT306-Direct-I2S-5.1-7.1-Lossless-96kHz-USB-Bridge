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
