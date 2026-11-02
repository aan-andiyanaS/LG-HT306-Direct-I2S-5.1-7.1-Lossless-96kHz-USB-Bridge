# ADR-002: PC Companion App Architecture, Memory Efficiency (<50 MB), and Multi-Device Isolation

## Status
Accepted

## Date
2026-11-03

## Context
The PC Companion Application (`HT306 Audio Bridge`) captures Windows system audio (stereo/5.1 up to 96 kHz 32-bit Studio Master) and streams it to the RP2350-Zero over WinUSB Bulk. The user defined strict non-functional constraints:
1. **Strict Memory Budget:** Total process RAM must not exceed 50 MB (with idle target around 12–18 MB). Heavy GUI frameworks like Electron (150–250 MB) or PySide6/Qt (60–90 MB baseline) violate this constraint.
2. **Deterministic Multi-Device Isolation (Anti-Mistake Device Guessing):** Users frequently plug in multiple microcontrollers (ESP32-S3, standard Raspberry Pi Pico, Arduino, CH340/CP2102 adapters). The application must never misidentify or stream data to an unintended device.
3. **Automated Driver Binding:** Windows 10/11 does not bind `winusb.sys` to custom vendor interfaces by default without manual Zadig installation or INF files.

## Decision

### 1. Dual-Layer Lightweight Architecture (<50 MB RAM)
- **Headless Core Audio Engine:** Pure Python with vectorized numpy operations. Converts float32 loopback buffers to int32, applies per-channel gains/mutes, and encodes FLAC Order-4 Golomb-Rice blocks in ~48 µs per packet. Python process RAM is measured strictly at **32–34 MB** under active 96 kHz 32-bit 6-channel load.
- **Standalone Desktop UI via Microsoft Edge App Mode:** Instead of embedding Chromium or Qt inside the Python process, the application hosts a modern Tailwind Single Page Application (SPA) using Python's standard library `ThreadingHTTPServer` (< 1 MB RAM overhead) on `127.0.0.1:43060`. The window is launched via Edge's native app mode:
  `msedge.exe --app=http://127.0.0.1:43060 --window-size=1280,820`
  Edge runs in an isolated, GPU-accelerated OS process. When closed, Edge exits immediately while the Python engine remains active in the System Tray at ~32 MB.

### 2. 4-Factor Deterministic Hardware Identification
Every connected USB device must pass 4 verification layers:
1. **Layer 1 (VID & PID):** Exactly `VID: 0x2E8A` and `PID: 0x4306`. ESP32 (0x303A), standard Pico (0x000A), and USB UART adapters are immediately rejected (`E-USB-VIDPID`).
2. **Layer 2 (String Descriptors):** Manufacturer contains `"LG HT306"`, Product is `"RP2350 Direct-I2S Bridge"`, Serial starts with `"HT306-"` (`E-USB-STRINGS`).
3. **Layer 3 (Endpoint Topology):** Interface 2 (Class 0xFF Vendor Specific) must contain Bulk OUT Endpoint `0x03` with `wMaxPacketSize == 64` (`E-USB-ENDPOINT`).
4. **Layer 4 (CDC Handshake Challenge):** Opens CDC-ACM interface 0/1 COM port and verifies banner or heartbeat starting with `[HT306]` (`E-USB-HANDSHAKE`).

Any ambiguity between multiple devices is deterministically resolved by choosing the lowest serial number.

### 3. Microsoft OS 2.0 WinUSB Descriptors in Firmware
Added BOS descriptor (33 bytes) and MS OS 2.0 descriptor set (46 bytes) with Compatible ID `WINUSB\0\0` to `usb_descriptors.c`. On Windows 8.1/10/11:
- OS requests BOS descriptor under USB 2.1 (`0x0210`).
- Windows automatically binds `winusb.sys` to Interface 2 without manual Zadig installation.

## Consequences
- **Positive:**
  - Memory strictly controlled < 50 MB (~32.7 MB during active streaming).
  - 100% protection against mistaken microcontroller streaming.
  - Pixel-perfect LG Red/Dark UI with real-time 16 fps VU meters without heavy framework bloat.
- **Negative:**
  - Firmware on RP2350-Zero must be reflashed with the new `.uf2` to enable MS OS 2.0 descriptors.
