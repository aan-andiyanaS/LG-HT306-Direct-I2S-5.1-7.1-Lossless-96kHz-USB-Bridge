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
 * Menerima sampel PCM 24-bit 6-channel mentah (Mode 48 kHz Raw) dan mengirim ke Ring Buffer I2S.
 */
void ht306_usb_receive_raw_48k_pcm24(const int32_t *raw_6ch_pcm24, size_t num_frames);

/**
 * Memproses aliran byte dari Endpoint Vendor USB (menyusun potongan 64-byte menjadi frame Lossless utuh).
 */
void ht306_usb_process_vendor_stream(const uint8_t *buf, uint32_t count);

/**
 * Menjalankan state machine TinyUSB dan menguras FIFO Endpoint Vendor secara langsung (Core 0).
 */
void ht306_usb_task(void);

#ifdef __cplusplus
}
#endif

#endif /* HT306_USB_H */
