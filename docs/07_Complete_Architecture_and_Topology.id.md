# 7. Arsitektur Utuh & Topologi Lengkap Sistem LG HT306 Direct-I2S

🌐 **Bahasa:** [English](./07_Complete_Architecture_and_Topology.md) | **Bahasa Indonesia**

[⬅️ Kembali ke README Utama](../README.id.md) | [Hardware & Mod Guide](./01_Hardware_Architecture_and_Mod_Guide.id.md) | [Codec 96 kHz](./02_Lossless_96kHz_Codec_Specification.id.md) | [PIO 4-Lane & DMA](./03_PIO_4Lane_I2S_and_DualCore_DMA.id.md) | [PC Audio Bridge PRD](./05_PC_Audio_Bridge_PRD_and_Architecture.id.md) | [Topologi Audio JRiver](./06_JRiver_Audio_Data_Topology.id.md) | [Kredit & Stack Teknologi](./08_Credits_and_Acknowledgments.id.md)

Dokumen ini menyajikan gambaran **end-to-end** seluruh sistem — mulai dari aplikasi audio di Windows 10/11, transport USB pada `Waveshare RP2350-Zero`, lapisan proteksi **Anti-Backpowering**, hingga skematik aktual mainboard **LG HT306PD (`EAX63554302`)** berdasarkan verifikasi *Service Manual* resmi.

---

## 7.1. Topologi Sistem Utuh (*End-to-End Hardware & Software Architecture*)

Diagram berikut memetakan aliran sinyal audio 6-kanal (`FL, FR, C, LFE, SL, SR`), jalur kontrol/telemetri, dan batas pemisahan catu daya dari PC hingga ke 6 speaker.

```mermaid
flowchart TB
    subgraph PC["1. HOST PC WINDOWS 10/11 (pc_stream_app)"]
        direction TB
        APPS["Aplikasi Pemutar\nJRiver / Spotify / Netflix / Games"]
        VDEV["Virtual Audio Sink\nVB-CABLE ('LG HT306 Home Theater')\n48 kHz / 96 kHz Multichannel 5.1"]
        APM["AudioPolicyManager & AudioEndpointHelper\nAuto-Switch Default Endpoint + Sync Nama"]
        CAP["AudioCapture (WASAPI Event-Driven Loopback)\nSmoothed Gain + Soft Limiter + Test/Sweep"]
        ENC["Lossless Encoder (lossless_codec.c)\nFLAC Order-4 Delta + Golomb-Rice\nFrame 48 sampel + Sync 0xA55A + CRC-8"]
        USBTX["UsbTransport (WinUSB & CDC ACM)\nVID 0x2E8A : PID 0x4306\nBulk Pipe EP 0x03 OUT + Serial Monitor"]
        WEBUI["Desktop Dashboard (main_webview.cpp)\nWebView2 SPA + System Tray + VU Meter"]

        APPS --> VDEV --> CAP --> ENC --> USBTX
        APM -.->|"Policy Config"| VDEV
        USBTX <-->|"Telemetry & Hotplug"| WEBUI
    end

    subgraph MCU["2. WAVESHARE RP2350-ZERO (Cortex-M33 @ 150 MHz — 3.3V VBUS PC)"]
        direction LR
        TUSB["TinyUSB Stack\nEP 0x03 Vendor Bulk\nEP 0x82 CDC Serial"]
        REASM["Core 0: ht306_usb_task()\n64-Byte Stream Reassembler\n& LED/VU Heartbeat"]
        Q1["SPSC Queue 1\ng_packet_queue[32]\n__dmb() Lock-Free"]
        UNPK["Core 1: ht306_core1_lossless_worker()\nLossless Decode < 0.03 ms\n+ 4-Lane Nibble Interleaver"]
        Q2["SPSC Ring Buffer 2\ng_pio_ring_buffer[16 blok]\n48 frame/blok (32B/frame)"]
        DMA["Chained Ping-Pong DMA (A <-> B)\ng_silence_block Fallback Saat Underrun\nPLL Drift Tracking +/-0.02%"]
        PIO["PIO0_SM0 (i2s_multi.pio)\nOutput Falling-Edge BCK\nGP2..GP6 (4mA Drive Strength)"]

        TUSB --> REASM --> Q1 --> UNPK --> Q2 --> DMA --> PIO
        REASM -.->|"VU & Stats"| TUSB
    end

    subgraph PROT["3. LAPISAN PROTEKSI ANTI-BACKPOWERING (Batas Domain Daya 3.3V PC vs HT306)"]
        direction TB
        OPT_IC["Opsi A (Hardware I_off Isolation)\n1x SN74LVC244A (VCC = VCC33 HT306)\natau 1x SN74AXC8T245 (Dual-Supply)\natau 2x 74LVC157 (Dual-Mode DVD+PC)"]
        OPT_FW["Opsi B (Zero-IC Firmware Auto-Hi-Z)\nR 10k seri + R 100k pull-down ke GP10\nSense VCC33 -> Set GP2..GP8 Input Hi-Z"]
    end

    subgraph HT["4. MAINBOARD LG HT306PD (PCB EAX63554302)"]
        direction TB
        INJ["5x Resistor Seri Diangkat (Pad B)\nR648(47R), R647(47R), R631..R633(100R)"]
        MICOM["IC101: Audio MICOM (LC87F5M64A)\nBoot, Volume, EQ Preset, Power Control"]
        PS["IC602: Pulsus PS9850\nPin 4..8: SBCK, SLRCK, SSDIN0..2\n69-Band Biquad EQ + ASRC + 6-Ch PWM\nCrystal X602 (19.2 MHz Utuh)"]
        AMP["IC701 & IC702: TI TAS5352\nDual Class-D MOSFET Power Stage\nPVDD 34.8V DC | GVDD 12.2V DC"]
        SPK["JK701 -> 6 Speaker Fisik (5.1)\nFL, FR, Center, Subwoofer (LFE), SL, SR"]

        INJ --> PS --> AMP --> SPK
        MICOM -->|"I2C (R638/R637) & nRESET (R636)\nDibiarkan Utuh"| PS
    end

    USBTX ==>|"USB Type-C (WinUSB Bulk)"| TUSB
    USBTX -.->|"CDC ACM Serial"| TUSB
    PIO ==>|"GP2..GP6 (BCK, LRCK, ADATA0..2)"| PROT ==>|"Melalui Resistor Seri 33 Ohm"| INJ
```

---

## 7.2. Topologi Skematik Mainboard (`IC501` ↔ `IC201` ↔ Titik Injeksi `IC602`)

Berdasarkan verifikasi skematik *LG HT306PD Service Manual* (`MAIN & AMP (#1)` & `(#2)`), terdapat **dua kelompok resistor seri** di jalur I2S. Resistor yang **wajib diangkat** adalah lima resistor di sisi **`IC602` (`R648, R647, R631, R632, R633`)** agar ADC **`IC201 (CS5346)`** tetap terisolasi bersama `IC501`.

```mermaid
flowchart LR
    subgraph MT["IC501: MediaTek MT1389FE/MH (DVD SoC)"]
        P156["Pin 156 LFE/GPIO -> R545 (100R)"]
        P158["Pin 158 AR/GPIO -> R547 (33R)"]
        P161["Pin 161 ALS/GPIO -> R550 (100R)"]
        P162["Pin 162 CENTER -> R551 (33R)"]
        P160["Pin 160 AL/GPIO1 -> R549 (33R)"]
        P157["Pin 157 ARS/GPIO -> R546 (100R)"]
    end

    subgraph ADC["IC201: CS5346-CQZ (Stereo ADC AUX/Tuner)"]
        A44["Pin 44 MCLK <- R205 (22R)"]
        A42["Pin 42 SCLK <- R203 (22R)"]
        A43["Pin 43 LRCK <- R202 (22R)"]
        A41["Pin 41 SDOUT -> R204 (22R) -> ADC_DATA"]
    end

    subgraph TP["Test Point Hulu (Pad A — Diputus ke IC602)"]
        T_BCK["TP609: BCK\n[R648 47R DIANGKAT]"]
        T_LRCK["TP608: LRCK\n[R647 47R DIANGKAT]"]
        T_D0["TP610: ADATA0\n[R631 100R DIANGKAT]"]
        T_D1["TP611: ADATA1\n[R632 100R DIANGKAT]"]
        T_D2["TP612: ADATA2\n[R633 100R DIANGKAT]"]
    end

    subgraph RP["Injeksi RP2350-Zero (+ Buffer/33R)"]
        G2["GP2 (BCK) -> 33R"]
        G3["GP3 (LRCK) -> 33R"]
        G4["GP4 (ADATA0 FL/FR) -> 33R"]
        G5["GP5 (ADATA1 SL/SR) -> 33R"]
        G6["GP6 (ADATA2 C/LFE) -> 33R"]
        G7["GP7 (ADATA3 7.1) — TIDAK DISAMBUNG"]
    end

    subgraph PS["IC602: Pulsus PS9850 (VDD_IO 3.26V | Core 1.25V)"]
        PS4["Pin 4: SBCK (Pad B)"]
        PS5["Pin 5: SLRCK (Pad B)"]
        PS6["Pin 6: SSDIN0 (Pad B)"]
        PS7["Pin 7: SSDIN1 (Pad B)"]
        PS8["Pin 8: SSDIN2 (Pad B)"]
        PS9["Pin 9: SSDIN3 (NC di PCB)"]
        PS_CTL["Pin 50 SO_SDA, 51 SCK_SCL, 53 nRESET"]
        PS_CLK["Pin 55 XIN, 56 XOUT <-> Crystal X602 (19.2 MHz)"]
    end

    subgraph CTRL["IC101: Audio MICOM (LC87F5M64A)"]
        C_I2C["PWM_DAT -> R638 (100R)\nPWM_CLK -> R637 (100R)\n9830_RESET -> R636 (100R / TP613)"]
        C_IR["RCU_IN (CN103 Pin 5 <- Front IR RC301)"]
    end

    P156 -->|"MCLK"| A44
    P158 -->|"BCK"| A42
    P161 -->|"LRCK"| A43

    P158 --> T_BCK
    P161 --> T_LRCK
    P162 --> T_D0
    P160 --> T_D1
    P157 --> T_D2

    T_BCK -.-x|"Putus Fisik"| PS4
    T_LRCK -.-x|"Putus Fisik"| PS5
    T_D0 -.-x|"Putus Fisik"| PS6
    T_D1 -.-x|"Putus Fisik"| PS7
    T_D2 -.-x|"Putus Fisik"| PS8

    G2 ==>|"Pad B"| PS4
    G3 ==>|"Pad B"| PS5
    G4 ==>|"Pad B"| PS6
    G5 ==>|"Pad B"| PS7
    G6 ==>|"Pad B"| PS8

    C_I2C ==>|"UTUH (EQ, Vol, Unmute)"| PS_CTL
```

> **Catatan Penting Skematik:**
> * Jika menggunakan mode Dual-Mode (`74LVC157`), input `A` (sinyal DVD asli) dapat diambil dari **`TP609`, `TP608`, `TP610`, `TP611`, `TP612`** atau sisi `Pad A` bekas `R648..R633` (dibutuhkan **2× `74LVC157`** untuk 5 jalur).
> * **`GP9` (`REMOCON_TX`)** pada [ht306_control.h](file:///e:/Project/HomeTheather/firmware/include/ht306_control.h#L15) dibiarkan belum disambungkan pada injeksi awal karena jalur `RCU_IN` terhubung ke rel standby `3.7VA`.

---

## 7.3. Topologi Kelistrikan, Domain Daya, & Mekanisme Anti-Backpowering

Diagram di bawah menjelaskan akar masalah *back-powering* (rel `VCC33` turun ke `0V` saat HT306 Standby/Mati sementara RP2350-Zero tetap mendapat `5V` dari USB PC) dan bagaimana dua mekanisme pengaman menutup celah tersebut.

```mermaid
flowchart TB
    subgraph PWR_PC["Domain Daya PC (Selalu Menyala Saat PC Hidup)"]
        USB5["USB VBUS PC (5V)"] -->|"LDO Onboard RP2350"| RP33["3.3V IOVDD RP2350-Zero"]
        RP33 --> RP_IO["GP2..GP6 Output I2S 3.3V\n& GP8 MUX Select"]
    end

    subgraph PWR_HT["Domain Daya LG HT306 (SMPS -> PN901 -> Mainboard)"]
        AC["Listrik PLN 220V AC\n(Area SMPS HOT CIRCUIT\nC903 68uF/450V)"] --> T901["SMPS Standby (T901)"]
        AC --> T902["SMPS Power (T902)"]
        T901 --> R37["Rel Always-On 3.7VA\n(PN901 Pin 8-9 | C122 3.70V)"]
        R37 --> MIC_P["Catu IC101 MICOM (~3.48V)\n& Front IR RC301"]
        R37 --> IC102["IC102: Regulator 278R33\nPin 4 VCTL <- V33_CTL (R124)"]
        MIC_P -->|"P_CTL & V33_CTL\nHIGH saat Power ON\nLOW saat Standby/Mati"| IC102
        IC102 -->|"Pin 2 Out -> C146 (+)\n3.28V saat ON | 0V saat Standby"| VCC33["Rel VCC33 -> IC602 VDD_IO"]
        T902 -->|"Aktif Saat ON"| R_HI["VCC50 (5V) | 12V (GVDD)\nPVDD 34.8V-37.35V -> TAS5352"]
    end

    subgraph GUARD_A["Mekanisme Pengaman A: IC Buffer I_off (Direkomendasikan)"]
        BUF["SN74LVC244A (VCC = VCC33)\natau SN74AXC8T245 (VCCA=3V3, VCCB=VCC33)"]
        RP_IO -->|"Input 3.3V"| BUF
        VCC33 -->|"Catu Daya Buffer"| BUF
        BUF -->|"ON: Teruskan Sinyal I2S via 33R\nStandby (VCC33=0V): Hi-Z Total (I_off)"| IC602_IN["Pin 4..8 IC602 (PS9850)"]
    end

    subgraph GUARD_B["Mekanisme Pengaman B: Sensor GP10 + Firmware Hi-Z"]
        DIV["R Seri 10 kOhm + R Pull-down 100 kOhm"]
        FW_CHK["RP2350 GP10 (Digital Input)"]
        VCC33 -->|"Deteksi Tegangan"| DIV --> FW_CHK
        FW_CHK -->|"GP10 == LOW: Set GP2..GP8 = GPIO_IN (Hi-Z)\nGP10 == HIGH: Delay 300-500ms -> PIO0 ON"| IC602_IN
    end

    GND_BUS["Referensi Wajib: GND RP2350-Zero <==> DGND Mainboard HT306 (Kabel Pendek)"]
```

---

## 7.4. Diagram State Operasional Aman (Power Sequencing)

```mermaid
stateDiagram-v2
    [*] --> BOOT_HIZ: RP2350 Dicolok ke USB PC
    BOOT_HIZ: Semua Pin I2S (GP2..GP8) dalam Kondisi Hi-Z / Terisolasi\nLED Biru Standby | Endpoint WinUSB Terbaca Windows

    BOOT_HIZ --> WAIT_HT306_ON: Cek VCC33 HT306 (Hardware I_off atau GP10 == LOW)
    WAIT_HT306_ON --> HT306_BOOT_DELAY: HT306 Dinyalakan (Mode AUX, VCC33 naik ke 3.28V)

    HT306_BOOT_DELAY: Tunggu 300-500 ms agar IC101 selesai I2C init & reset IC602
    HT306_BOOT_DELAY --> I2S_IDLE_ZERO: Aktifkan Output I2S / Buffer Enable\nPIO0 Mengirim g_silence_block (Hening Bersih 0x00000000)

    I2S_IDLE_ZERO --> STREAMING_ACTIVE: Paket USB Bulk (0xA55A) Masuk di EP 0x03
    STREAMING_ACTIVE: Mode 48 kHz (LED Hijau) / 96 kHz (LED Ungu)\nCore 1 Decode < 0.03 ms + PIO0 4-Lane + PLL Drift Tracking

    STREAMING_ACTIVE --> I2S_IDLE_ZERO: PC Pause / Stop (Fallback ke g_silence_block)
    STREAMING_ACTIVE --> SAFE_ISOLATE: HT306 Dimatikan / Standby (VCC33 turun ke 0V)
    I2S_IDLE_ZERO --> SAFE_ISOLATE: HT306 Dimatikan / Standby (VCC33 turun ke 0V)

    SAFE_ISOLATE: Hardware I_off / Firmware langsung memutus output ke Hi-Z\nWindows TIDAK kehilangan perangkat USB (Anti Putus-Nyambung)
    SAFE_ISOLATE --> WAIT_HT306_ON: HT306 Standby
```

---

## 7.5. Tabel Referensi Cepat Meja Kerja (Pinout, Komponen, & Titik Ukur)

| Jalur / Sinyal | Pin `RP2350-Zero` | Resistor Proteksi | Komponen Asli PCB `EAX63554302` (Diangkat) | Test Point Hulu (`Pad A`) | Tujuan di `IC602 (PS9850)` (`Pad B`) |
| :--- | :---: | :---: | :--- | :---: | :--- |
| **`I2S_BCK`** | `GP2` | `33 Ω` seri | **`R648`** (`47 Ω`) | `TP609` | **Pin 4** (`SBCK`) |
| **`I2S_LRCK`** | `GP3` | `33 Ω` seri | **`R647`** (`47 Ω`) | `TP608` | **Pin 5** (`SLRCK`) |
| **`I2S_ADATA0`** (`FL/FR`) | `GP4` | `33 Ω` seri | **`R631`** (`100 Ω`) | `TP610` | **Pin 6** (`SSDIN0`) |
| **`I2S_ADATA1`** (`SL/SR`) | `GP5` | `33 Ω` seri | **`R632`** (`100 Ω`) | `TP611` | **Pin 7** (`SSDIN1`) |
| **`I2S_ADATA2`** (`C/LFE`) | `GP6` | `33 Ω` seri | **`R633`** (`100 Ω`) | `TP612` | **Pin 8** (`SSDIN2`) |
| **Sense `VCC33` *(Opsi B)*** | `GP10` | `10 kΩ` seri + `100 kΩ` ke `GND` | *(Tidak mengangkat resistor)* | — | Kaki **`+` `C146`** / **`IC102` Pin 2** (`3.28V` ON / `0V` Standby) |
| **`MUX_SELECT` *(Opsi Dual-Mode)*** | `GP8` | Pull-down `10 kΩ` ke `GND` | *(Pakai 2× `74LVC157`, catu `VCC33`)* | — | Pin `Select` kedua IC `74LVC157` |
| **Ground Bersama** | `GND` | Kabel pendek tebal | Titik `DGND` dekat `IC602` | — | Referensi bersama **wajib disolder lebih dulu** |
| **Tidak Dipakai** | `GP7`, `GP9` | — | Pin 9 `IC602` (`SSDIN3`) NC; `RCU_IN` pada `3.7VA` | — | **Jangan disambung** saat injeksi I2S awal |

---

## 7.6. Panduan Pengujian Meja Kerja Sementara (*Bench Testing Direct Injection*)

Untuk pengujian awal sebelum rangkaian proteksi `SN74LVC244A` dipasang permanen, penyambungan langsung `GP2..GP6` ke `Pad B` dapat dijalankan secara aman dengan mengikuti panduan ketat berikut:

### 7.6.1. Aturan Pengkabelan & Resistor Seri
* **Wajib Gunakan Resistor Seri:** Sisipkan resistor **`33 Ω – 100 Ω`** di antara pin `GP2..GP6` dan `Pad B` (misal: `47 Ω` untuk clock `GP2/GP3` dan `100 Ω` untuk data `GP4..GP6`). Jangan pernah menggunakan kawat jumper polos `0 Ω`.
* **Kabel Pendek:** Panjang kawat jumper serabut maksimal **≤ 10 cm** dan seragam.
* **GND Lebih Dahulu:** Solder `GND` `RP2350-Zero` ke `DGND` HT306 sebelum menghubungkan jalur sinyal.

### 7.6.2. Urutan Daya Manual Wajib (*Manual Power Sequencing*)
Karena mesin `PIO0` pada RP2350 memancarkan detak clock aktif `BCK/LRCK` (`3,3V`) begitu dicolok ke PC, urutan berikut **wajib dipatuhi tanpa boleh terbalik**:

```mermaid
sequenceDiagram
    autonumber
    participant HT as LG HT306 (VCC33)
    participant PS as IC602 (Pulsus PS9850)
    participant RP as Waveshare RP2350-Zero
    participant PC as Host PC (USB)

    Note over HT,PC: PROSEDUR MENYALAKAN (POWER-ON)
    HT->>PS: 1. Colok AC 220V & Nyalakan Power ON -> Mode AUX
    HT->>PS: 2. Tunggu 2-3 detik (VCC33 stabil 3.28V, IC101 selesai inisialisasi)
    PC->>RP: 3. BARU colokkan kabel USB Type-C ke PC (RP2350 boot)
    RP->>PS: 4. PIO0 mulai memancarkan I2S ke Pad B (Aman: VCC33 sudah aktif)
    PC->>PS: 5. Putar audio uji coba bertahap (Volume kecil VOL 3-5)

    Note over HT,PC: PROSEDUR MEMATIKAN (POWER-OFF)
    PC->>RP: 6. Hentikan pemutaran audio di PC
    PC->>RP: 7. CABUT kabel USB RP2350 dari PC TERLEBIH DAHULU (RP2350 mati 0V)
    HT->>PS: 8. BARU matikan / Standby-kan HT306 (VCC33 turun ke 0V)
```

> [!CAUTION]
> Jangan pernah mematikan HT306 saat USB RP2350 masih dicolok ke PC, dan jangan menyalakan USB PC saat HT306 masih mati/standby. Kelalaian urutan dapat menyebabkan tegangan `3,3V` dari RP2350 merembes masuk ke `IC602` yang sedang tidak berdaya (*parasitic back-powering*).
