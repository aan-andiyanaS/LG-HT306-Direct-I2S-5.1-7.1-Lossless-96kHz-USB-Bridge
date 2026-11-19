# 7. Complete System Architecture & End-to-End Topology (LG HT306 Direct-I2S)

🌐 **Language:** **English** | [Bahasa Indonesia](./07_Complete_Architecture_and_Topology.id.md)

[⬅️ Back to Main README](../README.md) | [Hardware & Mod Guide](./01_Hardware_Architecture_and_Mod_Guide.md) | [Codec 96 kHz](./02_Lossless_96kHz_Codec_Specification.md) | [PIO 4-Lane & DMA](./03_PIO_4Lane_I2S_and_DualCore_DMA.md) | [PC Audio Bridge PRD](./05_PC_Audio_Bridge_PRD_and_Architecture.md) | [JRiver Audio Topology](./06_JRiver_Audio_Data_Topology.md) | [Credits & Tech Stack](./08_Credits_and_Acknowledgments.md)

This document provides a comprehensive **end-to-end** overview of the entire system — covering Windows 10/11 host audio routing, USB transport on the `Waveshare RP2350-Zero`, the **Anti-Backpowering** electrical protection layer, and actual schematics of the **LG HT306PD (`EAX63554302`)** mainboard based on official *Service Manual* verification.

---

## 7.1. End-to-End System Topology (Hardware & Software Architecture)

The following diagram maps the 6-channel audio signal flow (`FL, FR, C, LFE, SL, SR`), control/telemetry pathways, and power domain boundaries from the PC down to the 6 physical speakers:

```mermaid
flowchart TB
    subgraph PC["1. HOST PC WINDOWS 10/11 (pc_stream_app)"]
        direction TB
        APPS["Playback Applications\nJRiver / Spotify / Netflix / Games"]
        VDEV["Virtual Audio Sink\nVB-CABLE ('LG HT306 Home Theater')\n48 kHz / 96 kHz Multichannel 5.1"]
        APM["AudioPolicyManager & AudioEndpointHelper\nAuto-Switch Default Endpoint + Sync Friendly Names"]
        CAP["AudioCapture (WASAPI Event-Driven Capture)\nSmoothed Gain + Soft Limiter + Test/Sweep Tone"]
        ENC["Lossless Encoder (lossless_codec.c)\nFLAC Order-4 Delta + Golomb-Rice\nFrame 48 samples + Sync 0xA55A + CRC-8"]
        USBTX["UsbTransport (WinUSB & CDC ACM)\nVID 0x2E8A : PID 0x4306\nBulk Pipe EP 0x03 OUT + Serial Monitor"]
        WEBUI["Desktop Dashboard (main_webview.cpp)\nWebView2 SPA + System Tray + Realtime VU Meter"]

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
        Q2["SPSC Ring Buffer 2\ng_pio_ring_buffer[16 blocks]\n48 frames/block (32B/frame)"]
        DMA["Chained Ping-Pong DMA (A <-> B)\ng_silence_block Fallback on Underrun\nPLL Drift Tracking +/-0.02%"]
        PIO["PIO0_SM0 (i2s_multi.pio)\nOutput Falling-Edge BCK\nGP2..GP6 (4mA Drive Strength)"]

        TUSB --> REASM --> Q1 --> UNPK --> Q2 --> DMA --> PIO
        REASM -.->|"VU & Stats"| TUSB
    end

    subgraph PROT["3. ANTI-BACKPOWERING PROTECTION LAYER (Power Domain Isolation: 3.3V PC vs HT306)"]
        direction TB
        OPT_IC["Option A (Hardware I_off Isolation)\n1x SN74LVC244A (VCC = VCC33 HT306)\nor 1x SN74AXC8T245 (Dual-Supply)\nor 2x 74LVC157 (Dual-Mode DVD+PC)"]
        OPT_FW["Option B (Zero-IC Firmware Auto-Hi-Z)\n10k Series Resistor + 100k Pull-down to GP10\nSense VCC33 -> Set GP2..GP8 to Input Hi-Z"]
    end

    subgraph HT["4. LG HT306PD MAINBOARD (PCB EAX63554302)"]
        direction TB
        INJ["5x Lifted Series Resistors (Pad B)\nR648(47R), R647(47R), R631..R633(100R)"]
        MICOM["IC101: Audio MICOM (LC87F5M64A)\nBoot, Volume, EQ Presets, Power Control"]
        PS["IC602: Pulsus PS9850\nPin 4..8: SBCK, SLRCK, SSDIN0..2\n69-Band Biquad EQ + ASRC + 6-Ch PWM\nCrystal X602 (19.2 MHz Intact)"]
        AMP["IC701 & IC702: TI TAS5352\nDual Class-D MOSFET Power Stage\nPVDD 34.8V DC | GVDD 12.2V DC"]
        SPK["JK701 -> 6 Physical Speakers (5.1)\nFL, FR, Center, Subwoofer (LFE), SL, SR"]

        INJ --> PS --> AMP --> SPK
        MICOM -->|"I2C (R638/R637) & nRESET (R636)\nLeft 100% Intact"| PS
    end

    USBTX ==>|"USB Type-C (WinUSB Bulk)"| TUSB
    USBTX -.->|"CDC ACM Serial"| TUSB
    PIO ==>|"GP2..GP6 (BCK, LRCK, ADATA0..2)"| PROT ==>|"Via 33-Ohm Series Resistors"| INJ
```

---

## 7.2. Mainboard Schematic Topology (`IC501` ↔ `IC201` ↔ `IC602` Injection Point)

Based on official schematic verification from the *LG HT306PD Service Manual* (`MAIN & AMP (#1)` & `(#2)`), there are **two groups of series damping resistors** along the I2S signal traces. The resistors that **must be desoldered/lifted** are the five resistors directly in front of **`IC602` (`R648, R647, R631, R632, R633`)** so that the ADC **`IC201 (CS5346)`** remains safely isolated alongside `IC501`.

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

    subgraph TP["Upstream Test Points (Pad A — Disconnected from IC602)"]
        T_BCK["TP609: BCK\n[R648 47R LIFTED]"]
        T_LRCK["TP608: LRCK\n[R647 47R LIFTED]"]
        T_D0["TP610: ADATA0\n[R631 100R LIFTED]"]
        T_D1["TP611: ADATA1\n[R632 100R LIFTED]"]
        T_D2["TP612: ADATA2\n[R633 100R LIFTED]"]
    end

    subgraph RP["RP2350-Zero Injection (+ Buffer/33R)"]
        G2["GP2 (BCK) -> 33R"]
        G3["GP3 (LRCK) -> 33R"]
        G4["GP4 (ADATA0 FL/FR) -> 33R"]
        G5["GP5 (ADATA1 SL/SR) -> 33R"]
        G6["GP6 (ADATA2 C/LFE) -> 33R"]
        G7["GP7 (ADATA3 7.1) — NOT CONNECTED"]
    end

    subgraph PS["IC602: Pulsus PS9850 (VDD_IO 3.26V | Core 1.25V)"]
        PS4["Pin 4: SBCK (Pad B)"]
        PS5["Pin 5: SLRCK (Pad B)"]
        PS6["Pin 6: SSDIN0 (Pad B)"]
        PS7["Pin 7: SSDIN1 (Pad B)"]
        PS8["Pin 8: SSDIN2 (Pad B)"]
        PS9["Pin 9: SSDIN3 (NC on PCB)"]
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

    T_BCK -.-x|"Physical Cut"| PS4
    T_LRCK -.-x|"Physical Cut"| PS5
    T_D0 -.-x|"Physical Cut"| PS6
    T_D1 -.-x|"Physical Cut"| PS7
    T_D2 -.-x|"Physical Cut"| PS8

    G2 ==>|"Pad B"| PS4
    G3 ==>|"Pad B"| PS5
    G4 ==>|"Pad B"| PS6
    G5 ==>|"Pad B"| PS7
    G6 ==>|"Pad B"| PS8

    C_I2C ==>|"INTACT (EQ, Vol, Unmute)"| PS_CTL
```

> **Crucial Schematic Notes:**
> * For Dual-Mode setups (`74LVC157`), Input `A` (stock DVD signal) can be tapped from **`TP609`, `TP608`, `TP610`, `TP611`, `TP612`** or `Pad A` of lifted `R648..R633` (requires **2× `74LVC157`** ICs for 5 lines).
> * **`GP9` (`REMOCON_TX`)** on `firmware/include/ht306_control.h` is left unconnected in the initial injection stage because the `RCU_IN` line connects to the `3.7VA` standby rail.

---

## 7.3. Electrical Topology, Power Domains, & Anti-Backpowering Protection

The diagram below highlights the root cause of *back-powering* (the `VCC33` rail drops to `0V` when HT306 is in Standby/Off while RP2350-Zero receives continuous `5V` from PC USB) and illustrates how two protection schemes prevent hardware damage:

```mermaid
flowchart TB
    subgraph PWR_PC["PC Power Domain (Always On While PC is Running)"]
        USB5["USB VBUS PC (5V)"] -->|"Onboard LDO RP2350"| RP33["3.3V IOVDD RP2350-Zero"]
        RP33 --> RP_IO["GP2..GP6 Output I2S 3.3V\n& GP8 MUX Select"]
    end

    subgraph PWR_HT["LG HT306 Power Domain (SMPS -> PN901 -> Mainboard)"]
        AC["Mains AC 220V\n(SMPS HOT CIRCUIT Area\nC903 68uF/450V)"] --> T901["SMPS Standby (T901)"]
        AC --> T902["SMPS Power (T902)"]
        T901 --> R37["Always-On 3.7VA Rail\n(PN901 Pin 8-9 | C122 3.70V)"]
        R37 --> MIC_P["IC101 MICOM Supply (~3.48V)\n& Front IR RC301"]
        R37 --> IC102["IC102: Regulator 278R33\nPin 4 VCTL <- V33_CTL (R124)"]
        MIC_P -->|"P_CTL & V33_CTL\nHIGH when Power ON\nLOW when Standby/Off"| IC102
        IC102 -->|"Pin 2 Out -> C146 (+)\n3.28V when ON | 0V when Standby"| VCC33["VCC33 Rail -> IC602 VDD_IO"]
        T902 -->|"Active when ON"| R_HI["VCC50 (5V) | 12V (GVDD)\nPVDD 34.8V-37.35V -> TAS5352"]
    end

    subgraph GUARD_A["Protection Option A: Hardware I_off Buffer IC (Recommended)"]
        BUF["SN74LVC244A (VCC = VCC33)\nor SN74AXC8T245 (VCCA=3V3, VCCB=VCC33)"]
        RP_IO -->|"Input 3.3V"| BUF
        VCC33 -->|"Buffer Power Supply"| BUF
        BUF -->|"ON: Pass I2S Signals via 33R\nStandby (VCC33=0V): Complete Hi-Z (I_off)"| IC602_IN["Pin 4..8 IC602 (PS9850)"]
    end

    subgraph GUARD_B["Protection Option B: GP10 Voltage Sense + Firmware Hi-Z"]
        DIV["10 kOhm Series + 100 kOhm Pull-down to GND"]
        FW_CHK["RP2350 GP10 (Digital Input)"]
        VCC33 -->|"Voltage Sensing"| DIV --> FW_CHK
        FW_CHK -->|"GP10 == LOW: Set GP2..GP8 = GPIO_IN (Hi-Z)\nGP10 == HIGH: Delay 300-500ms -> PIO0 ON"| IC602_IN
    end

    GND_BUS["Mandatory Reference: GND RP2350-Zero <==> DGND Mainboard HT306 (Short Thick Wire)"]
```

---

## 7.4. Safe Power Sequencing State Machine

```mermaid
stateDiagram-v2
    [*] --> BOOT_HIZ: RP2350 Plugged into PC USB
    BOOT_HIZ: All I2S Pins (GP2..GP8) in Hi-Z / Isolated State\nBlue Standby LED | WinUSB Endpoint Enumerated by Windows

    BOOT_HIZ --> WAIT_HT306_ON: Sense VCC33 HT306 (Hardware I_off or GP10 == LOW)
    WAIT_HT306_ON --> HT306_BOOT_DELAY: HT306 Powered ON (AUX Mode, VCC33 reaches 3.28V)

    HT306_BOOT_DELAY: Wait 300-500 ms for IC101 I2C init & IC602 reset completion
    HT306_BOOT_DELAY --> I2S_IDLE_ZERO: Enable I2S Outputs / Buffer Active\nPIO0 Streams g_silence_block (Zero Audio 0x00000000)

    I2S_IDLE_ZERO --> STREAMING_ACTIVE: USB Bulk Packets (0xA55A) Received on EP 0x03
    STREAMING_ACTIVE: 48 kHz Mode (Green LED) / 96 kHz Mode (Purple LED)\nCore 1 Decode < 0.03 ms + PIO0 4-Lane + PLL Drift Tracking

    STREAMING_ACTIVE --> I2S_IDLE_ZERO: PC Paused / Stopped (Fallback to g_silence_block)
    STREAMING_ACTIVE --> SAFE_ISOLATE: HT306 Powered Off / Standby (VCC33 drops to 0V)
    I2S_IDLE_ZERO --> SAFE_ISOLATE: HT306 Powered Off / Standby (VCC33 drops to 0V)

    SAFE_ISOLATE: Hardware I_off / Firmware immediately disconnects outputs to Hi-Z\nWindows DOES NOT lose USB connection (Anti Disconnect-Loop)
    SAFE_ISOLATE --> WAIT_HT306_ON: HT306 Standby
```

---

## 7.5. Workbench Quick Reference Table (Pinout, Parts, & Test Points)

| Line / Signal | Pin `RP2350-Zero` | Series Resistor | Original Component on PCB `EAX63554302` (Lifted) | Upstream Test Point (`Pad A`) | Target on `IC602 (PS9850)` (`Pad B`) |
| :--- | :---: | :---: | :--- | :---: | :--- |
| **`I2S_BCK`** | `GP2` | `33 Ω` series | **`R648`** (`47 Ω`) | `TP609` | **Pin 4** (`SBCK`) |
| **`I2S_LRCK`** | `GP3` | `33 Ω` series | **`R647`** (`47 Ω`) | `TP608` | **Pin 5** (`SLRCK`) |
| **`I2S_ADATA0`** (`FL/FR`) | `GP4` | `33 Ω` series | **`R631`** (`100 Ω`) | `TP610` | **Pin 6** (`SSDIN0`) |
| **`I2S_ADATA1`** (`SL/SR`) | `GP5` | `33 Ω` series | **`R632`** (`100 Ω`) | `TP611` | **Pin 7** (`SSDIN1`) |
| **`I2S_ADATA2`** (`C/LFE`) | `GP6` | `33 Ω` series | **`R633`** (`100 Ω`) | `TP612` | **Pin 8** (`SSDIN2`) |
| **Sense `VCC33` *(Option B)*** | `GP10` | `10 kΩ` series + `100 kΩ` to `GND` | *(Do not lift any resistor)* | — | **`+` Leg of `C146`** / **`IC102` Pin 2** (`3.28V` ON / `0V` Standby) |
| **`MUX_SELECT` *(Dual-Mode)*** | `GP8` | Pull-down `10 kΩ` to `GND` | *(Uses 2× `74LVC157`, powered by `VCC33`)* | — | Pin `Select` on both `74LVC157` ICs |
| **Common Ground** | `GND` | Short thick wire | `DGND` point near `IC602` | — | Common reference **must be soldered first** |
| **Not Connected** | `GP7`, `GP9` | — | Pin 9 `IC602` (`SSDIN3`) is NC; `RCU_IN` is on `3.7VA` | — | **Do not connect** during initial I2S setup |

---

## 7.6. Bench Testing Direct Injection Guidelines

For bench tests before the permanent `SN74LVC244A` buffer circuit is installed, direct connections from `GP2..GP6` to `Pad B` can be safely conducted under strict rules:

### 7.6.1. Wiring Rules & Series Damping Resistors
* **Mandatory Series Resistors:** Always insert **`33 Ω – 100 Ω`** damping resistors between `GP2..GP6` and `Pad B` (e.g., `47 Ω` for clocks `GP2/GP3` and `100 Ω` for data lines `GP4..GP6`). Never use raw `0 Ω` wire.
* **Short Leads:** Keep wire lengths uniform and **≤ 10 cm**.
* **GND Connection First:** Solder the `GND` line between `RP2350-Zero` and HT306 `DGND` prior to connecting any signal wires.

### 7.6.2. Strict Manual Power-On / Power-Off Sequence
Because the `PIO0` engine on the RP2350 begins outputting active `3.3V` clock signals as soon as it is plugged into the PC, the following sequence **must be obeyed without exception**:

```mermaid
sequenceDiagram
    autonumber
    participant HT as LG HT306 (VCC33)
    participant PS as IC602 (Pulsus PS9850)
    participant RP as Waveshare RP2350-Zero
    participant PC as Host PC (USB)

    Note over HT,PC: POWER-ON PROCEDURE
    HT->>PS: 1. Plug in AC 220V & Turn Power ON -> Set to AUX Mode
    HT->>PS: 2. Wait 2-3 seconds (VCC33 stabilizes at 3.28V, IC101 completes init)
    PC->>RP: 3. ONLY THEN connect USB Type-C to PC (RP2350 boots)
    RP->>PS: 4. PIO0 begins streaming I2S to Pad B (Safe: VCC33 is already live)
    PC->>PS: 5. Play test audio with low volume (VOL 3-5)

    Note over HT,PC: POWER-OFF PROCEDURE
    PC->>RP: 6. Stop audio playback on PC
    PC->>RP: 7. UNPLUG RP2350 USB CABLE FROM PC FIRST (RP2350 powers down to 0V)
    HT->>PS: 8. ONLY THEN turn off or standby the HT306 (VCC33 drops to 0V)
```

> [!CAUTION]
> Never turn off or standby the HT306 while the RP2350 USB cable is still plugged into the PC, and never plug the USB cable in while the HT306 is off. Inverting this sequence will cause `3.3V` logic voltages from the RP2350 to backfeed into the unpowered `IC602` (*parasitic back-powering*), risking permanent silicon degradation.
