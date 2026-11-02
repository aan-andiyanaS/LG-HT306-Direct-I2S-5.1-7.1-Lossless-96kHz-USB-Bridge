#include "ht306_control.h"

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#endif

// Inisialisasi sentinel (0xFF) agar pemanggilan pertama saat boot selalu menyalakan LED fisik
static HT306_StatusLed_t g_current_led = (HT306_StatusLed_t)0xFF;

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
static inline void delay_cycles(uint32_t count) {
    __asm volatile (
        "1: subs %0, %0, #1 \n"
        "   bne 1b          \n"
        : "+r" (count)
        :
        : "cc"
    );
}

/**
 * Pengirim sinyal 800 kHz protokol WS2812 (GRB format) untuk LED RGB GP16 RP2350-Zero.
 * Menggunakan direct register atomic SIO dengan pemblokiran interupsi singkat (30 us).
 */
static void ws2812_write_grb(uint8_t g, uint8_t r, uint8_t b) {
    uint32_t data = ((uint32_t)g << 16U) | ((uint32_t)r << 8U) | (uint32_t)b;
    const uint32_t pin_mask = 1U << HT306_PIN_WS2812_LED;

    uint32_t ints = save_and_disable_interrupts();

    for (int bit = 23; bit >= 0; --bit) {
        if ((data >> bit) & 1U) {
            // Bit 1: ~700 ns HIGH, ~600 ns LOW
            sio_hw->gpio_set = pin_mask;
            delay_cycles(35); // ~700 ns @ 150MHz
            sio_hw->gpio_clr = pin_mask;
            delay_cycles(30); // ~600 ns @ 150MHz
        } else {
            // Bit 0: ~350 ns HIGH, ~800 ns LOW
            sio_hw->gpio_set = pin_mask;
            delay_cycles(17); // ~350 ns @ 150MHz
            sio_hw->gpio_clr = pin_mask;
            delay_cycles(40); // ~800 ns @ 150MHz
        }
    }

    restore_interrupts(ints);
}
#endif

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
    if (g_current_led == status) {
        return;
    }
    g_current_led = status;
#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
    switch (status) {
        case HT306_STATUS_STANDBY_BLUE:
            ws2812_write_grb(0U, 0U, 32U);   // Biru
            break;
        case HT306_STATUS_MODE_48K_GREEN:
            ws2812_write_grb(32U, 0U, 0U);   // Hijau
            break;
        case HT306_STATUS_MODE_96K_PURPLE:
            ws2812_write_grb(0U, 32U, 32U);  // Ungu / Magenta
            break;
        case HT306_STATUS_ERROR_RED:
            ws2812_write_grb(0U, 32U, 0U);   // Merah
            break;
        default:
            break;
    }
#endif
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
