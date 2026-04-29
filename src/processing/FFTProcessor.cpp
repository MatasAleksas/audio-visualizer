#include "FFTProcessor.h"
#include <cmath>
#include <algorithm>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

FFTProcessor::FFTProcessor(uint32_t fftSize, uint32_t sampleRate, uint32_t numBars)
    : m_fftSize(fftSize)
    , m_sampleRate(sampleRate)
    , m_numBars(numBars)
    , m_window(fftSize)
    , m_fftInput(fftSize)
    , m_fftOutput(fftSize)
    , m_barBinStart(numBars)
    , m_barBinEnd(numBars)
    , m_bars(numBars, 0.0f)
    , m_prevBars(numBars, 0.0f)
{
    // initialize KissFFT
    m_fftConfig = kiss_fft_alloc(fftSize, 0, nullptr, nullptr);
    if (!m_fftConfig) {
        std::cerr << "Error allocating KissFFT configuration." << std::endl;
    }

    computeWindow();
    computeBarMapping();
}

FFTProcessor::~FFTProcessor() {
    if (m_fftConfig) {
        kiss_fft_free(m_fftConfig);
    }
}

void FFTProcessor::computeWindow() {
    for (uint32_t i = 0; i < m_fftSize; i++) {
        m_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_fftSize -1)));
    }
}

void FFTProcessor::computeBarMapping() {
    float minFreq = 20.0f;
    float maxFreq = 20000.0f;

    // clamp max freq to Nyquist
    float nyquist = m_sampleRate / 2.0f;
    if (maxFreq > nyquist) {
        maxFreq = nyquist;
    }

    float freqRatio = maxFreq / minFreq;

    for (uint32_t i = 0; i < m_numBars; i++) {
        // log frequency boundaries for this bar
        float freqLow = minFreq * powf(freqRatio, (float)i / m_numBars);
        float freqHigh = maxFreq * powf(freqRatio, (float)(i+1) / m_numBars);

        // convert frequencies to FFT bin indices
        // bin = freq * fftSize / sampleRate
        uint32_t binLow = (uint32_t)(freqLow * m_fftSize / m_sampleRate);
        uint32_t binHigh = (uint32_t)(freqHigh * m_fftSize / m_sampleRate);

        // clamp to vaid range
        if (binLow < 1) {
            binLow = 1; // skip bin 0
        }

        if (binHigh >= m_fftSize / 2) {
            binHigh = m_fftSize / 2 - 1;
        }

        if (binHigh < binLow) {
            binHigh = binLow; // ensure at least one bin
        }

        m_barBinStart[i] = binLow;
        m_barBinEnd[i] = binHigh;
    }
}

void FFTProcessor::process(const float* samples, size_t count) {
    // fill FFT input with windowed samples
    for (uint32_t i = 0; i < m_fftSize; i++) {
        if (i < count) {
            m_fftInput[i].r = samples[i] * m_window[i];
        } else {
            m_fftInput[i].r = 0.0f; // 0 pad if not enough samples
        }
        m_fftInput[i].i = 0.0f; // imaginary part is always 0 for real input
    }

    // run FFT
    kiss_fft(m_fftConfig, m_fftInput.data(), m_fftOutput.data());

    // DEBUG: Check if FFT is actually producing output
    float maxFFT = 0.0f;
    for (uint32_t i = 0; i < m_fftSize / 2; i++) {
        float mag = sqrtf(m_fftOutput[i].r * m_fftOutput[i].r + m_fftOutput[i].i * m_fftOutput[i].i);
        if (mag > maxFFT) maxFFT = mag;
    }
    std::cout << "FFT config: " << (m_fftConfig ? "valid" : "NULL") << "     \n";
    std::cout << "Max FFT magnitude: " << maxFFT << "     \n";

    // compute magnitude for each bar
    for (uint32_t i = 0; i < m_numBars; i++) {
        float sum = 0.0f;
        uint32_t count = 0;

        for (uint32_t bin = m_barBinStart[i]; bin <= m_barBinEnd[i]; bin++) {
            float real = m_fftOutput[bin].r;
            float imag = m_fftOutput[bin].i;
            float magnitude = sqrt(real * real + imag * imag);
            sum += magnitude;
            count++;
        }

        // average magnitude for this bar
        float avgMagnitude = (count > 0) ? (sum / count) : 0.0f;

        // normalize by FFT size
        avgMagnitude /= m_fftSize;

        // boost higher bars to compensate for natural energy rolloff
        float weight = 1.0f + (float)i / (float)m_numBars * 3.0f;
        avgMagnitude *= weight;

        // convert to dB
        float dB;
        if (avgMagnitude > 0.0f) {
            dB = 20.0f * log10f(avgMagnitude);
        } else {
            dB = m_minDb;
        }

        // normalize dB to 0.0 - 1.0 range
        float normalized = (dB - m_minDb) / (0.0f - m_minDb);
        normalized = std::clamp(normalized, 0.0f, 1.0f);

        // smoothing, instant attack, slow decay
        if (normalized > m_prevBars[i]) {
            m_bars[i] = normalized;
        } else {
            m_bars[i] = m_prevBars[i] * m_decay;
        }

        m_prevBars[i] = m_bars[i];
    }
}

