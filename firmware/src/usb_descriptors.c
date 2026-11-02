#include "ht306_usb.h"
#include "lossless_codec.h"
#include "i2s_engine.h"
#include "ht306_control.h"
#include "usb_descriptors.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
#include "tusb.h"
#define HT306_USB_RAM_FUNC(fn) __not_in_flash_func(fn)
#else
#define HT306_USB_RAM_FUNC(fn) fn
#endif

#define HT306_USB_VID                 0x2E8AU
#define HT306_USB_PID                 0x430AU  /* PID baru agar Windows menginisialisasi driver UAC2 segar */
#define HT306_USB_BCD                 0x0200U

/* Alokasi Endpoint USB (Audio Adaptive Out + CDC ACM Serial) */
#define EPNUM_AUDIO_OUT               0x01U
#define EPNUM_CDC_CMD                 0x82U
#define EPNUM_CDC_DATA_OUT            0x03U
#define EPNUM_CDC_DATA_IN             0x83U

/* Status Audio UAC2 */
static const uint32_t g_sample_rates[] = {44100, 48000, 96000};
#define N_SAMPLE_RATES (sizeof(g_sample_rates) / sizeof(g_sample_rates[0]))
static uint32_t g_current_audio_sample_rate = 96000;
static int8_t   g_audio_mute[3]   = {0, 0, 0};
static int16_t  g_audio_volume[3] = {0, 0, 0};

/**
 * Mesin Perakitan Paket Stream USB (Reassembler untuk potongan 64-Byte pada USB Bulk Endpoint).
 * Dipertahankan untuk kompatibilitas suite verifikasi paket.
 */
static uint8_t  g_rx_accum_buf[HT306_MAX_PACKET_BYTES];
static uint16_t g_rx_accum_pos  = 0U;
static uint16_t g_rx_target_len = 0U;

void HT306_USB_RAM_FUNC(ht306_usb_process_vendor_stream)(const uint8_t *buf, uint32_t count) {
    const uint16_t min_pkt_len = (uint16_t)(sizeof(HT306_LosslessHeader_t) + (HT306_NUM_CHANNELS * 3U));

    for (uint32_t i = 0U; i < count; ++i) {
        uint8_t b = buf[i];

        if (g_rx_accum_pos == 0U) {
            if (b == (uint8_t)(HT306_SYNC_WORD & 0xFFU)) {
                g_rx_accum_buf[g_rx_accum_pos++] = b;
            }
            continue;
        }

        if (g_rx_accum_pos == 1U) {
            if (b == (uint8_t)(HT306_SYNC_WORD >> 8U)) {
                g_rx_accum_buf[g_rx_accum_pos++] = b;
            } else if (b == (uint8_t)(HT306_SYNC_WORD & 0xFFU)) {
                g_rx_accum_buf[0] = b;
                g_rx_accum_pos = 1U;
            } else {
                g_rx_accum_pos = 0U;
            }
            continue;
        }

        g_rx_accum_buf[g_rx_accum_pos++] = b;

        if (g_rx_accum_pos == 4U) {
            g_rx_target_len = (uint16_t)g_rx_accum_buf[2] | ((uint16_t)g_rx_accum_buf[3] << 8U);
            if (g_rx_target_len < min_pkt_len || g_rx_target_len > HT306_MAX_PACKET_BYTES) {
                g_rx_accum_pos  = (g_rx_target_len == HT306_SYNC_WORD) ? 2U : 0U;
                g_rx_target_len = 0U;
            }
            continue;
        }

        if (g_rx_accum_pos >= 4U && g_rx_accum_pos == g_rx_target_len) {
            (void)ht306_usb_enqueue_lossless_packet(g_rx_accum_buf, g_rx_target_len);
            g_rx_accum_pos  = 0U;
            g_rx_target_len = 0U;
        }
    }
}

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)

/* Composite Device Descriptor (MISC / IAD untuk UAC2 Speaker + CDC ACM) */
static const tusb_desc_device_t g_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = HT306_USB_BCD,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = HT306_USB_VID,
    .idProduct          = HT306_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&g_desc_device;
}

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_AUDIO_SPEAKER_STEREO_ADAPTIVE_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t g_desc_configuration[] = {
    /* Konfigurasi Utama */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),

    /* UAC2 Adaptive Desktop Speaker (Tanpa Feedback Endpoint) */
    TUD_AUDIO_SPEAKER_STEREO_ADAPTIVE_DESCRIPTOR(
        ITF_NUM_AUDIO_CONTROL,
        5,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX,
        CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX,
        EPNUM_AUDIO_OUT,
        CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX
    ),

    /* CDC ACM Serial Monitor */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_CMD, 8, EPNUM_CDC_DATA_OUT, EPNUM_CDC_DATA_IN, 64)
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return g_desc_configuration;
}

static const char * const g_string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },                            /* 0: English (0x0409) */
    "LG Electronics / Custom Digital Mod",                   /* 1: Manufacturer */
    "LG HT306 Home Theater (RP2350-Zero)",                   /* 2: Product */
    "HT306-RP2350Z-001",                                     /* 3: Serial Number */
    "HT306 Diagnostics Serial Monitor",                      /* 4: CDC Serial Port (COM4) */
    "Speakers (LG HT306 Home Theater)"                       /* 5: Audio Endpoint Speaker Name */
};

static uint16_t g_desc_str[48];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0U) {
        memcpy(&g_desc_str[1], g_string_desc_arr[0], 2U);
        chr_count = 1U;
    } else {
        if (index >= (sizeof(g_string_desc_arr) / sizeof(g_string_desc_arr[0]))) {
            return NULL;
        }
        const char *str = g_string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 47U) {
            chr_count = 47U;
        }
        for (uint8_t i = 0U; i < chr_count; ++i) {
            g_desc_str[1U + i] = (uint16_t)str[i];
        }
    }

    g_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8U) | (2U * chr_count + 2U));
    return g_desc_str;
}

/* ====================================================================
 * UAC2 Callbacks untuk Kontrol Jam & Volume Windows
 * ==================================================================== */

static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request) {
    TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);

    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
        if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_4_t curf = { (int32_t)tu_htole32(g_current_audio_sample_rate) };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
        } else if (request->bRequest == AUDIO_CS_REQ_RANGE) {
            audio_control_range_4_n_t(N_SAMPLE_RATES) rangef = {
                .wNumSubRanges = tu_htole16(N_SAMPLE_RATES)
            };
            for (uint8_t i = 0; i < N_SAMPLE_RATES; ++i) {
                rangef.subrange[i].bMin = (int32_t)tu_htole32(g_sample_rates[i]);
                rangef.subrange[i].bMax = (int32_t)tu_htole32(g_sample_rates[i]);
                rangef.subrange[i].bRes = 0;
            }
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
        }
    } else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID && request->bRequest == AUDIO_CS_REQ_CUR) {
        audio_control_cur_1_t cur_valid = { .bCur = 1 };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
    }
    return false;
}

static bool tud_audio_clock_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf) {
    (void)rhport;
    TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
    TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
        TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));
        uint32_t new_rate = (uint32_t)((audio_control_cur_4_t const *)buf)->bCur;
        g_current_audio_sample_rate = new_rate;
        ht306_i2s_set_sample_rate(new_rate >= 88200U ? HT306_SAMPLE_RATE_96000 : HT306_SAMPLE_RATE_48000);
        ht306_control_set_status_led(new_rate >= 88200U ? HT306_STATUS_MODE_96K_PURPLE : HT306_STATUS_MODE_48K_GREEN);
        return true;
    }
    return false;
}

static bool tud_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request) {
    TU_ASSERT(request->bEntityID == UAC2_ENTITY_FEATURE_UNIT);

    if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR) {
        audio_control_cur_1_t mute1 = { .bCur = g_audio_mute[request->bChannelNumber] };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
    } else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
        if (request->bRequest == AUDIO_CS_REQ_RANGE) {
            audio_control_range_2_n_t(1) range_vol = {
                .wNumSubRanges = tu_htole16(1),
                .subrange[0] = {
                    .bMin = tu_htole16(-12800), /* -50 dB */
                    .bMax = tu_htole16(0),       /* 0 dB */
                    .bRes = tu_htole16(256)      /* 1 dB */
                }
            };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
        } else if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_2_t cur_vol = { .bCur = tu_htole16(g_audio_volume[request->bChannelNumber]) };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
        }
    }
    return false;
}

static bool tud_audio_feature_unit_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf) {
    (void)rhport;
    TU_ASSERT(request->bEntityID == UAC2_ENTITY_FEATURE_UNIT);
    TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

    if (request->bControlSelector == AUDIO_FU_CTRL_MUTE) {
        TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));
        g_audio_mute[request->bChannelNumber] = ((audio_control_cur_1_t const *)buf)->bCur;
        return true;
    } else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
        TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));
        g_audio_volume[request->bChannelNumber] = ((audio_control_cur_2_t const *)buf)->bCur;
        return true;
    }
    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    if (request->bEntityID == UAC2_ENTITY_CLOCK)
        return tud_audio_clock_get_request(rhport, request);
    if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT)
        return tud_audio_feature_unit_get_request(rhport, request);
    return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
    audio_control_request_t const *request = (audio_control_request_t const *)p_request;
    if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT)
        return tud_audio_feature_unit_set_request(rhport, request, buf);
    if (request->bEntityID == UAC2_ENTITY_CLOCK)
        return tud_audio_clock_set_request(rhport, request, buf);
    return false;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const * p_request) {
    (void)rhport; (void)p_request;
    return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request) {
    (void)rhport; (void)p_request;
    return true;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff) {
    (void)rhport; (void)p_request; (void)pBuff; return false;
}
bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request) {
    (void)rhport; (void)p_request; return false;
}
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff) {
    (void)rhport; (void)p_request; (void)pBuff; return false;
}
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const * p_request) {
    (void)rhport; (void)p_request; return false;
}

/* ====================================================================
 * Loop Utama USB & Audio Streaming Task
 * ==================================================================== */

void HT306_USB_RAM_FUNC(ht306_usb_task)(void) {
    tud_task();

    /* Mengalirkan sampel audio PCM dari Windows UAC2 ke Engine PIO0 I2S */
    uint32_t avail = tud_audio_available();
    if (avail >= 4U) {
        int16_t pcm16[64 * 2];
        uint32_t bytes_to_read = avail > sizeof(pcm16) ? (uint32_t)sizeof(pcm16) : avail;
        bytes_to_read &= ~3U; /* Align 4 byte (2 channel * 2 byte per sampel) */

        uint32_t n = tud_audio_read(pcm16, (uint16_t)bytes_to_read);
        if (n >= 4U) {
            uint32_t frames = n / 4U;
            int32_t samples_6ch[64 * 6];

            bool is_mute = (g_audio_mute[0] != 0 || g_audio_mute[1] != 0 || g_audio_mute[2] != 0);

            for (uint32_t i = 0U; i < frames; ++i) {
                int32_t l = is_mute ? 0 : ((int32_t)pcm16[i * 2U + 0U] << 16U);
                int32_t r = is_mute ? 0 : ((int32_t)pcm16[i * 2U + 1U] << 16U);

                /* Saluran I2S 6-Channel: [0]=FL, [1]=FR, [2]=C, [3]=LFE, [4]=SL, [5]=SR */
                samples_6ch[i * 6U + 0U] = l;
                samples_6ch[i * 6U + 1U] = r;
                samples_6ch[i * 6U + 2U] = 0;
                samples_6ch[i * 6U + 3U] = 0;
                samples_6ch[i * 6U + 4U] = 0;
                samples_6ch[i * 6U + 5U] = 0;
            }

            ht306_i2s_write_frames_6ch(samples_6ch, frames);
        }
    }
}

void ht306_usb_log(const char *msg) {
    if (msg == NULL) return;
    if (tud_ready()) {
        uint32_t len = (uint32_t)strlen(msg);
        uint32_t pos = 0U;
        while (pos < len) {
            uint32_t avail = tud_cdc_write_available();
            if (avail == 0U) break;
            uint32_t chunk = (len - pos < avail) ? (len - pos) : avail;
            uint32_t written = tud_cdc_write(msg + pos, chunk);
            if (written == 0U) break;
            pos += written;
            tud_cdc_write_flush();
        }
    }
}

void ht306_usb_log_printf(const char *fmt, ...) {
    if (fmt == NULL) return;
    if (tud_ready()) {
        char buf[160];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n > 0) {
            ht306_usb_log(buf);
        }
    }
}

#else

void ht306_usb_task(void) {}
void ht306_usb_log(const char *msg) { (void)msg; }
void ht306_usb_log_printf(const char *fmt, ...) { (void)fmt; }

#endif
