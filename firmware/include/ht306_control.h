#ifndef HT306_CONTROL_H
#define HT306_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pin GPIO Kontrol pada Waveshare RP2350-Zero
 */
#define HT306_PIN_MUX_SELECT      8U   /* GP8 : LOW = Mode DVD Bawaan, HIGH = Mode PC USB */
#define HT306_PIN_REMOCON_TX      9U   /* GP9 : Injeksi sinyal Remote LG (Open-Drain Active-Low) */
#define HT306_PIN_WS2812_LED      16U  /* GP16: Onboard RGB LED pada Waveshare RP2350-Zero */

/**
 * Status Indikator Warna LED RGB (GP16) pada Waveshare RP2350-Zero
 */
typedef enum {
    HT306_STATUS_STANDBY_BLUE = 0,     /* Biru  : Menunggu koneksi/stream dari PC */
    HT306_STATUS_MODE_48K_GREEN,       /* Hijau : Streaming 5.1 Standar (44.1 / 48 kHz) */
    HT306_STATUS_MODE_96K_PURPLE,      /* Ungu  : Streaming 5.1 Hi-Res Lossless (96 kHz) */
    HT306_STATUS_ERROR_RED             /* Merah : CRC Error / Buffer Underrun / Muted */
} HT306_StatusLed_t;

/**
 * Kode Perintah Remote Asli LG HT306 (Protokol NEC, Address = 0x2C)
 */
typedef enum {
    HT306_CMD_POWER_TOGGLE  = 0x2CB649U,
    HT306_CMD_VOLUME_UP     = 0x2CC43BU,
    HT306_CMD_VOLUME_DOWN   = 0x2CC53AU,
    HT306_CMD_MUTE_TOGGLE   = 0x2CC936U,
    HT306_CMD_EQ_SOUND_MODE = 0x2CD02FU, /* Mengganti Bass Blast / Natural / Bypass di PS9850 */
    HT306_CMD_FUNCTION_SRC  = 0x2CA857U
} HT306_RemoteCmd_t;

/**
 * Menginisialisasi pin kontrol MUX (GP8), REMOCON (GP9), dan LED RGB (GP16).
 */
void ht306_control_init(void);

/**
 * Mengaktifkan atau menonaktifkan jalur Multiplexer I2S (74LVC157).
 * @param pc_mode_active  true = arahkan I2S RP2350-Zero ke PS9850; false = gunakan DVD/USB asli HT306.
 */
void ht306_control_set_pc_mode(bool pc_mode_active);

/**
 * Mengatur warna lampu indikator WS2812 RGB pada GP16 Waveshare RP2350-Zero.
 */
void ht306_control_set_status_led(HT306_StatusLed_t status);

/**
 * Menyuntikkan pulsa digital protokol NEC langsung ke jalur REMOCON internal LG HT306
 * (tanpa modulasi carrier 38 kHz karena masuk setelah sensor IR penerima).
 */
void ht306_control_send_remote_cmd(HT306_RemoteCmd_t cmd_code);

#ifdef __cplusplus
}
#endif

#endif /* HT306_CONTROL_H */
