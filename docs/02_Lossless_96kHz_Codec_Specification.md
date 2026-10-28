# 2. Spesifikasi Matematis & Protokol Codec *Real-Time Lossless* (`96 kHz / 24-bit 5.1`)

[⬅️ Kembali ke README Utama](../README.md)

Dokumen ini menjabarkan spesifikasi matematis dan struktur paket pada [`firmware/include/lossless_codec.h`](../firmware/include/lossless_codec.h), [`firmware/src/lossless_codec.c`](../firmware/src/lossless_codec.c), dan [`firmware/src/usb_descriptors.c`](../firmware/src/usb_descriptors.c).

---

## 2.1. Mengapa Diperlukan Kompresi *Real-Time Lossless*?

Antarmuka USB bawaan pada mikrokontroler `RP2350` / `RP2040` bekerja pada standar **USB 1.1 Full-Speed (`12 Mbps`)** yang memiliki batas kapasitas praktis sekitar **`1.000 KB/detik`**:
* **Mode `48 kHz / 24-bit 5.1` (Mentah):** `48.000 × 6 × 3 Byte = 864 KB/detik` *(Muat di kabel USB Full-Speed)*.
* **Mode `96 kHz / 24-bit 5.1` (Mentah):** `96.000 × 6 × 3 Byte = 1.728 KB/detik` *(Melebihi batas fisik USB Full-Speed sebesar 72%)*.

Dengan algoritma **Single-Pass FLAC Order-4 Linear Predictor + Escaped Golomb-Rice**, aliran `1.728,0 KB/detik` dipadatkan menjadi rata-rata **`716,1 KB/detik` (`41,4%`)** dengan **kemurnian matematis 100% (`0 bit error`)** dan waktu buka kompresi `< 0,03 milidetik` di `Core 1` `RP2350-Zero`.

---

## 2.2. Mikro-Frame `0,5 ms` (`48 Sampel / Channel`)

Setiap paket mengemas tepat **48 frame waktu** (`0,5 milidetik` pada `96 kHz`, atau `1,0 milidetik` pada `48 kHz`), total `288 sampel` (`6 channel × 48`).
* **Sampel ke-0 (`Anchor Frame`):** Dikirim mentah tanpa kompresi (`6 channel × 3 byte = 18 byte` pada Offset `14..31`).
* Ini menjamin bahwa setiap paket bersifat **100% independen (*stateless across packets*)** — jika 1 paket terganggu pada kabel USB, paket berikutnya langsung pulih seketika tanpa merusak aliran audio.

---

## 2.3. Prediktor Linear Orde-4 Bergaya FLAC (*Single-Pass*)

Untuk indeks sampel $n \in \lbrace 1, \dots, 47 \rbrace$ pada setiap channel $c \in \lbrace 0, \dots, 5 \rbrace$, prediktor menghitung estimasi nilai sampel menggunakan polinomial beda hingga (*finite-difference*) orde-4:

* **Sampel Transisi Awal** ($n = 1, 2, 3$):
  * $n = 1 \implies \hat{x}_c[1] = x_c[0]$
  * $n = 2 \implies \hat{x}_c[2] = 2x_c[1] - x_c[0]$
  * $n = 3 \implies \hat{x}_c[3] = 3x_c[2] - 3x_c[1] + x_c[0]$
* **Sampel Kondisi Tunak Orde-4** ($n \ge 4$):

$$\hat{x}_c[n] = 4x_c[n-1] - 6x_c[n-2] + 4x_c[n-3] - x_c[n-4]$$

Nilai selisih (*residual*) dihitung dalam aritmetika komplemen-2 24-bit:

$$e_c[n] = \mathrm{sext}_{24}\left(x_c[n] - \hat{x}_c[n]\right)$$

---

## 2.4. Pemetaan ZigZag & *Escaped Golomb-Rice Coding*

1. **ZigZag Mapping:** Mengubah angka bertanda $e_c[n] \in [-2^{23}, 2^{23}-1]$ menjadi bilangan bulat tak bertanda $z \in [0, 2^{24}-1]$ agar angka di sekitar nol selalu bernilai positif kecil:

   $$z = (e \ll 1) \oplus (e \gg 31)$$

2. **Penentuan Parameter Optimal $k$ dengan Instruksi Hardware `CLZ`:**
   Parameter Golomb-Rice $k_c \in \lbrace 0, \dots, 22 \rbrace$ dihitung dari rata-rata residual kondisi tunak ($n \ge 3$):

   $$k_c = \min\left(22,\ 31 - \mathrm{CLZ}(\bar{z}_c)\right)$$

   Untuk sampel transisi awal $n \in \lbrace 1, 2 \rbrace$, digunakan $k_{\mathrm{warmup}} = k_c + 6$ agar lonjakan polinomial awal tidak memperbesar kode unary.

3. **Kode Bit *Escaped Golomb-Rice*:**
   Bagi nilai $z$ menjadi kuosien $q = \lfloor z / 2^k \rfloor$ (`q = z >> k`) dan sisa $r = z \bmod 2^k$ (`r = z & ((1 << k) - 1)`):
   * **Kondisi Normal** ($q < 15$): Tulis $q$ buah bit `1`, diikuti 1 bit `0` (*stop bit*), lalu tulis sisa $r$ sebanyak $k$ bit.
   * **Kondisi *Transient / Explosion Escape*** ($q \ge 15$): Tulis `15` buah bit `1` diikuti bit `0`, lalu tulis nilai utuh $z$ sebanyak **25 bit mentah**. Mekanisme *escape* ini menjamin bahwa ledakan ekstrem (*full-scale white noise*) sekalipun tidak akan pernah menyebabkan *buffer overflow*.

---

## 2.5. Struktur Paket USB (`HT306_LosslessHeader_t` — 14 Byte Header)

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

| Offset Byte | Nama Field | Tipe | Deskripsi |
| :---: | :--- | :---: | :--- |
| **`0..1`** | `sync_word` | `uint16_t` | Penanda awal paket (`0xA55A`, Little-Endian: `0x5A, 0xA5`). |
| **`2..3`** | `packet_len` | `uint16_t` | Panjang total paket dalam byte (`header + payload`) untuk penyatuan potongan 64-byte USB Bulk. |
| **`4`** | `sequence` | `uint8_t` | Nomor urut paket (`0..255`) untuk deteksi *packet drop*. |
| **`5`** | `flags` | `uint8_t` | Bit `0`: `HT306_FLAG_RATE_96KHZ` (`1` = `96 kHz`, `0` = `48 kHz`), Bit `1`: `24-bit`. |
| **`6`** | `num_frames` | `uint8_t` | Jumlah sampel per channel dalam paket (default: `48`). |
| **`7..12`** | `rice_k[6]` | `uint8_t[6]` | Parameter Golomb-Rice $k \in \lbrace 0, \dots, 22 \rbrace$ untuk masing-masing channel `0..5`. |
| **`13`** | `payload_crc8` | `uint8_t` | Checksum `CRC-8` (polinomial `0x07`) yang dihitung atas seluruh byte `payload`. |
| **`14..31`** | `anchor[6]` | `18 Byte` | Sampel pertama (`Frame 0`) mentah 24-bit signed untuk ke-6 channel (`6 × 3 Byte`). |
| **`32..N`** | `bitstream` | Variabel | Aliran bit residual `Frame 1..47` yang dipadatkan dengan *Escaped Golomb-Rice*. |
