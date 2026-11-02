#include "ht306_usb.h"
#include "usb_descriptors.h"
#include "lossless_codec.h"
#include "i2s_engine.h"
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
#define HT306_USB_PID                 0x4308U   /* Unique PID for UAC2 Speaker + CDC ACM */
#define HT306_USB_BCD                 0x0200U

/* Isochronous Audio Endpoints (UAC2 Desktop Speaker) */
#define EPNUM_AUDIO_OUT               0x01U
#define EPNUM_AUDIO_FB                0x81U

/* CDC ACM Endpoints (Serial Monitor) */
#define EPNUM_CDC_CMD                 0x82U
#define EPNUM_CDC_DATA_OUT            0x02U
#define EPNUM_CDC_DATA_IN             0x83U

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)

/* Composite Device Descriptor (MISC / IAD untuk UAC2 Speaker + CDC ACM) */
static const tusb_desc_device_t g_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
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

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_AUDIO_SPEAKER_STEREO_FB_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t g_desc_configuration[] = {
    /* 1. Configuration Descriptor */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),

    /* 2. UAC2 Desktop Speaker (Interface 0: AC, Interface 1: AS) */
    TUD_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(
        ITF_NUM_AUDIO_CONTROL, 4,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX,
        EPNUM_AUDIO_OUT, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
        EPNUM_AUDIO_FB, 4
    ),

    /* 3. CDC ACM Serial Monitor (Interface 2: CMD, Interface 3: DATA) */
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC, 5,
        EPNUM_CDC_CMD, 8,
        EPNUM_CDC_DATA_OUT, EPNUM_CDC_DATA_IN, 64
    )
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return g_desc_configuration;
}

static const char * const g_string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },                            /* 0: English (0x0409) */
    "LG HT306 Custom Digital Mod",                            /* 1: Manufacturer */
    "LG HT306 Direct-I2S Audio Bridge",                       /* 2: Product */
    "HT306-RP2350Z-UAC2",                                     /* 3: Serial */
    "Speakers (LG HT306 Home Theater)",                       /* 4: Audio Device Name */
    "HT306 Diagnostics Serial Monitor"                        /* 5: CDC Serial Monitor */
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
 * Implementasi Callback USB Audio Class 2.0 (UAC2 Speaker)
 * ==================================================================== */

static const uint32_t g_audio_sample_rates[] = {44100, 48000, 88200, 96000};
#define N_AUDIO_SAMPLE_RATES TU_ARRAY_SIZE(g_audio_sample_rates)
static uint32_t g_current_audio_sample_rate = 96000;

static int8_t  g_audio_mute[3] = {0, 0, 0};
static int16_t g_audio_volume[3] = {0, 0, 0};

static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request) {
    TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
    if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
        if (request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_4_t curf = { (int32_t)tu_htole32(g_current_audio_sample_rate) };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
        } else if (request->bRequest == AUDIO_CS_REQ_RANGE) {
            audio_control_range_4_n_t(N_AUDIO_SAMPLE_RATES) rangef = {
                .wNumSubRanges = tu_htole16(N_AUDIO_SAMPLE_RATES)
            };
            for (uint8_t i = 0; i < N_AUDIO_SAMPLE_RATES; i++) {
                rangef.subrange[i].bMin = (int32_t)g_audio_sample_rates[i];
                rangef.subrange[i].bMax = (int32_t)g_audio_sample_rates[i];
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
        g_current_audio_sample_rate = (uint32_t)((audio_control_cur_4_t const *)buf)->bCur;
        ht306_i2s_set_sample_rate(g_current_audio_sample_rate >= 88200 ? HT306_SAMPLE_RATE_96000 : HT306_SAMPLE_RATE_48000);
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
                .subrange[0] = { .bMin = tu_htole16(-12800), tu_htole16(0), tu_htole16(256) }
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

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t* feedback_param) {
    (void)func_id; (void)alt_itf;
    feedback_param->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
    feedback_param->sample_freq = g_current_audio_sample_rate;
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

        uint32_t n = tud_audio_read(pcm16, bytes_to_read);
        if (n >= 4U) {
            uint32_t frames = n / 4U;
            int32_t samples_6ch[64 * 6];

            for (uint32_t i = 0U; i < frames; ++i) {
                int32_t l = (int32_t)pcm16[i * 2U + 0U] << 16U;
                int32_t r = (int32_t)pcm16[i * 2U + 1U] << 16U;

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
