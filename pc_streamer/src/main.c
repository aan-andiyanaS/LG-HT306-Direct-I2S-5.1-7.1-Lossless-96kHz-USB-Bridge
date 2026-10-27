#include "lossless_codec.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/**
 * Modul Pengirim PC (PC Companion Streamer) untuk Mode 96 kHz Lossless
 * Menerima blok 48 frame x 6 channel (0.5 ms @ 96 kHz) dari output JRiver Media Center,
 * mengemasnya dengan ht306_lossless_encode_frame(), dan mengirimkannya ke Endpoint USB RP2350-Zero.
 */

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t  seq_counter;
    uint64_t total_frames_sent;
    uint64_t total_raw_bytes;
    uint64_t total_usb_bytes;
} HT306_PcStreamerContext_t;

void ht306_streamer_init(HT306_PcStreamerContext_t *ctx) {
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(*ctx));
        ctx->sample_rate_hz = 96000U;
    }
}

HT306_CodecStatus_t ht306_streamer_process_jriver_block(
    HT306_PcStreamerContext_t *ctx,
    const int32_t *jriver_pcm24_6ch,
    uint8_t num_frames,
    uint8_t *usb_packet_out,
    size_t usb_packet_cap,
    size_t *usb_packet_len_out)
{
    if (ctx == NULL || jriver_pcm24_6ch == NULL || usb_packet_out == NULL || usb_packet_len_out == NULL) {
        return HT306_CODEC_ERR_INVALID_ARG;
    }

    HT306_CodecStatus_t st = ht306_lossless_encode_frame(
        jriver_pcm24_6ch,
        num_frames,
        ctx->seq_counter++,
        usb_packet_out,
        usb_packet_cap,
        usb_packet_len_out
    );

    if (st == HT306_CODEC_OK) {
        /* Sesuaikan bit flag 48kHz vs 96kHz mengikuti Sample Rate aktif di JRiver */
        if (ctx->sample_rate_hz < 96000U) {
            usb_packet_out[5] &= (uint8_t)~HT306_FLAG_RATE_96KHZ;
        }
        ctx->total_frames_sent += num_frames;
        ctx->total_raw_bytes   += (uint64_t)num_frames * HT306_NUM_CHANNELS * 3U;
        ctx->total_usb_bytes   += *usb_packet_len_out;
    }

    return st;
}

int main(void) {
    printf("HT306 PC Companion Streamer (96 kHz 24-bit 5.1 Lossless -> Waveshare RP2350-Zero)\n");
    printf("Status: Siap menerima stream 6-channel dari JRiver Media Center.\n");
    return 0;
}
