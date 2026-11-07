#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app_state.h"
#include "usb_transport.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <atomic>
#include <vector>
#include <cstdint>

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

    void ProcessAudioFrames(
        const float* float_src,
        UINT32 num_frames,
        DWORD flags,
        uint16_t mix_channels,
        uint32_t mix_rate,
        std::vector<int32_t>& pcm6ch_block,
        int& frame_in_block,
        std::vector<uint8_t>& usb_packet
    );

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
