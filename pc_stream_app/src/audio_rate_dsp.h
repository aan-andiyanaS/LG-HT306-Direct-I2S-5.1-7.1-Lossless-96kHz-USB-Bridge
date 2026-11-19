#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ht306::audio {

enum class SampleFormat : uint8_t {
    UNSUPPORTED = 0,
    IEEE_FLOAT32,
    PCM16,
    PCM24_PACKED,
    PCM32
};

enum class SampleRateMode : uint8_t {
    AUTO = 0,
    LOCK_96K = 1,
    LOCK_48K = 2
};

struct AudioStreamFormat {
    SampleFormat format = SampleFormat::UNSUPPORTED;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t valid_bits = 0;
};

struct ResolvedRate {
    uint32_t target_rate = 48000u;
    bool valid_input = false;
    bool clamped_from_high = false;
    bool resampled = false;
};

/**
 * Resolves the target RP2350 output rate (always 48000 Hz or 96000 Hz)
 * from the active UI mode and captured Windows/VB-Cable input rate.
 */
inline ResolvedRate ResolveTargetSampleRate(SampleRateMode mode, uint32_t input_rate) {
    ResolvedRate res{};
    if (input_rate < 8000u || input_rate > 384000u) {
        res.target_rate = (mode == SampleRateMode::LOCK_48K) ? 48000u : 96000u;
        res.valid_input = false;
        res.clamped_from_high = false;
        res.resampled = false;
        return res;
    }

    res.valid_input = true;
    switch (mode) {
        case SampleRateMode::LOCK_48K:
            res.target_rate = 48000u;
            res.clamped_from_high = (input_rate > 48000u);
            break;
        case SampleRateMode::LOCK_96K:
            res.target_rate = 96000u;
            res.clamped_from_high = (input_rate > 96000u);
            break;
        case SampleRateMode::AUTO:
        default:
            res.target_rate = (input_rate <= 48000u) ? 48000u : 96000u;
            res.clamped_from_high = (input_rate > 96000u);
            break;
    }
    res.resampled = (input_rate != res.target_rate);
    return res;
}

/**
 * Converts normalized float [-1.0, 1.0] to signed 32-bit PCM using double
 * arithmetic so +1.0f never rounds up to 2^31 (UB / INT32_MIN wrap-around).
 */
inline int32_t FloatToInt32Safe(float s) {
    if (!std::isfinite(s)) {
        return 0;
    }
    double clamped = std::clamp(static_cast<double>(s), -1.0, 1.0);
    double scaled = std::clamp(clamped * 2147483647.0, -2147483648.0, 2147483647.0);
    return static_cast<int32_t>(std::llround(scaled));
}

inline uint32_t BytesPerSample(SampleFormat fmt) {
    switch (fmt) {
        case SampleFormat::IEEE_FLOAT32:
        case SampleFormat::PCM32:
            return 4u;
        case SampleFormat::PCM24_PACKED:
            return 3u;
        case SampleFormat::PCM16:
            return 2u;
        case SampleFormat::UNSUPPORTED:
        default:
            return 0u;
    }
}

/**
 * Decodes up to 6 channels of a single interleaved input frame into normalized
 * float [-1.0, 1.0]. Sample stride strictly follows container SampleFormat
 * (e.g., 24-in-32 uses PCM32 with 4-byte stride).
 */
inline bool DecodeInterleavedFrameToFloat6(
    const uint8_t* src,
    uint32_t frame_idx,
    const AudioStreamFormat& fmt,
    float out_ch[6]
) {
    for (int c = 0; c < 6; ++c) {
        out_ch[c] = 0.0f;
    }
    if (src == nullptr || fmt.channels == 0u) {
        return false;
    }
    uint32_t bps = BytesPerSample(fmt.format);
    if (bps == 0u) {
        return false;
    }

    const size_t ch_count = static_cast<size_t>(fmt.channels);
    const size_t copy_ch = (std::min)(ch_count, size_t{6});
    const uint8_t* frame_ptr = src + static_cast<size_t>(frame_idx) * ch_count * bps;

    for (size_t c = 0; c < copy_ch; ++c) {
        const uint8_t* p = frame_ptr + c * bps;
        switch (fmt.format) {
            case SampleFormat::IEEE_FLOAT32: {
                float f = 0.0f;
                std::memcpy(&f, p, sizeof(float));
                out_ch[c] = std::isfinite(f) ? f : 0.0f;
                break;
            }
            case SampleFormat::PCM32: {
                int32_t v = 0;
                std::memcpy(&v, p, sizeof(int32_t));
                out_ch[c] = static_cast<float>(static_cast<double>(v) / 2147483648.0);
                break;
            }
            case SampleFormat::PCM24_PACKED: {
                uint32_t u = static_cast<uint32_t>(p[0]) |
                             (static_cast<uint32_t>(p[1]) << 8) |
                             (static_cast<uint32_t>(p[2]) << 16);
                if (u & 0x800000u) {
                    u |= 0xFF000000u;
                }
                int32_t v = static_cast<int32_t>(u);
                out_ch[c] = static_cast<float>(v) / 8388608.0f;
                break;
            }
            case SampleFormat::PCM16: {
                int16_t v = 0;
                std::memcpy(&v, p, sizeof(int16_t));
                out_ch[c] = static_cast<float>(v) / 32768.0f;
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

/**
 * Stateful 6-channel Windowed-Sinc (Blackman) sample-rate converter.
 * Bypasses processing with zero bit modification when in_rate == out_rate.
 */
class RateResampler6Ch {
public:
    static constexpr size_t kChannels = 6u;
    static constexpr size_t kMaxTaps = 64u;
    static constexpr double kPi = 3.14159265358979323846;

    void Configure(uint32_t in_rate, uint32_t out_rate) {
        if (in_rate == in_rate_ && out_rate == out_rate_ && configured_) {
            return;
        }
        in_rate_ = in_rate;
        out_rate_ = out_rate;
        configured_ = (in_rate >= 8000u && in_rate <= 384000u &&
                       out_rate >= 8000u && out_rate <= 384000u);

        if (!configured_ || in_rate_ == out_rate_) {
            taps_ = 16u;
        } else {
            // Scale tap count with decimation ratio so 2:1 uses 32 taps and 4:1 uses 64 taps.
            double down_ratio = (in_rate_ > out_rate_)
                ? (static_cast<double>(in_rate_) / static_cast<double>(out_rate_))
                : 1.0;
            size_t desired = static_cast<size_t>(std::ceil(16.0 * down_ratio));
            if (desired & 1u) {
                desired++;
            }
            taps_ = std::clamp(desired, size_t{16}, kMaxTaps);
        }
        half_taps_ = taps_ / 2u;
        cutoff_ = (in_rate_ > out_rate_)
            ? (0.45 * static_cast<double>(out_rate_) / static_cast<double>(in_rate_))
            : 0.45;
        Reset();
    }

    void Reset() {
        buffer_.assign((half_taps_ - 1u) * kChannels, 0.0f);
        phase_ = static_cast<double>(half_taps_ - 1u);
    }

    bool IsBypass() const {
        return configured_ && (in_rate_ == out_rate_);
    }

    /**
     * Appends input frames (nullptr = in_frames of zeros) and writes resampled
     * 6-channel interleaved output into out_interleaved_6ch (cleared first).
     * Returns number of output frames written.
     */
    size_t Process(
        const float* in_interleaved_6ch,
        size_t in_frames,
        std::vector<float>& out_interleaved_6ch
    ) {
        out_interleaved_6ch.clear();
        if (!configured_ || in_frames == 0u) {
            return 0u;
        }

        if (in_rate_ == out_rate_) {
            out_interleaved_6ch.resize(in_frames * kChannels, 0.0f);
            if (in_interleaved_6ch != nullptr) {
                std::memcpy(
                    out_interleaved_6ch.data(),
                    in_interleaved_6ch,
                    in_frames * kChannels * sizeof(float)
                );
            }
            return in_frames;
        }

        const size_t prev_samples = buffer_.size();
        buffer_.resize(prev_samples + in_frames * kChannels, 0.0f);
        if (in_interleaved_6ch != nullptr) {
            std::memcpy(
                buffer_.data() + prev_samples,
                in_interleaved_6ch,
                in_frames * kChannels * sizeof(float)
            );
        }

        const size_t total_frames = buffer_.size() / kChannels;
        const double step = static_cast<double>(in_rate_) / static_cast<double>(out_rate_);
        const size_t est_out = static_cast<size_t>(std::ceil(static_cast<double>(in_frames) / step)) + 8u;
        out_interleaved_6ch.reserve(est_out * kChannels);

        std::array<double, kMaxTaps> weights{};
        const double half_span = static_cast<double>(half_taps_);
        const double inv_span = 1.0 / static_cast<double>(taps_);

        while (true) {
            const size_t idx = static_cast<size_t>(std::floor(phase_));
            if (idx + half_taps_ >= total_frames) {
                break;
            }
            const double frac = phase_ - static_cast<double>(idx);
            const size_t start_idx = idx - (half_taps_ - 1u);

            double weight_sum = 0.0;
            for (size_t k = 0; k < taps_; ++k) {
                const double x = (static_cast<double>(k) - static_cast<double>(half_taps_ - 1u)) - frac;
                const double win_arg = (x + half_span) * inv_span;
                double w = 0.0;
                if (win_arg > 0.0 && win_arg < 1.0) {
                    w = 0.42 - 0.5 * std::cos(2.0 * kPi * win_arg) + 0.08 * std::cos(4.0 * kPi * win_arg);
                }
                const double sinc_arg = 2.0 * cutoff_ * x;
                const double s = (std::fabs(sinc_arg) < 1e-9)
                    ? 1.0
                    : std::sin(kPi * sinc_arg) / (kPi * sinc_arg);
                const double coeff = w * s;
                weights[k] = coeff;
                weight_sum += coeff;
            }

            const double norm = (std::fabs(weight_sum) > 1e-12) ? (1.0 / weight_sum) : 1.0;

            for (size_t c = 0; c < kChannels; ++c) {
                double accum = 0.0;
                for (size_t k = 0; k < taps_; ++k) {
                    accum += static_cast<double>(buffer_[(start_idx + k) * kChannels + c]) * weights[k];
                }
                out_interleaved_6ch.push_back(static_cast<float>(accum * norm));
            }
            phase_ += step;
        }

        // Discard already consumed prefix while preserving (half_taps_ - 1) lookback samples.
        const size_t next_idx = static_cast<size_t>(std::floor(phase_));
        if (next_idx > (half_taps_ - 1u)) {
            const size_t drop_frames = next_idx - (half_taps_ - 1u);
            if (drop_frames < total_frames) {
                const size_t drop_elems = drop_frames * kChannels;
                buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(drop_elems));
                phase_ -= static_cast<double>(drop_frames);
            } else {
                Reset();
            }
        }

        return out_interleaved_6ch.size() / kChannels;
    }

private:
    uint32_t in_rate_ = 0u;
    uint32_t out_rate_ = 0u;
    bool configured_ = false;
    size_t taps_ = 16u;
    size_t half_taps_ = 8u;
    double cutoff_ = 0.45;
    double phase_ = 7.0;
    std::vector<float> buffer_;
};

} // namespace ht306::audio
