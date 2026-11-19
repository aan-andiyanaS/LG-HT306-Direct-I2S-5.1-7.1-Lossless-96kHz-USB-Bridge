#include "audio_rate_dsp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ht306::audio;

static int g_failures = 0;

#define EXPECT_TRUE(cond, msg)                                                 \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static double MeasureToneGainDb(
    const std::vector<float>& interleaved_6ch,
    uint32_t sample_rate,
    double freq_hz,
    size_t skip_frames
) {
    const size_t total_frames = interleaved_6ch.size() / 6u;
    if (total_frames <= skip_frames + 64u) {
        return -200.0;
    }
    double cos_sum = 0.0;
    double sin_sum = 0.0;
    const size_t count = total_frames - skip_frames;
    const double two_pi = 2.0 * 3.14159265358979323846;
    for (size_t i = skip_frames; i < total_frames; ++i) {
        double phase = two_pi * freq_hz * (static_cast<double>(i) / static_cast<double>(sample_rate));
        double sample = static_cast<double>(interleaved_6ch[i * 6u]);
        cos_sum += sample * std::cos(phase);
        sin_sum += sample * std::sin(phase);
    }
    double mag = (2.0 / static_cast<double>(count)) * std::sqrt(cos_sum * cos_sum + sin_sum * sin_sum);
    if (mag < 1e-10) {
        return -200.0;
    }
    return 20.0 * std::log10(mag);
}

static void TestResolveTargetSampleRate() {
    auto r0 = ResolveTargetSampleRate(SampleRateMode::AUTO, 0u);
    EXPECT_TRUE(!r0.valid_input && r0.target_rate == 96000u && !r0.resampled, "rate 0 invalid");

    auto r_bad = ResolveTargetSampleRate(SampleRateMode::AUTO, 999999u);
    EXPECT_TRUE(!r_bad.valid_input, "rate 999999 invalid");

    auto r44 = ResolveTargetSampleRate(SampleRateMode::AUTO, 44100u);
    EXPECT_TRUE(r44.valid_input && r44.target_rate == 48000u && !r44.clamped_from_high && r44.resampled, "auto 44.1k -> 48k");

    auto r48 = ResolveTargetSampleRate(SampleRateMode::AUTO, 48000u);
    EXPECT_TRUE(r48.valid_input && r48.target_rate == 48000u && !r48.clamped_from_high && !r48.resampled, "auto 48k native");

    auto r88 = ResolveTargetSampleRate(SampleRateMode::AUTO, 88200u);
    EXPECT_TRUE(r88.valid_input && r88.target_rate == 96000u && !r88.clamped_from_high && r88.resampled, "auto 88.2k -> 96k");

    auto r96 = ResolveTargetSampleRate(SampleRateMode::AUTO, 96000u);
    EXPECT_TRUE(r96.valid_input && r96.target_rate == 96000u && !r96.clamped_from_high && !r96.resampled, "auto 96k native");

    auto r176 = ResolveTargetSampleRate(SampleRateMode::AUTO, 176400u);
    EXPECT_TRUE(r176.valid_input && r176.target_rate == 96000u && r176.clamped_from_high && r176.resampled, "auto 176.4k -> 96k clamped");

    auto r192 = ResolveTargetSampleRate(SampleRateMode::AUTO, 192000u);
    EXPECT_TRUE(r192.valid_input && r192.target_rate == 96000u && r192.clamped_from_high && r192.resampled, "auto 192k -> 96k clamped");

    auto l48 = ResolveTargetSampleRate(SampleRateMode::LOCK_48K, 96000u);
    EXPECT_TRUE(l48.valid_input && l48.target_rate == 48000u && l48.clamped_from_high && l48.resampled, "lock48k 96k -> 48k");

    auto l96 = ResolveTargetSampleRate(SampleRateMode::LOCK_96K, 48000u);
    EXPECT_TRUE(l96.valid_input && l96.target_rate == 96000u && !l96.clamped_from_high && l96.resampled, "lock96k 48k -> 96k");

    std::printf("[PASS] TestResolveTargetSampleRate\n");
}

static void TestFloatToInt32Safe() {
    int32_t max_pos = FloatToInt32Safe(1.0f);
    int32_t over_pos = FloatToInt32Safe(1.5f);
    int32_t min_neg = FloatToInt32Safe(-1.0f);
    int32_t over_neg = FloatToInt32Safe(-1.5f);
    int32_t zero_val = FloatToInt32Safe(0.0f);
    int32_t nan_val = FloatToInt32Safe(0.0f / 0.0f);

    EXPECT_TRUE(max_pos == 2147483647, "+1.0f must convert to INT32_MAX without wrap-around");
    EXPECT_TRUE(over_pos == 2147483647, "+1.5f must clamp cleanly to INT32_MAX");
    EXPECT_TRUE(min_neg == -2147483647 || min_neg == INT32_MIN, "-1.0f valid negative full scale");
    EXPECT_TRUE(over_neg == min_neg, "-1.5f clamped cleanly");
    EXPECT_TRUE(zero_val == 0, "0.0f -> 0");
    EXPECT_TRUE(nan_val == 0, "NaN -> 0");

    std::printf("[PASS] TestFloatToInt32Safe\n");
}

static void TestDecodeInterleavedFormats() {
    float out[6] = {};

    // 1. IEEE Float 2ch
    float f2[2] = { 0.5f, -0.25f };
    AudioStreamFormat fmt_f{ SampleFormat::IEEE_FLOAT32, 2, 96000, 32 };
    EXPECT_TRUE(DecodeInterleavedFrameToFloat6(reinterpret_cast<const uint8_t*>(f2), 0, fmt_f, out), "float decode");
    EXPECT_TRUE(std::fabs(out[0] - 0.5f) < 1e-6f && std::fabs(out[1] + 0.25f) < 1e-6f && out[2] == 0.0f, "float values");

    // 2. PCM 16-bit
    int16_t p16[2] = { 16384, -32768 };
    AudioStreamFormat fmt_16{ SampleFormat::PCM16, 2, 48000, 16 };
    EXPECT_TRUE(DecodeInterleavedFrameToFloat6(reinterpret_cast<const uint8_t*>(p16), 0, fmt_16, out), "pcm16 decode");
    EXPECT_TRUE(std::fabs(out[0] - 0.5f) < 1e-4f && std::fabs(out[1] + 1.0f) < 1e-4f, "pcm16 values");

    // 3. PCM 24-bit packed (3 bytes per sample)
    uint8_t p24[6] = {
        0xFF, 0xFF, 0x7F, // +max (8388607)
        0x00, 0x00, 0x80  // -1.0 (-8388608)
    };
    AudioStreamFormat fmt_24{ SampleFormat::PCM24_PACKED, 2, 96000, 24 };
    EXPECT_TRUE(DecodeInterleavedFrameToFloat6(p24, 0, fmt_24, out), "pcm24 decode");
    EXPECT_TRUE(out[0] > 0.9999f && std::fabs(out[1] + 1.0f) < 1e-6f, "pcm24 values");

    // 4. PCM 24-in-32 (4-byte container -> SampleFormat::PCM32)
    int32_t p32[2] = { 1073741824, INT32_MIN };
    AudioStreamFormat fmt_32{ SampleFormat::PCM32, 2, 96000, 24 };
    EXPECT_TRUE(DecodeInterleavedFrameToFloat6(reinterpret_cast<const uint8_t*>(p32), 0, fmt_32, out), "pcm32 decode");
    EXPECT_TRUE(std::fabs(out[0] - 0.5f) < 1e-6f && std::fabs(out[1] + 1.0f) < 1e-6f, "pcm32 values");

    // 5. Unsupported
    AudioStreamFormat fmt_bad{ SampleFormat::UNSUPPORTED, 2, 48000, 16 };
    EXPECT_TRUE(!DecodeInterleavedFrameToFloat6(p24, 0, fmt_bad, out), "unsupported must return false");

    std::printf("[PASS] TestDecodeInterleavedFormats\n");
}

static void RunResampleRatioCheck(uint32_t in_rate, uint32_t out_rate, double pass_freq, double alias_freq) {
    RateResampler6Ch rs;
    rs.Configure(in_rate, out_rate);

    const size_t chunk = in_rate / 100u; // 10 ms chunks for 1 full second
    std::vector<float> in_chunk(chunk * 6u, 0.0f);
    std::vector<float> out_chunk;
    std::vector<float> all_out;
    all_out.reserve(out_rate * 6u);

    const double two_pi = 2.0 * 3.14159265358979323846;
    size_t sample_idx = 0;

    for (int step = 0; step < 100; ++step) {
        for (size_t i = 0; i < chunk; ++i) {
            double t = static_cast<double>(sample_idx++) / static_cast<double>(in_rate);
            float sig = static_cast<float>(0.5 * std::sin(two_pi * pass_freq * t));
            if (alias_freq > 0.0) {
                sig += static_cast<float>(0.5 * std::sin(two_pi * alias_freq * t));
            }
            for (int c = 0; c < 6; ++c) {
                in_chunk[i * 6u + c] = sig;
            }
        }
        rs.Process(in_chunk.data(), chunk, out_chunk);
        all_out.insert(all_out.end(), out_chunk.begin(), out_chunk.end());
    }

    const size_t total_out_frames = all_out.size() / 6u;
    int64_t diff = static_cast<int64_t>(total_out_frames) - static_cast<int64_t>(out_rate);
    EXPECT_TRUE(std::llabs(diff) <= 32, "1s resampled frame count within tolerance");

    double gain_pass = MeasureToneGainDb(all_out, out_rate, pass_freq, out_rate / 10u);
    double expected_db = -6.02; // amplitude 0.5
    EXPECT_TRUE(gain_pass > expected_db - 1.5 && gain_pass < expected_db + 1.0, "passband 1kHz preserved");

    if (alias_freq > 0.0) {
        double folded = std::fabs(std::fmod(alias_freq, static_cast<double>(out_rate)));
        if (folded > static_cast<double>(out_rate) * 0.5) {
            folded = static_cast<double>(out_rate) - folded;
        }
        double alias_db = MeasureToneGainDb(all_out, out_rate, folded, out_rate / 10u);
        EXPECT_TRUE(alias_db < -40.0, "ultrasonic alias suppressed >40dB");
    }
}

static void TestResampler() {
    // 1. Bit-exact bypass check
    {
        RateResampler6Ch rs;
        rs.Configure(96000u, 96000u);
        EXPECT_TRUE(rs.IsBypass(), "96k->96k is bypass");
        std::vector<float> in(48 * 6, 0.1234567f);
        in[7] = -0.9876543f;
        std::vector<float> out;
        size_t n = rs.Process(in.data(), 48, out);
        EXPECT_TRUE(n == 48 && std::memcmp(in.data(), out.data(), in.size() * sizeof(float)) == 0, "bitwise exact bypass");
    }

    // 2. Ratios & anti-aliasing
    RunResampleRatioCheck(192000u, 96000u, 1000.0, 70000.0);
    RunResampleRatioCheck(192000u, 48000u, 1000.0, 35000.0);
    RunResampleRatioCheck(176400u, 96000u, 1000.0, 70000.0);
    RunResampleRatioCheck(96000u, 48000u, 1000.0, 35000.0);
    RunResampleRatioCheck(44100u, 48000u, 1000.0, 0.0);
    RunResampleRatioCheck(48000u, 96000u, 1000.0, 0.0);

    // 3. Guard input_rate == 0 and nullptr flush
    {
        RateResampler6Ch rs;
        rs.Configure(0u, 48000u);
        std::vector<float> in(48 * 6, 0.5f), out;
        EXPECT_TRUE(rs.Process(in.data(), 48, out) == 0, "invalid rate safely produces 0 frames");

        rs.Configure(192000u, 96000u);
        EXPECT_TRUE(rs.Process(nullptr, 96, out) > 0, "nullptr zero flush succeeds");
    }

    std::printf("[PASS] TestResampler\n");
}

int main() {
    TestResolveTargetSampleRate();
    TestFloatToInt32Safe();
    TestDecodeInterleavedFormats();
    TestResampler();

    if (g_failures > 0) {
        std::fprintf(stderr, "FAILED: %d assertions failed\n", g_failures);
        return 1;
    }
    std::printf("ALL AUDIO_RATE_DSP TESTS PASSED\n");
    return 0;
}
