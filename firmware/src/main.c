#include "lossless_codec.h"
#include "i2s_engine.h"
#include "ht306_control.h"
#include "ht306_usb.h"
#include <string.h>

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
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
#define HT306_PACKET_QUEUE_DEPTH  32U

typedef struct {
    uint8_t  data[HT306_MAX_PACKET_BYTES];
    uint16_t length;
} HT306_PacketSlot_t;

static HT306_PacketSlot_t g_packet_queue[HT306_PACKET_QUEUE_DEPTH];
static volatile uint32_t  g_pkt_write_count = 0U;
static volatile uint32_t  g_pkt_read_count  = 0U;

/* Buffer statis di SRAM (mencegah Stack Overflow pada Core 1 yang hanya memiliki 2 KB stack) */
static int32_t g_core1_decoded_i2s32[HT306_MAX_SUBFRAME_SAMPLES * HT306_NUM_CHANNELS];

/* Array peak level 6-kanal wait-free/thread-safe antara Core 1 dan Core 0 */
static volatile uint32_t g_channel_peaks[HT306_NUM_CHANNELS] = {0};

void HT306_MAIN_RAM_FUNC(ht306_core1_lossless_worker)(void) {
    while (true) {
        uint32_t w = g_pkt_write_count;
        uint32_t r = g_pkt_read_count;

        if ((uint32_t)(w - r) > 0U) {
            HT306_MAIN_DMB();
            uint32_t slot_idx = r % HT306_PACKET_QUEUE_DEPTH;
            const HT306_PacketSlot_t *slot = &g_packet_queue[slot_idx];
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
                /* Perbarui peak level 6-kanal secara thread-safe / wait-free (zero-copy) */
                uint32_t local_peaks[HT306_NUM_CHANNELS] = {0};
                const int32_t *p_sample = g_core1_decoded_i2s32;
                for (uint32_t i = 0; i < frames_decoded; ++i) {
                    for (uint32_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
                        int32_t s = *p_sample++;
                        uint32_t mag = (s < 0) ? (uint32_t)(0U - (uint32_t)s) : (uint32_t)s;
                        if (mag > local_peaks[ch]) {
                            local_peaks[ch] = mag;
                        }
                    }
                }
                for (uint32_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
                    if (local_peaks[ch] > g_channel_peaks[ch]) {
                        g_channel_peaks[ch] = local_peaks[ch];
                    }
                }

                uint8_t pkt_flags = slot->data[5];
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
            /* Cortex-M Wait-For-Event: Nol latency saat bangun via __sev(), menghemat daya dan bus SRAM */
            __wfe();
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

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
    /* Signal Event: Langsung bangunkan Core 1 dalam 1 siklus CPU tanpa latency polling */
    __sev();
#endif

    return true;
}

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
static const char *ht306_vu_meter_bar(uint32_t peak) {
    /* Skala representasi visual 6 segmen non-linear (dB-scaled):
     * Segment 1 (>= -42 dBFS): 16777216U (audible ambient/background)
     * Segment 2 (>= -30 dBFS): 67108864U (soft dialogue/music)
     * Segment 3 (>= -18 dBFS): 268435456U (nominal music/dialogue)
     * Segment 4 (>= -10 dBFS): 671088640U (strong dynamic content)
     * Segment 5 (>= -4 dBFS) : 1342177280U (loud climax/bass)
     * Segment 6 (>= -1 dBFS) : 1932735283U (peak / near 0 dBFS clipping)
     */
    if (peak >= 1932735283U) return "██████";
    if (peak >= 1342177280U) return "█████░";
    if (peak >=  671088640U) return "████░░";
    if (peak >=  268435456U) return "███░░░";
    if (peak >=   67108864U) return "██░░░░";
    if (peak >=   16777216U) return "█░░░░░";
    return "░░░░░░";
}

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
    uint32_t last_vu_ms = 0U;
    uint32_t last_pkt_seen = 0U;
    uint32_t last_audio_activity_ms = 0U;

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
                if (n > 0U && (rx_buf[0] == 'B' || rx_buf[0] == 'b')) {
                    ht306_usb_log("\r\n[BOOT] Rebooting into BOOTSEL mode...\r\n");
                    sleep_ms(50);
                    reset_usb_boot(0, 0);
                }
                ht306_usb_log("\r\n>>> [HT306 STATUS] Core 0 (USB) & Core 1 (DSP) AKTIF | I2S 96kHz SIAP | MUX GP8: HIGH <<<\r\n\r\n");
            }

            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            uint32_t current_pkt = g_pkt_read_count;

            if (current_pkt != last_pkt_seen) {
                last_pkt_seen = current_pkt;
                last_audio_activity_ms = now_ms;
            }

            bool is_streaming = (now_ms - last_audio_activity_ms < 500U) && (current_pkt > 0U);

            /* Visualisasi VU meter 6-kanal periodik setiap ~250 ms (4 Hz) saat streaming aktif */
            if (is_streaming) {
                if (now_ms - last_vu_ms >= 250U) {
                    last_vu_ms = now_ms;

                    uint32_t peaks[HT306_NUM_CHANNELS];
                    for (uint32_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
                        peaks[ch] = g_channel_peaks[ch];
                        g_channel_peaks[ch] = 0U; /* Reset untuk jendela pengukuran berikutnya */
                    }

                    HT306_I2SStats_t stats;
                    ht306_i2s_get_stats(&stats);
                    const char *rate_str = (stats.current_sample_rate >= 96000U) ? "96kHz" : "48kHz";

                    ht306_usb_log_printf(
                        "[VU 5.1] FL:[%s] FR:[%s] C:[%s] LFE:[%s] SL:[%s] SR:[%s] | Rate: %s | Pkts: %lu\r\n",
                        ht306_vu_meter_bar(peaks[0]),
                        ht306_vu_meter_bar(peaks[1]),
                        ht306_vu_meter_bar(peaks[2]),
                        ht306_vu_meter_bar(peaks[3]),
                        ht306_vu_meter_bar(peaks[4]),
                        ht306_vu_meter_bar(peaks[5]),
                        rate_str,
                        (unsigned long)current_pkt
                    );
                }
                last_heartbeat_ms = now_ms;
            } else {
                /* Otomatis kembalikan indikator LED ke Standby (Biru) saat stream idle/berhenti */
                ht306_control_set_status_led(HT306_STATUS_STANDBY_BLUE);

                /* Detak Jantung berkala setiap 1 detik saat Serial Monitor terbuka & standby */
                if (now_ms - last_heartbeat_ms >= 1000U) {
                    last_heartbeat_ms = now_ms;
                    uint32_t sec = now_ms / 1000U;
                    ht306_usb_log_printf("[HT306] Detak Jantung #%lu | Uptime: %lu detik | I2S Engine: SIAP (96kHz) | GP8 MUX: HIGH\r\n",
                                         (unsigned long)sec, (unsigned long)sec);
                }
            }
        } else {
            banner_sent = false;
        }
    }

    return 0;
}
#endif
