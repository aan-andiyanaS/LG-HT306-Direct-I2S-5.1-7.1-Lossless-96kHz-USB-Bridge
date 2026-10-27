#include "ht306_control.h"

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#endif

static HT306_StatusLed_t g_current_led = HT306_STATUS_STANDBY_BLUE;

void ht306_control_init(void) {
#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
    gpio_init(HT306_PIN_MUX_SELECT);
    gpio_set_dir(HT306_PIN_MUX_SELECT, GPIO_OUT);
    gpio_put(HT306_PIN_MUX_SELECT, 1);

    // ponytail: open-drain emulation (idle = input high-Z so stock IR remote still works)
    gpio_init(HT306_PIN_REMOCON_TX);
    gpio_put(HT306_PIN_REMOCON_TX, 0);
    gpio_set_dir(HT306_PIN_REMOCON_TX, GPIO_IN);

    gpio_init(HT306_PIN_WS2812_LED);
    gpio_set_dir(HT306_PIN_WS2812_LED, GPIO_OUT);
    gpio_put(HT306_PIN_WS2812_LED, 0);
#endif
    ht306_control_set_status_led(HT306_STATUS_STANDBY_BLUE);
}

void ht306_control_set_pc_mode(bool pc_mode_active) {
#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
    gpio_put(HT306_PIN_MUX_SELECT, pc_mode_active ? 1 : 0);
#else
    (void)pc_mode_active;
#endif
}

void ht306_control_set_status_led(HT306_StatusLed_t status) {
    g_current_led = status;
    (void)g_current_led;
}

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
// ponytail: single pulse helper replaces separate pull_low / release_high functions
static inline void remocon_pulse(uint32_t low_us, uint32_t high_us) {
    gpio_set_dir(HT306_PIN_REMOCON_TX, GPIO_OUT);
    sleep_us(low_us);
    gpio_set_dir(HT306_PIN_REMOCON_TX, GPIO_IN);
    sleep_us(high_us);
}
#endif

void ht306_control_send_remote_cmd(HT306_RemoteCmd_t cmd_code) {
#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
    uint8_t addr = (uint8_t)(cmd_code >> 16U);
    uint8_t cmd  = (uint8_t)(cmd_code >> 8U);
    uint32_t frame = (uint32_t)addr | ((uint32_t)(uint8_t)~addr << 8U)
                   | ((uint32_t)cmd << 16U) | ((uint32_t)(uint8_t)~cmd << 24U);

    remocon_pulse(9000U, 4500U);
    for (uint8_t b = 0U; b < 32U; ++b) {
        remocon_pulse(560U, ((frame >> b) & 1U) ? 1690U : 560U);
    }
    remocon_pulse(560U, 1000U);
#else
    (void)cmd_code;
#endif
}
