#include "lossless_codec.h"
#include "i2s_engine.h"
#include "ht306_control.h"
#include "ht306_usb.h"
#include <string.h>

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "tusb.h"
#define HT306_MAIN_RAM_FUNC(fn) __not_in_flash_func(fn)
#define HT306_MAIN_DMB() __dmb()
#else
#define HT306_MAIN_RAM_FUNC(fn) fn
#define HT306_MAIN_DMB() ((void)0)
#endif

/**
 * Antrian Paket 96 kHz Lossless Wait-Free SPSC antara Core 0 (USB) dan Core 1 (Unpacker).
 */
#define HT306_PACKET_QUEUE_DEPTH  8U

typedef struct {
    uint8_t  data[HT306_MAX_PACKET_BYTES];
    uint16_t length;
} HT306_PacketSlot_t;

static HT306_PacketSlot_t g_packet_queue[HT306_PACKET_QUEUE_DEPTH];
static volatile uint32_t  g_pkt_write_count = 0U;
static volatile uint32_t  g_pkt_read_count  = 0U;

/* Buffer statis di SRAM (mencegah Stack Overflow pada Core 1 yang hanya memiliki 2 KB stack) */
static int32_t g_core1_decoded_i2s32[HT306_MAX_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS];
static int32_t g_core0_raw48k_i2s32[HT306_DMA_BLOCK_FRAMES * HT306_NUM_CHANNELS];

void HT306_MAIN_RAM_FUNC(ht306_core1_lossless_worker)(void) {
    while (true) {
        uint32_t w = g_pkt_write_count;
        uint32_t r = g_pkt_read_count;

        if ((uint32_t)(w - r) > 0U) {
            HT306_MAIN_DMB();
            uint32_t slot_idx = r % HT306_PACKET_QUEUE_DEPTH;
            const HT306_PacketSlot_t *slot = &g_packet_queue[slot_idx];
            uint8_t pkt_flags = slot->data[5];
            uint8_t frames_decoded = 0U;

            HT306_CodecStatus_t status = ht306_lossless_decode_frame(
                slot->data,
                slot->length,
                NULL,
                g_core1_decoded_i2s32,
                &frames_decoded
            );

            HT306_MAIN_DMB();
            g_pkt_read_count = r + 1U;

            if (status == HT306_CODEC_OK && frames_decoded > 0U) {
                bool is_96k = (pkt_flags & HT306_FLAG_RATE_96KHZ) != 0U;
                ht306_i2s_set_sample_rate(is_96k ? HT306_SAMPLE_RATE_96000 : HT306_SAMPLE_RATE_48000);
                ht306_i2s_write_frames_6ch(g_core1_decoded_i2s32, frames_decoded);
                ht306_control_set_status_led(is_96k ? HT306_STATUS_MODE_96K_PURPLE : HT306_STATUS_MODE_48K_GREEN);
            } else {
                ht306_control_set_status_led(HT306_STATUS_ERROR_RED);
            }
        }
#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
        else {
            tight_loop_contents();
        }
#else
        else {
            break;
        }
#endif
    }
}

bool HT306_MAIN_RAM_FUNC(ht306_usb_enqueue_lossless_packet)(const uint8_t *usb_data, uint16_t len) {
    if (usb_data == NULL || len == 0U || len > HT306_MAX_PACKET_BYTES) {
        return false;
    }

    uint32_t w = g_pkt_write_count;
    uint32_t r = g_pkt_read_count;
    if ((uint32_t)(w - r) >= HT306_PACKET_QUEUE_DEPTH) {
        return false;
    }

    uint32_t slot_idx = w % HT306_PACKET_QUEUE_DEPTH;
    HT306_PacketSlot_t *slot = &g_packet_queue[slot_idx];
    memcpy(slot->data, usb_data, len);
    slot->length = len;

    HT306_MAIN_DMB();
    g_pkt_write_count = w + 1U;
    return true;
}

void HT306_MAIN_RAM_FUNC(ht306_usb_receive_raw_48k_pcm24)(
    const int32_t *raw_6ch_pcm24,
    size_t num_frames)
{
    if (raw_6ch_pcm24 == NULL || num_frames == 0U) {
        return;
    }
    if (num_frames > HT306_DMA_BLOCK_FRAMES) {
        num_frames = HT306_DMA_BLOCK_FRAMES;
    }

    size_t total_samples = num_frames * HT306_NUM_CHANNELS;
    for (size_t i = 0U; i < total_samples; ++i) {
        g_core0_raw48k_i2s32[i] = (int32_t)((uint32_t)raw_6ch_pcm24[i] << 8U);
    }

    ht306_i2s_set_sample_rate(HT306_SAMPLE_RATE_48000);
    ht306_i2s_write_frames_6ch(g_core0_raw48k_i2s32, num_frames);
    ht306_control_set_status_led(HT306_STATUS_MODE_48K_GREEN);
}

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
int main(void) {
    /* 1. Inisialisasi pin kontrol HT306 (MUX GP8, REMOCON GP9, WS2812 GP16) */
    ht306_control_init();
    ht306_control_set_pc_mode(true);

    /* 2. Inisialisasi mesin PIO0 4-Lane I2S & DMA Ping-Pong */
    if (!ht306_i2s_engine_init(HT306_SAMPLE_RATE_96000)) {
        ht306_control_set_status_led(HT306_STATUS_ERROR_RED);
        while (true) {
            tight_loop_contents();
        }
    }

    /* 3. Inisialisasi TinyUSB Device Stack */
    tusb_init();

    /* 4. Jalankan Core 1 khusus untuk Lossless 96 kHz Unpacker di SRAM */
    multicore_launch_core1(ht306_core1_lossless_worker);

    /* 5. Loop utama Core 0: Melayani event USB & menguras FIFO Bulk dari Windows / JRiver */
    while (true) {
        ht306_usb_task();
    }

    return 0;
}
#endif
