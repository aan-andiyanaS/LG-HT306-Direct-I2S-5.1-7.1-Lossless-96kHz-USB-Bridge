# 🎬 LG HT306 Direct-I2S 5.1/7.1 Lossless 96 kHz USB Bridge (`Waveshare RP2350-Zero`)

🌐 **Bahasa:** [English](./README.md) | **Bahasa Indonesia**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: RP2350-Zero](https://img.shields.io/badge/MCU-Waveshare%20RP2350--Zero-purple.svg)](https://www.waveshare.com/rp2350-zero.htm)
[![Audio: 96kHz 24-bit 5.1](https://img.shields.io/badge/Audio-96kHz%20%2F%2024--bit%205.1%20Bit--Perfect-success.svg)](docs/02_Lossless_96kHz_Codec_Specification.id.md)
[![Target DSP: Pulsus PS9850](https://img.shields.io/badge/DSP-Pulsus%20PS9850%20(IC602)-orange.svg)](docs/01_Hardware_Architecture_and_Mod_Guide.id.md)
[![Tests: 100% Passing](https://img.shields.io/badge/Verification-100%25%20Bit--Exact-brightgreen.svg)](docs/04_Build_Flash_and_JRiver_Setup.id.md)

**Jembatan Audio Digital Multichannel (*Direct-to-PWM Processor*) berbasis `Waveshare RP2350-Zero` untuk mengubah Home Theater DVD LG HT306 (`HT306SU` / `HT306SF` / `HT306PD`) menjadi sistem Surround 5.1/7.1 murni digital hingga `96 kHz / 24-bit Lossless` dari PC (JRiver Media Center) — sambil tetap mempertahankan 100% fitur Hardware EQ bawaan LG.**

---

## 1. Tentang Proyek Ini

Home Theater **LG HT306** memiliki penguat daya *Full-Digital Class-D* **`330W RMS`** (berbasis prosesor PWM 8-kanal **`Pulsus PS9850` / `IC602`** dan *power stage* **`TI TAS5352`**), namun bawaan pabriknya hanya menyediakan input analog stereo (`AUX` RCA) tanpa input optik/HDMI multichannel.

Proyek ini memotong jalur analog tersebut dan menyuntikkan **6-Channel Digital I2S (`BCK`, `LRCK`, `ADATA0..2`)** secara langsung dari PC ke kaki input **`IC602 (Pulsus PS9850)`** menggunakan **`Waveshare RP2350-Zero`**:
* **100% Murni Digital (*Bit-Perfect*):** Tanpa konversi DAC/ADC analog sedikit pun.
* **Menembus Batas USB 1.1 Full-Speed untuk `96 kHz / 24-bit 5.1`:** Memadatkan aliran `1.728,0 KB/detik` menjadi **`716,1 KB/detik` (`41,4%`)** menggunakan algoritma *Real-Time Lossless FLAC Order-4 + Escaped Golomb-Rice* (`0 bit error`, latensi `< 0,03 ms` di `Core 1`).
* **EQ Bawaan LG HT306 Tetap Aktif:** Jalur kontrol **`I2C` (`SDA`/`SCL`)** dari chip utama **`MediaTek MT1389MH` (`IC501`)** dibiarkan utuh sehingga seluruh preset **69-Band Hardware EQ** (*Bass Blast, Natural, Pop, Classic, Volume*) tetap berfungsi lewat remote asli LG.

---

## 2. Cara Kerja & Diagram Arsitektur Garis Besar

Secara ringkas dan sederhana, gambaran besar seluruh alur data audio adalah:

$$\text{Pemutar (JRiver / Windows)} \longrightarrow \text{VB-Cable (Kartu Suara Virtual)} \longrightarrow \text{Aplikasi Desktop HT306} \longrightarrow \text{RP2350-Zero} \longrightarrow \text{Amplifier LG HT306} \longrightarrow \text{6 Speaker}$$

```mermaid
flowchart LR
    subgraph PC["1. Host PC (Windows 10 / 11)"]
        direction TB
        APP["Aplikasi Pemutar\n(JRiver / YouTube / Spotify / Film)"]
        VBC["Kartu Suara Virtual (VB-Cable)\n'LG HT306 Home Theater'\n(Dua Pintu: Playback & Loopback)"]
        HOST["Aplikasi Desktop HT306 (ht306_desktop_app)\nWASAPI Capture + Resampler <= 96 kHz\n+ Encoder Lossless Delta-Rice + WinUSB"]
        APP ==>|"Aliran Audio"| VBC
        VBC ==>|"Loopback Capture"| HOST
    end

    subgraph MCU["2. Waveshare RP2350-Zero (USB Bridge)"]
        direction TB
        C0["Core 0: Penerima TinyUSB\nWinUSB Bulk (EP 0x03 OUT)\nRing Buffer + Rekonstruksi Paket"]
        C1["Core 1: Dekoder Lossless\nBuka ke PCM 32-bit (< 0.03 ms)"]
        PIO["DMA Ping-Pong + Mesin PIO0\nPembangkit I2S Master Multi-Lane\n(BCK, LRCK, ADATA0..2)"]
        C0 --> C1 --> PIO
    end

    subgraph HT306["3. LG HT306 Home Theater"]
        direction TB
        PS["IC602: Pulsus PS9850\n(Prosesor PWM Digital + 69-Band EQ)"]
        TAS["IC701/702: TI TAS5352\n(Power Stage Class-D 330W RMS)"]
        SPK["6 Speaker Fisik (5.1 Surround)\nFL, FR, Center, Subwoofer, SL, SR"]
        PS --> TAS --> SPK
    end

    HOST ==>|"Kabel USB Type-C (WinUSB Bulk)"| C0
    PIO ==>|"5x Resistor Damping 33Ω\n(GP2..GP6)"| PS
```

### 4 Tahapan Utama Rantai Alur Audio:
1. **Sumber Audio & Kartu Suara Virtual:** **JRiver Media Center** (atau aplikasi Windows lainnya) memutar audio ke perangkat virtual **VB-Cable** (`LG HT306 Home Theater`). VB-Cable bertindak sebagai "kartu suara virtual" sehingga Windows memiliki target pemutaran resmi.
2. **Aplikasi Desktop HT306 (`ht306_desktop_app.exe`):** Menangkap data audio dari pin rekaman VB-Cable (`LG HT306 Loopback Stream`), menyeragamkan kanal menjadi 5.1 surround, menjaga kestabilan kecepatan sampling aman $\le 96\text{ kHz}$ (48k atau 96k), mengompresi paket secara *lossless*, lalu mengirimkannya ke jalur **WinUSB Bulk (EP 0x03 OUT)**.
3. **Mikrokontroler `Waveshare RP2350-Zero`:** `Core 0` menerima paket USB ke SRAM, `Core 1` membuka kompresi kembali menjadi PCM 32-bit murni secara instan (< 0,03 ms), dan **Mesin PIO0 Multi-Lane I2S** membangkitkan detak sinyal digital fisik (`BCK`, `LRCK`, `ADATA0..2`).
4. **Mainboard LG HT306:** Sinyal I2S masuk ke `Pad B` dari 5 resistor seri menuju **`IC602 (Pulsus PS9850)`**, diolah oleh **69-Band Hardware EQ** bawaan LG (yang dikontrol oleh `MT1389MH` via `I2C` pada mode `AUX`), lalu diperkuat oleh **`TI TAS5352`** langsung ke 6 speaker fisik.

> 📖 **Dokumentasi Detail & Mendalam:**
> * Untuk rincian alur audio khusus JRiver, pencegahan bentrok *WASAPI Exclusive*, dan panduan awam, lihat **[06_JRiver_Audio_Data_Topology.id.md](docs/06_JRiver_Audio_Data_Topology.id.md)**.
> * Untuk arsitektur perangkat keras lengkap, proteksi kelistrikan *Anti-Backpowering*, dan pembagian skematik PCB, lihat **[07_Complete_Architecture_and_Topology.id.md](docs/07_Complete_Architecture_and_Topology.id.md)**.
> * Untuk inspirasi proyek dan daftar lengkap stack teknologi yang digunakan, lihat **[08_Credits_and_Acknowledgments.id.md](docs/08_Credits_and_Acknowledgments.id.md)**.

---

## 3. Ringkasan Pemetaan Pin (`RP2350-Zero` $\rightarrow$ `IC602 Pulsus PS9850`)

| Pin `RP2350-Zero` | Sinyal | Kanal Audio | Titik Sambung di Mainboard LG HT306 (`IC602`) |
| :---: | :--- | :--- | :--- |
| **`GP2`** | `I2S_BCK` | Bit Clock (`64 × Fs`) | Via Resistor `33 Ω` $\rightarrow$ Pad Resistor `BCK` (arah `IC602`) |
| **`GP3`** | `I2S_LRCK` | Word Select (`Fs`) | Via Resistor `33 Ω` $\rightarrow$ Pad Resistor `LRCK` (arah `IC602`) |
| **`GP4`** | `I2S_ADATA0` | **Front L & Front R** | Via Resistor `33 Ω` $\rightarrow$ Pad Resistor `ADATA0` (arah `IC602`) |
| **`GP5`** | `I2S_ADATA1` | **Surround L & Surround R** | Via Resistor `33 Ω` $\rightarrow$ Pad Resistor `ADATA1` (arah `IC602`) |
| **`GP6`** | `I2S_ADATA2` | **Center & Subwoofer (LFE)** | Via Resistor `33 Ω` $\rightarrow$ Pad Resistor `ADATA2` (arah `IC602`) |
| **`GP7` / `GP8` / `GP9`** | Ekspansi | `ADATA3` (7.1) / `MUX` / `IR` | *(Opsional)* Ekspansi 7.1, Switch `74LVC157`, & Remote LG |
| **`GND`** | `Ground` | Referensi Digital (`3.3V`) | Titik **`GND` Digital** di dekat `IC602` pada Mainboard HT306 |

---

## 4. Quick Start (Build, Flash, & Verifikasi)

```powershell
# 1. Jalankan Verifikasi End-to-End (Codec Bit-Perfect, PIO 4-Lane, & USB 64-Byte Reassembly)
python tests/verify_end_to_end.py

# 2. Compile Firmware menjadi ht306_rp2350_zero.uf2 (Membutuhkan Pico SDK v2.0+)
cmake -B firmware/build -S firmware -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/build --config Release -j
```
* **Cara Flash:** Tahan tombol **`BOOT`** pada `RP2350-Zero` saat mencolokkan USB Type-C ke PC, lalu salin file **`firmware/build/ht306_rp2350_zero.uf2`** ke drive **`RP2350`** yang muncul.

---

## 5. Dokumentasi Teknis Lengkap (`docs/`)

Seluruh penjelasan teknis mendalam telah dipisahkan ke dalam modul bilingual tersendiri di direktori **[`docs/`](docs/)**:

| Dokumen | Isi Penjelasan Detail |
| :--- | :--- |
| 📄 **[01_Hardware_Architecture_and_Mod_Guide.id.md](docs/01_Hardware_Architecture_and_Mod_Guide.id.md)**<br>*(EN: [English Version](docs/01_Hardware_Architecture_and_Mod_Guide.md))* | Bedah chip mainboard LG HT306, anatomi **5 resistor seri SMD** (*no trace cutting*), serta alasan mengapa mode **`AUX`** & settingan **`2 to 5.1 (PLII)` / EQ Bawaan** tidak bentrok dengan sinyal 5.1 PC. |
| 📄 **[02_Lossless_96kHz_Codec_Specification.id.md](docs/02_Lossless_96kHz_Codec_Specification.id.md)**<br>*(EN: [English Version](docs/02_Lossless_96kHz_Codec_Specification.md))* | Rumus matematis **Single-Pass FLAC Order-4 Linear Predictor**, **ZigZag Mapping**, **Escaped Golomb-Rice**, dan rincian **Header Paket 14-Byte (`HT306_LosslessHeader_t`)**. |
| 📄 **[03_PIO_4Lane_I2S_and_DualCore_DMA.id.md](docs/03_PIO_4Lane_I2S_and_DualCore_DMA.id.md)**<br>*(EN: [English Version](docs/03_PIO_4Lane_I2S_and_DualCore_DMA.md))* | Diagram sekuens timing **`i2s_multi.pio`** pada *falling edge* `BCK`, algoritma *4-Lane Nibble Bit-Interleaver*, dan arsitektur **Wait-Free SPSC Chained Ping-Pong DMA** di SRAM. |
| 📄 **[04_Build_Flash_and_JRiver_Setup.id.md](docs/04_Build_Flash_and_JRiver_Setup.id.md)**<br>*(EN: [English Version](docs/04_Build_Flash_and_JRiver_Setup.md))* | Panduan lengkap kompilasi `.uf2`, cara *flash* ke `Waveshare RP2350-Zero`, hasil benchmark pengujian, dan pengaturan **JRiver Media Center (`DSP Studio`)**. |
| 📄 **[05_PC_Audio_Bridge_PRD_and_Architecture.id.md](docs/05_PC_Audio_Bridge_PRD_and_Architecture.id.md)**<br>*(EN: [English Version](docs/05_PC_Audio_Bridge_PRD_and_Architecture.md))* | Dokumen Kebutuhan Produk (PRD) dan spesifikasi teknis arsitektur **Aplikasi Perantara Audio PC (HT306 Audio Bridge)** pada Windows 10/11. |
| 📄 **[06_JRiver_Audio_Data_Topology.id.md](docs/06_JRiver_Audio_Data_Topology.id.md)**<br>*(EN: [English Version](docs/06_JRiver_Audio_Data_Topology.md))* | Topologi alur data audio khusus **JRiver Media Center**, sistem dual-endpoint VB-Cable, pencegahan bentrok WASAPI Exclusive, dan pemetaan file kode sumber. |
| 📄 **[07_Complete_Architecture_and_Topology.id.md](docs/07_Complete_Architecture_and_Topology.id.md)**<br>*(EN: [English Version](docs/07_Complete_Architecture_and_Topology.md))* | Arsitektur utuh sistem hardware & software, proteksi kelistrikan **Anti-Backpowering**, *state machine* urutan daya aman, serta pemetaan skematik PCB resmi. |
| 📄 **[08_Credits_and_Acknowledgments.id.md](docs/08_Credits_and_Acknowledgments.id.md)**<br>*(EN: [English Version](docs/08_Credits_and_Acknowledgments.md))* | Dokumentasi sumber inspirasi proyek, latar belakang rekayasa balik (*upcycling*), serta daftar lengkap seluruh teknologi silikon, firmware, desktop, dan perkakas yang digunakan. |

---

## 6. Struktur Direktori

```text
HomeTheather/
├── README.md                              # Ringkasan proyek & navigasi utama (Bahasa Inggris)
├── README.id.md                           # Ringkasan proyek & navigasi utama (Bahasa Indonesia)
├── LICENSE                                # Lisensi Open-Source MIT
├── docs/                                  # Dokumentasi teknis mendalam (Bilingual: EN & ID)
├── firmware/                              # Firmware C/PIO untuk Waveshare RP2350-Zero
├── pc_stream_app/                         # Aplikasi desktop native C++ WebView2
├── pc_streamer/                           # Purwarupa streamer audio PC (Python/C++)
└── tests/                                 # Suite verifikasi matematis & simulasi hardware
```

---

## 7. Inspirasi & Referensi Proyek Serupa (*Prior Art*)

* **[`tierneytim/Pico-USB-audio`](https://github.com/tierneytim/Pico-USB-audio):** Pelopor 8-Channel Multi-Lane I2S pada Raspberry Pi Pico (`48 kHz`), yang kami kembangkan lebih lanjut dengan *Core 1 Lossless Order-4 Golomb-Rice Unpacker* agar menembus **`96 kHz / 24-bit 5.1`**.
* **[`xiph/flac`](https://github.com/xiph/flac)** & **[`dbry/WavPack`](https://github.com/dbry/WavPack):** Inspirasi matematis prediktor polinomial linear Orde-4 dan pengodean residual Golomb-Rice.
* **[`raspberrypi/pico-extras`](https://github.com/raspberrypi/pico-extras)** & **[`hathach/tinyusb`](https://github.com/hathach/tinyusb):** Fondasi *Chained Ping-Pong DMA* + `PIO` dan USB Device stack pada `RP2350`.

---

## 8. Lisensi (MIT License)

Proyek ini dilisensikan di bawah **[MIT License](LICENSE)** — bebas digunakan, dimodifikasi, dan dikembangkan untuk keperluan pribadi maupun komersial.
