#include "lossless_codec.h"
#include <string.h>

#if defined(PICO_BUILD) || defined(PICO_RP2350) || defined(PICO_RP2040)
#include "pico.h"
#define HT306_RAM_FUNC(fn) __not_in_flash_func(fn)
#else
#define HT306_RAM_FUNC(fn) fn
#endif

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    uint64_t acc;
    uint8_t  bits;
    bool     overflow;
} BitWriter_t;

static inline void bw_put(BitWriter_t *bw, uint32_t val, uint8_t nbits) {
    uint64_t mask = (nbits >= 32U) ? 0xFFFFFFFFULL : ((1ULL << nbits) - 1ULL);
    bw->acc |= ((uint64_t)val & mask) << bw->bits;
    bw->bits = (uint8_t)(bw->bits + nbits);
    while (bw->bits >= 8U) {
        if (bw->pos < bw->cap) {
            bw->buf[bw->pos++] = (uint8_t)(bw->acc & 0xFFULL);
        } else {
            bw->overflow = true;
        }
        bw->acc >>= 8U;
        bw->bits = (uint8_t)(bw->bits - 8U);
    }
}

static inline size_t bw_flush(BitWriter_t *bw) {
    if (bw->bits > 0U) {
        if (bw->pos < bw->cap) {
            bw->buf[bw->pos++] = (uint8_t)(bw->acc & 0xFFULL);
        } else {
            bw->overflow = true;
        }
        bw->acc = 0ULL;
        bw->bits = 0U;
    }
    return bw->pos;
}

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         pos;
    uint64_t       acc;
    uint8_t        avail;
    bool           underflow;
} BitReader_t;

static inline uint32_t br_get(BitReader_t *br, uint8_t nbits) {
    if (nbits == 0U) {
        return 0U;
    }
    while (br->avail < nbits) {
        if (br->pos < br->len) {
            br->acc |= ((uint64_t)br->buf[br->pos++]) << br->avail;
            br->avail = (uint8_t)(br->avail + 8U);
        } else {
            br->underflow = true;
            return 0U;
        }
    }
    uint64_t mask = (nbits >= 32U) ? 0xFFFFFFFFULL : ((1ULL << nbits) - 1ULL);
    uint32_t val = (uint32_t)(br->acc & mask);
    if (nbits >= 64U) {
        br->acc = 0ULL;
        br->avail = 0U;
    } else {
        br->acc >>= nbits;
        br->avail = (uint8_t)(br->avail - nbits);
    }
    return val;
}

// ponytail: arithmetic shift replaces 5-line branch for 24-bit sign extension
static inline int32_t sext24(int32_t v) {
    return (int32_t)((uint32_t)v << 8U) >> 8;
}

static inline uint32_t zz_enc(int32_t r) {
    return ((uint32_t)r << 1U) ^ (uint32_t)(r >> 31);
}

static inline int32_t zz_dec(uint32_t z) {
    return (int32_t)((z >> 1U) ^ -(int32_t)(z & 1U));
}

// ponytail: fixed FLAC Order-4 predictor for 24-bit signed samples
static inline int32_t predict_order4_24(uint8_t n, int32_t p1, int32_t p2, int32_t p3, int32_t p4) {
    int64_t pred = (n == 1U) ? (int64_t)p1
                 : (n == 2U) ? (2LL * p1 - p2)
                 : (n == 3U) ? (3LL * p1 - 3LL * p2 + p3)
                             : (4LL * p1 - 6LL * p2 + 4LL * p3 - p4);
    return sext24((int32_t)pred);
}

// ponytail: native 32-bit modular arithmetic FLAC Order-4 predictor
static inline int32_t predict_order4_32(uint8_t n, int32_t p1, int32_t p2, int32_t p3, int32_t p4) {
    int64_t pred = (n == 1U) ? (int64_t)p1
                 : (n == 2U) ? (2LL * p1 - p2)
                 : (n == 3U) ? (3LL * p1 - 3LL * p2 + p3)
                             : (4LL * p1 - 6LL * p2 + 4LL * p3 - p4);
    return (int32_t)pred;
}

// ponytail: single-cycle CLZ replaces while-loop log2 estimation
static inline uint8_t optimal_rice_k(uint64_t sum_zz, uint8_t count, uint8_t max_k) {
    uint32_t mean = (count > 0U) ? (uint32_t)(sum_zz / count) : 0U;
    if (mean <= 1U) {
        return 0U;
    }
    uint8_t k = (uint8_t)(31U - (uint32_t)__builtin_clz(mean));
    return (k > max_k) ? max_k : k;
}

uint8_t HT306_RAM_FUNC(ht306_crc8)(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00U;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8U; ++b) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1U) ^ 0x07U) : (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

HT306_CodecStatus_t ht306_lossless_encode_frame_flags(
    const int32_t *pcm_in,
    uint8_t num_frames,
    uint8_t sequence,
    uint8_t flags,
    uint8_t *packet_out,
    size_t packet_out_cap,
    size_t *packet_len_out)
{
    const size_t hdr_sz = sizeof(HT306_LosslessHeader_t);
    const bool is_32bit = (flags & HT306_FLAG_BITDEPTH_32) != 0U;
    const size_t bytes_per_anchor = is_32bit ? 4U : 3U;
    const size_t anc_sz = HT306_NUM_CHANNELS * bytes_per_anchor;
    const uint8_t escape_bits = is_32bit ? HT306_ESCAPE_RAW_BITS_32 : HT306_ESCAPE_RAW_BITS;
    const uint8_t max_k = is_32bit ? 30U : 22U;

    if (!pcm_in || !packet_out || !packet_len_out || num_frames == 0U ||
        num_frames > HT306_MAX_SUBFRAME_SAMPLES) {
        return HT306_CODEC_ERR_INVALID_ARG;
    }
    if (packet_out_cap < (hdr_sz + anc_sz + 4U)) {
        return HT306_CODEC_ERR_BUFFER_TOO_SMALL;
    }

    HT306_LosslessHeader_t hdr = {
        .sync_word  = HT306_SYNC_WORD,
        .sequence   = sequence,
        .flags      = flags,
        .num_frames = num_frames
    };

    uint32_t zz_buf[HT306_MAX_SUBFRAME_SAMPLES][HT306_NUM_CHANNELS];
    uint64_t sum_zz[HT306_NUM_CHANNELS] = {0};
    int32_t  p1[HT306_NUM_CHANNELS], p2[HT306_NUM_CHANNELS], p3[HT306_NUM_CHANNELS], p4[HT306_NUM_CHANNELS];
    uint8_t *payload = packet_out + hdr_sz;

    for (uint8_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
        int32_t s0 = is_32bit ? pcm_in[ch] : sext24(pcm_in[ch]);
        p1[ch] = p2[ch] = p3[ch] = p4[ch] = s0;
        if (is_32bit) {
            payload[ch * 4U + 0U] = (uint8_t)s0;
            payload[ch * 4U + 1U] = (uint8_t)(s0 >> 8);
            payload[ch * 4U + 2U] = (uint8_t)(s0 >> 16);
            payload[ch * 4U + 3U] = (uint8_t)(s0 >> 24);
        } else {
            payload[ch * 3U + 0U] = (uint8_t)s0;
            payload[ch * 3U + 1U] = (uint8_t)(s0 >> 8);
            payload[ch * 3U + 2U] = (uint8_t)(s0 >> 16);
        }
    }

    for (uint8_t n = 1U; n < num_frames; ++n) {
        const int32_t *row = &pcm_in[(size_t)n * HT306_NUM_CHANNELS];
        for (uint8_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
            int32_t s = is_32bit ? row[ch] : sext24(row[ch]);
            int32_t res;
            if (is_32bit) {
                res = (int32_t)(s - predict_order4_32(n, p1[ch], p2[ch], p3[ch], p4[ch]));
            } else {
                res = sext24(s - predict_order4_24(n, p1[ch], p2[ch], p3[ch], p4[ch]));
            }
            uint32_t zz = zz_enc(res);
            zz_buf[n][ch] = zz;
            if (n >= 3U || num_frames <= 3U) {
                sum_zz[ch] += zz;
            }
            p4[ch] = p3[ch];
            p3[ch] = p2[ch];
            p2[ch] = p1[ch];
            p1[ch] = s;
        }
    }

    uint8_t steady_cnt = (num_frames > 3U) ? (uint8_t)(num_frames - 3U) : 1U;
    for (uint8_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
        hdr.rice_k[ch] = optimal_rice_k(sum_zz[ch], steady_cnt, max_k);
    }

    BitWriter_t bw = { .buf = payload + anc_sz, .cap = packet_out_cap - hdr_sz - anc_sz };
    for (uint8_t n = 1U; n < num_frames; ++n) {
        for (uint8_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
            uint8_t base_k = hdr.rice_k[ch];
            uint8_t k = (n < 3U && base_k <= 16U) ? (uint8_t)(base_k + 6U) : base_k;
            if (k > max_k) k = max_k;
            uint32_t zz = zz_buf[n][ch];
            uint32_t q = zz >> k;

            if (q < HT306_RICE_ESCAPE_QUOTIENT) {
                bw_put(&bw, (1U << q) - 1U, (uint8_t)(q + 1U));
                if (k > 0U) {
                    bw_put(&bw, zz & ((1U << k) - 1U), k);
                }
            } else {
                bw_put(&bw, (1U << HT306_RICE_ESCAPE_QUOTIENT) - 1U, HT306_RICE_ESCAPE_QUOTIENT + 1U);
                bw_put(&bw, zz, escape_bits);
            }
        }
    }

    size_t total_payload = anc_sz + bw_flush(&bw);
    if (bw.overflow) {
        return HT306_CODEC_ERR_BUFFER_TOO_SMALL;
    }

    hdr.packet_len   = (uint16_t)(hdr_sz + total_payload);
    hdr.payload_crc8 = ht306_crc8(payload, total_payload);
    memcpy(packet_out, &hdr, hdr_sz);
    *packet_len_out = hdr.packet_len;
    return HT306_CODEC_OK;
}

HT306_CodecStatus_t ht306_lossless_encode_frame(
    const int32_t *pcm_in,
    uint8_t num_frames,
    uint8_t sequence,
    uint8_t *packet_out,
    size_t packet_out_cap,
    size_t *packet_len_out)
{
    return ht306_lossless_encode_frame_flags(
        pcm_in,
        num_frames,
        sequence,
        HT306_FLAG_RATE_96KHZ | HT306_FLAG_BITDEPTH_24,
        packet_out,
        packet_out_cap,
        packet_len_out
    );
}

HT306_CodecStatus_t HT306_RAM_FUNC(ht306_lossless_decode_frame)(
    const uint8_t *packet_in,
    size_t packet_len,
    int32_t *pcm24_out,
    int32_t *i2s32_msb_out,
    uint8_t *frames_decoded_out)
{
    const size_t hdr_sz = sizeof(HT306_LosslessHeader_t);
    if (!packet_in || packet_len < hdr_sz) {
        return HT306_CODEC_ERR_INVALID_ARG;
    }

    HT306_LosslessHeader_t hdr;
    memcpy(&hdr, packet_in, hdr_sz);
    if (hdr.sync_word != HT306_SYNC_WORD) {
        return HT306_CODEC_ERR_BAD_SYNC;
    }
    if (hdr.packet_len != packet_len || hdr.num_frames == 0U ||
        hdr.num_frames > HT306_MAX_SUBFRAME_SAMPLES) {
        return HT306_CODEC_ERR_CORRUPT_STREAM;
    }

    const bool is_32bit = (hdr.flags & HT306_FLAG_BITDEPTH_32) != 0U;
    const size_t bytes_per_anchor = is_32bit ? 4U : 3U;
    const size_t anc_sz = HT306_NUM_CHANNELS * bytes_per_anchor;
    const uint8_t escape_bits = is_32bit ? HT306_ESCAPE_RAW_BITS_32 : HT306_ESCAPE_RAW_BITS;
    const uint8_t max_k = is_32bit ? 30U : 22U;

    if (packet_len < (hdr_sz + anc_sz)) {
        return HT306_CODEC_ERR_INVALID_ARG;
    }

    const uint8_t *payload = packet_in + hdr_sz;
    size_t payload_len = packet_len - hdr_sz;
    if (ht306_crc8(payload, payload_len) != hdr.payload_crc8) {
        return HT306_CODEC_ERR_CRC_MISMATCH;
    }

    int32_t p1[HT306_NUM_CHANNELS], p2[HT306_NUM_CHANNELS], p3[HT306_NUM_CHANNELS], p4[HT306_NUM_CHANNELS];
    for (uint8_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
        int32_t s0;
        if (is_32bit) {
            uint32_t u32 = (uint32_t)payload[ch * 4U]
                         | ((uint32_t)payload[ch * 4U + 1U] << 8U)
                         | ((uint32_t)payload[ch * 4U + 2U] << 16U)
                         | ((uint32_t)payload[ch * 4U + 3U] << 24U);
            s0 = (int32_t)u32;
        } else {
            uint32_t u24 = (uint32_t)payload[ch * 3U]
                         | ((uint32_t)payload[ch * 3U + 1U] << 8U)
                         | ((uint32_t)payload[ch * 3U + 2U] << 16U);
            s0 = sext24((int32_t)u24);
        }
        p1[ch] = p2[ch] = p3[ch] = p4[ch] = s0;
        if (pcm24_out) {
            pcm24_out[ch] = is_32bit ? (int32_t)(s0 >> 8) : s0;
        }
        if (i2s32_msb_out) {
            i2s32_msb_out[ch] = is_32bit ? s0 : (int32_t)((uint32_t)s0 << 8U);
        }
    }

    BitReader_t br = { .buf = payload + anc_sz, .len = payload_len - anc_sz };
    for (uint8_t n = 1U; n < hdr.num_frames; ++n) {
        size_t base = (size_t)n * HT306_NUM_CHANNELS;
        for (uint8_t ch = 0; ch < HT306_NUM_CHANNELS; ++ch) {
            uint8_t base_k = hdr.rice_k[ch] & HT306_RICE_K_MASK;
            if (base_k > max_k) {
                return HT306_CODEC_ERR_CORRUPT_STREAM;
            }
            uint8_t k = (n < 3U && base_k <= 16U) ? (uint8_t)(base_k + 6U) : base_k;
            if (k > max_k) k = max_k;

            uint32_t q = 0U;
            while (br_get(&br, 1U) == 1U) {
                if (++q > HT306_RICE_ESCAPE_QUOTIENT) {
                    return HT306_CODEC_ERR_CORRUPT_STREAM;
                }
            }

            uint32_t zz = (q == HT306_RICE_ESCAPE_QUOTIENT)
                        ? br_get(&br, escape_bits)
                        : ((q << k) | br_get(&br, k));
            if (br.underflow) {
                return HT306_CODEC_ERR_CORRUPT_STREAM;
            }

            int32_t s;
            if (is_32bit) {
                s = (int32_t)(predict_order4_32(n, p1[ch], p2[ch], p3[ch], p4[ch]) + zz_dec(zz));
            } else {
                s = sext24(predict_order4_24(n, p1[ch], p2[ch], p3[ch], p4[ch]) + zz_dec(zz));
            }
            p4[ch] = p3[ch];
            p3[ch] = p2[ch];
            p2[ch] = p1[ch];
            p1[ch] = s;

            if (pcm24_out) {
                pcm24_out[base + ch] = is_32bit ? (int32_t)(s >> 8) : s;
            }
            if (i2s32_msb_out) {
                i2s32_msb_out[base + ch] = is_32bit ? s : (int32_t)((uint32_t)s << 8U);
            }
        }
    }

    if (frames_decoded_out) {
        *frames_decoded_out = hdr.num_frames;
    }
    return HT306_CODEC_OK;
}
