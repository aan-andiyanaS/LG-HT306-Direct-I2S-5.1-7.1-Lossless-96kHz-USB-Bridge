# ADR-003: Modernizing Desktop App Interface via C++ Native + WebView2 (JUCE 8 Webview Pattern)

## Status
**ACCEPTED** — November 3, 2026

## Context
The PC Companion `HT306 Audio Bridge` application previously existed in two forms:
1. `pc_streamer`: Python-based (FastAPI/ThreadingHTTPServer) serving a local web interface via Chrome/Edge app-mode on `localhost:8000`. Although visually modern (Stitch Design System), it risked audio glitches due to Python Garbage Collector (GC) pauses and required open networking ports.
2. `pc_stream_app` (Dear ImGui DirectX 11): Pure C++ binary with low RAM footprint (~12–15 MB), but rigid layout lacking CSS Flexbox/Grid, rasterized font rendering, and prone to unwanted scrollbars or text clipping under DPI scaling.

Requirements:
* **100% Modern & Commercial Quality UI**: Flat cards, Tailwind CSS layout, SVG vector icons, razor-sharp DirectWrite font rendering, zero unwanted scrollbars.
* **Ultra-Low Memory Footprint**: Strictly < 50 MB RAM.
* **Deterministic Real-Time Audio Performance**: WASAPI loopback capture and WinUSB bulk streaming running in native C++ without latency interruptions.

## Decision
We adopted the **C++ Native Desktop Window + Embedded Microsoft Edge WebView2** architecture (following the modern **JUCE 8 Webview** design pattern):
1. **Host Executable (`ht306_desktop_app.exe`)**:
   - Built in pure C++17 using Win32 API and the `webview.h` single-header library, integrating directly with Microsoft Edge WebView2 runtime native on Windows 10 & 11.
   - Enables Windows 11 Immersive Dark Mode (`DWMWA_USE_IMMERSIVE_DARK_MODE`).
2. **Frontend UI**:
   - Uses local static assets at `pc_stream_app/ui/index.html` (HTML5, Tailwind CSS, SVG, Plus Jakarta Sans, and JetBrains Mono).
   - All 7 tabs (Dashboard, Mixer, Routing, Profiles, Test, Log, Settings) are served locally from disk without network ports.
3. **High-Speed In-Memory Bidirectional IPC**:
   - **C++ to JS**: 60 Hz background telemetry thread pushing 6-channel peak VU levels, 96kHz 32-bit audio format, link budget, and hardware status via in-memory dispatch.
   - **JS to C++**: User control interactions (fader trims, master volume, mode switch, 5.1 pink noise sweep test, vbcable installer, bootsel reboot) transmitted via `window.chrome.webview.postMessage` / `nativePostCommand`.
4. **Core Audio & USB Pipeline**:
   - Directly binds `audio_capture.cpp` (WASAPI) and `usb_transport.cpp` (WinUSB Bulk EP 0x03 OUT) in C++ background worker threads.

## Consequences & Results
* **Measured RAM Allocation**: **~34.89 MB RAM** (well below the 50 MB threshold).
* **Standalone Executable Size**: **3.5 MB**.
* **Visual Quality**: 100% faithful to Stitch Design System, subpixel font sharpness, 60 FPS transitions, and zero unwanted scrollbars.
