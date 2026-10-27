# Rencana Implementasi Teknis (Technical Implementation Plan)

## Urutan Implementasi (*Vertical & Risk-First Slicing*)

1. **Slice 1 — Inti Codec *Lossless Delta-Rice* 24-bit 96 kHz (`common/lossless_codec.h` & `lossless_codec.c`) + Unit Test:**
   - Membangun *encoder* (*Packer* untuk sisi PC) dan *decoder* (*Unpacker* untuk `Core 1` `RP2350-Zero`) yang berbagi satu definisi protokol yang sama.
   - Menyertakan *escaped Rice coding* (`q >= 16`) agar sinyal transien ekstrem tetap **100% Bit-Exact Lossless** tanpa membengkakkan ukuran paket.
   - Memverifikasi dengan unit test untuk membuktikan `memcmp(input, output) == 0` dan mengukur rasio pemadatan (`< 880 KB/s` pada 96 kHz 5.1).

2. **Slice 2 — Mesin PIO Multi-Lane I2S (`firmware/src/i2s_multi.pio` & `i2s_engine.c`):**
   - Menulis program assembly PIO untuk mengeluarkan `BCK`, `LRCK`, `ADATA0` (`GP4`), `ADATA1` (`GP5`), `ADATA2` (`GP6`), dan `ADATA3` (`GP7`) secara serentak pada pin `Waveshare RP2350-Zero`.
   - Mengimplementasikan *Ping-Pong Ring Buffer* bebas kunci (*lock-free*) dan driver DMA berantai (*chained DMA*).

3. **Slice 3 — Stack USB Composite (`TinyUSB` UAC2 48 kHz + High-Throughput 96 kHz Lossless Endpoint + Kontrol HT306):**
   - Menyusun `usb_descriptors.c`, `ht306_control.c` (kontrol saklar `74LVC157` pada `GP8`, injeksi `REMOCON` LG NEC protocol pada `GP9`, dan indikator WS2812 RGB LED pada `GP16`).
   - Menyusun `main.c` untuk orkestrasi **Core 0** (USB & DMA) dan **Core 1** (*Lossless 96 kHz Unpacker*).

4. **Slice 4 — Aplikasi Pengirim PC (`pc_streamer`) & Skrip Build (`CMakeLists.txt`):**
   - Menyusun *PC Companion Streamer* untuk menangkap aliran 6-channel 96 kHz/24-bit dari JRiver Media Center, mengemasnya secara *real-time* menggunakan `lossless_codec`, dan mengirimkannya via USB ke `RP2350-Zero`.
