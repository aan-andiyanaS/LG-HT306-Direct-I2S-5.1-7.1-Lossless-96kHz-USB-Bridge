---
name: Direct-I2S Precision Audio
colors:
  surface: '#1C1C1E'
  surface-dim: '#131313'
  surface-bright: '#393939'
  surface-container-lowest: '#0e0e0e'
  surface-container-low: '#1c1b1b'
  surface-container: '#201f1f'
  surface-container-high: '#2a2a2a'
  surface-container-highest: '#353534'
  on-surface: '#e5e2e1'
  on-surface-variant: '#e2bec0'
  inverse-surface: '#e5e2e1'
  inverse-on-surface: '#313030'
  outline: '#a9898b'
  outline-variant: '#5a4042'
  surface-tint: '#ffb2b8'
  primary: '#ffb2b8'
  on-primary: '#67001d'
  primary-container: '#a50034'
  on-primary-container: '#ffafb5'
  inverse-primary: '#b81840'
  secondary: '#a9c7ff'
  on-secondary: '#003063'
  secondary-container: '#0464c3'
  on-secondary-container: '#d9e4ff'
  tertiary: '#57df8d'
  on-tertiary: '#00391c'
  tertiary-container: '#005e32'
  on-tertiary-container: '#54dd8b'
  error: '#ffb4ab'
  on-error: '#690005'
  error-container: '#93000a'
  on-error-container: '#ffdad6'
  primary-fixed: '#ffdadb'
  primary-fixed-dim: '#ffb2b8'
  on-primary-fixed: '#40000f'
  on-primary-fixed-variant: '#91002d'
  secondary-fixed: '#d6e3ff'
  secondary-fixed-dim: '#a9c7ff'
  on-secondary-fixed: '#001b3d'
  on-secondary-fixed-variant: '#00468c'
  tertiary-fixed: '#75fca7'
  tertiary-fixed-dim: '#57df8d'
  on-tertiary-fixed: '#00210e'
  on-tertiary-fixed-variant: '#00522b'
  background: '#131313'
  on-background: '#e5e2e1'
  surface-variant: '#353534'
  lg-red-hover: '#C2174A'
  lg-red-pressed: '#7D0027'
  lg-red-tint: '#A5003426'
  surface-2: '#262628'
  border: '#38383A'
  text-primary: '#FFFFFF'
  text-secondary: '#B8B8BC'
  icon-gray: '#6B6B6B'
  status-ok: '#2FBF71'
  status-warn: '#F5A623'
  status-error: '#FF5A5F'
  status-info: '#5B9DFF'
typography:
  headline-lg:
    fontFamily: Plus Jakarta Sans
    fontSize: 28px
    fontWeight: '600'
    lineHeight: 34px
    letterSpacing: -0.02em
  headline-md:
    fontFamily: Plus Jakarta Sans
    fontSize: 20px
    fontWeight: '600'
    lineHeight: 26px
    letterSpacing: -0.01em
  body-lg:
    fontFamily: Plus Jakarta Sans
    fontSize: 16px
    fontWeight: '400'
    lineHeight: 24px
  body-sm:
    fontFamily: Plus Jakarta Sans
    fontSize: 14px
    fontWeight: '400'
    lineHeight: 20px
  caption:
    fontFamily: Plus Jakarta Sans
    fontSize: 12px
    fontWeight: '400'
    lineHeight: 16px
  code-lg:
    fontFamily: JetBrains Mono
    fontSize: 14px
    fontWeight: '500'
    lineHeight: 20px
  code-sm:
    fontFamily: JetBrains Mono
    fontSize: 12px
    fontWeight: '400'
    lineHeight: 16px
  meter-num:
    fontFamily: JetBrains Mono
    fontSize: 11px
    fontWeight: '600'
    lineHeight: 14px
    letterSpacing: 0.02em
rounded:
  sm: 0.25rem
  DEFAULT: 0.5rem
  md: 0.75rem
  lg: 1rem
  xl: 1.5rem
  full: 9999px
spacing:
  gutter: 1rem
  margin: 1.25rem
  space-xs: 0.25rem
  space-sm: 0.5rem
  space-md: 0.75rem
  space-lg: 1rem
  space-xl: 1.5rem
---

## Brand & Style

This design system delivers an audio-engineering desktop control interface inspired by high-end consumer hardware aesthetics. The visual tone bridges consumer electronics refinement with studio-grade telemetry: confident, immaculate, highly legible, and tactile.

The design movement combines **Modern High-Contrast Technical** with **Sleek Minimal Surface Tiering**. It rejects noisy skeuomorphic studio racks in favor of crisp dark surfaces, subtle hairline framing, pill-shaped control ergonomics, and surgical visual feedback. The user experience evokes low-latency precision, zero-jitter telemetry, and hardware fidelity while maintaining approachable, friendly micro-interactions.

## Colors

The palette operates around the brand magenta-crimson accent (`#A50034`), anchored against neutral dark layers. 

### Critical Semantic Disambiguation
Hardware errors and clipping alerts utilize `--status-error` (`#FF5A5F`), which is distinctly brighter and warmer than the deep brand magenta-red (`#A50034`). Brand interactions and active navigation items must never share identical color profiles with hardware clipping, buffer underruns, or link failures.

### Audio VU Meter Dynamic Scale
Audio signal monitoring transitions across strict decibel stages:
- **Nominal / Linear Range (−60 dBFS to −12 dBFS):** Solid `--status-ok` (`#2FBF71`).
- **Headroom Warning (−12 dBFS to −3 dBFS):** Stepped transition into `--status-warn` (`#F5A623`).
- **Peak / Inter-Sample Clip (> −3 dBFS):** Bright `--status-error` (`#FF5A5F`) with a 1.5s decay peak-hold line.

## Typography

Typography is split into two specialized roles:
1. **Interface & Interaction:** Sans-serif with rounded, confident geometry, optimized for quick scanning across desktop operating systems.
2. **Telemetry & Metering:** Strict tabular monospaced numerals. Used exclusively for real-time sample rates (e.g., `44.1 kHz`, `192 kHz`), buffer latencies (`1.2 ms`), packet drops, and VU decibel scales (`−6.2 dBFS`). Numerical readouts must maintain fixed character widths to eliminate layout jitter during high-frequency frame updates.

## Layout & Spacing

The layout is optimized for desktop utility dimensions (minimum window size: `760px` × `560px`).

### Spatial Hierarchy
- **Master-Detail Navigation:** A fixed left sidebar (width: `220px`) housing device selector rings, persistent bridge status, and routing anchors.
- **Main Viewport:** Fluid multi-card grid utilizing 16px (`1rem`) gutters and 20px (`1.25rem`) perimeter canvas margins.
- **Micro-Padding:** Internal card containers and metering segments rely on tight 8px (`0.5rem`) and 12px (`0.75rem`) increments to maintain high technical information density without visual crowding.

## Elevation & Depth

Visual depth avoids decorative drop shadows in favor of a **Layered Surface-Container Tonal Architecture**:
- **Canvas Base (`--bg` / #121212):** The host desktop window foundation.
- **Level 1 Containers (`--surface` / #1C1C1E):** Modular cards, panels, and routing matrices. Outlined with `1px solid var(--border)` (`#38383A`).
- **Level 2 Containers (`--surface-2` / #262628):** Inset controls, form inputs, inactive progress troughs, and hover fills.
- **Interactive Focus:** Focused inputs, selected cards, and active toggle states employ a 2px outer outline paired with `--lg-red-tint` (`#A5003426`) halo rings, producing precise hardware-like tactile activation without elevation shift.

## Shapes

The design system employs a hybrid geometric strategy:
- **Panels & Cards:** 12px (`rounded-lg`) corner radii for structured containment.
- **Interactive Controls:** Strict 999px (Pill geometry) applied to action buttons, selectable channel chips, and mode switch tabs.
- **Status Rings & Hardware Indicators:** Perfectly circular (50% radius) concentric rings depicting streaming locks, I2S clock sync, and hardware heartbeat pings.

## Components

### Buttons
- **Primary:** Pill-shaped (`border-radius: 999px`), solid `--primary_color_hex` (`#A50034`), white typography (`#FFFFFF`). Hover: `#C2174A`. Active: `#7D0027`. Focus: 2px ring with `#A5003426`.
- **Secondary / Outlined:** Pill-shaped, transparent background with `1px solid var(--border)`. Hover transitions to `var(--surface-2)`.

### Channel Chips & Selectors
- Compact pill-shaped selectors (`padding: 4px 12px`). Inactive state rests on `var(--surface-2)` with `text-secondary`. Active state switches to high-contrast white text over a subtly tinted brand background (`rgba(165,0,52,0.2)`) and a `1px solid var(--primary_color_hex)` perimeter.

### Diagnostic Cards & Audio Panels
- Background: `var(--surface)`. Border: `1px solid var(--border)`. Corner radius: `12px`.
- Headers feature section titles paired with small monospace device badges.

### Input Controls & Steppers
- Background: `var(--surface-2)`. Border: `1px solid var(--border)`. Corner radius: `8px`.
- Numerical entries (buffer sizes, sample frequencies) render in tabular monospace (`JetBrains Mono`).

### VU Meters & Signal Telemetry
- Segmented or smooth horizontal/vertical bars housed in `var(--surface-2)` troughs.
- Multi-color segmented stops mapped across:
  - Nominal: `#2FBF71`
  - Warning: `#F5A623`
  - Clip: `#FF5A5F`
- Numeric peak readouts in monospaced captions with fixed tabular spacing.

### Status Indicator Ring
- A 12px concentric status ring:
  - Inner solid dot (6px) surrounded by a 2px stroke halo.
  - Streaming Active: `#2FBF71` with a subtle 1500ms breathe pulse.
  - Clock Sync Degraded: `#F5A623`.
  - Disconnected: `#6B6B6B`.
  - Buffer Stall / Error: `#FF5A5F`.