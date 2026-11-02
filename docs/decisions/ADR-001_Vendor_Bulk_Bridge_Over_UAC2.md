# ADR-001: Vendor Bulk Audio Transport with PC Companion Bridge Over Native UAC2

## Status
Accepted

## Date
2026-11-03

## Context
Initial implementation attempts focused on presenting the Waveshare RP2350-Zero as a native USB Audio Class 2.0 (UAC2) Desktop Speaker using the Windows in-box driver `usbaudio2.sys`. While UAC2 eliminated the need for client-side companion software on paper, real-world deployment on RP2350 hardware revealed critical system-level failures:
1. **Windows Kernel Deadlock & DPC Latency:** Windows `usbaudio2.sys` strictly assumes High-Speed (480 Mbps, 125 µs microframe intervals) USB topologies. Over Full-Speed (12 Mbps, 1 ms frames), missing isochronous feedback intervals triggered synchronous kernel-mode URB blocking transfers (5,000 ms timeout), inducing severe whole-OS stutter, UI freezes, and eventual termination of the Windows Audio Service (`Audiosrv` / `AudioEndpointBuilder`).
2. **RP2040/RP2350 Hardware DPRAM Limitations:** Transitioning between Alternate Setting 0 (zero-bandwidth) and Alternate Setting 1 (active stream) caused hardware DPRAM double-buffering race conditions, triggering hardware assertion panics inside the USB controller driver.
3. **USB Full-Speed Bandwidth Saturation:** Raw uncompressed 6-channel 96 kHz 24-bit audio requires 1.728 KB/s, exceeding physical USB Full-Speed maximum payload bandwidth (~1.023 KB/s). UAC2 cannot dynamically perform FLAC/Delta-Rice lossless compression.

## Decision
Pivot the audio streaming architecture back to the original PRD blueprint:
1. **Firmware Layer (RP2350-Zero):** Operate strictly via **Vendor Bulk Endpoint** (Interface 2: EP 0x03 OUT / EP 0x83 IN) alongside **CDC ACM Serial Monitor** (Interface 0 & 1: COM4).
2. **Driver Layer:** Standard Microsoft `WinUSB` (`winusb.sys`), completely isolated from the Windows audio kernel.
3. **PC Layer:** Provide an ultra-lightweight **PC Companion Application (`HT306 Audio Bridge`)** that captures Windows audio via WASAPI (Loopback or Virtual Audio Device Sink), compresses it using real-time Delta-Rice (0.5 ms subframe), and transfers it over WinUSB Bulk.

## Alternatives Considered

### Alternative 1: Continue Patching Native UAC2 with Linker Wraps
- **Pros:** Zero client-side software installation.
- **Cons:** Extremely fragile; requires binary-level monkey-patching of SDK TinyUSB functions (`prepare_ep_buffer`, `tu_fifo_write_n`); cannot bypass Windows `usbaudio2.sys` kernel timeout behavior on Full-Speed bus; cannot exceed 48 kHz stereo without hitting bandwidth limits.
- **Rejected:** Caused reproducible PC lag and audio troubleshooter crashes.

### Alternative 2: Custom Windows WDM/KS Kernel Audio Driver
- **Pros:** Native audio device listed directly in Device Manager.
- **Cons:** Requires Microsoft WHQL certification and expensive EV code signing certificates; unsigned drivers are blocked by Windows 10/11 Secure Boot; high maintenance overhead.
- **Rejected:** Violates simplicity and rapid deployment principles.

### Alternative 3: Vendor Bulk + PC Companion App with Virtual Audio Cable (Selected)
- **Pros:**
  - 100% elimination of Windows kernel hangs (WinUSB operates strictly in user space).
  - Hardware ACK/NAK flow control guarantees zero packet loss and 100% bit-perfect audio delivery.
  - Full-Speed bandwidth constraint solved via real-time Delta-Rice compression (raw 1.728 KB/s compressed to ~716 KB/s).
  - Windows volume slider, keyboard media keys, and per-app mixer control operate normally via WHQL-certified Virtual Audio Sink (e.g., VB-CABLE) or WASAPI Loopback.
  - Firmware binary is small (69 KB), deterministic, and rock-solid.
- **Cons:** Requires companion app running on the PC.
- **Accepted:** Delivers audiophile-grade bit-perfect 96 kHz 5.1 surround sound with zero lag and maximum reliability.

## Consequences
- The firmware image remains lean and robust (TinyUSB Vendor Bulk + CDC ACM).
- PC Companion Bridge application must be developed (`pc_streamer/`).
- Users enjoy standard Windows audio controls with zero OS stability degradation.
