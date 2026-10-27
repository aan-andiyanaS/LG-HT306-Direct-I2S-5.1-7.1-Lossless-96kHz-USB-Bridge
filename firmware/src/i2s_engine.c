#include "i2s_engine.h"
#include <string.h>

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "i2s_multi.pio.h"
#define HT306_I2S_RAM_FUNC(fn) __not_in_flash_func(fn)
#define HT306_MEMORY_BARRIER() __dmb()
#else
#define HT306_I2S_RAM_FUNC(fn) fn
#define HT306_MEMORY_BARRIER() ((void)0)
#endif

/**
 * Ring Buffer SPSC Wait-Free di SRAM (8 uint32_t per frame waktu).
 * Menggunakan counter monotonik `g_write_total_frames` (hanya ditulis oleh Producer)
 * dan `g_read_total_frames` (hanya ditulis oleh Consumer DMA ISR) sehingga
 * bebas 100% dari Race Condition Read-Modify-Write antara Core 1 dan DMA_IRQ_0.
 */
static uint32_t g_pio_ring_buffer[HT306_RING_BUFFER_FRAMES * HT306_I2S_WORDS_PER_FRAME];
static uint32_t g_silence_block[HT306_DMA_BLOCK_FRAMES * HT306_I2S_WORDS_PER_FRAME];

static volatile uint32_t g_write_total_frames = 0U;
static volatile uint32_t g_read_total_frames  = 0U;

static HT306_I2SStats_t g_i2s_stats = {
    .current_sample_rate = HT306_SAMPLE_RATE_48000,
    .buffered_frames = 0U,
    .underrun_count = 0U,
    .overrun_count = 0U,
    .blocks_transmitted = 0U
};

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
static int  g_dma_chan_a = -1;
static int  g_dma_chan_b = -1;
static PIO  g_pio_instance = pio0;
static uint g_pio_sm = 0U;
static uint g_pio_offset = 0U;
static bool g_pio_program_loaded = false;
#endif

static inline uint32_t spsc_get_buffered_frames(void) {
    uint32_t w = g_write_total_frames;
    uint32_t r = g_read_total_frames;
    return (w - r);
}

void HT306_I2S_RAM_FUNC(ht306_interleave_4lanes_to_pio_words)(
    uint32_t lane0_sample32,
    uint32_t lane1_sample32,
    uint32_t lane2_sample32,
    uint32_t lane3_sample32,
    uint32_t pio_words_out[4])
{
    for (uint32_t w = 0U; w < 4U; ++w) {
        uint32_t word_accum = 0U;
        uint32_t base_bit = 31U - (w * 8U);

        for (uint32_t step = 0U; step < 8U; ++step) {
            uint32_t bit_pos = base_bit - step;
            uint32_t nibble = (((lane0_sample32 >> bit_pos) & 1U) << 0U)
                            | (((lane1_sample32 >> bit_pos) & 1U) << 1U)
                            | (((lane2_sample32 >> bit_pos) & 1U) << 2U)
                            | (((lane3_sample32 >> bit_pos) & 1U) << 3U);
            word_accum |= (nibble << (step * 4U));
        }
        pio_words_out[w] = word_accum;
    }
}

size_t HT306_I2S_RAM_FUNC(ht306_i2s_write_frames_6ch)(
    const int32_t *samples_6ch_i2s32,
    size_t num_frames)
{
    if (samples_6ch_i2s32 == NULL || num_frames == 0U) {
        return 0U;
    }

    size_t written = 0U;
    uint32_t local_write = g_write_total_frames;

    for (size_t n = 0U; n < num_frames; ++n) {
        if ((uint32_t)(local_write - g_read_total_frames) >= HT306_RING_BUFFER_FRAMES) {
            g_i2s_stats.overrun_count++;
            break;
        }

        const int32_t *f = &samples_6ch_i2s32[n * 6U];
        uint32_t *dst = &g_pio_ring_buffer[(local_write % HT306_RING_BUFFER_FRAMES) * HT306_I2S_WORDS_PER_FRAME];

        // ponytail: pass channels directly [0]=FL, [1]=FR, [2]=C, [3]=LFE, [4]=SL, [5]=SR
        ht306_interleave_4lanes_to_pio_words((uint32_t)f[0], (uint32_t)f[4], (uint32_t)f[2], 0U, &dst[0]);
        ht306_interleave_4lanes_to_pio_words((uint32_t)f[1], (uint32_t)f[5], (uint32_t)f[3], 0U, &dst[4]);

        local_write++;
        written++;
    }

    HT306_MEMORY_BARRIER();
    g_write_total_frames = local_write;
    return written;
}

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
static void __not_in_flash_func(ht306_dma_irq_handler)(void) {
    int active_chan = -1;
    if (dma_channel_get_irq0_status((uint)g_dma_chan_a)) {
        dma_channel_acknowledge_irq0((uint)g_dma_chan_a);
        active_chan = g_dma_chan_a;
    } else if (dma_channel_get_irq0_status((uint)g_dma_chan_b)) {
        dma_channel_acknowledge_irq0((uint)g_dma_chan_b);
        active_chan = g_dma_chan_b;
    }

    if (active_chan < 0) {
        return;
    }

    g_i2s_stats.blocks_transmitted++;
    uint32_t local_read = g_read_total_frames;

    if ((uint32_t)(g_write_total_frames - local_read) >= HT306_DMA_BLOCK_FRAMES) {
        HT306_MEMORY_BARRIER();
        uint32_t ring_idx = local_read % HT306_RING_BUFFER_FRAMES;
        const uint32_t *src = &g_pio_ring_buffer[ring_idx * HT306_I2S_WORDS_PER_FRAME];
        dma_channel_set_read_addr((uint)active_chan, src, false);
        HT306_MEMORY_BARRIER();
        g_read_total_frames = local_read + HT306_DMA_BLOCK_FRAMES;
    } else {
        dma_channel_set_read_addr((uint)active_chan, g_silence_block, false);
        g_i2s_stats.underrun_count++;
    }
}
#endif

void ht306_i2s_set_sample_rate(HT306_SampleRate_t new_rate) {
    if (g_i2s_stats.current_sample_rate == (uint32_t)new_rate) {
        return;
    }
    g_i2s_stats.current_sample_rate = (uint32_t)new_rate;

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
    if (g_pio_program_loaded) {
        uint32_t sys_hz = clock_get_hz(clk_sys);
        float clkdiv = (float)sys_hz / ((float)new_rate * 128.0f);
        pio_sm_set_clkdiv(g_pio_instance, g_pio_sm, clkdiv);
    }
#endif
}

bool ht306_i2s_engine_init(HT306_SampleRate_t initial_rate) {
    memset(g_pio_ring_buffer, 0, sizeof(g_pio_ring_buffer));
    memset(g_silence_block, 0, sizeof(g_silence_block));
    g_write_total_frames = 0U;
    g_read_total_frames  = 0U;

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
    /* 1. Muat program assembly PIO `i2s_multi_4lane` ke memori instruksi PIO0 */
    if (!g_pio_program_loaded) {
        if (!pio_can_add_program(g_pio_instance, &i2s_multi_4lane_program)) {
            return false;
        }
        g_pio_offset = pio_add_program(g_pio_instance, &i2s_multi_4lane_program);
        g_pio_program_loaded = true;
    }

    /* 2. Inisialisasi GPIO GP2..GP7 pada Waveshare RP2350-Zero */
    for (uint pin = HT306_PIN_I2S_BCK; pin <= HT306_PIN_I2S_ADATA3; ++pin) {
        pio_gpio_init(g_pio_instance, pin);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
    }
    pio_sm_set_consecutive_pindirs(g_pio_instance, g_pio_sm, HT306_PIN_I2S_BCK, 6U, true);

    /* 3. Konfigurasi State Machine PIO0_SM0 (Side-set: GP2/GP3, OUT: GP4..GP7, Autopull 32-bit) */
    pio_sm_config sm_cfg = i2s_multi_4lane_program_get_default_config(g_pio_offset);
    sm_config_set_sideset_pins(&sm_cfg, HT306_PIN_I2S_BCK);
    sm_config_set_out_pins(&sm_cfg, HT306_PIN_I2S_ADATA0, 4U);
    sm_config_set_out_shift(&sm_cfg, true, true, 32U); /* Shift Right, Autopull threshold = 32 */
    sm_config_set_fifo_join(&sm_cfg, PIO_FIFO_JOIN_TX); /* Gabungkan FIFO RX+TX menjadi 8-word TX FIFO */

    uint32_t sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)sys_hz / ((float)initial_rate * 128.0f);
    sm_config_set_clkdiv(&sm_cfg, clkdiv);

    pio_sm_init(g_pio_instance, g_pio_sm, g_pio_offset, &sm_cfg);

    /* 4. Klaim & konfigurasi 2 kanal DMA untuk operasi Chained Ping-Pong */
    g_dma_chan_a = dma_claim_unused_channel(true);
    g_dma_chan_b = dma_claim_unused_channel(true);

    dma_channel_config cfg_a = dma_channel_get_default_config((uint)g_dma_chan_a);
    channel_config_set_transfer_data_size(&cfg_a, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg_a, true);
    channel_config_set_write_increment(&cfg_a, false);
    channel_config_set_dreq(&cfg_a, pio_get_dreq(g_pio_instance, g_pio_sm, true));
    channel_config_set_chain_to(&cfg_a, (uint)g_dma_chan_b);

    dma_channel_config cfg_b = dma_channel_get_default_config((uint)g_dma_chan_b);
    channel_config_set_transfer_data_size(&cfg_b, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg_b, true);
    channel_config_set_write_increment(&cfg_b, false);
    channel_config_set_dreq(&cfg_b, pio_get_dreq(g_pio_instance, g_pio_sm, true));
    channel_config_set_chain_to(&cfg_b, (uint)g_dma_chan_a);

    const uint32_t transfer_words = HT306_DMA_BLOCK_FRAMES * HT306_I2S_WORDS_PER_FRAME;

    dma_channel_configure(
        (uint)g_dma_chan_a, &cfg_a,
        &g_pio_instance->txf[g_pio_sm],
        g_silence_block, transfer_words, false
    );
    dma_channel_configure(
        (uint)g_dma_chan_b, &cfg_b,
        &g_pio_instance->txf[g_pio_sm],
        g_silence_block, transfer_words, false
    );

    dma_channel_set_irq0_enabled((uint)g_dma_chan_a, true);
    dma_channel_set_irq0_enabled((uint)g_dma_chan_b, true);
    irq_set_exclusive_handler(DMA_IRQ_0, ht306_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    /* 5. Aktifkan DMA dan State Machine PIO0_SM0 */
    dma_channel_start((uint)g_dma_chan_a);
    pio_sm_set_enabled(g_pio_instance, g_pio_sm, true);
#endif

    g_i2s_stats.current_sample_rate = (uint32_t)initial_rate;
    return true;
}

void ht306_i2s_get_stats(HT306_I2SStats_t *stats_out) {
    if (stats_out != NULL) {
        *stats_out = g_i2s_stats;
        stats_out->buffered_frames = spsc_get_buffered_frames();
    }
}
