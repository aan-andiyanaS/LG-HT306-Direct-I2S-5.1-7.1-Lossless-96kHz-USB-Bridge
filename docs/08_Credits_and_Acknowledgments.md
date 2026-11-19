# 8. Project Credits, Inspirations & Technology Stack

🌐 **Language:** **English** | [Bahasa Indonesia](./08_Credits_and_Acknowledgments.id.md)

[⬅️ Back to Main README](../README.md) | [Hardware & Mod Guide](./01_Hardware_Architecture_and_Mod_Guide.md) | [Codec 96 kHz](./02_Lossless_96kHz_Codec_Specification.md) | [JRiver Audio Topology](./06_JRiver_Audio_Data_Topology.md) | [Complete Architecture](./07_Complete_Architecture_and_Topology.md)

This document records the foundational inspirations, architectural design rationale, and the complete inventory of technologies, hardware components, software libraries, protocols, and developer toolchains utilized to build the **LG HT306 USB Direct-I2S Hi-Res Audio Bridge**.

---

## 8.1. Project Background & Inspirations

### 1. Upcycling & Preserving Classic Home Theater Systems
5.1-channel DVD Home Theater systems from the 2010s, such as the **LG HT306 (`HT306PD` / `HT306SU` / `HT306SF`)**, feature discrete, high-performance Class-D power stages and digital audio signal processors:
* The mainboard couples a **Pulsus PS9850** digital processor (featuring internal 32-bit math, a 69-band biquad parametric EQ, and multi-channel PWM modulation) with a pair of **Texas Instruments TAS5352** power stage amplifiers driven by a high-voltage rail ($P_{\text{VDD}} \approx 35\text{V}$).
* Despite this robust acoustic hardware, these units are frequently discarded or left idle because **they lack modern HDMI ARC, optical S/PDIF, or USB multi-channel digital audio inputs**. Their sole external input is an analog stereo RCA pair (2.0) on the rear panel, drastically limiting their true 5.1 potential.
* **Core Inspiration:** Trace the official schematic of the **LG EAX63554302** mainboard, physically decouple the outdated MediaTek DVD processor from the I2S lines, and inject pure, bit-exact digital audio directly from a modern PC without double ADC-DAC conversion losses. This project transforms a vintage home theater system into a high-grade **Studio Master 5.1 Surround Sound Card (96 kHz 24/32-Bit)** without requiring expensive modern AV receivers.

### 2. Breakthrough Capabilities of the Raspberry Pi RP2350 (PIO & Dual-Core)
* Utilizing the **Waveshare RP2350-Zero** was inspired by the unprecedented versatility of Raspberry Pi's **Programmable I/O (PIO)** architecture, which enables developer-defined hardware interfaces with cycle-accurate timing.
* In this project, a single PIO state machine (`i2s_multi.pio`) serves as a synchronous **4-Lane I2S transmitter**, generating a Bit Clock (`BCK`), Word Select (`LRCK`), and 3–4 Data Lines (`ADATA0`..`ADATA3`) in parallel on the *falling clock edge* with zero CPU cycle penalty.
* The **Dual-Core ARM Cortex-M33** topology provides complete execution isolation: Core 0 dedicates its execution cycles to the USB stack and stream reassembly, while Core 1 performs real-time Delta-Rice lossless decompression entirely in high-speed SRAM without interruption.

### 3. Bit-Perfect Audiophile Standards & JRiver Media Center
* Driven by the audiophile standard for absolute signal fidelity (*bit-exact playback*), this system was designed to maintain data integrity from the digital media player down to the physical speaker cones.
* **JRiver Media Center**—with its double-precision **64-bit float DSP Studio**, parametric EQ, and bass management (*Room Correction*)—served as the gold standard for verifying dynamic range and channel mapping accuracy.

---

## 8.2. Comprehensive Technology Stack

### A. Hardware & Silicon Platform

| Component / Silicon | Manufacturer / Specifications | Project Role |
| :--- | :--- | :--- |
| **RP2350-Zero** | Waveshare / Raspberry Pi Ltd | Primary microcontroller: Dual ARM Cortex-M33 @ 150 MHz, 520 KB SRAM, USB 1.1 Full-Speed controller, dual-core execution, PIO hardware state machines. |
| **Pulsus PS9850** (`IC602`) | Pulsus Technologies | Digital Audio Processor & 6-Ch PWM Modulator on the HT306 mainboard: Direct 4-lane I2S input, 69-band biquad EQ, master/channel volume control, 19.2 MHz crystal (`X602`). |
| **TAS5352** (`IC701`, `IC702`) | Texas Instruments | Dual High-Performance 125W Stereo / 5.1 Class-D Power Stage Amplifiers powered by $P_{\text{VDD}} = 34.8\text{V}$–$37.35\text{V}$ DC rails. |
| **SN74LVC244A** | Texas Instruments | Octal Buffer/Line Driver with 3-state outputs and $I_{\text{off}}$ partial-power-down protection: Isolates the Pulsus processor from parasitic back-powering when the HT306 is unpowered. |
| **74LVC157** *(Dual-Mode Option)* | NXP / TI | Quad 2-input high-speed multiplexer for seamless hardware switching between the factory DVD player and PC USB mode. |
| **MediaTek MT1389** (`IC501`) | MediaTek Inc | Factory DVD SoC: I2S output traces physically cut via lifted damping resistors (`R648, R647, R631..R633`) to permit RP2350 direct injection. |
| **LC87F5M64A** (`IC101`) | Sanyo / ON Semiconductor | Mainboard Audio MICOM: Manages system boot sequencing, front IR remote decoding, AUX mode switching, and I2C register configuration of the PS9850. |
| **WS2812B Smart RGB LED** | Worldsemi | Onboard status telemetry indicator on pin `GP16` (Blue = Standby, Green = 48 kHz, Purple = 96 kHz, Red = Error). |

---

### B. Embedded Firmware Architecture

| Technology / Library | Version / License | Project Role |
| :--- | :--- | :--- |
| **Raspberry Pi Pico SDK** | v2.3.1 (BSD-3-Clause) | Core C/C++ firmware foundation, hardware abstraction layer (HAL), memory barriers, and dual-core synchronization primitives (`multicore_launch_core1`). |
| **TinyUSB** | v0.18.0+ (MIT License) | Lightweight open-source USB device stack: Implements USB Composite Device (Vendor Bulk Pipe + CDC ACM Serial Monitor). |
| **Microsoft OS 2.0 Descriptors** | Microsoft Standard | Embedded BOS & MS OS 2.0 platform registry descriptors enabling automatic Windows 10/11 PnP binding to `winusb.sys` without Zadig. |
| **Custom Delta-Rice Codec** | Proprietary Algorithmic (MIT) | Real-time lossless compression: FLAC Order-4 polynomial predictor + Zigzag mapping + adaptive Golomb-Rice coding with escape raw bits, ensuring USB Full-Speed throughput $< 880\text{ KB/s}$. |
| **PIO Assembly (`i2s_multi.pio`)** | Custom Assembly (Pico PIO) | 32-bit micro-instruction assembler program driving 4-lane synchronous I2S clocked precisely on the falling edge of BCK (`64 × Fs`). |
| **Chained Ping-Pong DMA** | RP2350 Hardware DMA | Dual DMA channels configured in ping-pong circular chaining with silence block protection and micro-drift clock tracking ($\pm 0.02\%$). |

---

### C. Host Desktop Application (`pc_stream_app/`)

| Component / Subsystem | Technology Base | Project Role |
| :--- | :--- | :--- |
| **C++17 Desktop Companion** | Native Win32 / C++17 | Standalone, thread-safe, high-performance host bridge with ultra-low memory overhead ($< 35\text{ MB}$ RAM) and zero third-party runtime baggage. |
| **Windows Core Audio (WASAPI)** | Windows SDK (`mmdeviceapi.h`) | Real-time desktop audio capture using event-driven loopback mode (`IAudioCaptureClient`) with $< 3\text{ ms}$ buffer latency. |
| **Microsoft WinUSB (`winusb.dll`)** | Windows Kernel Usermode API | Direct user-space bulk transmission to Endpoint 0x03 OUT, bypassing the latency-prone Windows kernel audio driver (`usbaudio2.sys`). |
| **VB-Audio Virtual Cable** | VB-Audio Software | Virtual audio routing pipeline providing a dedicated playback pin (*`eRender`*) for exclusive locks and a recording pin (*`eCapture`*) for bridge ingestion. |
| **Windows Audio Policy Manager** | COM `IPolicyConfig` & Registry | Internal C++ module to synchronize friendly endpoint names, force recording endpoints active, and manage default playback transitions on hotplug. |
| **Multi-Channel DSP Resampler** | Pure C++17 (`audio_rate_dsp.h`) | Multi-format decoder (`IEEE_FLOAT32`, `PCM16`, `PCM24`, `PCM32`), Blackman Windowed-Sinc anti-aliased resampler, auto-clamping ($\le 96\text{ kHz}$), and `tanh()` soft limiter. |
| **Microsoft Edge WebView2** | Chromium Embedded Framework | Lightweight, high-performance graphical user interface utilizing the evergreen Windows 10/11 system runtime. |
| **Web Frontend UI** | HTML5, CSS3, ES6 Vanilla JS | Modern Stitch UI dashboard: 6-channel real-time VU meters, sample rate badges, device selectors, and DSP configuration controls. |

---

### D. Developer Tools, Testing & Verification

| Tool / Framework | Version / Function | Project Role |
| :--- | :--- | :--- |
| **Python 3** | Python 3.10+ | Mathematical end-to-end verification harness, PIO shift-register simulation, and initial bridge prototype (`pc_streamer/`). |
| **CMake & w64devkit (GCC)** | CMake 3.20+ / GCC 14+ | Cross-platform build toolchain for compiling ARM Cortex-M33 firmware and native Windows C++17 binaries. |
| **Playwright MCP Server** | Automation & Testing | Headless and interactive verification of the 7-page WebView2 user interface. |
| **Antigravity AI Agent** | Google DeepMind | Orchestration architecture, formal specification drafting, isolated code implementation, adversarial review, and debugging. |

---

## 8.3. Supported Media Players & Ecosystem

This audio bridge has been tested and verified across industry-standard playback environments:
1. **JRiver Media Center (DSP Studio):** Bit-perfect streaming via **WASAPI Exclusive**, room acoustic correction, crossover management, and 64-bit float internal processing.
2. **Foobar2000:** Audiophile media player utilizing WASAPI output components or virtual ASIO sinks.
3. **VLC Media Player:** High-definition video playback with 5.1 discrete surround audio tracks (Dolby Digital, DTS, AAC 5.1, FLAC 5.1).
4. **Daily Streaming Platforms:** YouTube, Spotify, Netflix, Disney+, and PC Gaming via standard Windows Shared Audio.

---

## 8.4. Acknowledgments & Credits

* **Raspberry Pi Foundation:** For engineering the versatile RP2040 and RP2350 silicon architectures and providing exceptional Pico SDK and PIO documentation.
* **LG Electronics:** For designing the enduring, acoustically solid **LG HT306** hardware platform and publishing comprehensive *Service Manual* schematics.
* **Vincent Burel (VB-Audio Software):** For developing **VB-Audio Virtual Cable**, an indispensable utility for Windows virtual audio routing.
* **Pulsus Technologies & Texas Instruments:** For the **PS9850** digital processor and **TAS5352** power stage silicon that continue to deliver remarkable acoustic fidelity.

---

## 8.5. License

All firmware source code, desktop companion source code, test suites, and technical documentation within this repository are released under the:

**[MIT License](file:///e:/Project/HomeTheather/LICENSE)**  
Copyright (c) 2026 LG HT306 Audio Bridge Project Contributors.
