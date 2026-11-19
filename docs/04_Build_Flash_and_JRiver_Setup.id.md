# 4. Panduan Kompilasi Firmware (`.uf2`), Flashing `RP2350-Zero`, Pengujian, & Setup JRiver

🌐 **Bahasa:** [English](./04_Build_Flash_and_JRiver_Setup.md) | **Bahasa Indonesia**

[⬅️ Kembali ke README Utama](../README.id.md)

Dokumen ini berisi panduan langkah demi langkah untuk mengompilasi firmware, memasukkannya ke papan **`Waveshare RP2350-Zero`**, menjalankan suite verifikasi, serta mengonfigurasi **JRiver Media Center (`DSP Studio`)**.

---

## 4.1. Kompilasi Firmware (`.uf2`)

### Prasyarat
* **Raspberry Pi Pico SDK** versi **`2.0.0`** atau lebih baru (mendukung arsitektur `RP2350` / `pico2`).
* **CMake** `>= 3.20`, **Ninja**, dan toolchain **ARM GNU (`arm-none-eabi-gcc`)** — dapat diinstal otomatis menggunakan ekstensi resmi **Raspberry Pi Pico** di Visual Studio Code.

### Perintah Kompilasi
```powershell
# 1. Masuk ke direktori firmware
cd firmware

# 2. Buat direktori build & jalankan CMake untuk target pico2 (RP2350-Zero)
cmake -B build -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=Release

# 3. Compile menjadi file ht306_rp2350_zero.uf2
cmake --build build --config Release -j
```

---

## 4.2. Flash ke Papan `Waveshare RP2350-Zero`

1. Tekan dan tahan tombol **`BOOT`** kecil pada papan `Waveshare RP2350-Zero`.
2. Hubungkan kabel USB Type-C ke PC, lalu lepaskan tombol **`BOOT`**.
3. Drive baru bernama **`RP2350`** akan muncul di Windows Explorer.
4. Salin (*Drag & Drop*) file **`firmware/build/ht306_rp2350_zero.uf2`** ke dalam drive **`RP2350`**.
5. Papan akan melakukan *reboot* otomatis dan langsung terdeteksi di Windows sebagai **`LG HT306 5.1 Surround Direct-I2S (RP2350-Zero)`** (`VID: 0x2E8A`, `PID: 0x4306`).

---

## 4.3. Menjalankan Verifikasi & Benchmark End-to-End

Seluruh algoritma (mulai dari *Single-Pass FLAC Order-4 + Escaped Golomb-Rice Codec*, *4-Lane PIO Bit-Interleaver*, *Falling-Edge Clock Timing*, hingga *64-Byte USB Bulk Stream Reassembler*) dapat diverifikasi langsung menggunakan Python:

```powershell
python tests/verify_end_to_end.py
```

### Output Verifikasi Standar
```text
=== [TEST 1A] Verifikasi Bit-Perfect Codec 96 kHz 24-bit 5.1 (RP2350-Zero) ===
  -> Bit-Exactness : LULUS 100% (0 bit error dari 115.200 sampel 24-bit)
  -> Data Mentah   : 1728.0 KB/detik
  -> Data USB Pico : 716.1 KB/detik (41.4% dari ukuran asli, Batas Aman USB = 1000 KB/dtk)
  -> Paket Maks    : 812 Byte / 0.5 ms

=== [TEST 1B] Verifikasi Bit-Perfect Codec 96 kHz 32-bit 5.1 (True 32-bit Studio Master) ===
  -> Bit-Exactness : LULUS 100% (0 bit error dari 115.200 sampel 32-bit)
  -> Data Mentah   : 2304.0 KB/detik (2.3 MB/s)
  -> Data USB Pico : 1246.5 KB/detik (54.1% rasio kompresi)
  -> Paket Maks    : 1099 Byte / 0.5 ms

=== [TEST 2] Verifikasi Simulasi Hardware PIO 4-Lane I2S (GP4..GP7 -> PS9850) ===
  -> ADATA0 (GP4 Front)  : 0x12345678 == 0x12345678 [OK]
  -> ADATA1 (GP5 Rear)   : 0x9ABCDEF0 == 0x9ABCDEF0 [OK]
  -> ADATA2 (GP6 C/Sub)  : 0x13579BDF == 0x13579BDF [OK]
  -> ADATA3 (GP7 7.1)    : 0x2468ACE0 == 0x2468ACE0 [OK]

=== [TEST 3] Verifikasi Siklus Clock & Edge Timing i2s_multi.pio ===
  -> Semua 32 bit Kiri & 32 bit Kanan di-output tepat pada FALLING edge BCK [OK]
  -> Total Siklus per Frame Stereo = 128 siklus PIO (64 BCK @ 50% duty cycle) [OK]

=== [TEST 4] Verifikasi Reassembly Stream USB Bulk 64-Byte (ht306_usb_process_vendor_stream) ===
  -> 10 paket variabel (24-bit & 32-bit) berhasil disatukan kembali [OK]

[SEMUA VERIFIKASI & AUDIT LULUS 100%]
```

---

## 4.4. Konfigurasi JRiver Media Center (`DSP Studio`)

Agar pemisahan kanal film (**Dolby Digital AC-3, TrueHD, Atmos, DTS-HD**) dan musik terpetakan secara presisi ke 6 speaker LG HT306:

1. Buka **JRiver Media Center** $\rightarrow$ klik menu **`Tools`** $\rightarrow$ **`Options`** $\rightarrow$ **`Audio`**.
2. Pada bagian **`DSP & Output Format`**, buka **`DSP Studio`**:
   * **Output Format:**
     * **Channels:** Pilih **`5.1`** (`6 Channels: FL, FR, C, LFE, SL, SR`).
     * **Sample Rate:**
       * **Opsi Otomatis:** Biarkan default untuk mengikuti frekuensi asli file (`48.000 Hz` untuk AC-3, `96.000 Hz` untuk TrueHD/Hi-Res) — `RP2350-Zero` dan `Pulsus PS9850` akan otomatis menyesuaikan detak `LRCK` & `BCK`.
       * **Opsi Upsampling 96 kHz:** Atur semua input menjadi **`96.000 Hz`** jika Anda ingin JRiver melakukan *upsampling* 64-bit otomatis untuk seluruh sumber suara.
   * **Room Correction:**
     * Atur jarak (*delay*) tiap speaker sesuai posisi duduk Anda, dan aktifkan *Bass Management* (crossover `80 Hz` menuju kanal Subwoofer/LFE).
3. Pada unit **LG HT306**:
   * Pilih mode **`AUX`** menggunakan remote asli.
   * Pilih preset EQ 5.1 seperti **`BASS BLAST`**, **`NATURAL`**, atau **`PLII MOVIE`** agar ke-6 kanal amplifier di dalam `IC602 (Pulsus PS9850)` aktif penuh beserta kurva 69-Band EQ bawaan LG.
