# PRD & Spesifikasi Teknis: Firmware Waveshare RP2350-Zero 5.1/7.1 Lossless 96 kHz untuk LG HT306

## 1. Tujuan & Visi Produk (*Objective*)

Membangun *firmware* berkinerja tinggi pada papan mikrokontroler **Waveshare `RP2350-Zero`** (berbasis chip **Raspberry Pi `RP2350A`**, Dual Cortex-M33 @ 150 MHz, USB Type-C) beserta modul pengirim ringan di PC (*Companion Streamer*) yang menjembatani **JRiver Media Center (DSP Studio)** menuju chip prosesor PWM **`Pulsus PS9850` (`IC602`)** di dalam **LG HT306** secara **100% Murni Digital (*Bit-Perfect*)**.

Sistem ini dirancang untuk menembus batas *bandwidth* fisik USB Full-Speed (`12 Mbps` / maks `~1.023 KB/detik`) agar sanggup mengalirkan audio **Surround 5.1 (6-Channel) hingga 96 kHz / 24-bit** (yang ukuran mentahnya `1.728 KB/detik`) menggunakan algoritma **Real-Time Lossless Differential Packing** dengan latensi *unpacking* di bawah **`0,03 milidetik`**, sambil tetap mempertahankan seluruh fungsi **EQ Bawaan LG HT306 (`MT1389MH` via I2C)**.

---

## 2. Spesifikasi Hardware Target (`Waveshare RP2350-Zero`)

* **MCU:** Raspberry Pi `RP2350A` (Dual ARM Cortex-M33 @ 150 MHz, 520 KB SRAM, 4 MB QSPI Flash).
* **Konektor USB:** Onboard USB Type-C Female.
* **Indikator Visual:** Onboard WS2812 RGB LED pada **`GP16`**:
  * **Biru:** Standby / Menunggu stream USB.
  * **Hijau:** Mode Standar 5.1 (`44.1 / 48 kHz` Raw UAC2) aktif.
  * **Ungu:** Mode Hi-Res Lossless 5.1 (`96 kHz` Delta-Rice) aktif.
  * **Merah:** *CRC Error* / *Buffer Underrun* / *Muted*.

### Pemetaan Pin (`RP2350-Zero` ➔ Mainboard LG HT306 `IC602 PS9850`)

| Pin `RP2350-Zero` | Nama Sinyal | Arah | Tujuan di Mainboard LG HT306 (`IC602 PS9850`) |
| :---: | :--- | :---: | :--- |
| **`GP2`** | `I2S_BCK` | Output | Pad Resistor Seri jalur `BCK` menuju `IC602` |
| **`GP3`** | `I2S_LRCK` | Output | Pad Resistor Seri jalur `LRCK` menuju `IC602` |
| **`GP4`** | `I2S_ADATA0` | Output | Pad Resistor Seri jalur `ADATA0` (**Front L / Front R**) menuju `IC602` |
| **`GP5`** | `I2S_ADATA1` | Output | Pad Resistor Seri jalur `ADATA1` (**Rear Surround L / R**) menuju `IC602` |
| **`GP6`** | `I2S_ADATA2` | Output | Pad Resistor Seri jalur `ADATA2` (**Center / Subwoofer**) menuju `IC602` |
| **`GP7`** | `I2S_ADATA3` | Output | *(Opsional Ekspansi 7.1 / Atmos)* Jalur **Back Surround L / R** |
| **`GP8`** | `MUX_SELECT` | Output | Pin Select IC `74LVC157` (`LOW` = DVD Asli HT306, `HIGH` = Mode PC USB) |
| **`GP9`** | `REMOCON_TX` | Output | *(Opsional)* Jalur injeksi kode Remote LG ke kabel pita Front Panel |
| **`GP16`** | `WS2812_LED` | Internal | Onboard RGB LED Status Indicator (`RP2350-Zero`) |
| **`GND`** | `Ground` | Power | Titik Ground (`GND`) Digital Mainboard HT306 (Dekat `IC602`) |

---

## 3. Spesifikasi Protokol Kompresi *Real-Time Lossless Delta-Rice* (96 kHz Mode)

1. **Ukuran Sub-Frame:** `48 sampel per channel` (`0,5 ms` pada `96 kHz`), total `288 sampel` per paket (`6 channel × 48`).
2. **Prediktor Linear Orde-4 FLAC-Style (*Single-Pass Lossless*):**
   $$\hat{x}_c[n] = 4 \cdot x_c[n-1] - 6 \cdot x_c[n-2] + 4 \cdot x_c[n-3] - x_c[n-4]$$
   $$e_c[n] = x_c[n] - \hat{x}_c[n]$$
3. **Pemetaan ZigZag & Escaped Golomb-Rice Bit-Packing:**
   * Residual bertanda $e_c[n]$ diubah menjadi bilangan bulat tak bertanda:
     $$z = (e \ll 1) \oplus (e \gg 31)$$
   * Parameter Rice $k \in \{0..22\}$ dihitung secara optimal per channel pada setiap *sub-frame* menggunakan instruksi hardware `CLZ` (`__builtin_clz`).
   * Kuosien $q = \lfloor z / 2^k \rfloor$ (`z >> k`) ditulis sebagai kode *unary* (atau *escaped 25-bit fallback* jika $q \ge 15$ untuk menjamin batas ukuran bit maksimum pada adegan ledakan), diikuti sisa $r = z \bmod 2^k$ (`z & ((1 << k) - 1)`) sebanyak $k$ bit.
4. **Struktur Header Paket (`HT306_LosslessHeader_t` — 14 Byte):**
   * `sync_word` (`0xA55A`, 2 Byte — Offset `0..1`)
   * `packet_len` (2 Byte — Offset `2..3`, panjang total paket untuk penyatuan potongan 64-Byte USB Bulk)
   * `sequence` (1 Byte — Offset `4`)
   * `flags` (1 Byte — Offset `5`: Sample Rate `96kHz`/`48kHz`, Bit Depth `24-bit`)
   * `num_frames` (1 Byte — Offset `6`: default `48`)
   * `rice_k[6]` (6 Byte — Offset `7..12`: parameter $k$ tiap channel)
   * `payload_crc8` (1 Byte — Offset `13`: polinomial `0x07` atas seluruh payload)
   * `payload[]` (18 Byte Anchor + Bit-packed Residual stream)
