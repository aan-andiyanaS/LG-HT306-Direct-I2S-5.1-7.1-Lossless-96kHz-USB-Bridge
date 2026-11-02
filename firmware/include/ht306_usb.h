#ifndef HT306_USB_H
#define HT306_USB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Memasukkan paket Lossless dari USB Endpoint ke antrian Core 1 Unpacker.
 */
bool ht306_usb_enqueue_lossless_packet(const uint8_t *usb_data, uint16_t len);

/**
 * Memproses aliran byte dari Endpoint Vendor USB (menyusun potongan 64-byte menjadi frame Lossless utuh).
 */
void ht306_usb_process_vendor_stream(const uint8_t *buf, uint32_t count);

/**
 * Menjalankan state machine TinyUSB dan menguras FIFO Endpoint Vendor secara langsung (Core 0).
 */
void ht306_usb_task(void);

/**
 * Mengirim pesan teks diagnostik ke Serial Monitor (CDC ACM / COM Port).
 */
void ht306_usb_log(const char *msg);
void ht306_usb_log_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* HT306_USB_H */
