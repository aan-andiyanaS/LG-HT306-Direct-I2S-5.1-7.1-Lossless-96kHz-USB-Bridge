# 🎬 LG HT306 Direct-I2S 5.1/7.1 Lossless 96 kHz USB Bridge (`Waveshare RP2350-Zero`)

🌐 **Language:** **English** | [Bahasa Indonesia](./README.id.md)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: RP2350-Zero](https://img.shields.io/badge/MCU-Waveshare%20RP2350--Zero-purple.svg)](https://www.waveshare.com/rp2350-zero.htm)
[![Audio: 96kHz 24-bit 5.1](https://img.shields.io/badge/Audio-96kHz%20%2F%2024--bit%205.1%20Bit--Perfect-success.svg)](docs/02_Lossless_96kHz_Codec_Specification.md)
[![Target DSP: Pulsus PS9850](https://img.shields.io/badge/DSP-Pulsus%20PS9850%20(IC602)-orange.svg)](docs/01_Hardware_Architecture_and_Mod_Guide.md)
[![Tests: 100% Passing](https://img.shields.io/badge/Verification-100%25%20Bit--Exact-brightgreen.svg)](docs/04_Build_Flash_and_JRiver_Setup.md)

**A high-performance Multichannel Digital Audio Bridge (*Direct-to-PWM Processor*) powered by `Waveshare RP2350-Zero` to transform legacy LG HT306 DVD Home Theaters (`HT306SU` / `HT306SF` / `HT306PD`) into a pure digital 5.1/7.1 Surround sound system streaming up to `96 kHz / 24-bit Lossless` from PC (JRiver Media Center) — while preserving 100% of LG's stock Hardware EQ features.**

---

## 1. About This Project

The **LG HT306** Home Theater system features a potent **`330W RMS`** *Full-Digital Class-D* power amplifier driven by an 8-channel PWM processor (**`Pulsus PS9850` / `IC602`**) and a **`TI TAS5352`** power stage. However, the stock unit is hobbled by its lack of digital surround inputs, only offering a basic stereo analog `AUX` RCA input.

This project bypasses that analog bottleneck entirely by injecting **6-Channel Digital I2S (`BCK`, `LRCK`, `ADATA0..2`)** directly from the PC into the digital input pins of **`IC602 (Pulsus PS9850)`** using a **`Waveshare RP2350-Zero`**:
* **100% Pure Digital (*Bit-Perfect*):** Completely free from noisy DAC/ADC analog roundtrips.
* **Overcoming USB 1.1 Full-Speed Limits for `96 kHz / 24-bit 5.1`:** Compresses the raw `1,728.0 KB/s` stream down to an average of **`716.1 KB/s` (`41.4%`)** using an ultra-low latency *Real-Time Lossless FLAC Order-4 + Escaped Golomb-Rice Codec* (`0 bit error`, `< 0.03 ms` unpacking latency on `Core 1`).
* **Preserving Stock LG Hardware EQ:** The original **`I2C` (`SDA`/`SCL`)** control lines from the master DVD SoC (**`MediaTek MT1389MH` / `IC501`**) are kept intact so that all **69-Band Hardware Biquad EQ** presets (*Bass Blast, Natural, Pop, Classic, Master Volume*) remain fully controllable via the original LG remote control.

---

## 2. How It Works & High-Level Architecture

In simple terms, the complete audio pipeline flows as follows:

$$\text{Player (JRiver / Windows)} \longrightarrow \text{VB-Cable (Virtual Soundcard)} \longrightarrow \text{HT306 Desktop App} \longrightarrow \text{RP2350-Zero} \longrightarrow \text{LG HT306 Amplifier} \longrightarrow \text{6 Speakers}$$

```mermaid
flowchart LR
    subgraph PC["1. Host PC (Windows 10 / 11)"]
        direction TB
        APP["Audio Players\n(JRiver / YouTube / Spotify / Movies)"]
        VBC["Virtual Soundcard (VB-Cable)\n'LG HT306 Home Theater'\n(Dual-Endpoint: Playback & Loopback)"]
        HOST["HT306 Desktop App (ht306_desktop_app)\nWASAPI Capture + Resampler <= 96 kHz\n+ Lossless Delta-Rice Encoder + WinUSB"]
        APP ==>|"Audio Stream"| VBC
        VBC ==>|"Loopback Capture"| HOST
    end

    subgraph MCU["2. Waveshare RP2350-Zero (USB Bridge)"]
        direction TB
        C0["Core 0: TinyUSB Receiver\nWinUSB Bulk (EP 0x03 OUT)\nRing Buffer + Reassembly"]
        C1["Core 1: Lossless Decoder\nUnpacks to PCM 32-bit (< 0.03 ms)"]
        PIO["DMA Ping-Pong + PIO0 Engine\nSynchronous Multi-Lane I2S Master\n(BCK, LRCK, ADATA0..2)"]
        C0 --> C1 --> PIO
    end

    subgraph HT306["3. LG HT306 Home Theater"]
        direction TB
        PS["IC602: Pulsus PS9850\n(Digital PWM Processor + 69-Band EQ)"]
        TAS["IC701/702: TI TAS5352\n(330W RMS Class-D Power Stage)"]
        SPK["6 Physical Speakers (5.1 Surround)\nFL, FR, Center, Subwoofer, SL, SR"]
        PS --> TAS --> SPK
    end

    HOST ==>|"USB Type-C (WinUSB Bulk)"| C0
    PIO ==>|"5x 33Ω Damping Resistors\n(GP2..GP6)"| PS
```

### The 4 Stages of the Audio Pipeline:
1. **Audio Source & Virtual Soundcard:** **JRiver Media Center** (or any Windows application) plays multichannel or stereo audio to **VB-Cable** (`LG HT306 Home Theater`). VB-Cable acts as the virtual soundcard so players have a standard Windows output target.
2. **HT306 Desktop Companion App (`ht306_desktop_app.exe`):** Captures audio from VB-Cable's recording pin (`LG HT306 Loopback Stream`), normalizes channels into 5.1 surround, clamps/resamples safely to $\le 96\text{ kHz}$ (48k or 96k), compresses frames losslessly, and streams packets over **WinUSB Bulk (EP 0x03 OUT)**.
3. **`Waveshare RP2350-Zero` Microcontroller:** `Core 0` receives USB bulk packets into SRAM, `Core 1` losslessly unpacks them back into pristine PCM 32-bit audio with zero latency, and the **PIO0 Multi-Lane I2S Engine** outputs synchronous digital audio pulses (`BCK`, `LRCK`, `ADATA0..2`).
4. **LG HT306 Mainboard:** Digital I2S signals arrive at `Pad B` of the lifted damping resistors directly into **`IC602 (Pulsus PS9850)`**, pass through LG's **69-Band Hardware EQ** (controlled via `I2C` in `AUX` mode), and are amplified by **`TI TAS5352`** directly to the 6 speakers.

> 📖 **Deep Dive Documentation:**
> * For JRiver-specific dual-endpoint routing, WASAPI Exclusive collision prevention, and DSP Studio setup, see **[06_JRiver_Audio_Data_Topology.md](docs/06_JRiver_Audio_Data_Topology.md)**.
> * For the complete end-to-end hardware schematics, electrical protection, and power sequencing, see **[07_Complete_Architecture_and_Topology.md](docs/07_Complete_Architecture_and_Topology.md)**.
> * For project inspirations and the comprehensive technology stack inventory, see **[08_Credits_and_Acknowledgments.md](docs/08_Credits_and_Acknowledgments.md)**.

---

## 3. Pinout Mapping Summary (`RP2350-Zero` $\rightarrow$ `IC602 Pulsus PS9850`)

| Pin `RP2350-Zero` | Signal | Audio Channel | Connection on LG HT306 Mainboard (`IC602`) |
| :---: | :--- | :--- | :--- |
| **`GP2`** | `I2S_BCK` | Bit Clock (`64 × Fs`) | Via `33 Ω` Resistor $\rightarrow$ Pad of lifted `BCK` resistor (towards `IC602`) |
| **`GP3`** | `I2S_LRCK` | Word Select (`Fs`) | Via `33 Ω` Resistor $\rightarrow$ Pad of lifted `LRCK` resistor (towards `IC602`) |
| **`GP4`** | `I2S_ADATA0` | **Front L & Front R** | Via `33 Ω` Resistor $\rightarrow$ Pad of lifted `ADATA0` resistor (towards `IC602`) |
| **`GP5`** | `I2S_ADATA1` | **Surround L & Surround R** | Via `33 Ω` Resistor $\rightarrow$ Pad of lifted `ADATA1` resistor (towards `IC602`) |
| **`GP6`** | `I2S_ADATA2` | **Center & Subwoofer (LFE)**| Via `33 Ω` Resistor $\rightarrow$ Pad of lifted `ADATA2` resistor (towards `IC602`) |
| **`GP7` / `GP8` / `GP9`** | Expansion | `ADATA3` (7.1) / `MUX` / `IR` | *(Optional)* 7.1 Expansion, `74LVC157` Switch, & LG Remote injection |
| **`GND`** | `Ground` | Digital Reference (`3.3V`)| Digital **`GND`** point near `IC602` on HT306 mainboard |

---

## 4. Quick Start (Build, Flash, & Verify)

```powershell
# 1. Run End-to-End Verification (Bit-Perfect Codec, 4-Lane PIO, & USB 64-Byte Stream Reassembly)
python tests/verify_end_to_end.py

# 2. Compile Firmware into ht306_rp2350_zero.uf2 (Requires Pico SDK v2.0+)
cmake -B firmware/build -S firmware -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/build --config Release -j
```
* **Flashing Guide:** Hold the **`BOOT`** button on the `RP2350-Zero` while plugging the USB Type-C cable into your PC, then copy **`firmware/build/ht306_rp2350_zero.uf2`** into the mounted **`RP2350`** volume.

---

## 5. Comprehensive Documentation (`docs/`)

Deep technical documentation has been organized into dedicated bilingual modules under **[`docs/`](docs/)**:

| Document | Detailed Contents |
| :--- | :--- |
| 📄 **[01_Hardware_Architecture_and_Mod_Guide.md](docs/01_Hardware_Architecture_and_Mod_Guide.md)**<br>*(ID: [Versi Bahasa Indonesia](docs/01_Hardware_Architecture_and_Mod_Guide.id.md))* | Detailed breakdown of LG HT306 ICs, anatomy of the **5 SMD series damping resistors** (*no trace cutting*), and why **`AUX` mode & `2 to 5.1 (PLII)` / Stock EQ** will never interfere with PC 5.1 audio. |
| 📄 **[02_Lossless_96kHz_Codec_Specification.md](docs/02_Lossless_96kHz_Codec_Specification.md)**<br>*(ID: [Versi Bahasa Indonesia](docs/02_Lossless_96kHz_Codec_Specification.id.md))* | Mathematical equations for **Single-Pass FLAC Order-4 Predictor**, **ZigZag Mapping**, **Escaped Golomb-Rice**, and detailed specification of the **14-Byte Packet Header (`HT306_LosslessHeader_t`)**. |
| 📄 **[03_PIO_4Lane_I2S_and_DualCore_DMA.md](docs/03_PIO_4Lane_I2S_and_DualCore_DMA.md)**<br>*(ID: [Versi Bahasa Indonesia](docs/03_PIO_4Lane_I2S_and_DualCore_DMA.id.md))* | Precision clock timing diagrams for **`i2s_multi.pio`** on *falling edge* `BCK`, the *4-Lane Nibble Bit-Interleaver*, and the **Wait-Free SPSC Chained Ping-Pong DMA** engine in SRAM. |
| 📄 **[04_Build_Flash_and_JRiver_Setup.md](docs/04_Build_Flash_and_JRiver_Setup.md)**<br>*(ID: [Versi Bahasa Indonesia](docs/04_Build_Flash_and_JRiver_Setup.id.md))* | Step-by-step firmware build guide, `.uf2` flashing, benchmark test outputs, and **JRiver Media Center (`DSP Studio`)** configuration. |
| 📄 **[05_PC_Audio_Bridge_PRD_and_Architecture.md](docs/05_PC_Audio_Bridge_PRD_and_Architecture.md)**<br>*(ID: [Versi Bahasa Indonesia](docs/05_PC_Audio_Bridge_PRD_and_Architecture.id.md))* | Product Requirements Document (PRD) and technical architecture for the **HT306 PC Audio Bridge Companion Application** on Windows 10/11. |
| 📄 **[06_JRiver_Audio_Data_Topology.md](docs/06_JRiver_Audio_Data_Topology.md)**<br>*(ID: [Versi Bahasa Indonesia](docs/06_JRiver_Audio_Data_Topology.id.md))* | Comprehensive audio data pipeline for **JRiver Media Center**, dual-endpoint VB-Cable routing, WASAPI Exclusive collision prevention, and source code mapping. |
| 📄 **[07_Complete_Architecture_and_Topology.md](docs/07_Complete_Architecture_and_Topology.md)**<br>*(ID: [Versi Bahasa Indonesia](docs/07_Complete_Architecture_and_Topology.id.md))* | Full end-to-end hardware and software architecture, **Anti-Backpowering** electrical protection, power sequencing state machine, and official schematic mapping. |
| 📄 **[08_Credits_and_Acknowledgments.md](docs/08_Credits_and_Acknowledgments.md)**<br>*(ID: [Versi Bahasa Indonesia](docs/08_Credits_and_Acknowledgments.id.md))* | Record of project inspirations, reverse engineering rationale (*upcycling*), and full technology inventory across silicon, firmware, desktop host, and developer toolchains. |

---

## 6. Directory Structure

```text
HomeTheather/
├── README.md                              # Main overview & navigation (English)
├── README.id.md                           # Main overview & navigation (Bahasa Indonesia)
├── LICENSE                                # MIT Open-Source License
├── docs/                                  # In-depth technical guides (Bilingual: EN & ID)
├── firmware/                              # C/PIO Firmware for Waveshare RP2350-Zero
├── pc_stream_app/                         # Native C++ WebView2 desktop companion application
├── pc_streamer/                           # Host-side Python/C++ streaming prototype
└── tests/                                 # End-to-end mathematical & hardware simulation suite
```

---

## 7. Inspirations & Prior Art

* **[`tierneytim/Pico-USB-audio`](https://github.com/tierneytim/Pico-USB-audio):** Pioneered 8-Channel Multi-Lane I2S on Raspberry Pi Pico (`48 kHz`), which we expanded with a *Core 1 Lossless Order-4 Golomb-Rice Unpacker* to support **`96 kHz / 24-bit 5.1`**.
* **[`xiph/flac`](https://github.com/xiph/flac)** & **[`dbry/WavPack`](https://github.com/dbry/WavPack):** Mathematical foundation for Order-4 linear prediction and Golomb-Rice residual coding.
* **[`raspberrypi/pico-extras`](https://github.com/raspberrypi/pico-extras)** & **[`hathach/tinyusb`](https://github.com/hathach/tinyusb):** Foundation for *Chained Ping-Pong DMA* + `PIO` and USB Device stack on `RP2350`.

---

## 8. License (MIT License)

This project is licensed under the **[MIT License](LICENSE)** — free to use, modify, and distribute for both personal and commercial applications.
