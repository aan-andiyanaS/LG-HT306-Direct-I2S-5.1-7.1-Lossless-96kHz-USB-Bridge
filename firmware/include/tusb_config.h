#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_descriptors.h"

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

#define CFG_TUD_AUDIO               1U
#define CFG_TUD_CDC                 1U
#define CFG_TUD_VENDOR              0U
#define CFG_TUD_MSC                 0U
#define CFG_TUD_HID                 0U
#define CFG_TUD_MIDI                0U

/* Buffer FIFO Endpoint CDC (VS Code Serial Monitor) */
#define CFG_TUD_CDC_RX_BUFSIZE      256U
#define CFG_TUD_CDC_TX_BUFSIZE      512U
#define CFG_TUD_CDC_EP_BUFSIZE      64U

/* Konfigurasi UAC2 Desktop Speaker (Windows 10/11 Plug & Play) */
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN          TUD_AUDIO_SPEAKER_STEREO_FB_DESC_LEN
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_FORMAT_CORRECTION 0

#define CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE   96000U
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX     2U
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX 2U
#define CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX     16U

#define CFG_TUD_AUDIO_ENABLE_EP_OUT            1U
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP       1U

#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX     TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ  (4U * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX)

#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT          1U
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ       64U

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
