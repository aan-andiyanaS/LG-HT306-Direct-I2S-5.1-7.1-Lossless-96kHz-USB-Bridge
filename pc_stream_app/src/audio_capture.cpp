#include "audio_capture.h"
#include "lossless_codec.h"
#include <avrt.h>
#include <cmath>
#include <algorithm>
#include <vector>

namespace ht306::audio {

AudioCapture::AudioCapture(AppState& state, usb::UsbTransport& transport)
    : m_state(state), m_transport(transport) {
    m_stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
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

void AudioCapture::CaptureLoop() {
    // Elevate thread priority for Windows Pro Audio
    DWORD task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);

    IMMDeviceEnumerator* enumerator = NULL;
    CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (!enumerator) {
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        return;
    }

    IMMDevice* device = NULL;
    // Check if target device selected
    int dev_idx = m_state.active_device_index;
    if (dev_idx > 0 && dev_idx < (int)m_state.available_devices.size() && !m_state.available_devices[dev_idx].id.empty()) {
        enumerator->GetDevice(m_state.available_devices[dev_idx].id.c_str(), &device);
    }
    if (!device) {
        enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    }
    enumerator->Release();

    if (!device) {
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        return;
    }

    IAudioClient* audio_client = NULL;
    device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audio_client);
    device->Release();

    if (!audio_client) {
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        return;
    }

    WAVEFORMATEX* pwfx = NULL;
    audio_client->GetMixFormat(&pwfx);

    // Request WASAPI Shared Loopback
    HRESULT hr = audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        1000000, // 100ms buffer
        0,
        pwfx,
        NULL
    );

    if (FAILED(hr)) {
        if (pwfx) CoTaskMemFree(pwfx);
        audio_client->Release();
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        return;
    }

    IAudioCaptureClient* capture_client = NULL;
    audio_client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture_client);
    audio_client->Start();

    uint32_t mix_rate = pwfx ? pwfx->nSamplesPerSec : 96000;
    uint16_t mix_channels = pwfx ? pwfx->nChannels : 2;
    m_state.telemetry.sample_rate = mix_rate;
    m_state.telemetry.channels = 6;
    m_state.telemetry.bit_depth = 32;

    std::vector<int32_t> pcm6ch_block;
    pcm6ch_block.resize(48 * 6, 0); // 48 frames x 6 channels
    std::vector<uint8_t> usb_packet;
    usb_packet.resize(2048, 0);

    float sweep_phase = 0.0f;
    const float two_pi = 6.28318530718f;
    float sweep_freq = 1000.0f; // 1 kHz test tone

    while (m_running) {
        UINT32 packet_length = 0;
        capture_client->GetNextPacketSize(&packet_length);

        if (packet_length == 0) {
            Sleep(2);
            continue;
        }

        BYTE* pData = NULL;
        UINT32 numFramesAvailable = 0;
        DWORD flags = 0;

        hr = capture_client->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
        if (SUCCEEDED(hr)) {
            const float* float_src = reinterpret_cast<const float*>(pData);

            // Channel peaks accumulator for this buffer
            float ch_max[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

            int active_sweep_ch = m_state.sweep_active_channel.load();

            // Process frames in sub-blocks of 48 frames (0.5ms @ 96kHz)
            for (UINT32 f = 0; f < numFramesAvailable; ++f) {
                int frame_in_block = f % 48;

                int32_t ch_samples[6] = { 0, 0, 0, 0, 0, 0 };

                if (active_sweep_ch >= 0 && active_sweep_ch < 6) {
                    // Generate sweep tone on active channel
                    float s = std::sin(sweep_phase) * 0.125f; // -18 dBFS
                    sweep_phase += (two_pi * sweep_freq) / (float)mix_rate;
                    if (sweep_phase > two_pi) sweep_phase -= two_pi;

                    ch_samples[active_sweep_ch] = (int32_t)(s * 2147483647.0f);
                    ch_max[active_sweep_ch] = (std::max)(ch_max[active_sweep_ch], std::fabs(s));
                } else if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && float_src) {
                    if (mix_channels >= 6) {
                        for (int c = 0; c < 6; ++c) {
                            float val = float_src[f * mix_channels + c];
                            ch_max[c] = (std::max)(ch_max[c], std::fabs(val));
                            val = (std::clamp)(val, -1.0f, 1.0f);
                            ch_samples[c] = (int32_t)(val * 2147483647.0f);
                        }
                    } else if (mix_channels == 2) {
                        // Stereo mapping: FL and FR active, C/LFE/SL/SR silent (or direct pass)
                        float l = float_src[f * 2 + 0];
                        float r = float_src[f * 2 + 1];
                        ch_max[0] = (std::max)(ch_max[0], std::fabs(l));
                        ch_max[1] = (std::max)(ch_max[1], std::fabs(r));

                        l = (std::clamp)(l, -1.0f, 1.0f);
                        r = (std::clamp)(r, -1.0f, 1.0f);
                        ch_samples[0] = (int32_t)(l * 2147483647.0f); // FL
                        ch_samples[1] = (int32_t)(r * 2147483647.0f); // FR
                    }
                }

                // Master mute check
                if (m_state.settings.mute_master) {
                    for (int c = 0; c < 6; ++c) ch_samples[c] = 0;
                }

                // Copy to block
                for (int c = 0; c < 6; ++c) {
                    pcm6ch_block[frame_in_block * 6 + c] = ch_samples[c];
                }

                // When 48 frames reached, encode and transmit
                if (frame_in_block == 47) {
                    size_t enc_len = 0;
                    uint8_t flags = HT306_FLAG_RATE_96KHZ | HT306_FLAG_BITDEPTH_32;
                    HT306_CodecStatus_t st = ht306_lossless_encode_frame_flags(
                        pcm6ch_block.data(),
                        48,
                        m_seq++,
                        flags,
                        usb_packet.data(),
                        usb_packet.size(),
                        &enc_len
                    );

                    if (st == HT306_CODEC_OK && enc_len > 0) {
                        m_transport.WritePacket(usb_packet.data(), enc_len, 50);
                        m_state.telemetry.total_frames += 48;
                        m_state.telemetry.total_packets += 1;
                        m_state.telemetry.throughput_kbs = (enc_len * 2000.0f) / 1024.0f;
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
            }

            capture_client->ReleaseBuffer(numFramesAvailable);
        }
    }

    audio_client->Stop();
    capture_client->Release();
    audio_client->Release();
    if (pwfx) CoTaskMemFree(pwfx);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
}

} // namespace ht306::audio
