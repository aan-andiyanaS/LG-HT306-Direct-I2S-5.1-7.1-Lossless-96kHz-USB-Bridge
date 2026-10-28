# 4. Firmware Build (`.uf2`), RP2350-Zero Flashing, Test Verification, & JRiver Setup

<p align="left">
  <strong>Language:</strong> <strong>English</strong> | <a href="04_Build_Flash_and_JRiver_Setup.id.md">Bahasa Indonesia</a>
</p>

[⬅️ Back to Main README](../README.md)

This document provides a step-by-step guide to compiling the firmware, flashing the resulting `.uf2` onto the **`Waveshare RP2350-Zero`**, running the automated mathematical verification test suite, and configuring **JRiver Media Center (`DSP Studio`)** for bit-perfect multichannel playback.

---

## 4.1. Firmware Compilation (`.uf2`)

### Prerequisites
* **Raspberry Pi Pico SDK** version **`2.0.0`** or higher (with native `RP2350` / `pico2` target support).
* **CMake** `>= 3.20`, **Ninja**, and the **ARM GNU Toolchain (`arm-none-eabi-gcc`)** — readily configured via the official **Raspberry Pi Pico** extension in Visual Studio Code.

### Compilation Commands
```powershell
# 1. Navigate to the firmware workspace
cd firmware

# 2. Generate build tree for pico2 target (Waveshare RP2350-Zero)
cmake -B build -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=Release

# 3. Build the binary firmware artifact ht306_rp2350_zero.uf2
cmake --build build --config Release -j
```

---

## 4.2. Flashing the `Waveshare RP2350-Zero`

1. Press and hold down the tiny **`BOOT`** button on the `Waveshare RP2350-Zero`.
2. Connect the board to your PC via a USB Type-C cable, then release the **`BOOT`** button.
3. A mass-storage drive named **`RP2350`** will mount in Windows Explorer.
4. Drag and drop (or copy) **`firmware/build/ht306_rp2350_zero.uf2`** directly into the **`RP2350`** drive.
5. The MCU will reboot automatically and be recognized by Windows as **`LG HT306 5.1 Surround Direct-I2S (RP2350-Zero)`** (`VID: 0x2E8A`, `PID: 0x4306`).

---

## 4.3. Running Automated End-to-End Verification

The complete audio pipeline (from *Single-Pass FLAC Order-4 + Escaped Golomb-Rice Codec*, *4-Lane PIO Bit-Interleaving*, *Falling-Edge Clocking*, to the *64-Byte USB Bulk Reassembler with Noise Guard*) can be verified instantly via Python:

```powershell
python tests/verify_end_to_end.py
```

### Reference Test Suite Output
```text
=== [TEST 1] Verifikasi Bit-Perfect Codec 96 kHz 24-bit 5.1 (RP2350-Zero) ===
  -> Bit-Exactness : LULUS 100% (0 bit error dari 115.200 sampel 24-bit)
  -> Data Mentah   : 1728.0 KB/detik
  -> Data USB Pico : 716.1 KB/detik (41.4% dari ukuran asli, Batas Aman USB = 1000 KB/dtk)
  -> Paket Maks    : 812 Byte / 0.5 ms

=== [TEST 2] Verifikasi Simulasi Hardware PIO 4-Lane I2S (GP4..GP7 -> PS9850) ===
  -> ADATA0 (GP4 Front)  : 0x12345600 == 0x12345600 [OK]
  -> ADATA1 (GP5 Rear)   : 0x789ABC00 == 0x789ABC00 [OK]
  -> ADATA2 (GP6 C/Sub)  : 0xDEF01200 == 0xDEF01200 [OK]
  -> ADATA3 (GP7 7.1)    : 0x34567800 == 0x34567800 [OK]

=== [TEST 3] Verifikasi Siklus Clock & Edge Timing i2s_multi.pio ===
  -> Semua 32 bit Kiri & 32 bit Kanan di-output tepat pada FALLING edge BCK [OK]
  -> Total Siklus per Frame Stereo = 128 siklus PIO (64 BCK @ 50% duty cycle) [OK]

=== [TEST 4] Verifikasi Reassembly Stream USB Bulk 64-Byte (ht306_usb_process_vendor_stream) ===
  -> 10 paket variabel berhasil disatukan kembali (termasuk auto-resync dari false sync) [OK]

[SEMUA VERIFIKASI & AUDIT LULUS 100%]
```

---

## 4.4. JRiver Media Center (`DSP Studio`) Configuration

To route discrete movie audio (**Dolby Digital AC-3, TrueHD, Atmos, DTS-HD MA**) and music accurately across the 6 physical HT306 speakers:

1. In **JRiver Media Center**, navigate to **`Tools`** $\rightarrow$ **`Options`** $\rightarrow$ **`Audio`**.
2. Under **`DSP & Output Format`**, open **`DSP Studio`**:
   * **Output Format:**
     * **Channels:** Select **`5.1`** (`6 Channels: FL, FR, C, LFE, SL, SR`).
     * **Sample Rate:**
       * **Automatic Matching:** Keep default to follow source content rate (`48,000 Hz` for AC-3, `96,000 Hz` for TrueHD/Hi-Res) — `RP2350-Zero` and `Pulsus PS9850` will adjust `LRCK` and `BCK` clocking automatically.
       * **96 kHz Upsampling:** Set target output to **`96,000 Hz`** if you prefer JRiver's 64-bit engine to upsample all audio streams consistently.
   * **Room Correction:**
     * Set speaker distance delays to align with your listening position, and enable *Bass Management* (crossover at `80 Hz` routed to Subwoofer/LFE).
3. On the **LG HT306 Unit**:
   * Select **`AUX`** mode using the stock remote control.
   * Choose a 5.1 sound preset such as **`BASS BLAST`**, **`NATURAL`**, or **`PLII MOVIE`** to keep all 6 amplifier stages unmuted in `IC602 (Pulsus PS9850)` with LG's custom 69-Band EQ tuning enabled.
