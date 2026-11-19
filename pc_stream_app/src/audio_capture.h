#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app_state.h"
#include "audio_rate_dsp.h"
#include "usb_transport.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <atomic>
#include <vector>
#include <cstdint>
#include <string>

namespace ht306::audio {

class AudioCapture {
public:
    AudioCapture(AppState& state, usb::UsbTransport& transport);
    ~AudioCapture();

    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    static DWORD WINAPI CaptureThreadProc(LPVOID param);
    void CaptureLoop();

    static AudioStreamFormat ParseWaveFormat(const WAVEFORMATEX* pwfx);

    void NormalizeChannelsTo6Ch(
        const float* src6,
        float dst6[6],
        uint16_t in_channels,
        bool bit_perfect
    ) const;

    void EmitProcessedFrames6Ch(
        const float* frames_6ch,
        size_t num_frames,
        uint32_t output_rate,
        bool is_sweep,
        bool allow_silence_gate,
        std::vector<int32_t>& pcm6ch_block,
        int& frame_in_block,
        std::vector<uint8_t>& usb_packet
    );

    void PumpSweepIfActive(
        LARGE_INTEGER qpc_freq,
        LARGE_INTEGER& qpc_last,
        uint32_t sweep_rate,
        std::vector<int32_t>& pcm6ch_block,
        int& frame_in_block,
        std::vector<uint8_t>& usb_packet
    );

    void LogThrottled(LogLevel level, const std::string& key, const std::string& msg);

    float GenerateTestSample(uint32_t mix_rate, float amp);
    static float ApplySoftLimiter(float sample);

    AppState& m_state;
    usb::UsbTransport& m_transport;

    std::atomic<bool> m_running{false};
    HANDLE m_thread = NULL;
    HANDLE m_stop_event = NULL;

    uint8_t m_seq = 0;

    // Smoothed linear gain per channel (default 1.0f)
    float m_smoothed_gain[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    // Resampler & reusable DSP scratch buffers
    RateResampler6Ch m_resampler;
    std::vector<float> m_in_frames_6ch;
    std::vector<float> m_resampled_6ch;

    // Packet pacing & silence gate state
    float m_current_block_peak = 0.0f;
    uint32_t m_silent_blocks_streak = 0;
    uint32_t m_burst_packets_sent = 0;
    std::string m_last_status_key;

    // Test tone & acoustic sweep generator states
    int m_last_sweep_ch = -1;
    float m_chirp_phase = 0.0f;
    float m_chirp_t = 0.0f;
    float m_sine_phase = 0.0f;

    // Paul Kellet's 7-element pink noise filter states + fast LCG seed
    float m_b0 = 0.0f;
    float m_b1 = 0.0f;
    float m_b2 = 0.0f;
    float m_b3 = 0.0f;
    float m_b4 = 0.0f;
    float m_b5 = 0.0f;
    float m_b6 = 0.0f;
    uint32_t m_lcg_seed = 123456789u;
};

} // namespace ht306::audio
