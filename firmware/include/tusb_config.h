#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Konfigurasi TinyUSB untuk Waveshare RP2350-Zero (LG HT306 5.1 USB Bridge)
 * ponytail: Hanya aktifkan CFG_TUD_VENDOR (menghindari #error CFG_TUD_AUDIO_FUNC_1_DESC_LEN
 * pada TinyUSB audio_device.h sekaligus menghemat >3 KB SRAM).
 */
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU                OPT_MCU_RP2040
#endif

#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE
#define CFG_TUSB_OS                 OPT_OS_PICO

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE      64U

#define CFG_TUD_VENDOR              1U
#define CFG_TUD_AUDIO               0U
#define CFG_TUD_CDC                 0U
#define CFG_TUD_MSC                 0U
#define CFG_TUD_HID                 0U

/* Buffer FIFO Endpoint Vendor (Mode 96 kHz Lossless & 48 kHz PCM) */
#define CFG_TUD_VENDOR_RX_BUFSIZE   1024U
#define CFG_TUD_VENDOR_TX_BUFSIZE   64U
#define CFG_TUD_VENDOR_EPSIZE       64U

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
