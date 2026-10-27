#include "lossless_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * Menghasilkan sinyal uji 6-channel 96 kHz / 24-bit yang menyerupai film bioskop
 * (kombinasi frekuensi bass LFE, dialog Center, musik Front L/R, dan efek Surround)
 */
static void generate_multichannel_96k_signal(
    int32_t *pcm_out,
    size_t num_frames,
    size_t frame_offset,
    bool inject_explosion_transient)
{
    const double fs = 96000.0;
    const double freqs[HT306_NUM_CHANNELS] = {
        440.0,   /* FL: Nada Utama */
        880.0,   /* FR: Harmonik */
        300.0,   /* C : Vokal Dialog */
        55.0,    /* LFE: Bass Subwoofer */
        1200.0,  /* SL: Efek Surround Kiri */
        1600.0   /* SR: Efek Surround Kanan */
    };

    for (size_t n = 0; n < num_frames; ++n) {
        double t = (double)(frame_offset + n) / fs;
        for (size_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
            double wave = 0.65 * sin(2.0 * M_PI * freqs[ch] * t)
                        + 0.20 * cos(2.0 * M_PI * (freqs[ch] * 2.5) * t);
            int32_t sample24 = (int32_t)(wave * 4194303.0);

            if (inject_explosion_transient && (n == 17U || n == 18U)) {
                /* Injeksi lonjakan transien ekstrem penuh (Full-Scale 24-bit Step) */
                sample24 = (n & 1U) ? 8388600 : -8388600;
            }

            /* Pastikan berada dalam rentang 24-bit signed [-8388608..+8388607] */
            if (sample24 > 8388607) sample24 = 8388607;
            if (sample24 < -8388608) sample24 = -8388608;

            pcm_out[n * HT306_NUM_CHANNELS + ch] = sample24;
        }
    }
}

int main(void) {
    printf("=== Uji Verifikasi Bit-Perfect Codec Lossless 96 kHz (RP2350-Zero HT306) ===\n");

    const uint32_t total_subframes = 2000U; /* 2000 x 0.5 ms = 1.0 detik penuh @ 96 kHz (96.000 frame x 6 ch) */
    size_t total_raw_bytes = 0;
    size_t total_packed_bytes = 0;
    size_t max_packet_size = 0;

    int32_t pcm_in[HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS];
    int32_t pcm_out[HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS];
    int32_t i2s_out[HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS];
    uint8_t packet[HT306_MAX_PACKET_BYTES];

    for (uint32_t sf = 0; sf < total_subframes; ++sf) {
        bool is_transient_test = (sf % 250U == 125U);
        generate_multichannel_96k_signal(
            pcm_in,
            HT306_SUBFRAME_SAMPLES,
            (size_t)sf * HT306_SUBFRAME_SAMPLES,
            is_transient_test
        );

        size_t packet_len = 0;
        HT306_CodecStatus_t enc_status = ht306_lossless_encode_frame(
            pcm_in,
            HT306_SUBFRAME_SAMPLES,
            (uint8_t)(sf & 0xFFU),
            packet,
            sizeof(packet),
            &packet_len
        );

        if (enc_status != HT306_CODEC_OK) {
            printf("[GAGAL] Encode gagal pada subframe %u (status=%d)\n", sf, enc_status);
            return 1;
        }

        uint8_t decoded_frames = 0;
        memset(pcm_out, 0, sizeof(pcm_out));
        memset(i2s_out, 0, sizeof(i2s_out));

        HT306_CodecStatus_t dec_status = ht306_lossless_decode_frame(
            packet,
            packet_len,
            pcm_out,
            i2s_out,
            &decoded_frames
        );

        if (dec_status != HT306_CODEC_OK || decoded_frames != HT306_SUBFRAME_SAMPLES) {
            printf("[GAGAL] Decode gagal pada subframe %u (status=%d, frames=%u)\n",
                   sf, dec_status, decoded_frames);
            return 1;
        }

        /* Verifikasi 100% Bit-Exact (Input == Output tanpa selisih 1 bit pun) */
        if (memcmp(pcm_in, pcm_out, sizeof(pcm_in)) != 0) {
            printf("[GAGAL] Ketidaksesuaian bit ditemukan pada subframe %u!\n", sf);
            return 1;
        }

        /* Verifikasi pergeseran 32-bit MSB-aligned untuk Pulsus PS9850 */
        for (size_t i = 0; i < HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS; ++i) {
            int32_t expected_i2s = (int32_t)((uint32_t)pcm_in[i] << 8U);
            if (i2s_out[i] != expected_i2s) {
                printf("[GAGAL] Format slot I2S 32-bit salah pada indeks %zu!\n", i);
                return 1;
            }
        }

        total_raw_bytes += HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS * 3U;
        total_packed_bytes += packet_len;
        if (packet_len > max_packet_size) {
            max_packet_size = packet_len;
        }
    }

    /* Uji Deteksi Korupsi Paket (CRC-8 Guard) */
    packet[sizeof(HT306_LosslessHeader_t) + 5U] ^= 0x40U;
    uint8_t dummy_frames = 0;
    HT306_CodecStatus_t corrupt_status = ht306_lossless_decode_frame(
        packet, max_packet_size, pcm_out, i2s_out, &dummy_frames
    );
    if (corrupt_status != HT306_CODEC_ERR_CRC_MISMATCH) {
        printf("[GAGAL] Deteksi CRC-8 tidak menangkap kerusakan paket!\n");
        return 1;
    }

    double ratio_pct = ((double)total_packed_bytes / (double)total_raw_bytes) * 100.0;
    double kb_per_sec = (double)total_packed_bytes / 1000.0;

    printf("[LULUS] Verifikasi Bit-Perfect: 100%% Identik (0 bit error pada 576.000 sampel 24-bit)\n");
    printf("[LULUS] Uji Proteksi CRC-8    : Korupsi kabel USB terdeteksi akurat\n");
    printf("[INFO]  Kebutuhan Bandwidth Mentah (96kHz 6ch 24-bit) : %.2f KB/detik\n",
           (double)total_raw_bytes / 1000.0);
    printf("[INFO]  Kebutuhan Bandwidth Lossless di Kabel USB     : %.2f KB/detik (%.1f%% dari ukuran asli)\n",
           kb_per_sec, ratio_pct);
    printf("[INFO]  Ukuran Paket Maksimum (termasuk ledakan)      : %zu Byte / 0.5 ms\n", max_packet_size);

    if (kb_per_sec >= 950.0) {
        printf("[PERINGATAN] Bandwidth melebihi batas target USB Full-Speed!\n");
        return 1;
    }

    return 0;
}
