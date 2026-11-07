#include "audio_capture.h"
#include "lossless_codec.h"
#include <avrt.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstdlib>

namespace ht306::audio {

AudioCapture::AudioCapture(AppState& state, usb::UsbTransport& transport)
    : m_state(state), m_transport(transport) {
    m_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    for (int c = 0; c < 6; ++c) {
        m_smoothed_gain[c] = 1.0f;
    }
}

AudioCapture::~AudioCapture() {
    Stop();
    if (m_stop_event) {
        CloseHandle(m_stop_event);
        m_stop_event = NULL;
    }
}

bool AudioCapture::Start() {
    if (m_running) return true;
    ResetEvent(m_stop_event);
    m_running = true;
    m_thread = CreateThread(NULL, 0, CaptureThreadProc, this, 0, NULL);
    return (m_thread != NULL);
}

void AudioCapture::Stop() {
    if (!m_running) return;
    m_running = false;
    if (m_stop_event) SetEvent(m_stop_event);
    if (m_thread) {
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        m_thread = NULL;
    }
}

bool AudioCapture::IsRunning() const {
    return m_running.load();
}

DWORD WINAPI AudioCapture::CaptureThreadProc(LPVOID param) {
    auto self = reinterpret_cast<AudioCapture*>(param);
    self->CaptureLoop();
    return 0;
}

float AudioCapture::ApplySoftLimiter(float x) {
    const float threshold = 0.85f;
    const float ceiling = 0.9885f; // -0.1 dBFS soft knee ceiling
    const float headroom = ceiling - threshold; // 0.1385f
    float abs_x = std::fabs(x);
    if (abs_x <= threshold) {
        return x;
    }
    float compressed = threshold + headroom * std::tanh((abs_x - threshold) / headroom);
    return (x < 0.0f) ? -compressed : compressed;
}

float AudioCapture::GenerateTestSample(uint32_t mix_rate, float amp) {
    if (mix_rate == 0) mix_rate = 96000;
    const float two_pi = 6.28318530717958647692f;

    if (m_state.test_signal_type == 1) {
        // Paul Kellet's refined 7-element pink noise filter (-3 dB/octave)
        // Fast Linear Congruential Generator (LCG) for white noise in [-1.0f, 1.0f]
        m_lcg_seed = m_lcg_seed * 1664525u + 1013904223u;
        float white = ((float)(m_lcg_seed & 0x7FFFFFFF) / 1073741824.0f) - 1.0f;

        m_b0 = 0.99886f * m_b0 + white * 0.0555179f;
        m_b1 = 0.99332f * m_b1 + white * 0.0750759f;
        m_b2 = 0.96900f * m_b2 + white * 0.1538520f;
        m_b3 = 0.86650f * m_b3 + white * 0.3104856f;
        m_b4 = 0.55000f * m_b4 + white * 0.5329522f;
        m_b5 = -0.7616f * m_b5 - white * 0.0168980f;
        float pink = m_b0 + m_b1 + m_b2 + m_b3 + m_b4 + m_b5 + m_b6 + white * 0.5362f;
        m_b6 = white * 0.115926f;

        // Kellet filter output scale factor (~0.11f) mapped by calibrated target amplitude
        return pink * 0.11f * amp;
    } else if (m_state.test_signal_type == 0) {
        // Logarithmic Chirp Sweep smoothly between 40 Hz and 16 kHz
        const float f_start = 40.0f;
        const float f_end = 16000.0f;
        const float duration = 1.5f; // 1.5s sweep duration matching UI speaker step cycle

        float progress = m_chirp_t / duration;
        if (progress > 1.0f) progress = 1.0f;
        float f_inst = f_start * std::pow(f_end / f_start, progress);

        m_chirp_phase += (two_pi * f_inst) / (float)mix_rate;
        if (m_chirp_phase >= two_pi) {
            m_chirp_phase = std::fmod(m_chirp_phase, two_pi);
        }

        m_chirp_t += 1.0f / (float)mix_rate;
        if (m_chirp_t >= duration) {
            m_chirp_t = 0.0f;
        }

        return std::sin(m_chirp_phase) * amp;
    } else {
        // Clean 1 kHz reference sine wave
        m_sine_phase += (two_pi * 1000.0f) / (float)mix_rate;
        if (m_sine_phase >= two_pi) {
            m_sine_phase = std::fmod(m_sine_phase, two_pi);
        }
        return std::sin(m_sine_phase) * amp;
    }
}

void AudioCapture::ProcessAudioFrames(
    const float* float_src,
    UINT32 num_frames,
    DWORD flags,
    uint16_t mix_channels,
    uint32_t mix_rate,
    std::vector<int32_t>& pcm6ch_block,
    int& frame_in_block,
    std::vector<uint8_t>& usb_packet
) {
    float ch_max[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    bool ch_clipped[6] = { false, false, false, false, false, false };

    int active_sweep_ch = m_state.sweep_active_channel.load();
    bool is_sweep = m_state.sweep_running.load() || (active_sweep_ch >= 0 && active_sweep_ch < 6);

    if (is_sweep) {
        if (active_sweep_ch < 0 || active_sweep_ch >= 6) {
            active_sweep_ch = 0; // Default to Front Left if sweep was triggered without channel selection
        }
        if (active_sweep_ch != m_last_sweep_ch) {
            m_last_sweep_ch = active_sweep_ch;
            m_chirp_t = 0.0f;
            m_chirp_phase = 0.0f;
        }
    } else {
        m_last_sweep_ch = -1;
    }

    float test_dbfs = (std::clamp)(m_state.test_level_dbfs, -60.0f, 0.0f);
    float test_amp = std::pow(10.0f, test_dbfs / 20.0f);

    bool is_silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) || (float_src == nullptr);

    // Compute master linear gain
    float master_lin = 1.0f;
    if (!m_state.settings.bit_perfect_mode) {
        float master_db = (std::clamp)(m_state.settings.master_volume_db, -60.0f, 6.0f);
        master_lin = std::pow(10.0f, master_db / 20.0f);
    }

    // Hoist per-channel target gain computation ONCE outside the per-frame loop
    float target_gain[6];
    for (int c = 0; c < 6; ++c) {
        if (m_state.settings.bit_perfect_mode) {
            target_gain[c] = 1.0f;
        } else {
            target_gain[c] = master_lin * std::pow(10.0f, m_state.settings.trims[c] / 20.0f);
        }
    }

    // Evaluate Solo state
    bool any_solo = false;
    for (int c = 0; c < 6; ++c) {
        if (m_state.settings.solos[c]) {
            any_solo = true;
            break;
        }
    }

    for (UINT32 f = 0; f < num_frames; ++f) {
        float in_ch[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float routed[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        if (is_sweep) {
            float test_val = GenerateTestSample(mix_rate, test_amp);
            for (int c = 0; c < 6; ++c) {
                routed[c] = (c == active_sweep_ch) ? test_val : 0.0f;
            }
            // Keep smoothed gains tracking target gain in background
            for (int c = 0; c < 6; ++c) {
                m_smoothed_gain[c] += 0.005f * (target_gain[c] - m_smoothed_gain[c]);
            }
        } else {
            // 1. Input Normalization & Upmix
            if (!is_silent) {
                if (mix_channels >= 6) {
                    for (int c = 0; c < 6; ++c) {
                        in_ch[c] = float_src[f * mix_channels + c];
                    }
                    if (m_state.settings.routing_preset == RoutingPreset::STEREO_UPMIX) {
                        float L = in_ch[0];
                        float R = in_ch[1];
                        in_ch[0] = L;
                        in_ch[1] = R;
                        in_ch[2] = (L + R) * 0.5f;   // Center
                        in_ch[3] = (L + R) * 0.35f;  // LFE / Subwoofer
                        in_ch[4] = L * 0.707f;       // Surround Left
                        in_ch[5] = R * 0.707f;       // Surround Right
                    } else if (m_state.settings.routing_preset == RoutingPreset::DUAL_MONO) {
                        float L = in_ch[0];
                        float R = in_ch[1];
                        in_ch[0] = in_ch[2] = in_ch[4] = L;
                        in_ch[1] = in_ch[3] = in_ch[5] = R;
                    }
                } else if (mix_channels == 2) {
                    float L = float_src[f * 2 + 0];
                    float R = float_src[f * 2 + 1];

                    if (m_state.settings.routing_preset == RoutingPreset::STEREO_UPMIX) {
                        in_ch[0] = L;
                        in_ch[1] = R;
                        in_ch[2] = (L + R) * 0.5f;   // Center
                        in_ch[3] = (L + R) * 0.35f;  // LFE / Subwoofer
                        in_ch[4] = L * 0.707f;       // Surround Left
                        in_ch[5] = R * 0.707f;       // Surround Right
                    } else if (m_state.settings.routing_preset == RoutingPreset::DUAL_MONO) {
                        in_ch[0] = in_ch[2] = in_ch[4] = L;
                        in_ch[1] = in_ch[3] = in_ch[5] = R;
                    } else { // DISCRETE_1TO1 or others
                        in_ch[0] = L;
                        in_ch[1] = R;
                        in_ch[2] = 0.0f;
                        in_ch[3] = 0.0f;
                        in_ch[4] = 0.0f;
                        in_ch[5] = 0.0f;
                    }
                } else if (mix_channels == 1) {
                    float mono = float_src[f];
                    in_ch[0] = mono;
                    in_ch[1] = mono;
                    in_ch[2] = 0.0f;
                    in_ch[3] = 0.0f;
                    in_ch[4] = 0.0f;
                    in_ch[5] = 0.0f;
                } else {
                    for (int c = 0; c < mix_channels && c < 6; ++c) {
                        in_ch[c] = float_src[f * mix_channels + c];
                    }
                }
            }

            // 2. 6x6 Routing Matrix
            for (int c = 0; c < 6; ++c) {
                int src_idx = (std::clamp)(m_state.settings.routing_map[c], 0, 5);
                routed[c] = in_ch[src_idx];
            }

            // 3. Operating Modes & Smooth Gain Ramping & Phase Invert
            for (int c = 0; c < 6; ++c) {
                if (!m_state.settings.bit_perfect_mode && m_state.settings.phase_inverts[c]) {
                    routed[c] = -routed[c];
                }
                m_smoothed_gain[c] += 0.005f * (target_gain[c] - m_smoothed_gain[c]);
                routed[c] *= m_smoothed_gain[c];
            }
        }

        // 4. Solo & Mute Evaluation
        for (int c = 0; c < 6; ++c) {
            if (m_state.settings.mute_master || m_state.settings.mutes[c] || (any_solo && !m_state.settings.solos[c])) {
                routed[c] = 0.0f;
            }
        }

        // 5. Soft Limiter
        if (m_state.settings.limiter_enabled && !m_state.settings.bit_perfect_mode) {
            for (int c = 0; c < 6; ++c) {
                routed[c] = ApplySoftLimiter(routed[c]);
            }
        }

        // 6. Clipping Detection, Peak Tracking, & Output PCM Conversion
        int32_t ch_samples[6];
        for (int c = 0; c < 6; ++c) {
            float s = routed[c];
            if (std::fabs(s) >= 0.988f) {
                ch_clipped[c] = true;
            }
            ch_max[c] = (std::max)(ch_max[c], std::fabs(s));

            s = (std::clamp)(s, -1.0f, 1.0f);
            ch_samples[c] = (int32_t)(s * 2147483647.0f);
        }

        // Buffer into pcm6ch_block
        for (int c = 0; c < 6; ++c) {
            pcm6ch_block[frame_in_block * 6 + c] = ch_samples[c];
        }

        frame_in_block++;
        if (frame_in_block == 48) {
            frame_in_block = 0;

            size_t enc_len = 0;
            uint8_t enc_flags = HT306_FLAG_BITDEPTH_32;
            if (mix_rate >= 88200) {
                enc_flags |= HT306_FLAG_RATE_96KHZ;
            }

            HT306_CodecStatus_t st = ht306_lossless_encode_frame_flags(
                pcm6ch_block.data(),
                48,
                m_seq++,
                enc_flags,
                usb_packet.data(),
                usb_packet.size(),
                &enc_len
            );

            if (st == HT306_CODEC_OK && enc_len > 0) {
                m_transport.WritePacket(usb_packet.data(), enc_len, 50);
                m_state.telemetry.total_frames += 48;
                m_state.telemetry.total_packets += 1;
                m_state.telemetry.throughput_kbs = (enc_len * ((float)mix_rate / 48.0f)) / 1024.0f;
            }
        }
    }

    // Update VU meters
    for (int c = 0; c < 6; ++c) {
        float peak = ch_max[c];
        float db = -60.0f;
        if (peak > 1e-5f) {
            db = 20.0f * std::log10(peak);
        }
        if (db < -60.0f) db = -60.0f;
        if (db > 0.0f) db = 0.0f;

        // Smooth decay
        m_state.meters[c].peak_db = (m_state.meters[c].peak_db * 0.7f) + (db * 0.3f);
        if (db > m_state.meters[c].peak_hold_db) {
            m_state.meters[c].peak_hold_db = db;
        } else {
            m_state.meters[c].peak_hold_db -= 0.5f; // slow decay
        }
        m_state.meters[c].clipped = ch_clipped[c];
    }
}

void AudioCapture::CaptureLoop() {
    // Elevate thread priority for Windows Pro Audio
    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    HRESULT hr_co = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    std::vector<int32_t> pcm6ch_block(48 * 6, 0); // 48 frames x 6 channels
    std::vector<uint8_t> usb_packet(2048, 0);
    int frame_in_block = 0;

    while (m_running) {
        int current_dev_idx = m_state.active_device_index;

        IMMDeviceEnumerator* enumerator = NULL;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            NULL,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&enumerator
        );

        if (FAILED(hr) || !enumerator) {
            Sleep(50);
            continue;
        }

        IMMDevice* device = NULL;
        if (current_dev_idx > 0 && current_dev_idx < (int)m_state.available_devices.size() &&
            !m_state.available_devices[current_dev_idx].id.empty()) {
            enumerator->GetDevice(m_state.available_devices[current_dev_idx].id.c_str(), &device);
        }
        if (!device) {
            enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        }
        enumerator->Release();
        enumerator = NULL;

        if (!device) {
            Sleep(50);
            continue;
        }

        IAudioClient* audio_client = NULL;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audio_client);
        device->Release();
        device = NULL;

        if (FAILED(hr) || !audio_client) {
            Sleep(50);
            continue;
        }

        WAVEFORMATEX* pwfx = NULL;
        hr = audio_client->GetMixFormat(&pwfx);
        if (FAILED(hr) || !pwfx) {
            audio_client->Release();
            Sleep(50);
            continue;
        }

        // Request WASAPI Shared Loopback (100ms buffer)
        hr = audio_client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            1000000, // 100ms in 100ns units
            0,
            pwfx,
            NULL
        );

        if (FAILED(hr)) {
            CoTaskMemFree(pwfx);
            audio_client->Release();
            Sleep(50);
            continue;
        }

        IAudioCaptureClient* capture_client = NULL;
        hr = audio_client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture_client);
        if (FAILED(hr) || !capture_client) {
            CoTaskMemFree(pwfx);
            audio_client->Release();
            Sleep(50);
            continue;
        }

        hr = audio_client->Start();
        if (FAILED(hr)) {
            capture_client->Release();
            audio_client->Release();
            CoTaskMemFree(pwfx);
            Sleep(50);
            continue;
        }

        uint32_t mix_rate = pwfx ? pwfx->nSamplesPerSec : 96000;
        uint16_t mix_channels = pwfx ? pwfx->nChannels : 2;
        m_state.telemetry.sample_rate = mix_rate;
        m_state.telemetry.channels = 6;
        m_state.telemetry.bit_depth = 32;

        LARGE_INTEGER qpc_freq;
        QueryPerformanceFrequency(&qpc_freq);
        LARGE_INTEGER qpc_last;
        QueryPerformanceCounter(&qpc_last);

        while (m_running) {
            // Dynamic Audio Device Selection & Hot-Swap check
            if (m_state.active_device_index != current_dev_idx) {
                // Break out of inner loop to re-initialize on the newly selected endpoint
                break;
            }

            int active_sweep_ch = m_state.sweep_active_channel.load();
            bool is_sweep = m_state.sweep_running.load() || (active_sweep_ch >= 0 && active_sweep_ch < 6);

            UINT32 packet_length = 0;
            hr = capture_client->GetNextPacketSize(&packet_length);
            if (FAILED(hr)) {
                // Device may have been disconnected or invalidated
                break;
            }

            if (packet_length == 0) {
                if (is_sweep) {
                    LARGE_INTEGER qpc_now;
                    QueryPerformanceCounter(&qpc_now);
                    double dt = (double)(qpc_now.QuadPart - qpc_last.QuadPart) / (double)qpc_freq.QuadPart;
                    int blocks = (std::clamp)((int)(dt * mix_rate / 48.0), 1, 10);
                    qpc_last.QuadPart += (LONGLONG)((blocks * 48.0 / (double)mix_rate) * (double)qpc_freq.QuadPart);
                    if (qpc_now.QuadPart - qpc_last.QuadPart > qpc_freq.QuadPart) {
                        qpc_last = qpc_now;
                    }

                    for (int b = 0; b < blocks; ++b) {
                        ProcessAudioFrames(
                            nullptr,
                            48,
                            AUDCLNT_BUFFERFLAGS_SILENT,
                            mix_channels,
                            mix_rate,
                            pcm6ch_block,
                            frame_in_block,
                            usb_packet
                        );
                    }
                    Sleep(1);
                    continue;
                }
                QueryPerformanceCounter(&qpc_last);
                Sleep(2);
                continue;
            }

            QueryPerformanceCounter(&qpc_last);

            BYTE* pData = NULL;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            hr = capture_client->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
            if (FAILED(hr)) {
                break;
            }

            ProcessAudioFrames(
                reinterpret_cast<const float*>(pData),
                numFramesAvailable,
                flags,
                mix_channels,
                mix_rate,
                pcm6ch_block,
                frame_in_block,
                usb_packet
            );

            capture_client->ReleaseBuffer(numFramesAvailable);
        }

        audio_client->Stop();
        capture_client->Release();
        audio_client->Release();
        if (pwfx) {
            CoTaskMemFree(pwfx);
            pwfx = NULL;
        }

        if (m_running && m_state.active_device_index != current_dev_idx) {
            // Hot-swap re-attach pause
            Sleep(30);
        }
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    if (SUCCEEDED(hr_co)) CoUninitialize();
}

} // namespace ht306::audio
