#pragma once

#include <atomic>
#include <thread>
#include <memory>
#include <cstdint>

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

#include "RingBuffer.h"

using Microsoft::WRL::ComPtr;

class AudioCapture {
public:
    AudioCapture(RingBuffer<float>& ringBuffer);
    ~AudioCapture();

    bool start();
    void stop();

    uint32_t getSampleRate() const { return m_sampleRate; }
    uint16_t getChannels() const { return m_channels; }
    bool isRunning() const { return m_running.load(); }

private:
    void captureLoop();

    // buffer shared between capture thread and main thread, main owns it and it passes a reference to AudioCapture
    RingBuffer<float>& m_ringBuffer;

    // every raw COM pointer acquired must be released manually, ComPtr releases automatically in deconstructor
    // it also zeros the pointer, making double releasing impossible
    ComPtr<IMMDeviceEnumerator> m_enumerator;
    ComPtr<IMMDevice> m_device;
    ComPtr<IAudioClient> m_audioClient;
    ComPtr<IAudioCaptureClient> m_captureClient;

    // GetMixFormat allocates this with CoTaskMemAlloc, it must be freed with CoTaskMemFree, not delete
    // ComPtr wont handle it, it will be released with stop()
    WAVEFORMATEX* m_waveFormat = nullptr;

    uint32_t m_sampleRate = 0;
    uint16_t m_channels = 0;

    std::atomic<bool> m_running{false};
    std::thread m_captureThread;
};