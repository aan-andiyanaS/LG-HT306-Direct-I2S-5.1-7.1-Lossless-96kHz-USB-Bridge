# 2. Mathematical Specification & Protocol of the Real-Time Lossless Codec (`96 kHz / 24-bit 5.1`)

<p align="left">
  <strong>Language:</strong> <strong>English</strong> | <a href="02_Lossless_96kHz_Codec_Specification.id.md">Bahasa Indonesia</a>
</p>

[⬅️ Back to Main README](../README.md)

This document provides the mathematical foundation and packet layout defined in [`firmware/include/lossless_codec.h`](../firmware/include/lossless_codec.h), [`firmware/src/lossless_codec.c`](../firmware/src/lossless_codec.c), and [`firmware/src/usb_descriptors.c`](../firmware/src/usb_descriptors.c).

---

## 2.1. Why Real-Time Lossless Compression is Required

The native USB hardware on the `RP2350` / `RP2040` family is compliant with **USB 1.1 Full-Speed (`12 Mbps`)**, giving a practical throughput ceiling of roughly **`1,000 KB/s`**:
* **`48 kHz / 24-bit 5.1` (Raw):** `48,000 × 6 × 3 Bytes = 864 KB/s` *(Fits comfortably inside USB 1.1 bandwidth)*.
* **`96 kHz / 24-bit 5.1` (Raw):** `96,000 × 6 × 3 Bytes = 1,728 KB/s` *(Exceeds USB 1.1 bandwidth by 72%)*.

Using our **Single-Pass FLAC Order-4 Linear Predictor + Escaped Golomb-Rice Codec**, the uncompressed `1,728.0 KB/s` stream is reduced to an average of **`716.1 KB/s` (`41.4%`)** with **100% mathematical bit-exactness (`0 bit error`)** and an ultra-fast unpacking execution time of `< 0.03 ms` on Cortex-M33 `Core 1`.

---

## 2.2. Micro-Frame Architecture: `0.5 ms` (`48 Samples / Channel`)

Every USB packet packages exactly **48 time frames** (`0.5 milliseconds` at `96 kHz`, or `1.0 millisecond` at `48 kHz`), totaling `288 samples` (`6 channels × 48`).
* **Sample 0 (`Anchor Frame`):** Transmitted completely raw without compression (`6 channels × 3 bytes = 18 bytes` at Offsets `14..31`).
* This guarantees that every packet is **100% stateless across packets** — if a packet is lost due to USB cable interference, the very next packet resynchronizes instantaneously without audio glitches.

---

## 2.3. Single-Pass FLAC-Style Order-4 Linear Predictor

For each sample index $n \in \lbrace 1, \dots, 47 \rbrace$ across every channel $c \in \lbrace 0, \dots, 5 \rbrace$, the predictor computes an estimated sample value using an order-4 finite-difference polynomial:

* **Warm-up Samples** ($n = 1, 2, 3$):
  * $n = 1 \implies \hat{x}_c[1] = x_c[0]$
  * $n = 2 \implies \hat{x}_c[2] = 2x_c[1] - x_c[0]$
  * $n = 3 \implies \hat{x}_c[3] = 3x_c[2] - 3x_c[1] + x_c[0]$
* **Steady-State Order-4 Samples** ($n \ge 4$):

$$\hat{x}_c[n] = 4x_c[n-1] - 6x_c[n-2] + 4x_c[n-3] - x_c[n-4]$$

The residual difference is evaluated using 24-bit signed 2's complement arithmetic:

$$e_c[n] = \mathrm{sext}_{24}\left(x_c[n] - \hat{x}_c[n]\right)$$

---

## 2.4. ZigZag Mapping & Escaped Golomb-Rice Coding

1. **ZigZag Mapping:** Maps signed residuals $e_c[n] \in [-2^{23}, 2^{23}-1]$ to unsigned integers $z \in [0, 2^{24}-1]$ so values centered near zero remain small positive numbers:

   $$z = (e \ll 1) \oplus (e \gg 31)$$

2. **Optimal Parameter $k$ Estimation via Hardware `CLZ`:**
   The Golomb-Rice parameter $k_c \in \lbrace 0, \dots, 22 \rbrace$ is derived from the mean steady-state residual ($n \ge 3$):

   $$k_c = \min\left(22,\ 31 - \mathrm{CLZ}(\bar{z}_c)\right)$$

   For warm-up samples $n \in \lbrace 1, 2 \rbrace$, $k_{\mathrm{warmup}} = k_c + 6$ is applied to absorb transient polynomial step deltas without inflating unary code lengths.

3. **Escaped Golomb-Rice Bit Packing:**
   The unsigned residual $z$ is split into quotient $q = \lfloor z / 2^k \rfloor$ (`q = z >> k`) and remainder $r = z \bmod 2^k$ (`r = z & ((1 << k) - 1)`):
   * **Normal Encoding** ($q < 15$): Emit $q$ ones, followed by a terminating `0` stop bit, followed by the $k$-bit remainder $r$.
   * **Transient / Explosion Escape** ($q \ge 15$): Emit `15` ones followed by a `0` stop bit, followed by the full raw value of $z$ as **25 raw bits**. This escape mechanism guarantees that even full-scale white noise or extreme movie explosions will never trigger buffer overflow.

---

## 2.5. Packet Structure (`HT306_LosslessHeader_t` — 14-Byte Header)

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       sync_word (0xA55A)      |          packet_len           |  Offset 0..3
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   sequence    |     flags     |  num_frames   |   rice_k[0]   |  Offset 4..7
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   rice_k[1]   |   rice_k[2]   |   rice_k[3]   |   rice_k[4]   |  Offset 8..11
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   rice_k[5]   | payload_crc8  |                               |  Offset 12..13
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                  Anchor Frame 0 (6 Ch x 3B = 18B)             |  Offset 14..31
+                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               |  Packed Golomb-Rice Bitstream |  Offset 32..N
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Byte Offset | Field Name | Type | Description |
| :---: | :--- | :---: | :--- |
| **`0..1`** | `sync_word` | `uint16_t` | Synchronization preamble (`0xA55A`, Little-Endian: `0x5A, 0xA5`). |
| **`2..3`** | `packet_len` | `uint16_t` | Total packet length in bytes (`header + payload`) for 64-byte USB Bulk stream reassembly. |
| **`4`** | `sequence` | `uint8_t` | Packet sequence index (`0..255`) for drop detection. |
| **`5`** | `flags` | `uint8_t` | Bit `0`: `HT306_FLAG_RATE_96KHZ` (`1` = `96 kHz`, `0` = `48 kHz`), Bit `1`: `24-bit`. |
| **`6`** | `num_frames` | `uint8_t` | Number of time frames per channel in the packet (default: `48`). |
| **`7..12`** | `rice_k[6]` | `uint8_t[6]` | Golomb-Rice parameter $k \in \lbrace 0, \dots, 22 \rbrace$ for channels `0..5`. |
| **`13`** | `payload_crc8` | `uint8_t` | `CRC-8` checksum (polynomial `0x07`) calculated across all `payload` bytes. |
| **`14..31`** | `anchor[6]` | `18 Bytes` | Uncompressed initial sample (`Frame 0`) in 24-bit signed PCM for all 6 channels. |
| **`32..N`** | `bitstream` | Variable | Compressed Golomb-Rice residual stream for `Frames 1..47`. |
