# Rencana Implementasi: Modern Desktop UI (Arsitektur JUCE 8 Webview)

## 1. Latar Belakang & Tujuan
Dear ImGui (C++ DirectX 11) yang saat ini berjalan membuktikan binary C++ native dapat berjalan sangat ringan (~12–15 MB RAM). Namun, antarmukanya kaku, tidak memiliki CSS layout engine, dan rentan terhadap masalah scrollbar / scaling saat DPI berubah.

Di sisi lain, antarmuka pada `pc_streamer/stitch_design_system_implementation/` (HTML5, Tailwind CSS, SVG Icons, Plus Jakarta Sans, JetBrains Mono) memiliki kualitas visual komersial yang rapi dan modern.

**Tujuan Implementasi:**
1. Mengadopsi arsitektur **JUCE 8 Webview** (C++ Native Desktop Window + Microsoft Edge WebView2 Embedded).
2. Mempertahankan seluruh **Core Audio & USB Engine C++** (`audio_capture.cpp` WASAPI loopback dan `usb_transport.cpp` WinUSB bulk).
3. Menghadirkan antarmuka 100% identik dengan Stitch Design System: font tajam (DirectWrite), animasi halus, tidak ada scrollbar aneh, dan responsif.
4. Memastikan alokasi RAM tetap sangat efisien: **target < 50 MB** (rata-rata WebView2 shared memory ~30–42 MB).

---

## 2. Tahapan Implementasi (Vertical Slices)

### Slice 1: Persiapan Aset Frontend & Struktur Folder di `pc_stream_app`
- Menyalin dan mengonsolidasikan berkas frontend dari `pc_streamer/stitch_design_system_implementation/` ke dalam `pc_stream_app/ui/`.
- Mengonfigurasi client JS agar siap menerima event telemetri dari C++ Native Bridge.
- Sub-agent: `flash` (Web/Asset Integrator).

### Slice 2: Implementasi C++ Native Host Window (WebView2 / JUCE 8 Pattern)
- Mengimplementasikan C++ Native Window Host di Windows yang meng-embed Microsoft Edge WebView2 kompatibel dengan MinGW GCC 16.2.0.
- Mengonfigurasi properti jendela (1280x840, dark mode frame, disabling unnecessary browser context menus).
- Menghubungkan WebView2 ke folder lokal `pc_stream_app/ui/`.
- Sub-agent: `inherit` (C++ Desktop Engineer).

### Slice 3: Implementasi Native IPC Bridge (Komunikasi C++ <-> JS Dua Arah)
- C++ ke JS: Streaming telemetri (6-channel VU meter levels 60fps, Link Budget, RP2350 USB connection state, FIFO depth, uptime, buffer underruns, logs).
- JS ke C++: Perintah kontrol pengguna (Master volume dB, Channel gain trims, Buffer latency profile, Lip-sync delay ms, Limiter toggle, Test tone triggers).
- Sub-agent: `inherit` (Systems & IPC Engineer).

### Slice 4: Integrasi Core Audio WASAPI & WinUSB Transport
- Menghubungkan `audio_capture.cpp` (WASAPI Loopback) dan `usb_transport.cpp` (WinUSB Bulk EP 0x03 OUT) ke host C++ baru.
- Menghubungkan thread streaming audio ke lifecycle window desktop.
- Sub-agent: `pro` (Audio & USB Pipeline Engineer).

### Slice 5: Verifikasi Menyeluruh, Benchmark RAM, & Dokumentasi
- Verifikasi ke-7 tab (Dashboard, Mixer, Routing, Profiles, Test, Log, Settings).
- Mengukur alokasi RAM pada kondisi idle dan streaming (target < 50 MB).
- Memperbarui build system `CMakeLists.txt` untuk target `ht306_desktop_app.exe`.
- Sub-agent: `flash` (QA & Verification).
