#include "AudioCapture.h"

#include <iostream>
#include <vector>
#include <Functiondiscoverykeys_devpkey.h>

// macro for COM error checking
#define CHECK_HR(hr, msg) \
    if(FAILED(hr)) { \
        std::cerr << "WASAPI Error: " << msg << "(HRESULT: 0x" \
                  << std::hex << hr << std::dec << ")" << std::endl; \
        return false; \
    }

AudioCapture::AudioCapture(RingBuffer<float> &ringBuffer)
    : m_ringBuffer(ringBuffer)
{
}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::start() {

    // already running
    if (m_running.load()) {
        return true;
    }

    // initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // when S_FALSE, COM was already initialized on this thread
    if (FAILED(hr) && hr != S_FALSE) {
        std::cerr << "Failed to initialize COM" << std::endl;
        return false;
    }

    // create device enumerator
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(m_enumerator.GetAddressOf())
    );
    CHECK_HR(hr, "Failed to create device enumerator");

    // get default audio device
    // render is the render output device
    // console is default role
    hr = m_enumerator->GetDefaultAudioEndpoint(
        eRender,
        eConsole,
        m_device.GetAddressOf()
    );
    CHECK_HR(hr, "Failed to get default audio endpoint");

    // activate audio client
    hr = m_device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(m_audioClient.GetAddressOf())
    );
    CHECK_HR(hr, "Failed to activate audio client");

    // get mix format
    hr = m_audioClient->GetMixFormat(&m_waveFormat);
    CHECK_HR(hr, "Failed to get mix format");

    m_sampleRate = m_waveFormat->nSamplesPerSec;
    m_channels = m_waveFormat->nChannels;

    // initialize audio client in loopback mode
    // buffer duration in 100-nanosecond units
    // 200ms = 200 * 10000 = 2000000
    REFERENCE_TIME requestedDuration = 2000000;

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        requestedDuration,
        0, // periodicity (must be 0 for shared mode)
        m_waveFormat,
        nullptr // session guid
    );
    CHECK_HR(hr, "Failed to initialize audio client in loopback mode");

    // get capture client
    hr = m_audioClient->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(m_captureClient.GetAddressOf())
    );
    CHECK_HR(hr, "Failed to get capture client");

    // start capturing
    hr = m_audioClient->Start();
    CHECK_HR(hr, "Failed to start audio client");

    // launch capture thread
    m_running.store(true);
    m_captureThread = std::thread(&AudioCapture::captureLoop, this);

    return true;
}

void AudioCapture::captureLoop() {

    // initialize COM on this thread too as COM initialization is per thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE) {
        std::cerr << "Failed to initialize COM on capture thread" << std::endl;
        m_running.store(false);
        return;
    }

    std::vector<float> tempBuffer;

    while (m_running.load()) {
        UINT32 packetLength = 0;
        hr = m_captureClient->GetNextPacketSize(&packetLength);

        if (FAILED(hr)) {
            std::cerr << "GetNextPacketSize failed" << std::endl;
            break;
        }

        // inner while because WASAPI might have multiple packets queued up
        // if only read 1 packet per outerloop iteration we fall behind
        while (packetLength > 0) {
            BYTE* data = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            // dont need device and QPC position
            hr = m_captureClient->GetBuffer(
                &data,
                &numFramesAvailable,
                &flags,
                nullptr,
                nullptr
            );

            if (FAILED(hr)) {
                std::cerr << "GetBuffer failed" << std::endl;
                break;
            }

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) { // system is playing silence, write 0's to ring buffer
                tempBuffer.resize(numFramesAvailable, 0.0f);
                m_ringBuffer.write(tempBuffer.data(), numFramesAvailable);
            } else {
                // we have real data, data points relate to interleaved float samples
                // GetBuffer returns raw bytes but we know that it's 32 bits, so we cast to float
                float* floatData = reinterpret_cast<float*>(data);
                UINT32 totalSamples = numFramesAvailable * m_channels;

                // mix down to mono by averaging channels
                // for visualization we don't need left and right channels
                // averaging simplifies everything downstream
                tempBuffer.resize(numFramesAvailable);
                for (UINT32 i = 0; i < numFramesAvailable; i++) {
                    float sum = 0.0f;
                    for (UINT16 j = 0; j < m_channels; j++) {
                        sum+= floatData[i * m_channels + j];
                    }
                    tempBuffer[i] = sum / static_cast<float>(m_channels);
                }

                m_ringBuffer.write(tempBuffer.data(), numFramesAvailable);
            }

            hr = m_captureClient->ReleaseBuffer(numFramesAvailable);
            if (FAILED(hr)) {
                std::cerr << "ReleaseBuffer failed" << std::endl;
                break;
            }

            // check if theres still more packets left
            hr = m_captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                break;
            }
        }

        // sleep for about 5ms (half buffer period)
        // prevents busy waiting while still being responsive
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CoUninitialize();
}

void AudioCapture::stop() {
    if (!m_running.load()) {
        return;
    }

    m_running.store(false);

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }

    if (m_audioClient) {
        m_audioClient->Stop();
    }

    // release COM interfaces, even though COM handles this I'm being explicit
    m_captureClient.Reset();
    m_audioClient.Reset();
    m_device.Reset();
    m_enumerator.Reset();

    // free wave format
    if (m_waveFormat) {
        CoTaskMemFree(m_waveFormat);
        m_waveFormat = nullptr;
    }

    CoUninitialize();
}