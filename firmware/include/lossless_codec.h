#ifndef HT306_LOSSLESS_CODEC_H
#define HT306_LOSSLESS_CODEC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Konfigurasi Protokol Audio Lossless 96 kHz (LG HT306 - Waveshare RP2350-Zero)
 */
#define HT306_SYNC_WORD               0xA55AU
#define HT306_NUM_CHANNELS            6U      /* FL, FR, C, LFE, SL, SR (5.1) */
#define HT306_MAX_CHANNELS            8U      /* Ekspansi 7.1 / Atmos */
#define HT306_SUBFRAME_SAMPLES        48U     /* 0.5 ms pada 96 kHz */
#define HT306_MAX_SUBFRAME_SAMPLES    96U     /* 1.0 ms pada 96 kHz */
#define HT306_RICE_ESCAPE_QUOTIENT    15U     /* Batas kuosien unary sebelum escape 25-bit */
#define HT306_ESCAPE_RAW_BITS         25U     /* Lebar bit zigzag penuh untuk 24-bit residual */
#define HT306_MAX_PACKET_BYTES        1020U   /* Batas aman di bawah 1023B USB Full-Speed ISO */

/**
 * Kode status hasil operasi codec
 */
typedef enum {
    HT306_CODEC_OK = 0,
    HT306_CODEC_ERR_INVALID_ARG = -1,
    HT306_CODEC_ERR_BAD_SYNC = -2,
    HT306_CODEC_ERR_CRC_MISMATCH = -3,
    HT306_CODEC_ERR_BUFFER_TOO_SMALL = -4,
    HT306_CODEC_ERR_CORRUPT_STREAM = -5
} HT306_CodecStatus_t;

/**
 * Flag konfigurasi di dalam header paket USB Lossless
 */
#define HT306_FLAG_RATE_96KHZ         (1U << 0)
#define HT306_FLAG_BITDEPTH_24        (1U << 1)
#define HT306_FLAG_CHANNELS_8         (1U << 2)
#define HT306_FLAG_SHIFT_MASK         0xF0U   /* Bit [7:4] untuk Block Floating-Point Shift Guard */

/**
 * Masking byte rice_k[ch] (Bit [4:0] = Parameter Golomb-Rice k dalam rentang 0..22)
 */
#define HT306_RICE_K_MASK             0x1FU

/**
 * Struktur Header Paket USB Lossless (14 Byte)
 */
typedef struct __attribute__((packed)) {
    uint16_t sync_word;                       /* Offset 0..1 : Wajib bernilai HT306_SYNC_WORD (0xA55A) */
    uint16_t packet_len;                      /* Offset 2..3 : Panjang total paket (header + payload) dalam byte */
    uint8_t  sequence;                        /* Offset 4    : Nomor urut paket (0..255) untuk deteksi drop */
    uint8_t  flags;                           /* Offset 5    : Kombinasi HT306_FLAG_* */
    uint8_t  num_frames;                      /* Offset 6    : Jumlah sampel per channel dalam paket (default: 48) */
    uint8_t  rice_k[HT306_NUM_CHANNELS];      /* Offset 7..12: Parameter Golomb-Rice k (0..22) per channel */
    uint8_t  payload_crc8;                    /* Offset 13   : CRC-8 (poly 0x07) dari seluruh byte payload */
} HT306_LosslessHeader_t;

/**
 * Mengemas (Encode) array PCM 24-bit interleaved (6 channel) menjadi paket USB Lossless Delta-Rice.
 *
 * @param pcm_in          Input sampel 24-bit signed (disimpan dalam int32_t [-8388608..+8388607]),
 *                        berukuran num_frames * HT306_NUM_CHANNELS.
 * @param num_frames      Jumlah frame sampel per channel (1..HT306_MAX_SUBFRAME_SAMPLES).
 * @param sequence        Nomor urut paket (0..255).
 * @param packet_out      Buffer tujuan untuk paket terkodekan (header + bitstream payload).
 * @param packet_out_cap  Kapasitas maksimum packet_out dalam byte.
 * @param packet_len_out  Output panjang aktual paket dalam byte.
 * @return                HT306_CODEC_OK jika berhasil.
 */
HT306_CodecStatus_t ht306_lossless_encode_frame(
    const int32_t *pcm_in,
    uint8_t num_frames,
    uint8_t sequence,
    uint8_t *packet_out,
    size_t packet_out_cap,
    size_t *packet_len_out
);

/**
 * Membuka (Decode) paket USB Lossless Delta-Rice kembali menjadi PCM 24-bit & format slot I2S 32-bit.
 * Dirancang untuk berjalan di Core 1 RP2350-Zero di dalam SRAM (< 0.03 ms per 48 sampel).
 *
 * @param packet_in           Buffer paket USB yang diterima.
 * @param packet_len          Panjang byte paket yang diterima.
 * @param pcm24_out           Output sampel 24-bit signed (int32_t) untuk verifikasi bit-exact
 *                            (boleh NULL jika hanya membutuhkan i2s32_out).
 * @param i2s32_msb_out       Output sampel 32-bit MSB-aligned (sample24 << 8) siap kirim ke DMA PIO
 *                            untuk Pulsus PS9850 (boleh NULL jika hanya membutuhkan pcm24_out).
 * @param frames_decoded_out  Output jumlah frame per channel yang berhasil didekode.
 * @return                    HT306_CODEC_OK jika paket valid dan berhasil dibuka.
 */
HT306_CodecStatus_t ht306_lossless_decode_frame(
    const uint8_t *packet_in,
    size_t packet_len,
    int32_t *pcm24_out,
    int32_t *i2s32_msb_out,
    uint8_t *frames_decoded_out
);

/**
 * Menghitung checksum CRC-8 (Polinomial 0x07) untuk validasi integritas paket USB.
 */
uint8_t ht306_crc8(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HT306_LOSSLESS_CODEC_H */
