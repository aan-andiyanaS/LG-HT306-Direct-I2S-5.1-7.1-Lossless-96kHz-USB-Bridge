#include "audio_capture.h"
#include "audio_policy_manager.h"
#include "lossless_codec.h"
#include <avrt.h>
#include <endpointvolume.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmsystem.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>
#include <cstdint>

#ifndef AUDCLNT_E_DEVICE_INVALIDATED
#define AUDCLNT_E_DEVICE_INVALIDATED static_cast<HRESULT>(0x88890004L)
#endif
#ifndef AUDCLNT_E_DEVICE_IN_USE
#define AUDCLNT_E_DEVICE_IN_USE static_cast<HRESULT>(0x8889000AL)
#endif
#ifndef AUDCLNT_E_ENDPOINT_CREATE_FAILED
#define AUDCLNT_E_ENDPOINT_CREATE_FAILED static_cast<HRESULT>(0x8889000FL)
#endif
#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
static const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_CAP_GUID = {
    0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};
#define KSDATAFORMAT_SUBTYPE_IEEE_FLOAT KSDATAFORMAT_SUBTYPE_IEEE_FLOAT_CAP_GUID
#endif
#ifndef KSDATAFORMAT_SUBTYPE_PCM
static const GUID KSDATAFORMAT_SUBTYPE_PCM_CAP_GUID = {
    0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
};
#define KSDATAFORMAT_SUBTYPE_PCM KSDATAFORMAT_SUBTYPE_PCM_CAP_GUID
#endif

namespace ht306::audio {

namespace {

SampleRateMode ToDspMode(SampleRatePreference pref) {
    switch (pref) {
        case SampleRatePreference::LOCK_96K: return SampleRateMode::LOCK_96K;
        case SampleRatePreference::LOCK_48K: return SampleRateMode::LOCK_48K;
        case SampleRatePreference::AUTO:
        default:
            return SampleRateMode::AUTO;
    }
}

std::string HexHr(HRESULT hr) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    return oss.str();
}

std::wstring QueryCurrentDefaultRenderId(IMMDeviceEnumerator* enumerator) {
    if (!enumerator) return {};
    IMMDevice* dev = NULL;
    std::wstring out_id;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
        LPWSTR raw_id = NULL;
        if (SUCCEEDED(dev->GetId(&raw_id)) && raw_id) {
            out_id = raw_id;
            CoTaskMemFree(raw_id);
        }
        dev->Release();
    }
    return out_id;
}

void CheckCaptureEndpointGain(IMMDevice* cap_dev, AppState& state) {
    if (!cap_dev) return;
    IAudioEndpointVolume* ep_vol = NULL;
    if (SUCCEEDED(cap_dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL,
                                    reinterpret_cast<void**>(&ep_vol))) && ep_vol) {
        BOOL muted = FALSE;
        float scalar = 1.0f;
        ep_vol->GetMute(&muted);
        ep_vol->GetMasterVolumeLevelScalar(&scalar);
        if (muted) {
            if (SUCCEEDED(ep_vol->SetMute(FALSE, NULL))) {
                state.AddLog(LogLevel::LOG_INFO, "AudioCapture",
                    "LG HT306 Loopback Stream sempat MUTE — otomatis dibuka (unmute) agar sinyal audio masuk.");
            } else {
                state.AddLog(LogLevel::LOG_WARN, "AudioCapture",
                    "LG HT306 Loopback Stream (Recording) dalam kondisi MUTE di Windows Sound — audio tidak akan keluar!");
            }
        }
        if (scalar < 0.99f) {
            state.AddLog(LogLevel::LOG_WARN, "AudioCapture",
                "Level Recording LG HT306 Loopback Stream < 100% — atur ke 100 di Windows Sound agar bit-accurate.");
        }
        ep_vol->Release();
    }
}

struct OpenedStream {
    IAudioClient* client = NULL;
    IAudioCaptureClient* capture = NULL;
    WAVEFORMATEX* pwfx = NULL;
    std::wstring resolved_render_id;
    bool is_vbcable_capture = false;
    bool is_ht306_target = false;
    HRESULT last_hr = S_OK;

    void Release() {
        if (capture) {
            capture->Release();
            capture = NULL;
        }
        if (client) {
            client->Release();
            client = NULL;
        }
        if (pwfx) {
            CoTaskMemFree(pwfx);
            pwfx = NULL;
        }
    }
};

bool InitializeWasapiClient(
    IMMDevice* device,
    DWORD stream_flags,
    REFERENCE_TIME buffer_duration,
    IAudioClient** out_client,
    IAudioCaptureClient** out_capture,
    WAVEFORMATEX** out_pwfx,
    HRESULT* out_hr
) {
    *out_client = NULL;
    *out_capture = NULL;
    *out_pwfx = NULL;

    IAudioClient* client = NULL;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                                  reinterpret_cast<void**>(&client));
    if (FAILED(hr) || !client) {
        if (out_hr) *out_hr = hr;
        return false;
    }

    WAVEFORMATEX* pwfx = NULL;
    hr = client->GetMixFormat(&pwfx);
    if (FAILED(hr) || !pwfx) {
        client->Release();
        if (out_hr) *out_hr = hr;
        return false;
    }

    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags, buffer_duration, 0, pwfx, NULL);
    if (FAILED(hr)) {
        CoTaskMemFree(pwfx);
        client->Release();
        if (out_hr) *out_hr = hr;
        return false;
    }

    IAudioCaptureClient* capture = NULL;
    hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
    if (FAILED(hr) || !capture) {
        CoTaskMemFree(pwfx);
        client->Release();
        if (out_hr) *out_hr = hr;
        return false;
    }

    hr = client->Start();
    if (FAILED(hr)) {
        capture->Release();
        CoTaskMemFree(pwfx);
        client->Release();
        if (out_hr) *out_hr = hr;
        return false;
    }

    *out_client = client;
    *out_capture = capture;
    *out_pwfx = pwfx;
    if (out_hr) *out_hr = S_OK;
    return true;
}

} // namespace

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

void AudioCapture::LogThrottled(LogLevel level, const std::string& key, const std::string& msg) {
    if (m_last_status_key == key) {
        return;
    }
    m_last_status_key = key;
    m_state.AddLog(level, "AudioCapture", msg);
}

AudioStreamFormat AudioCapture::ParseWaveFormat(const WAVEFORMATEX* pwfx) {
    AudioStreamFormat fmt{};
    if (!pwfx || pwfx->nChannels == 0 || pwfx->nSamplesPerSec == 0) {
        return fmt;
    }
    fmt.channels = pwfx->nChannels;
    fmt.sample_rate = pwfx->nSamplesPerSec;
    fmt.valid_bits = pwfx->wBitsPerSample;

    if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        fmt.format = (pwfx->wBitsPerSample == 32) ? SampleFormat::IEEE_FLOAT32 : SampleFormat::UNSUPPORTED;
    } else if (pwfx->wFormatTag == WAVE_FORMAT_PCM) {
        if (pwfx->wBitsPerSample == 16) fmt.format = SampleFormat::PCM16;
        else if (pwfx->wBitsPerSample == 24) fmt.format = SampleFormat::PCM24_PACKED;
        else if (pwfx->wBitsPerSample == 32) fmt.format = SampleFormat::PCM32;
    } else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
               pwfx->cbSize >= (sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pwfx);
        fmt.valid_bits = ext->Samples.wValidBitsPerSample ? ext->Samples.wValidBitsPerSample : pwfx->wBitsPerSample;
        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            fmt.format = (pwfx->wBitsPerSample == 32) ? SampleFormat::IEEE_FLOAT32 : SampleFormat::UNSUPPORTED;
        } else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            if (pwfx->wBitsPerSample == 16) fmt.format = SampleFormat::PCM16;
            else if (pwfx->wBitsPerSample == 24) fmt.format = SampleFormat::PCM24_PACKED;
            else if (pwfx->wBitsPerSample == 32) fmt.format = SampleFormat::PCM32;
        }
    }
    return fmt;
}

float AudioCapture::ApplySoftLimiter(float x) {
    const float threshold = 0.85f;
    const float ceiling = 0.9885f;
    const float headroom = ceiling - threshold;
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
        return pink * 0.11f * amp;
    } else if (m_state.test_signal_type == 0) {
        const float f_start = 40.0f;
        const float f_end = 16000.0f;
        const float duration = 1.5f;

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
        m_sine_phase += (two_pi * 1000.0f) / (float)mix_rate;
        if (m_sine_phase >= two_pi) {
            m_sine_phase = std::fmod(m_sine_phase, two_pi);
        }
        return std::sin(m_sine_phase) * amp;
    }
}

void AudioCapture::NormalizeChannelsTo6Ch(
    const float* src6,
    float dst6[6],
    uint16_t in_channels,
    bool bit_perfect
) const {
    for (int c = 0; c < 6; ++c) {
        dst6[c] = src6[c];
    }
    if (in_channels >= 6) {
        if (!bit_perfect) {
            if (m_state.settings.routing_preset == RoutingPreset::STEREO_UPMIX) {
                float L = dst6[0];
                float R = dst6[1];
                dst6[2] = (L + R) * 0.5f;
                dst6[3] = (L + R) * 0.35f;
                dst6[4] = L * 0.707f;
                dst6[5] = R * 0.707f;
            } else if (m_state.settings.routing_preset == RoutingPreset::DUAL_MONO) {
                float L = dst6[0];
                float R = dst6[1];
                dst6[0] = dst6[2] = dst6[4] = L;
                dst6[1] = dst6[3] = dst6[5] = R;
            }
        }
    } else if (in_channels == 2) {
        float L = src6[0];
        float R = src6[1];
        if (!bit_perfect && m_state.settings.routing_preset == RoutingPreset::STEREO_UPMIX) {
            dst6[0] = L;
            dst6[1] = R;
            dst6[2] = (L + R) * 0.5f;
            dst6[3] = (L + R) * 0.35f;
            dst6[4] = L * 0.707f;
            dst6[5] = R * 0.707f;
        } else if (!bit_perfect && m_state.settings.routing_preset == RoutingPreset::DUAL_MONO) {
            dst6[0] = dst6[2] = dst6[4] = L;
            dst6[1] = dst6[3] = dst6[5] = R;
        } else {
            dst6[0] = L;
            dst6[1] = R;
            dst6[2] = dst6[3] = dst6[4] = dst6[5] = 0.0f;
        }
    } else if (in_channels == 1) {
        float mono = src6[0];
        dst6[0] = mono;
        dst6[1] = mono;
        dst6[2] = dst6[3] = dst6[4] = dst6[5] = 0.0f;
    }
}

void AudioCapture::EmitProcessedFrames6Ch(
    const float* frames_6ch,
    size_t num_frames,
    uint32_t output_rate,
    bool is_sweep,
    bool allow_silence_gate,
    std::vector<int32_t>& pcm6ch_block,
    int& frame_in_block,
    std::vector<uint8_t>& usb_packet
) {
    if (!frames_6ch || num_frames == 0 || output_rate == 0) {
        return;
    }

    float ch_max[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    bool ch_clipped[6] = { false, false, false, false, false, false };

    const bool bit_perfect = m_state.settings.bit_perfect_mode;
    float target_gain[6];
    if (bit_perfect) {
        for (int c = 0; c < 6; ++c) {
            target_gain[c] = 1.0f;
            m_smoothed_gain[c] = 1.0f;
        }
    } else {
        float master_db = (std::clamp)(m_state.settings.master_volume_db, -60.0f, 6.0f);
        float master_lin = std::pow(10.0f, master_db / 20.0f);
        for (int c = 0; c < 6; ++c) {
            target_gain[c] = master_lin * std::pow(10.0f, m_state.settings.trims[c] / 20.0f);
        }
    }

    bool any_solo = false;
    for (int c = 0; c < 6; ++c) {
        if (m_state.settings.solos[c]) {
            any_solo = true;
            break;
        }
    }

    const uint32_t silence_limit = ((output_rate / 48u) * 6u) / 10u; // ~600 ms

    for (size_t f = 0; f < num_frames; ++f) {
        const float* in6 = &frames_6ch[f * 6u];
        float routed[6];

        if (is_sweep || bit_perfect) {
            for (int c = 0; c < 6; ++c) {
                routed[c] = in6[c];
            }
        } else {
            for (int c = 0; c < 6; ++c) {
                int src_idx = (std::clamp)(m_state.settings.routing_map[c], 0, 5);
                float val = in6[src_idx];
                if (m_state.settings.phase_inverts[c]) {
                    val = -val;
                }
                m_smoothed_gain[c] += 0.005f * (target_gain[c] - m_smoothed_gain[c]);
                routed[c] = val * m_smoothed_gain[c];
            }
        }

        if (!is_sweep) {
            for (int c = 0; c < 6; ++c) {
                if (m_state.settings.mute_master || m_state.settings.mutes[c] ||
                    (any_solo && !m_state.settings.solos[c])) {
                    routed[c] = 0.0f;
                }
            }
            if (m_state.settings.limiter_enabled && !bit_perfect) {
                for (int c = 0; c < 6; ++c) {
                    routed[c] = ApplySoftLimiter(routed[c]);
                }
            }
        }

        for (int c = 0; c < 6; ++c) {
            float s = routed[c];
            float abs_s = std::fabs(s);
            if (abs_s >= 0.988f) {
                ch_clipped[c] = true;
            }
            if (abs_s > ch_max[c]) {
                ch_max[c] = abs_s;
            }
            if (abs_s > m_current_block_peak) {
                m_current_block_peak = abs_s;
            }
            pcm6ch_block[static_cast<size_t>(frame_in_block) * 6u + c] = FloatToInt32Safe(s);
        }

        frame_in_block++;
        if (frame_in_block == 48) {
            frame_in_block = 0;
            bool is_block_silent = (m_current_block_peak < 1e-6f);
            m_current_block_peak = 0.0f;

            if (allow_silence_gate && !is_sweep && is_block_silent) {
                m_silent_blocks_streak++;
            } else {
                m_silent_blocks_streak = 0;
            }

            if (allow_silence_gate && !is_sweep && m_silent_blocks_streak > silence_limit) {
                // Keep RP2350 idle so its LED returns cleanly to Blue Standby
                continue;
            }

            size_t enc_len = 0;
            uint8_t enc_flags = HT306_FLAG_BITDEPTH_32;
            if (output_rate >= 88200u) {
                enc_flags |= HT306_FLAG_RATE_96KHZ;
            }

            HT306_CodecStatus_t st = ht306_lossless_encode_frame_flags(
                pcm6ch_block.data(),
                48,
                m_seq++,
                enc_flags,
                usb_packet.data(),
                HT306_MAX_PACKET_BYTES,
                &enc_len
            );

            if (st == HT306_CODEC_OK && enc_len > 0 && enc_len <= HT306_MAX_PACKET_BYTES) {
                m_burst_packets_sent++;
                if (m_burst_packets_sent > 0 && (m_burst_packets_sent % 8u) == 0u) {
                    // Micro-pace large WASAPI bursts so RP2350's 16-block ring buffer never overflows
                    Sleep(2);
                }
                m_transport.WritePacket(usb_packet.data(), enc_len, 50);
                m_state.telemetry.total_frames.fetch_add(48);
                m_state.telemetry.total_packets.fetch_add(1);
                m_state.telemetry.throughput_kbs.store(
                    (static_cast<float>(enc_len) * (static_cast<float>(output_rate) / 48.0f)) / 1024.0f
                );
            }
        }
    }

    for (int c = 0; c < 6; ++c) {
        float peak = ch_max[c];
        float db = -60.0f;
        if (peak > 1e-5f) {
            db = 20.0f * std::log10(peak);
        }
        if (db < -60.0f) db = -60.0f;
        if (db > 0.0f) db = 0.0f;

        m_state.meters[c].peak_db = (m_state.meters[c].peak_db * 0.7f) + (db * 0.3f);
        if (db > m_state.meters[c].peak_hold_db) {
            m_state.meters[c].peak_hold_db = db;
        } else {
            m_state.meters[c].peak_hold_db -= 0.5f;
        }
        m_state.meters[c].clipped = ch_clipped[c];
    }
}

void AudioCapture::PumpSweepIfActive(
    LARGE_INTEGER qpc_freq,
    LARGE_INTEGER& qpc_last,
    uint32_t sweep_rate,
    std::vector<int32_t>& pcm6ch_block,
    int& frame_in_block,
    std::vector<uint8_t>& usb_packet
) {
    int active_sweep_ch = m_state.sweep_active_channel.load();
    bool is_sweep = m_state.sweep_running.load() || (active_sweep_ch >= 0 && active_sweep_ch < 6);
    if (!is_sweep) {
        m_last_sweep_ch = -1;
        QueryPerformanceCounter(&qpc_last);
        return;
    }

    if (active_sweep_ch < 0 || active_sweep_ch >= 6) {
        active_sweep_ch = 0;
    }
    if (active_sweep_ch != m_last_sweep_ch) {
        m_last_sweep_ch = active_sweep_ch;
        m_chirp_t = 0.0f;
        m_chirp_phase = 0.0f;
    }

    LARGE_INTEGER qpc_now;
    QueryPerformanceCounter(&qpc_now);
    double dt = static_cast<double>(qpc_now.QuadPart - qpc_last.QuadPart) / static_cast<double>(qpc_freq.QuadPart);
    int blocks = static_cast<int>(dt * static_cast<double>(sweep_rate) / 48.0);
    if (blocks <= 0) {
        return;
    }
    if (blocks > 12) {
        blocks = 12;
        qpc_last = qpc_now;
    } else {
        qpc_last.QuadPart += static_cast<LONGLONG>(
            (static_cast<double>(blocks * 48) / static_cast<double>(sweep_rate)) *
            static_cast<double>(qpc_freq.QuadPart)
        );
    }

    float test_dbfs = (std::clamp)(m_state.test_level_dbfs, -60.0f, 0.0f);
    float test_amp = std::pow(10.0f, test_dbfs / 20.0f);

    const size_t total_frames = static_cast<size_t>(blocks) * 48u;
    m_in_frames_6ch.assign(total_frames * 6u, 0.0f);
    for (size_t f = 0; f < total_frames; ++f) {
        m_in_frames_6ch[f * 6u + static_cast<size_t>(active_sweep_ch)] =
            GenerateTestSample(sweep_rate, test_amp);
    }

    m_burst_packets_sent = 0;
    EmitProcessedFrames6Ch(
        m_in_frames_6ch.data(),
        total_frames,
        sweep_rate,
        true,
        false,
        pcm6ch_block,
        frame_in_block,
        usb_packet
    );
}

void AudioCapture::CaptureLoop() {
    timeBeginPeriod(1);
    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    HRESULT hr_co = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    std::vector<int32_t> pcm6ch_block(48 * 6, 0);
    std::vector<uint8_t> usb_packet(HT306_MAX_PACKET_BYTES, 0);
    int frame_in_block = 0;

    LARGE_INTEGER qpc_freq;
    QueryPerformanceFrequency(&qpc_freq);
    LARGE_INTEGER qpc_last;
    QueryPerformanceCounter(&qpc_last);

    while (m_running) {
        int current_dev_idx = 0;
        AudioEndpointInfo active_dev = m_state.GetActiveDeviceSnapshot(&current_dev_idx);
        uint32_t current_gen = m_state.capture_reinit_generation.load(std::memory_order_acquire);
        SampleRatePreference current_rate_mode = m_state.sample_rate_mode.load();

        OpenedStream stream{};
        bool stream_opened = false;

        IMMDeviceEnumerator* enumerator = NULL;
        HRESULT hr_enum = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator)
        );

        if (SUCCEEDED(hr_enum) && enumerator) {
            IMMDevice* render_dev = NULL;
            if (!active_dev.id.empty()) {
                enumerator->GetDevice(active_dev.id.c_str(), &render_dev);
            }
            if (!render_dev) {
                enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &render_dev);
            }

            if (render_dev) {
                LPWSTR raw_id = NULL;
                if (SUCCEEDED(render_dev->GetId(&raw_id)) && raw_id) {
                    stream.resolved_render_id = raw_id;
                    CoTaskMemFree(raw_id);
                }
                std::wstring ht306_render_id = AudioPolicyManager::Instance().GetRenderEndpointId();
                stream.is_ht306_target = (!ht306_render_id.empty() &&
                                          stream.resolved_render_id == ht306_render_id);

                if (stream.is_ht306_target) {
                    // Step A (PRIMARY for HT306): Capture directly from VB-Cable Output (eCapture).
                    // Because this endpoint stays separate from eRender, JRiver WASAPI Exclusive never clashes with us!
                    std::wstring cap_id = AudioPolicyManager::Instance().GetCaptureEndpointId();
                    if (!cap_id.empty()) {
                        IMMDevice* cap_dev = NULL;
                        HRESULT get_hr = enumerator->GetDevice(cap_id.c_str(), &cap_dev);
                        DWORD cap_state = 0;
                        if (SUCCEEDED(get_hr) && cap_dev) {
                            cap_dev->GetState(&cap_state);
                        }
                        if (!cap_dev || cap_state != DEVICE_STATE_ACTIVE) {
                            if (cap_dev) {
                                cap_dev->Release();
                                cap_dev = NULL;
                            }
                            AudioPolicyManager::Instance().EnsureCaptureEndpointActive();
                            enumerator->GetDevice(cap_id.c_str(), &cap_dev);
                        }

                        if (cap_dev) {
                            HRESULT cap_hr = S_OK;
                            stream_opened = InitializeWasapiClient(
                                cap_dev,
                                0, // Recording endpoint MUST NOT pass AUDCLNT_STREAMFLAGS_LOOPBACK
                                300000, // 30 ms
                                &stream.client,
                                &stream.capture,
                                &stream.pwfx,
                                &cap_hr
                            );
                            if (stream_opened) {
                                stream.is_vbcable_capture = true;
                                CheckCaptureEndpointGain(cap_dev, m_state);
                            } else {
                                stream.last_hr = cap_hr;
                                if (cap_hr == E_ACCESSDENIED) {
                                    LogThrottled(LogLevel::LOG_WARN, "mic_denied",
                                        "Akses LG HT306 Loopback Stream ditolak (E_ACCESSDENIED). Aktifkan izin Mikrofon di Windows Privacy Settings!");
                                }
                            }
                            cap_dev->Release();
                        }
                    }

                    // Step B (FALLBACK for HT306): Only try eRender Shared Loopback if eCapture could not open
                    if (!stream_opened) {
                        HRESULT render_hr = S_OK;
                        stream_opened = InitializeWasapiClient(
                            render_dev,
                            AUDCLNT_STREAMFLAGS_LOOPBACK,
                            400000, // 40 ms
                            &stream.client,
                            &stream.capture,
                            &stream.pwfx,
                            &render_hr
                        );
                        if (!stream_opened && stream.last_hr == S_OK) {
                            stream.last_hr = render_hr;
                        }
                    }
                } else {
                    // Non-HT306 device (e.g. Realtek Speakers): use eRender Shared Loopback directly
                    stream_opened = InitializeWasapiClient(
                        render_dev,
                        AUDCLNT_STREAMFLAGS_LOOPBACK,
                        400000, // 40 ms
                        &stream.client,
                        &stream.capture,
                        &stream.pwfx,
                        &stream.last_hr
                    );
                }

                render_dev->Release();
                render_dev = NULL;
            }
            enumerator->Release();
            enumerator = NULL;
        }

        if (!stream_opened) {
            bool is_sweep = m_state.sweep_running.load() ||
                            (m_state.sweep_active_channel.load() >= 0 && m_state.sweep_active_channel.load() < 6);
            m_state.telemetry.capture_path.store(is_sweep ? CapturePathMode::SWEEP_ONLY : CapturePathMode::DISCONNECTED);

            uint32_t sweep_rate = (current_rate_mode == SampleRatePreference::LOCK_48K) ? 48000u : 96000u;
            m_state.telemetry.sample_rate.store(is_sweep ? sweep_rate : 0u);
            m_state.telemetry.input_sample_rate.store(0u);
            m_state.telemetry.rate_clamped.store(false);

            if (stream.last_hr != S_OK && stream.last_hr != E_ACCESSDENIED) {
                LogThrottled(LogLevel::LOG_WARN, "cap_open_fail_" + HexHr(stream.last_hr),
                    "Menunggu endpoint audio siap (" + HexHr(stream.last_hr) + "). Matikan Exclusive Mode di player atau cek izin Mikrofon.");
            }

            // Non-blocking 500ms retry backoff while still driving Sweep Test smoothly
            ULONGLONG retry_until = GetTickCount64() + 500;
            while (m_running && GetTickCount64() < retry_until) {
                if (m_state.capture_reinit_generation.load(std::memory_order_acquire) != current_gen ||
                    m_state.active_device_index.load() != current_dev_idx ||
                    m_state.sample_rate_mode.load() != current_rate_mode) {
                    break;
                }
                bool sweep_now = m_state.sweep_running.load() ||
                                 (m_state.sweep_active_channel.load() >= 0 && m_state.sweep_active_channel.load() < 6);
                m_state.telemetry.capture_path.store(sweep_now ? CapturePathMode::SWEEP_ONLY : CapturePathMode::DISCONNECTED);
                m_state.telemetry.sample_rate.store(sweep_now ? sweep_rate : 0u);
                PumpSweepIfActive(qpc_freq, qpc_last, sweep_rate, pcm6ch_block, frame_in_block, usb_packet);
                Sleep(1);
            }
            continue;
        }

        // Validate stream format cleanly (reject raw bitstream / non-PCM formats)
        AudioStreamFormat fmt = ParseWaveFormat(stream.pwfx);
        ResolvedRate rr = ResolveTargetSampleRate(ToDspMode(current_rate_mode), fmt.sample_rate);
        if (fmt.format == SampleFormat::UNSUPPORTED || !rr.valid_input) {
            LogThrottled(LogLevel::LOG_ERROR, "fmt_unsup",
                "Format audio endpoint tidak didukung (" + std::to_string(fmt.sample_rate) +
                " Hz, " + std::to_string(fmt.channels) + " ch). Pastikan PCM/Float dan non-encoded AC3/DTS.");
            stream.client->Stop();
            stream.Release();
            Sleep(250);
            continue;
        }

        // Clean slate for every re-initialization (no mixed-rate frame tail!)
        frame_in_block = 0;
        m_current_block_peak = 0.0f;
        m_silent_blocks_streak = 0;
        m_resampler.Configure(fmt.sample_rate, rr.target_rate);

        m_state.telemetry.capture_path.store(
            stream.is_vbcable_capture ? CapturePathMode::VBCABLE_CAPTURE : CapturePathMode::LOOPBACK
        );
        m_state.telemetry.input_sample_rate.store(fmt.sample_rate);
        m_state.telemetry.sample_rate.store(rr.target_rate);
        m_state.telemetry.rate_clamped.store(rr.clamped_from_high);
        m_state.telemetry.channels.store(6);
        m_state.telemetry.bit_depth.store(32);

        {
            std::string path_name = stream.is_vbcable_capture
                ? "VB-Cable Capture (LG HT306 Loopback Stream — Exclusive Safe)"
                : "WASAPI Shared Loopback";
            std::string key = path_name + "_" + std::to_string(fmt.sample_rate) + "_" + std::to_string(rr.target_rate);
            std::string msg = "Jalur aktif: " + path_name + " | Input " + std::to_string(fmt.sample_rate) +
                              " Hz (" + std::to_string(fmt.channels) + "ch) -> RP2350 " +
                              std::to_string(rr.target_rate) + " Hz";
            if (rr.clamped_from_high) {
                msg += " [Mode Auto: Dinetralkan <=96 kHz]";
            } else if (rr.resampled) {
                msg += " [Resampled]";
            }
            LogThrottled(LogLevel::LOG_INFO, key, msg);
        }

        QueryPerformanceCounter(&qpc_last);
        ULONGLONG last_def_check_tick = GetTickCount64();

        while (m_running) {
            if (m_state.capture_reinit_generation.load(std::memory_order_acquire) != current_gen ||
                m_state.active_device_index.load() != current_dev_idx ||
                m_state.sample_rate_mode.load() != current_rate_mode) {
                break;
            }

            // If capturing from VB-Cable while set to Default Windows Device (index 0),
            // verify every 1s that Windows default playback is still HT306.
            if (stream.is_vbcable_capture && current_dev_idx == 0) {
                ULONGLONG now_tick = GetTickCount64();
                if (now_tick - last_def_check_tick >= 1000) {
                    last_def_check_tick = now_tick;
                    IMMDeviceEnumerator* chk_enum = NULL;
                    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&chk_enum))) && chk_enum) {
                        std::wstring cur_def = QueryCurrentDefaultRenderId(chk_enum);
                        chk_enum->Release();
                        if (!cur_def.empty() && cur_def != stream.resolved_render_id) {
                            break;
                        }
                    }
                }
            }

            int active_sweep_ch = m_state.sweep_active_channel.load();
            bool is_sweep = m_state.sweep_running.load() || (active_sweep_ch >= 0 && active_sweep_ch < 6);

            UINT32 packet_length = 0;
            HRESULT hr = stream.capture->GetNextPacketSize(&packet_length);
            if (FAILED(hr)) {
                break;
            }

            if (is_sweep) {
                // Drain pending capture packets so driver ring doesn't stall while sweep runs at target_rate
                while (packet_length > 0) {
                    BYTE* pDummy = NULL;
                    UINT32 nFrames = 0;
                    DWORD flags = 0;
                    if (FAILED(stream.capture->GetBuffer(&pDummy, &nFrames, &flags, NULL, NULL))) {
                        break;
                    }
                    stream.capture->ReleaseBuffer(nFrames);
                    if (FAILED(stream.capture->GetNextPacketSize(&packet_length))) {
                        break;
                    }
                }
                PumpSweepIfActive(qpc_freq, qpc_last, rr.target_rate, pcm6ch_block, frame_in_block, usb_packet);
                Sleep(1);
                continue;
            }

            if (packet_length == 0) {
                QueryPerformanceCounter(&qpc_last);
                Sleep(2);
                continue;
            }

            while (packet_length > 0 && m_running) {
                BYTE* pData = NULL;
                UINT32 numFramesAvailable = 0;
                DWORD flags = 0;

                hr = stream.capture->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
                if (FAILED(hr)) {
                    packet_length = 0;
                    break;
                }

                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                    m_resampler.Reset();
                    frame_in_block = 0;
                    m_current_block_peak = 0.0f;
                }

                bool is_silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) || (pData == nullptr);
                if (numFramesAvailable > 0) {
                    m_in_frames_6ch.resize(static_cast<size_t>(numFramesAvailable) * 6u);
                    if (is_silent) {
                        std::fill(m_in_frames_6ch.begin(), m_in_frames_6ch.end(), 0.0f);
                    } else {
                        float decoded[6];
                        for (UINT32 f = 0; f < numFramesAvailable; ++f) {
                            DecodeInterleavedFrameToFloat6(pData, f, fmt, decoded);
                            NormalizeChannelsTo6Ch(
                                decoded,
                                &m_in_frames_6ch[static_cast<size_t>(f) * 6u],
                                fmt.channels,
                                m_state.settings.bit_perfect_mode
                            );
                        }
                    }

                    size_t out_frames = m_resampler.Process(
                        m_in_frames_6ch.data(),
                        numFramesAvailable,
                        m_resampled_6ch
                    );

                    m_burst_packets_sent = 0;
                    EmitProcessedFrames6Ch(
                        m_resampled_6ch.data(),
                        out_frames,
                        rr.target_rate,
                        false,
                        stream.is_vbcable_capture,
                        pcm6ch_block,
                        frame_in_block,
                        usb_packet
                    );
                }

                stream.capture->ReleaseBuffer(numFramesAvailable);
                QueryPerformanceCounter(&qpc_last);

                if (FAILED(stream.capture->GetNextPacketSize(&packet_length))) {
                    hr = E_FAIL;
                    break;
                }
            }
            if (FAILED(hr)) {
                break;
            }
        }

        stream.client->Stop();
        stream.Release();
        if (m_running) {
            Sleep(20);
        }
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    if (SUCCEEDED(hr_co)) CoUninitialize();
    timeEndPeriod(1);
}

} // namespace ht306::audio
