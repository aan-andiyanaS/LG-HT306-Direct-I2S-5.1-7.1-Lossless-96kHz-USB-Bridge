#ifndef HT306_I2S_ENGINE_H
#define HT306_I2S_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Definisi Pin GPIO Waveshare RP2350-Zero untuk Jalur I2S ke IC602 (Pulsus PS9850)
 */
#define HT306_PIN_I2S_BCK         2U   /* GP2: Side-set bit 0 */
#define HT306_PIN_I2S_LRCK        3U   /* GP3: Side-set bit 1 */
#define HT306_PIN_I2S_ADATA0      4U   /* GP4: Front Left & Front Right */
#define HT306_PIN_I2S_ADATA1      5U   /* GP5: Surround Left & Surround Right */
#define HT306_PIN_I2S_ADATA2      6U   /* GP6: Center & Subwoofer (LFE) */
#define HT306_PIN_I2S_ADATA3      7U   /* GP7: Extra 7.1 / Height Left & Right */

/**
 * Konfigurasi Ukuran Ring Buffer & Blok DMA
 * Setiap frame I2S multi-lane terdiri dari:
 * - 4 word (128 bit) untuk sub-frame Kiri (32 bit x 4 lane ADATA0..3)
 * - 4 word (128 bit) untuk sub-frame Kanan (32 bit x 4 lane ADATA0..3)
 * Total = 8 x uint32_t (32 Byte) per sampel waktu (1/Fs).
 */
#define HT306_I2S_WORDS_PER_FRAME     8U
#define HT306_DMA_BLOCK_FRAMES        48U   /* 0.5 ms pada 96 kHz, 1.0 ms pada 48 kHz */
#define HT306_RING_BUFFER_BLOCKS      16U   /* Total 16 blok (8 ms buffer @ 96 kHz) */
#define HT306_RING_BUFFER_FRAMES      (HT306_DMA_BLOCK_FRAMES * HT306_RING_BUFFER_BLOCKS)

/**
 * Mode Frekuensi Sampling I2S
 */
typedef enum {
    HT306_SAMPLE_RATE_44100 = 44100U,
    HT306_SAMPLE_RATE_48000 = 48000U,
    HT306_SAMPLE_RATE_96000 = 96000U
} HT306_SampleRate_t;

/**
 * Statistik Status Engine I2S & Ring Buffer
 */
typedef struct {
    uint32_t current_sample_rate;
    uint32_t buffered_frames;
    uint32_t underrun_count;
    uint32_t overrun_count;
    uint32_t blocks_transmitted;
} HT306_I2SStats_t;

/**
 * Menginisialisasi mesin PIO0 dan DMA Ping-Pong untuk output 4-lane I2S ke PS9850.
 */
bool ht306_i2s_engine_init(HT306_SampleRate_t initial_rate);

/**
 * Mengubah frekuensi detak I2S (44.1 kHz, 48 kHz, atau 96 kHz) secara dinamis.
 */
void ht306_i2s_set_sample_rate(HT306_SampleRate_t new_rate);

/**
 * Mengonversi 4 sampel 32-bit MSB-aligned (masing-masing untuk ADATA0, ADATA1, ADATA2, ADATA3)
 * menjadi 4 word 32-bit yang telah di-interleave per 4-bit nibble untuk instruksi `out pins, 4` PIO.
 *
 * @param lane0_sample32  Sampel 32-bit MSB-aligned untuk ADATA0 (FL atau FR)
 * @param lane1_sample32  Sampel 32-bit MSB-aligned untuk ADATA1 (SL atau SR)
 * @param lane2_sample32  Sampel 32-bit MSB-aligned untuk ADATA2 (C  atau LFE)
 * @param lane3_sample32  Sampel 32-bit MSB-aligned untuk ADATA3 (Back L atau Back R)
 * @param pio_words_out   Output 4 x uint32_t siap dimasukkan ke FIFO PIO / Ring Buffer DMA.
 */
void ht306_interleave_4lanes_to_pio_words(
    uint32_t lane0_sample32,
    uint32_t lane1_sample32,
    uint32_t lane2_sample32,
    uint32_t lane3_sample32,
    uint32_t pio_words_out[4]
);

/**
 * Menulis array sampel 6-channel (FL, FR, C, LFE, SL, SR) 32-bit MSB-aligned ke dalam Ring Buffer DMA.
 *
 * @param samples_6ch_i2s32  Array berukuran num_frames * 6 (urutan: FL, FR, C, LFE, SL, SR).
 * @param num_frames         Jumlah frame waktu yang akan ditulis.
 * @return                   Jumlah frame yang berhasil dimasukkan ke Ring Buffer.
 */
size_t ht306_i2s_write_frames_6ch(const int32_t *samples_6ch_i2s32, size_t num_frames);

/**
 * Membaca statistik performa dan kesehatan buffer I2S saat ini.
 */
void ht306_i2s_get_stats(HT306_I2SStats_t *stats_out);

#ifdef __cplusplus
}
#endif

#endif /* HT306_I2S_ENGINE_H */
