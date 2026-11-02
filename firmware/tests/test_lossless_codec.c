#include "lossless_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void generate_multichannel_96k_signal_24bit(
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
                sample24 = (n & 1U) ? 8388600 : -8388600;
            }

            if (sample24 > 8388607) sample24 = 8388607;
            if (sample24 < -8388608) sample24 = -8388608;

            pcm_out[n * HT306_NUM_CHANNELS + ch] = sample24;
        }
    }
}

static void generate_multichannel_96k_signal_32bit(
    int32_t *pcm_out,
    size_t num_frames,
    size_t frame_offset,
    bool inject_explosion_transient)
{
    const double fs = 96000.0;
    const double freqs[HT306_NUM_CHANNELS] = {
        440.0, 880.0, 300.0, 55.0, 1200.0, 1600.0
    };

    for (size_t n = 0; n < num_frames; ++n) {
        double t = (double)(frame_offset + n) / fs;
        for (size_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
            double wave = 0.65 * sin(2.0 * M_PI * freqs[ch] * t)
                        + 0.20 * cos(2.0 * M_PI * (freqs[ch] * 2.5) * t);
            int64_t sample32 = (int64_t)(wave * 1073741823.0);

            if (inject_explosion_transient && (n == 17U || n == 18U)) {
                sample32 = (n & 1U) ? 2147483640LL : -2147483640LL;
            }

            if (sample32 > 2147483647LL) sample32 = 2147483647LL;
            if (sample32 < -2147483648LL) sample32 = -2147483648LL;

            pcm_out[n * HT306_NUM_CHANNELS + ch] = (int32_t)sample32;
        }
    }
}

int main(void) {
    printf("=== Uji Verifikasi Bit-Perfect Codec Lossless 96 kHz (RP2350-Zero HT306) ===\n");

    const uint32_t total_subframes = 2000U; /* 2000 x 0.5 ms = 1.0 detik @ 96 kHz (96.000 frame x 6 ch) */
    size_t total_raw_bytes = 0;
    size_t total_packed_bytes = 0;
    size_t max_packet_size = 0;

    int32_t pcm_in[HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS];
    int32_t pcm_out[HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS];
    int32_t i2s_out[HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS];
    uint8_t packet[HT306_MAX_PACKET_BYTES];

    /* --- Bagian 1: Pengujian 24-bit 96 kHz --- */
    printf("\n[1/2] Menjalankan Uji 24-bit 96 kHz 6-Channel (576.000 sampel)...\n");
    for (uint32_t sf = 0; sf < total_subframes; ++sf) {
        bool is_transient_test = (sf % 250U == 125U);
        generate_multichannel_96k_signal_24bit(
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
            printf("[GAGAL] 24-bit Encode gagal pada subframe %u (status=%d)\n", sf, enc_status);
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
            printf("[GAGAL] 24-bit Decode gagal pada subframe %u (status=%d, frames=%u)\n",
                   sf, dec_status, decoded_frames);
            return 1;
        }

        if (memcmp(pcm_in, pcm_out, sizeof(pcm_in)) != 0) {
            printf("[GAGAL] 24-bit Bit mismatch pada subframe %u!\n", sf);
            return 1;
        }

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
    printf("  [LULUS] Bit-Perfect 24-bit: 100%% Identik (0 bit error pada 576.000 sampel)\n");
    printf("  [LULUS] Uji Proteksi CRC-8 : Korupsi terdeteksi akurat\n");
    printf("  [INFO]  Bandwidth Mentah   : %.2f KB/s | Lossless USB: %.2f KB/s (%.1f%%)\n",
           (double)total_raw_bytes / 1000.0, kb_per_sec, ratio_pct);

    /* --- Bagian 2: Pengujian 32-bit True Studio Master 96 kHz --- */
    printf("\n[2/2] Menjalankan Uji 32-bit 96 kHz 6-Channel True Studio Master (576.000 sampel)...\n");
    size_t total_32_raw = 0;
    size_t total_32_packed = 0;
    size_t max_32_packet = 0;

    for (uint32_t sf = 0; sf < total_subframes; ++sf) {
        bool is_transient_test = (sf % 250U == 125U);
        generate_multichannel_96k_signal_32bit(
            pcm_in,
            HT306_SUBFRAME_SAMPLES,
            (size_t)sf * HT306_SUBFRAME_SAMPLES,
            is_transient_test
        );

        size_t packet_len = 0;
        HT306_CodecStatus_t enc_status = ht306_lossless_encode_frame_flags(
            pcm_in,
            HT306_SUBFRAME_SAMPLES,
            (uint8_t)(sf & 0xFFU),
            HT306_FLAG_RATE_96KHZ | HT306_FLAG_BITDEPTH_32,
            packet,
            sizeof(packet),
            &packet_len
        );

        if (enc_status != HT306_CODEC_OK) {
            printf("[GAGAL] 32-bit Encode gagal pada subframe %u (status=%d)\n", sf, enc_status);
            return 1;
        }

        uint8_t decoded_frames = 0;
        memset(i2s_out, 0, sizeof(i2s_out));

        HT306_CodecStatus_t dec_status = ht306_lossless_decode_frame(
            packet,
            packet_len,
            NULL,
            i2s_out,
            &decoded_frames
        );

        if (dec_status != HT306_CODEC_OK || decoded_frames != HT306_SUBFRAME_SAMPLES) {
            printf("[GAGAL] 32-bit Decode gagal pada subframe %u (status=%d)\n", sf, dec_status);
            return 1;
        }

        /* 32-bit: slot i2s_out harus 100% bit-exact dengan pcm_in 32-bit */
        if (memcmp(pcm_in, i2s_out, sizeof(pcm_in)) != 0) {
            printf("[GAGAL] 32-bit Bit mismatch pada subframe %u!\n", sf);
            return 1;
        }

        total_32_raw += HT306_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS * 4U;
        total_32_packed += packet_len;
        if (packet_len > max_32_packet) {
            max_32_packet = packet_len;
        }
    }

    double ratio_32_pct = ((double)total_32_packed / (double)total_32_raw) * 100.0;
    double kb_32_per_sec = (double)total_32_packed / 1000.0;
    printf("  [LULUS] Bit-Perfect 32-bit: 100%% Identik (0 bit error pada 576.000 sampel)\n");
    printf("  [INFO]  Bandwidth Mentah   : %.2f KB/s | Lossless USB: %.2f KB/s (%.1f%%)\n",
           (double)total_32_raw / 1000.0, kb_32_per_sec, ratio_32_pct);
    printf("  [INFO]  Ukuran Maks Paket  : %zu Byte / 0.5 ms\n", max_32_packet);

    printf("\n=== SEMUA PENGUJIAN 24-BIT DAN 32-BIT 96 KHZ BERHASIL 100%% BIT-PERFECT ===\n");
    return 0;
}
