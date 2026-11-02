#include "ht306_usb.h"
#include "lossless_codec.h"
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
#define HT306_USB_PID                 0x4306U
#define HT306_USB_BCD                 0x0200U

/* Endpoint CDC ACM (Serial Monitor / Heartbeat / Diagnostik) */
#define EPNUM_CDC_CMD                 0x81U
#define EPNUM_CDC_DATA_OUT            0x02U
#define EPNUM_CDC_DATA_IN             0x82U

/* Endpoint Vendor (High-Throughput Lossless 96kHz Pipe) */
#define EPNUM_VENDOR_OUT              0x03U
#define EPNUM_VENDOR_IN               0x83U

/**
 * Mesin Perakitan Paket Stream USB (Reassembler untuk potongan 64-Byte pada USB Bulk Endpoint).
 * Membaca `sync_word` (Offset 0..1 = 0xA55A) dan `packet_len` (Offset 2..3) langsung dari
 * struktur `HT306_LosslessHeader_t`.
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

        if (g_rx_accum_pos >= sizeof(g_rx_accum_buf)) {
            g_rx_accum_pos = 0U;
            g_rx_target_len = 0U;
            continue;
        }

        g_rx_accum_buf[g_rx_accum_pos++] = b;

        if (g_rx_accum_pos == 4U) {
            g_rx_target_len = (uint16_t)g_rx_accum_buf[2] | ((uint16_t)g_rx_accum_buf[3] << 8U);
            if (g_rx_target_len < min_pkt_len || g_rx_target_len > HT306_MAX_PACKET_BYTES) {
                /* Jika byte [2..3] ternyata adalah sync_word baru (0xA55A), pertahankan posisi 2 */
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

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_VENDOR,
    ITF_NUM_TOTAL
};

/* Composite Device Descriptor (MISC / IAD untuk CDC ACM + Vendor Bulk) */
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

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_VENDOR_DESC_LEN)

static const uint8_t g_desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 250),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_CMD, 8, EPNUM_CDC_DATA_OUT, EPNUM_CDC_DATA_IN, 64),
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 5, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, 64)
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return g_desc_configuration;
}

static const char * const g_string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },
    "LG HT306 Custom Digital Mod",
    "LG HT306 5.1 Surround Direct-I2S (RP2350-Zero)",
    "HT306-RP2350Z-96K",
    "HT306 Diagnostics Serial Monitor",
    "HT306 96kHz Lossless Direct Pipe"
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

void HT306_USB_RAM_FUNC(ht306_usb_task)(void) {
    tud_task();

    while (tud_vendor_available() > 0U) {
        uint8_t chunk[64];
        uint32_t n = tud_vendor_read(chunk, sizeof(chunk));
        if (n > 0U) {
            ht306_usb_process_vendor_stream(chunk, n);
        }
    }
}

void ht306_usb_log(const char *msg) {
    if (msg == NULL) return;
    if (tud_cdc_connected()) {
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
    if (tud_cdc_connected()) {
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
