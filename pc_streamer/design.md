# DESIGN.md — LG HT306 Direct-I2S Audio Bridge (Desktop GUI)

> Version 3.0 — Design document for a Windows desktop application that bridges PC audio (stereo or 5.1, up to 96 kHz / 32-bit) to the LG HT306 mainboard through an RP2350-Zero acting as an I2S transmitter.

**Branding note:** This is an independent, unofficial utility. The palette follows LG's visual character, but the app must not ship the LG logo or wordmark as its own branding. "for LG HT306" is used only to describe compatibility. Verify the final colors against the official LG brand guidelines before public release.

---

## 1. Overview

**Goal:** Capture Windows system audio (2.0 or 5.1), process it, and stream it over USB to an RP2350-Zero, which outputs I2S directly into the LG HT306 mainboard.

**Design principles**
1. **Lightweight:** idle CPU < 1%, RAM < 120 MB, cold start < 2 s.
2. **Invisible in use:** lives in the System Tray while you watch or play.
3. **Transparent:** hardware, stream, and error states are always visible.
4. **Hard to misconfigure:** an Auto Format mode picks the best format the link can sustain; a wizard and a 5.1 sweep catch swapped I2S pins.
5. **One core, two front ends:** GUI and CLI share the same engine.

**Audience:** LG HT306 owners who want a PC audio path without optical or HDMI.

---

## 2. Scope

### 2.1 Required Features (MVP)
| # | Feature | Notes |
|---|---------|-------|
| F1 | Hardware detection and status | RP2350-Zero connected/disconnected, VID/PID, COM port, uptime, heartbeat |
| F2 | System audio capture | WASAPI loopback, device picker, follows the default device |
| F3 | Multi-format streaming | **2.0 and 5.1**, 44.1 / 48 / 88.2 / **96 kHz**, 16 / 24 / **32-bit** |
| F4 | **Auto Format** | Follow Windows, Best Possible, or Adaptive (see §5) |
| F5 | Live telemetry | Sample rate, bit depth, channels, bitrate, latency, buffer fill |
| F6 | 6-channel VU meter | FL, FR, C, LFE, SL, SR with peak hold and dB readout |
| F7 | 5.1 Sweep test | FL → C → FR → SR → SL → LFE |
| F8 | Operating modes | Desktop (OS volume) and Bit-Perfect |
| F9 | System Tray | Minimize to tray, double-click toggle, right-click menu |
| F10 | CLI mode (`--cli`) | Headless use, benchmarking, auto-startup |

### 2.2 Enhancements over v1
| # | Feature | Benefit |
|---|---------|---------|
| N1 | Auto-reconnect | Unplug and replug the cable, the stream resumes |
| N2 | First-run wizard | Checks Windows format, hardware, and speaker wiring |
| N3 | Channel mapping and swap | Fix swapped wires without resoldering |
| N4 | Per-channel mixer | Trim −12 to +12 dB, mute, solo |
| N5 | Downmix and upmix | Stereo → 5.1, 7.1 → 5.1 |
| N6 | Lip-sync delay | 0–200 ms |
| N7 | Profiles | Presets (Movie, Music, Game, Bit-Perfect) with per-app auto-switch |
| N8 | Diagnostics | Log viewer, underrun/overrun counters, exportable report |
| N9 | Start with Windows | Optional, can start minimized to tray |
| N10 | Global hotkeys | Mute, switch mode, run test |
| N11 | Toast notifications | Connect, disconnect, important errors |
| N12 | Themes | Dark (default), Light, follow system, high contrast |
| N13 | Firmware update | Version check and UF2 flashing from the GUI |
| N14 | Languages | English and Indonesian |

### 2.3 Out of Scope (v1)
- macOS and Linux (architecture leaves room for them later).
- Dolby/DTS decoding (the app forwards the PCM Windows or the player produces).
- LG HT306 remote control (IR/CEC).

---

## 3. System Architecture

```text
┌──────────────────────── Windows PC ─────────────────────────┐
│                                                             │
│  Apps (player/game) → Windows Audio Engine (2.0 / 5.1)      │
│                                 │                           │
│                         WASAPI Loopback                     │
│                                 ▼                           │
│  ┌──────────────────────── Bridge App ───────────────────┐  │
│  │ Capture → Ring Buffer → Format Manager → DSP →        │  │
│  │              │            (Auto Format)    Packetizer │  │
│  │          Level Meter ──► GUI / Tray            │       │  │
│  │                                           USB Transport│  │
│  └────────────────────────────────────────────────┬──────┘  │
└───────────────────────────────────────────────────┼─────────┘
                                                    │ USB
                                    ┌───────────────▼───────────┐
                                    │ RP2350-Zero               │
                                    │ USB RX → FIFO → PIO I2S   │
                                    └───────────────┬───────────┘
                                                    │ BCLK / LRCK / DATA×3 (/MCLK)
                                    ┌───────────────▼───────────┐
                                    │ LG HT306 mainboard (DAC)  │
                                    └───────────────────────────┘
```

### 3.1 Software Layers
| Layer | Responsibility | Module |
|-------|----------------|--------|
| **Core Engine** | Capture, format management, DSP, transport, state machine | `core/` |
| **Device Layer** | USB/serial detection, RP2350 protocol, firmware update | `device/` |
| **Service Layer** | Config, profiles, logging, updater, hotkeys | `services/` |
| **UI Layer** | Window, tray, widgets, themes | `ui/` |
| **CLI** | Argument parsing, text/JSON output | `cli/` |

Rule: **the UI never holds audio logic.** It subscribes to Core events and sends commands.

### 3.2 Threading Model
| Thread | Priority | Job |
|--------|----------|-----|
| Audio Capture | High (MMCSS "Pro Audio") | Read WASAPI buffers into the ring buffer |
| Transport TX | High | Pull packets from the ring buffer and send over USB |
| Control | Normal | Heartbeat, serial commands, telemetry, format negotiation |
| Meter | Normal | Peak/RMS at 30 Hz |
| UI (main) | Normal | Rendering and events |

The audio path uses a lock-free SPSC ring buffer. Control and telemetry use a typed event bus. No allocation or logging happens in the critical audio path.

### 3.3 Technology
| Option | Stack | Notes |
|--------|-------|-------|
| **A (recommended)** | Python 3.12 + PySide6 (Qt) + `pyaudiowpatch` + `pyserial`/`pyusb` + `numpy`, with the hot path (capture → packet) in a native extension | Fast to build, mature UI |
| **B (minimum footprint)** | Rust + `egui` or Tauri + `wasapi` + `serialport` | Smaller binary, steadier latency |

---

## 4. Audio Formats and Link Budget

### 4.1 Supported Formats
| Parameter | Values |
|-----------|--------|
| Channels | **2.0 (stereo)**, **5.1 (6 ch)** |
| Sample rate | 44.1, 48, 88.2, **96 kHz** |
| Bit depth (container) | 16, 24, **32-bit** |
| Maximum | **96 kHz / 32-bit / 6 ch** and **96 kHz / 32-bit / 2 ch** |

Both **96 kHz stereo** and **96 kHz 5.1** are first-class formats, not special cases.

### 4.2 Raw Bandwidth
| Format | Raw rate |
|--------|----------|
| 2 ch · 44.1 kHz · 16-bit | 176.4 KB/s |
| 2 ch · 48 kHz · 24-bit | 288 KB/s |
| 2 ch · **96 kHz · 24-bit** | 576 KB/s |
| 2 ch · **96 kHz · 32-bit** | 768 KB/s |
| 6 ch · 48 kHz · 24-bit | 864 KB/s |
| 6 ch · 96 kHz · 24-bit | 1,728 KB/s |
| 6 ch · **96 kHz · 32-bit** | **2,304 KB/s** |

### 4.3 Link Budget (important)
The RP2350 has a **USB 1.1 Full-Speed** controller only (12 Mbps). Sustained bulk throughput is roughly **1.0–1.1 MB/s** (to be measured on the prototype). Consequences:

| Format | Fits uncompressed? | Needs |
|--------|--------------------|-------|
| All 2.0 formats up to 96 kHz / 32-bit | ✅ Yes (≤ 768 KB/s) | Nothing |
| 5.1 up to 48 kHz / 24-bit | ✅ Yes (864 KB/s, tight) | Nothing |
| 5.1 at 96 kHz / 24-bit | ❌ No | ≥ ~42% lossless savings |
| 5.1 at 96 kHz / 32-bit | ❌ No | ≥ ~57% savings, or Smart 32-bit (below) |

**Smart 32-bit transport.** Windows shared mode delivers float32, which carries about 24 significant bits. The app inspects the low byte of each frame block:
- If the audio uses ≤ 24 significant bits, send **24-bit payload plus a "valid bits = 24" flag**. The firmware pads to a 32-bit I2S slot. The DAC sees a normal 32-bit stream and 25% of the bandwidth is saved at zero quality cost.
- If the source truly uses > 24 bits (rare, exclusive mode with 32-bit content), send full 32-bit.

**Lossless compression** (delta + entropy coding, per packet) covers 5.1 at 96 kHz. Its ratio varies with content, so the Format Manager monitors link health and downgrades gracefully (§5.3).

> If sustained 5.1 / 96 kHz / 32-bit proves unreliable on Full-Speed USB, the fallback is a different transport (for example a USB High-Speed MCU or a PIO-USB/Ethernet bridge). Decide after the Phase 0 throughput test.

### 4.4 Hardware Caveats to Verify
- Whether the HT306 DAC/DSP accepts 96 kHz and 32-bit I2S slots, and whether it needs an MCLK (typically 256×fs = 24.576 MHz at 96 kHz).
- I2S line rates: at 96 kHz with 32-bit slots, BCLK = 6.144 MHz per data line (64 × fs). Achievable with RP2350 PIO.
- True end-to-end resolution is limited by the source and the DAC; a 32-bit container does not imply 32-bit resolution.

---

## 5. Auto Format

### 5.1 Format Modes
| Mode | Behavior |
|------|----------|
| **Auto: Follow Windows** *(default)* | Mirrors the format of the capture device (rate, depth, channels). Re-negotiates automatically when Windows changes it. |
| **Auto: Best Possible** | Chooses the highest format that both the link and the sink (HT306) sustain, up to 96 kHz / 32-bit / 6 ch. |
| **Auto: Adaptive** | Follows the incoming stream's rate and channel count per source, and switches as they change (for example 44.1 kHz music → 48 kHz movie). |
| **Manual** | You fix sample rate, bit depth, and channels. |

### 5.2 Stereo vs 5.1 Handling
| Source | Output option |
|--------|---------------|
| Stereo (2.0) | **Direct 2.0** (FL/FR only, other channels silent), **Upmix to 5.1**, or Dual-mono to front pair |
| 5.1 | Direct 6 ch |
| 7.1 | Downmix to 5.1 |

Stereo at 96 kHz / 32-bit always runs uncompressed.

### 5.3 Degradation Ladder
When underruns or link saturation appear, the Format Manager steps down and tells the user (toast and Dashboard banner):

```text
5.1 · 96 kHz · 32-bit
  → 5.1 · 96 kHz · 24-bit (Smart 32-bit / compression)
  → 5.1 · 48 kHz · 24-bit (uncompressed)
  → 2.0 · 96 kHz · 32-bit  (only if the user allowed channel reduction)
```
Rules:
- Hysteresis: after a downgrade, wait 30 s of clean stream before trying to upgrade again.
- In Manual mode, the app never changes format; it only warns.
- Sample-rate conversion is never silent: if rates must change, the Dashboard shows "Resampled 96 → 48 kHz".

### 5.4 Negotiation Sequence
1. Core reads the Windows capture format.
2. Format Manager asks the firmware for capabilities (`CAPS`).
3. It picks the target format by mode, then sends `FORMAT` with rate, container bits, valid bits, and channels.
4. Firmware reconfigures the PIO I2S clock and replies `OK` or `NAK(reason)`.
5. Stream starts. Interruption target for a format switch: < 200 ms, with a muted ramp to avoid pops.

---

## 6. Audio Pipeline

1. **Capture:** WASAPI loopback (shared). Bit-Perfect mode uses exclusive mode when available.
2. **Normalize:** convert to internal float32 planar.
3. **DSP** (fixed order): channel mapping → downmix/upmix → per-channel gain and mute/solo → lip-sync delay → soft limiter.
4. **Meter tap:** peak and RMS before transmission.
5. **Packetizer:** pack to the negotiated depth (Smart 32-bit applied here), frame, sequence number, CRC, optional lossless compression.
6. **USB TX.**

### 6.1 Operating Modes
| Mode | Behavior |
|------|----------|
| **Desktop** | Follows Windows volume; DSP active |
| **Bit-Perfect** | DSP off except mapping, no resample or limiter; warns if OS volume ≠ 100% |
| **Movie** *(profile)* | Center boost, optional light dynamic range compression |
| **Music** *(profile)* | Downmix bypass, neutral trims |

### 6.2 Performance Targets
| Metric | Target |
|--------|--------|
| End-to-end latency | ≤ 5 ms (goal 3.8 ms) at 48 kHz; ≤ 5 ms at 96 kHz |
| Jitter | < 0.5 ms |
| Underruns per hour | 0 under normal use |
| CPU (stream active, 96 kHz / 6 ch) | < 5% |
| Compression savings (5.1, 96 kHz / 24-bit) | ≥ 42% required, ~58% goal |

---

## 7. Hardware Protocol (PC ↔ RP2350-Zero)

### 7.1 Identification
- VID `0x2E8A`; PID set in firmware (example `0x4306`, **confirm the final value**).
- Detection via `WM_DEVICECHANGE` plus a 2 s polling fallback.
- COM port is discovered by VID/PID, never hardcoded.

### 7.2 Two Channels
| Channel | Purpose |
|---------|---------|
| **Data** (bulk endpoint) | Audio stream |
| **Control** (CDC serial) | Commands, status, heartbeat, firmware version |

### 7.3 Audio Packet
```text
| SYNC(2) | SEQ(2) | FLAGS(1) | RATE(1) | CONT(1) | VALID(1) | CH(1) | LEN(2) | PAYLOAD(n) | CRC16(2) |
```
- `RATE`: enum 44.1 / 48 / 88.2 / 96 kHz.
- `CONT`: container bits (16 / 24 / 32). `VALID`: significant bits (≤ `CONT`).
- `CH`: 2 or 6.
- `FLAGS`: bit0 compressed, bit1 mute, bit2 end of stream, bit3 bit-perfect.
- `SEQ` lets the firmware detect lost packets and report them.

### 7.4 Control Commands
| Command | Purpose |
|---------|---------|
| `HELLO` | Handshake, returns firmware version |
| `CAPS` | Returns supported rates, depths, channel counts, compression support |
| `FORMAT <rate> <cont> <valid> <ch>` | Set stream format |
| `PING` / `PONG` | Heartbeat every 1 s |
| `STAT` | Uptime, buffer fill, underrun count, lost packets |
| `MUTE <0\|1>` | Hardware mute |
| `TEST <ch>` | Test pulse on a channel |
| `BOOTSEL` | Enter UF2 mode for firmware update |

Heartbeat timeout of 3 s moves the state to **Disconnected** and starts auto-reconnect.

---

## 8. Connection State Machine

```text
        ┌──────────┐ device found ┌────────────┐ handshake ok ┌───────┐
  ────► │ DISCONN. │ ───────────► │ CONNECTING │ ───────────► │ READY │
        └────▲─────┘              └─────┬──────┘              └───┬───┘
             │                          │ fail (backoff)           │ start
             │ cable lost / timeout     ▼                          ▼
             └───────────────────  ┌────────┐ ◄──── stop ──── ┌──────────┐
                                   │ ERROR  │                 │ STREAMING│
                                   └────────┘ ──recover────►  └──────────┘
```
Retry backoff: 0.5 s → 1 s → 2 s → 5 s (max), unlimited retries while the app runs.

---

## 9. Visual Design (LG-Inspired)

### 9.1 Brand Character
LG's look is clean, friendly, and confident: a signature red-magenta, calm neutral grays, generous white space, and rounded, circular forms (echoing the circular logo face). The UI uses that character without copying the logo.

### 9.2 Color Tokens

**Brand**
| Token | Hex | Use |
|-------|-----|-----|
| `--lg-red` | `#A50034` | Primary accent, primary buttons, active nav, tray streaming ring |
| `--lg-red-hover` | `#C2174A` | Hover state |
| `--lg-red-pressed` | `#7D0027` | Pressed state |
| `--lg-red-tint` | `#A5003426` (15% alpha) | Selected row, focus halo |
| `--lg-gray` | `#6B6B6B` | Secondary text on light, icons |

**Dark theme (default)**
| Token | Hex | Use |
|-------|-----|-----|
| `--bg` | `#121212` | Window background |
| `--surface` | `#1C1C1E` | Cards and panels |
| `--surface-2` | `#262628` | Inputs, hover surfaces |
| `--border` | `#38383A` | Dividers |
| `--text` | `#FFFFFF` | Primary text |
| `--text-2` | `#B8B8BC` | Secondary text |

**Light theme**
| Token | Hex | Use |
|-------|-----|-----|
| `--bg` | `#F4F4F4` | Window background |
| `--surface` | `#FFFFFF` | Cards |
| `--surface-2` | `#EDEDED` | Inputs |
| `--border` | `#DADADA` | Dividers |
| `--text` | `#1A1A1A` | Primary text |
| `--text-2` | `#6B6B6B` | Secondary text |

**Status (kept distinct from brand red so errors never look like "active")**
| Token | Hex | Use |
|-------|-----|-----|
| `--ok` | `#2FBF71` | Connected, healthy |
| `--warn` | `#F5A623` | Degraded format, warnings |
| `--error` | `#FF5A5F` | Errors, clipping (always paired with an icon and text) |
| `--info` | `#5B9DFF` | Neutral information |

**VU meter gradient:** green `#2FBF71` (below −12 dB) → amber `#F5A623` (−12 to −3 dB) → `--error` (above −3 dB). The meter's channel labels and peak caps use `--lg-red`.

Contrast: all text and control pairs meet WCAG AA (4.5:1 for text). White on `#A50034` is about 9:1.

### 9.3 Typography
- UI: **LG Smart** if licensed or installed, otherwise Segoe UI Variable.
- Numbers and logs: Cascadia Mono.
- Scale: 12 / 14 / 16 / 20 / 28 px; semibold for section titles.

### 9.4 Shape and Motion
- Corner radius 12 px for cards, 999 px (pill) for buttons and chips, circles for status indicators.
- Status indicator is a **ring**: gray (disconnected), blue (ready), green (streaming, soft pulse), amber (degraded), red-ring (error).
- Motion ≤ 150 ms ease-out; respects "reduce motion".
- Minimum window 760×560; supports 100–200% DPI.

### 9.5 Main Layout
```text
┌─────────────────────────────────────────────────────────────────────────┐
│ ◯ HT306 Audio Bridge     ● Connected    Profile: Movie ▾    ─  □  ✕      │
├────────────┬────────────────────────────────────────────────────────────┤
│ ◉ Dashboard│  HARDWARE                                                  │
│ ○ Mixer    │  ● RP2350-Zero  fw 1.3.0 │ COM4 │ Uptime 02:14:09 │ ♥ OK    │
│ ○ Routing  │ ────────────────────────────────────────────────────────── │
│ ○ Profiles │  FORMAT                                                    │
│ ○ Test     │  Mode: [ Auto: Follow Windows ▾ ]                          │
│ ○ Log      │  Now:  96 kHz · 32-bit (24 valid) · 5.1 · Compressed 58%   │
│ ○ Settings │  Bitrate 716 KB/s │ Latency 3.8 ms │ Buffer ██████░░ 62%   │
│            │ ────────────────────────────────────────────────────────── │
│            │  LIVE VU METER                                             │
│            │  FL  ████████████████░░░░  −6 dB  ▏peak −3                 │
│            │  FR  ████████████████░░░░  −6 dB                           │
│            │  C   ████████████████████   0 dB                           │
│            │  LFE ██████████████░░░░░░  −8 dB                           │
│            │  SL  ██████████░░░░░░░░░░ −14 dB                           │
│            │  SR  ██████████░░░░░░░░░░ −14 dB                           │
│            │ ────────────────────────────────────────────────────────── │
│            │  [ ▶ Start/Stop ]  [ 🔊 5.1 Test ]  [ 🔇 Mute ]  [ ⬇ Tray ] │
└────────────┴────────────────────────────────────────────────────────────┘
```
Primary actions use the filled `--lg-red` pill; secondary actions use an outlined pill.

### 9.6 Pages
- **Dashboard:** hardware status, format card (mode, current format, valid bits, compression ratio), telemetry, VU meter, quick controls.
- **Format** (section on the Dashboard and in Settings): mode selector (Follow Windows, Best Possible, Adaptive, Manual); manual pickers for sample rate (44.1 / 48 / 88.2 / 96 kHz), bit depth (16 / 24 / 32), channels (2.0 / 5.1); stereo handling (Direct 2.0 / Upmix to 5.1); "Allow quality downgrade" toggle; read-only link-budget bar showing used vs available bandwidth.
- **Mixer:** per-channel trim, mute/solo, master volume (Desktop mode), lip-sync delay, limiter toggle.
- **Routing:** 6×6 channel map, reset, one-click "Swap SL↔SR", downmix/upmix.
- **Profiles:** presets, per-app auto-switch rules (for example `vlc.exe` → Movie), import/export JSON.
- **Test:** 5.1 sweep, per-channel tone, pink noise, test level (default −20 dBFS), and a "did every speaker play in the right place?" checklist that jumps to Routing on "No".
- **Log:** live log with level filter, copy, export diagnostic ZIP.
- **Settings:** autostart, start minimized, theme, language, hotkeys, notifications, firmware update, buffer size (Auto / Low / Safe).

### 9.7 VU Meter Spec
- 30 FPS, scale −60 to 0 dBFS.
- Peak hold 1.5 s; instant attack, 300 ms release.
- Clip indicator stays lit until clicked.
- Idle state: dimmed bars with "Waiting for audio…".
- Stereo sources show only FL/FR active; the other four bars stay dimmed.
- Rendering uses dirty rectangles and stops entirely while the window is hidden.

### 9.8 System Tray
| Action | Result |
|--------|--------|
| Double-click | Show/hide window |
| Left-click | Mini popup: status, mini VU, current format, mute |
| Right-click | Open, Mute, Mode, Format mode ▸, Profile ▸, 5.1 Test, Start/Stop, Exit |
| Hover | Tooltip: status, format, latency |

Tray icon is a monochrome circular glyph with a status ring: gray, blue, green (pulsing), amber, red. Closing with ✕ minimizes to tray (configurable).

### 9.9 First-Run Wizard
1. Welcome and language.
2. Detect the RP2350-Zero (help screen if missing).
3. Check the Windows default format and offer to set 5.1 / 96 kHz / 24-bit or 32-bit.
4. Choose a format mode (default Auto: Follow Windows).
5. Run the 5.1 sweep with per-speaker confirmation.
6. Pick the starting mode and autostart.

### 9.10 Accessibility
Full keyboard navigation, logical focus order, AA contrast, status never conveyed by color alone, UI Automation labels, reduce-motion option.

---

## 10. CLI Mode

```text
bridge --cli [options]

  --device <name|id>        Audio source (default: default loopback)
  --mode desktop|bitperfect
  --format auto-windows|auto-best|auto-adaptive|manual
  --rate 44100|48000|88200|96000
  --bits 16|24|32
  --channels 2|6
  --stereo direct|upmix     How to treat 2.0 sources
  --profile <name>
  --test sweep|<channel>
  --monitor                 ANSI text VU meter
  --json                    Telemetry as JSON each second
  --benchmark <seconds>     Measure latency, jitter, underruns, link usage
  --list-devices
  --no-reconnect
  --log-level debug|info|warn|error
```
Exit codes: `0` ok, `1` bad args, `2` hardware not found, `3` audio error, `4` transport error, `5` format not supported.

Example (Task Scheduler): `bridge --cli --format auto-windows --profile Movie`.

---

## 11. Configuration

Location: `%APPDATA%\HT306Bridge\`
```text
config.json
profiles/*.json
logs/bridge-YYYYMMDD.log      # 7-day rotation, 10 MB max each
```

Example:
```json
{
  "language": "en",
  "theme": "dark",
  "start_with_windows": true,
  "start_minimized": true,
  "close_to_tray": true,
  "audio": {
    "source": "default_loopback",
    "mode": "desktop",
    "buffer": "auto",
    "format": {
      "mode": "auto-windows",
      "rate": 96000,
      "bits": 32,
      "channels": 6,
      "stereo": "direct",
      "allow_downgrade": true
    }
  },
  "hotkeys": { "mute": "Ctrl+Alt+M", "toggle_mode": "Ctrl+Alt+B", "test": "Ctrl+Alt+T" }
}
```
Writes are atomic (temp file then rename), validated against a schema, with a `.bak` fallback.

---

## 12. Error Handling

| Condition | Response |
|-----------|----------|
| USB cable unplugged | Red ring, toast, stream paused, auto-reconnect, resume |
| Default audio device changes | Re-bind without restart |
| Windows format changes | Re-negotiate via `FORMAT`, < 200 ms gap, muted ramp |
| Buffer underrun | Count and log; in Auto mode raise buffer, then step down the ladder if repeated |
| Link saturated at chosen format | Amber banner "Reduced to 96 kHz / 24-bit" with a link to Format settings |
| Sink rejects format (`NAK`) | Fall back to the next supported format and explain why |
| COM port held by another app | Show the offending process when detectable |
| Incompatible firmware | Offer firmware update |
| Windows not set to 5.1 | Banner plus "Fix" button that opens Sound settings |
| Second instance launched | Focus the first instance and exit |

Every error carries a code (for example `E-USB-02`) and links to the Log page.

---

## 13. Security and Privacy
- No outbound telemetry; all data stays local.
- Update checks only when enabled, over HTTPS with checksum or signature verification.
- Runs without admin rights (elevation only for optional driver steps).
- Packet length and CRC validated on both host and firmware to prevent overflows.

---

## 14. Project Structure

```text
ht306-bridge/
├── core/            # capture, format manager, dsp, ringbuffer, state machine
├── device/          # usb, serial, protocol, firmware update
├── services/        # config, profiles, logging, updater, hotkeys
├── ui/
│   ├── pages/       # dashboard, mixer, routing, profiles, test, log, settings
│   ├── widgets/     # vu_meter, status_ring, channel_strip, link_budget_bar
│   ├── tray.py
│   └── theme/       # tokens.json, dark.qss, light.qss
├── cli/
├── i18n/            # en.json, id.json
├── firmware/        # RP2350 source (PIO I2S + USB)
├── tests/
├── packaging/
└── design.md
```

---

## 15. Testing

| Level | Coverage |
|-------|----------|
| Unit | DSP, Smart 32-bit detection, packetizer/CRC, compression round-trip, format negotiation, state machine |
| Integration | Capture → transport with a **mock device** |
| Hardware-in-the-loop | Real RP2350-Zero, logic analyzer on I2S, measure latency, jitter, bit accuracy |
| Format matrix | Every combination of {2, 6 ch} × {44.1, 48, 88.2, 96 kHz} × {16, 24, 32-bit}, including live switching |
| Soak | 24 h at 96 kHz / 32-bit / 6 ch, count underruns and memory growth |
| UI | pytest-qt, visual snapshots at 100/150/200% DPI, both themes |
| Compatibility | Windows 10 22H2 and 11; VLC, MPC-HC, browsers, games |

Release criteria: 24 h soak with zero underruns, p95 latency ≤ 5 ms, 100 plug/unplug cycles without a crash, bit-exact output in Bit-Perfect mode.

---

## 16. Distribution
- Installer (Inno Setup) and portable ZIP.
- Multi-size tray icons (16/20/24/32 px, high-DPI).
- Code signing to avoid SmartScreen warnings.
- Firmware `.uf2` bundled; version checked at `HELLO`.

---

## 17. Roadmap

| Phase | Content | Outcome |
|-------|---------|---------|
| **0 Prototype** | Capture + send, RP2350 → I2S, **USB throughput test**, HT306 96 kHz / 32-bit acceptance test | Go/no-go on 5.1 · 96 kHz · 32-bit over USB Full-Speed |
| **1 MVP** | Dashboard, VU meter, sweep test, tray, auto-reconnect, manual formats | F1–F10 minus Auto |
| **2 Auto Format** | Format Manager, Smart 32-bit, compression, degradation ladder | F4, link-budget UI |
| **3 Control** | Mixer, routing, delay, profiles, hotkeys | N3–N7, N10 |
| **4 Polish** | Wizard, diagnostics, themes, i18n, firmware update | N2, N8, N11–N14 |
| **5 Release** | Soak tests, installer, docs | v1.0 |

---

## 18. Open Questions
1. If Phase 0 shows Full-Speed USB cannot sustain 5.1 · 96 kHz, do we use compression only, or change the transport hardware?
2. USB CDC/bulk custom protocol or USB Audio Class 2.0 (driverless, but harder for 6 ch / 96 kHz on RP2350)?
3. Does the HT306 I2S input need an MCLK, and which slot width and justification does it expect?
4. Python/PySide6 or Rust for the final stack (decide after jitter tests)?
5. Is firmware developed in this repository?
6. Are 7.1 → 5.1 downmix and stereo → 5.1 upmix part of v1.0 or v1.1?
