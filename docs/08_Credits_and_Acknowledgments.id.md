# 8. Kredit, Inspirasi & Stack Teknologi Proyek

🌐 **Bahasa:** [English](./08_Credits_and_Acknowledgments.md) | **Bahasa Indonesia**

[⬅️ Kembali ke README Utama](../README.id.md) | [Hardware & Mod Guide](./01_Hardware_Architecture_and_Mod_Guide.id.md) | [Codec 96 kHz](./02_Lossless_96kHz_Codec_Specification.id.md) | [Topologi Audio JRiver](./06_JRiver_Audio_Data_Topology.id.md) | [Arsitektur Utuh](./07_Complete_Architecture_and_Topology.id.md)

Dokumen ini mendokumentasikan seluruh sumber inspirasi, latar belakang perancangan, serta daftar lengkap teknologi, komponen perangkat keras, pustaka perangkat lunak (*software libraries*), protokol, dan alat bantu (*toolchains*) yang digunakan dalam membangun sistem **LG HT306 USB Direct-I2S Hi-Res Audio Bridge**.

---

## 8.1. Latar Belakang & Inspirasi Proyek

### 1. Penyelamatan & Peningkatan Nilai (*Upcycling*) Unit Home Theater Klasik
Sistem DVD Home Theater 5.1 era 2010-an seperti **LG HT306 (`HT306PD` / `HT306SU` / `HT306SF`)** memiliki tahap penguat (*power amplifier*) dan prosesor suara diskrit kelas-D yang sangat bertenaga dan jernih:
* Menggabungkan prosesor digital **Pulsus PS9850** (resolusi internal hingga 32-bit, 69-Band Biquad Parametric EQ, dan modulasi PWM multi-kanal) serta sepasang power stage **Texas Instruments TAS5352** dengan tegangan rel tinggi ($P_{\text{VDD}} \approx 35\text{V}$).
* Namun, perangkat keras bertenaga ini umumnya terlantar atau hanya digunakan sebagai pemutar radio/DVD usang karena **tidak memiliki input digital HDMI ARC maupun Optical S/PDIF multichannel**. Input eksternal satu-satunya hanyalah sepasang soket analog RCA Stereo (2.0) di panel belakang yang secara drastis membatasi kemampuan surround 5.1 aslinya.
* **Inspirasi Utama:** Membedah skematik resmi mainboard **LG EAX63554302**, memotong jalur I2S bawaan pemutar DVD MediaTek, dan menyuntikkan aliran I2S digital murni langsung dari PC modern tanpa melalui konversi analog-ke-digital ganda (ADC-DAC loss). Proyek ini mengubah perangkat audio *vintage* menjadi sebuah **Kartu Suara USB 5.1 Surround Studio-Grade (96 kHz 24/32-Bit)** mandiri tanpa perlu membeli AV Receiver baru yang mahal.

### 2. Kemampuan Revolusioner Raspberry Pi RP2350 (PIO & Dual-Core)
* Pemanfaatan mikrokontroler **Waveshare RP2350-Zero** terinspirasi oleh fleksibilitas unit **Programmable I/O (PIO)** rancangan Raspberry Pi. PIO memungkinkan pembuatan antarmuka perangkat keras khusus di luar fungsi periferal standar silikon.
* Pada proyek ini, satu state machine PIO (`i2s_multi.pio`) dikonfigurasi menjadi pemancar **4-Lane Synchronous I2S** yang menggerakkan 1 jalur Bit Clock (`BCK`), 1 jalur Word Select (`LRCK`), dan 3–4 jalur Data (`ADATA0`..`ADATA3`) secara paralel pada *falling edge* clock dengan latensi deterministik nol siklus CPU.
* Eksekusi **Dual-Core ARM Cortex-M33** memungkinkan isolasi tugas mutlak: Core 0 mendedikasikan siklusnya untuk tumpukan USB dan penyusunan paket, sementara Core 1 menjalankan dekompresi lossless Delta-Rice real-time di memori SRAM cepat tanpa interupsi.

### 3. Filosofi Bit-Perfect & Ekosistem JRiver Media Center
* Terinspirasi oleh kebutuhan komunitas audiophile akan kemurnian sinyal (*bit-exact playback*), sistem ini dirancang untuk mempertahankan integritas data dari pemutar media hingga ke tingkat modulasi PWM speaker.
* **JRiver Media Center** dengan mesin komputasi **64-bit float DSP Studio**, *Parametric EQ*, dan manajemen bass (*Room Correction*) dijadikan standar acuan pemutaran suara master untuk memverifikasi akurasi matematis kanal dan dynamic range.

---

## 8.2. Daftar Lengkap Teknologi yang Digunakan

### A. Perangkat Keras & Komponen Silikon (*Hardware & Silicon*)

| Komponen / Silikon | Produsen / Spesifikasi | Peran dalam Proyek |
| :--- | :--- | :--- |
| **RP2350-Zero** | Waveshare / Raspberry Pi Ltd | Mikrokontroler utama: Dual ARM Cortex-M33 @ 150 MHz, 520 KB SRAM, USB 1.1 Full-Speed controller, Dual-Core execution, PIO hardware state machines. |
| **Pulsus PS9850** (`IC602`) | Pulsus Technologies | Digital Audio Processor & 6-Ch PWM Modulator pada mainboard HT306: Menerima 4-lane I2S langsung, pemrosesan 69-band biquad EQ, kontrol volume master/kanal, dan kristal master 19.2 MHz (`X602`). |
| **TAS5352** (`IC701`, `IC702`) | Texas Instruments | Dual High-Performance 125W Stereo / 5.1 Class-D Power Stage Amplifier bertenaga rel $P_{\text{VDD}} = 34.8\text{V}$–$37.35\text{V}$ DC. |
| **SN74LVC244A** | Texas Instruments | Octal Buffer/Line Driver 3-state dengan fitur partial-power-down $I_{\text{off}}$: Melindungi chip Pulsus dari kerusakan *parasitic back-powering* saat HT306 mati dan PC tetap hidup. |
| **74LVC157** *(Opsi Dual-Mode)* | NXP / TI | Quad 2-input multiplexer kecepatan tinggi untuk beralih secara mulus antara mode pemutar DVD asli dan mode PC USB. |
| **MediaTek MT1389** (`IC501`) | MediaTek Inc | SoC pemutar DVD asli LG HT306: Jalur output I2S-nya diputus secara fisik pada resistor seri (`R648, R647, R631..R633`) untuk memberi ruang bagi injeksi RP2350. |
| **LC87F5M64A** (`IC101`) | Sanyo / ON Semiconductor | Audio MICOM: Mengelola inisialisasi boot mainboard, remote control IR, switching mode AUX, dan komunikasi I2C register ke PS9850. |
| **WS2812B Smart RGB LED** | Worldsemi | Lampu indikator status telemetri real-time onboard pin `GP16` (Biru = Standby, Hijau = 48 kHz, Ungu = 96 kHz, Merah = Error). |

---

### B. Firmware & Sistem Tertanam (*Embedded Firmware*)

| Teknologi / Pustaka | Versi / Lisensi | Peran dalam Proyek |
| :--- | :--- | :--- |
| **Raspberry Pi Pico SDK** | v2.3.1 (BSD-3-Clause) | Fondasi firmware C/C++, HAL perangkat keras RP2350, memori barrier, dan primitif sinkronisasi antarmuka dual-core (`multicore_launch_core1`). |
| **TinyUSB** | v0.18.0+ (MIT License) | Tumpukan protokol USB open-source: Mengimplementasikan USB Composite Device (Vendor Bulk Pipe + CDC ACM Serial Monitor). |
| **Microsoft OS 2.0 Descriptors** | Microsoft Standard | Menanamkan descriptor platform BOS & MS OS 2.0 registry property di firmware agar Windows 10/11 otomatis memasang driver `winusb.sys` tanpa perlu konfigurasi Zadig manual. |
| **Custom Delta-Rice Codec** | Proprietary Algorithmic (MIT) | Algoritma kompresi lossless real-time: Prediktor polinomial FLAC Order-4 + pemetaan Zigzag + pengkodean Golomb-Rice adaptif dengan mekanisme *escape raw bits* untuk menjamin throughput selalu $< 880\text{ KB/s}$ pada batas Full-Speed USB. |
| **PIO Assembly (`i2s_multi.pio`)** | Custom Assembly (Pico PIO) | Program assembler mikro-instruksi 32-bit untuk output 4-lane I2S sinkronis tepat pada *falling edge* bit clock (`BCK = 64 × Fs`). |
| **Chained Ping-Pong DMA** | RP2350 Hardware DMA | Saluran DMA ganda yang saling memicu (*ping-pong chaining*) dengan proteksi blok hening (*silence block*) dan penyesuaian clock mikro ($\pm 0.02\%$). |

---

### C. Aplikasi Host Desktop PC (`pc_stream_app/`)

| Komponen / Modul | Basis Teknologi | Peran dalam Proyek |
| :--- | :--- | :--- |
| **C++17 Desktop Companion** | Native Win32 / C++17 | Aplikasi jembatan sistem mandiri berkinerja tinggi, thread-safe, konsumsi memori sangat rendah ($< 35\text{ MB}$ RAM), dan zero third-party framework bloating. |
| **Windows Core Audio (WASAPI)** | Windows SDK (`mmdeviceapi.h`) | Menangkap audio desktop secara *real-time* via mode event-driven loopback capture (`IAudioCaptureClient`) dengan latensi buffer $< 3\text{ ms}$. |
| **Microsoft WinUSB (`winusb.dll`)** | Windows Kernel Usermode API | Komunikasi langsung tingkat tinggi dari ruang pengguna (*user space*) ke Endpoint Bulk OUT `0x03` mikrokontroler tanpa menyentuh kernel driver audio Windows (`usbaudio2.sys`). |
| **VB-Audio Virtual Cable** | VB-Audio Software | Pipa kartu suara virtual Windows: Menyediakan endpoint masukan pemutaran (*Playback Pin / `eRender`*) yang dapat dikunci eksklusif oleh pemutar media dan endpoint rekaman (*Recording Pin / `eCapture`*) untuk disadap oleh aplikasi. |
| **Windows Audio Policy Manager** | COM `IPolicyConfig` & Registry | Modul C++ internal untuk menyinkronkan nama ramah endpoint (*"LG HT306 Home Theater"* & *"LG HT306 Loopback Stream"*), memaksa endpoint capture aktif, dan mengalihkan default playback secara otomatis saat kabel USB dicolok/dicabut. |
| **Multi-Channel DSP Resampler** | Pure C++17 (`audio_rate_dsp.h`) | Konversi format multi-dekoder (`IEEE_FLOAT32`, `PCM16`, `PCM24`, `PCM32`), Blackman Windowed-Sinc anti-aliased resampler multi-kanal, penyesuai sample rate otomatis ($\le 96\text{ kHz}$), dan soft limiter `tanh()`. |
| **Microsoft Edge WebView2** | Chromium Embedded Framework | Mesin perender antarmuka pengguna grafis (GUI) modern yang ringan, memanfaatkan runtime bawaan Windows 10/11. |
| **Web Frontend UI** | HTML5, CSS3, ES6 Vanilla JS | Dashboard monitor audio modern bergaya Stitch UI: VU meter real-time 6-kanal, visualizer status sample rate, selektor perangkat, dan pengaturan DSP. |

---

### D. Perangkat Bantu, Pengujian & Toolchain (*Tooling & Verification*)

| Alat / Toolchain | Versi / Fungsi | Peran dalam Proyek |
| :--- | :--- | :--- |
| **Python 3** | Python 3.10+ | Bahasa pendukung untuk pembuatan harness uji end-to-end matematis, simulasi shift-register PIO, dan prototipe bridge awal (`pc_streamer/`). |
| **CMake & w64devkit (GCC)** | CMake 3.20+ / GCC 14+ | Build system lintas platform untuk mengompilasi firmware mikroprosesor ARM dan biner C++ desktop Windows. |
| **Playwright MCP Server** | Automation & Testing | Pengujian fungsional dan verifikasi integritas antarmuka WebView2 7-halaman secara headless dan interaktif. |
| **Antigravity AI Agent** | Google DeepMind | Arsitektur orkestrasi perancangan, pembuatan dokumen spesifikasi, implementasi kode terisolasi, audit adversarial, dan pemecahan masalah teknis. |

---

## 8.3. Pemutar Media & Ekosistem yang Didukung

Sistem audio bridge ini teruji dan dapat digunakan secara optimal bersama berbagai ekosistem pemutar audio terkemuka:
1. **JRiver Media Center (DSP Studio):** Mendukung pemutaran bit-perfect melalui antarmuka **WASAPI Exclusive**, perataan kurva akustik ruangan (*Room Correction*), *bass management crossover*, dan pemrosesan internal presisi tinggi 64-bit float.
2. **Foobar2000:** Pemutar audio audiophile dengan plugin output WASAPI atau ASIO virtual.
3. **VLC Media Player:** Pemutaran film resolusi tinggi dengan multichannel audio passthrough 5.1 (Dolby Digital, DTS, AAC 5.1, FLAC 5.1).
4. **Platform Streaming & Hiburan Harian:** YouTube, Spotify, Netflix, Disney+, dan Game PC melalui subsistem audio Windows Shared Mode standar.

---

## 8.4. Ucapan Terima Kasih & Penghargaan

* **Komunitas Open-Source Raspberry Pi:** Atas perancangan chip mikrokontroler RP2040 dan RP2350 yang sangat fleksibel serta penyediaan dokumentasi Pico SDK dan arsitektur PIO yang luar biasa.
* **LG Electronics:** Atas rancang bangun sirkuit mainboard audio **LG HT306** yang kokoh, berdaya tahan tinggi, dan dilengkapi dokumentasi *Service Manual* terperinci yang memungkinkan proyek modifikasi ini terlaksana dengan presisi tinggi.
* **Vincent Burel (VB-Audio Software):** Atas penyediaan perangkat lunak **VB-Audio Virtual Cable** yang andal untuk perutean audio virtual di Windows.
* **Pulsus Technologies & Texas Instruments:** Atas penciptaan silikon pemroses audio **PS9850** dan amplifier **TAS5352** yang mampu menghasilkan fidelitas suara berkelas tinggi melampaui masanya.

---

## 8.5. Lisensi Proyek

Seluruh kode sumber firmware, aplikasi host desktop, skrip pengujian, dan dokumentasi teknis dalam repositori ini dilisensikan di bawah:

**[MIT License](file:///e:/Project/HomeTheather/LICENSE)**  
Hak Cipta (c) 2026 Tim Pengembang & Kontributor Proyek LG HT306 Audio Bridge.
