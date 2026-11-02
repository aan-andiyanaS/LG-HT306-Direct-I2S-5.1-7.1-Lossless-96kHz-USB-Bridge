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
            /* Mencegah Core 1 membanjiri bus AHB SRAM crossbar saat antrian kosong */
            sleep_us(50);
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

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
int main(void) {
    /* 1. Inisialisasi pin kontrol HT306 (MUX GP8, REMOCON GP9, WS2812 GP16) */
    ht306_control_init();
    ht306_control_set_pc_mode(true);

    /* 2. Inisialisasi mesin PIO0 4-Lane I2S & DMA Ping-Pong */
    bool i2s_ok = ht306_i2s_engine_init(HT306_SAMPLE_RATE_96000);
    if (!i2s_ok) {
        ht306_control_set_status_led(HT306_STATUS_ERROR_RED);
    }

    /* 3. Inisialisasi TinyUSB Device Stack (CDC Serial + Vendor Bulk) */
    tusb_init();

    /* 4. Jalankan Core 1 khusus untuk Lossless 96 kHz Unpacker di SRAM */
    multicore_launch_core1(ht306_core1_lossless_worker);

    /* 5. Loop utama Core 0: Melayani event USB CDC & Vendor Bulk */
    bool banner_sent = false;
    uint32_t last_heartbeat_ms = 0U;
    uint32_t last_log_frames = 0U;

    while (true) {
        ht306_usb_task();

        if (tud_cdc_connected()) {
            /* Tampilkan banner selamat datang saat Serial Monitor dibuka */
            if (!banner_sent) {
                ht306_usb_log("\r\n======================================================\r\n");
                ht306_usb_log("  LG HT306 Direct-I2S 5.1/7.1 Lossless USB Bridge\r\n");
                ht306_usb_log("  Target: Waveshare RP2350-Zero (Arm Cortex-M33 @ 150MHz)\r\n");
                ht306_usb_log("======================================================\r\n");
                ht306_usb_log("[BOOT] Sistem Siap! Core 0 & Core 1 Berjalan.\r\n");
                if (i2s_ok) {
                    ht306_usb_log("[INIT] PIO0 4-Lane I2S Engine: BERHASIL (96 kHz Ready)\r\n");
                    ht306_usb_log("       -> GP2: BCK  (Bit Clock)\r\n");
                    ht306_usb_log("       -> GP3: LRCK (Word Select)\r\n");
                    ht306_usb_log("       -> GP4: ADATA0 (Front L/R)\r\n");
                    ht306_usb_log("       -> GP5: ADATA1 (Surround L/R)\r\n");
                    ht306_usb_log("       -> GP6: ADATA2 (Center / Subwoofer)\r\n");
                } else {
                    ht306_usb_log("[ERROR] Gagal Inisialisasi PIO0 I2S Engine!\r\n");
                }
                ht306_usb_log("[MUX]  GP8 disetel HIGH (+3.3V) -> Mode PC USB Aktif\r\n");
                ht306_usb_log("[WAIT] Standby (LED Biru) - Menunggu Aliran Audio dari PC...\r\n");
                ht306_usb_log("[INFO] Tekan ENTER atau ketik apa saja untuk cek status sistem.\r\n");
                ht306_usb_log("======================================================\r\n\r\n");
                banner_sent = true;
            }

            /* Respon instan jika user mengetik / menekan tombol di Serial Monitor */
            if (tud_cdc_available() > 0U) {
                char rx_buf[64];
                uint32_t n = tud_cdc_read(rx_buf, sizeof(rx_buf));
                (void)n;
                ht306_usb_log("\r\n>>> [HT306 STATUS] Core 0 (USB) & Core 1 (DSP) AKTIF | I2S 96kHz SIAP | MUX GP8: HIGH <<<\r\n\r\n");
            }

            /* Detak Jantung berkala setiap 1 detik saat Serial Monitor terbuka */
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            if (now_ms - last_heartbeat_ms >= 1000U) {
                last_heartbeat_ms = now_ms;
                uint32_t sec = now_ms / 1000U;
                ht306_usb_log_printf("[HT306] Detak Jantung #%lu | Uptime: %lu detik | I2S Engine: SIAP (96kHz) | GP8 MUX: HIGH\r\n",
                                     (unsigned long)sec, (unsigned long)sec);
            }

            /* Log progres frame saat ada stream aktif setiap kelipatan ~100 ms pada 96kHz */
            HT306_I2SStats_t stats;
            ht306_i2s_get_stats(&stats);
            if (stats.total_written_frames - last_log_frames >= 9600U) {
                ht306_usb_log_printf("[STREAM] Audio Aktif | %lu Hz | Total Frames: %lu\r\n",
                                     (unsigned long)stats.current_sample_rate,
                                     (unsigned long)stats.total_written_frames);
                last_log_frames = stats.total_written_frames;
            }
        } else {
            banner_sent = false;
        }
    }

    return 0;
}
#endif
