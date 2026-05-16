#pragma once

#include <vector>
#include <cstdint>
#include "../../external/kissfft/kiss_fft.h"

class FFTProcessor {
public:
    FFTProcessor(uint32_t fftSize, uint32_t sampleRate, uint32_t numBars);
    ~FFTProcessor();

    // feed raw mono audio samples, get back bar magnitudes
    void process(const float* samples, size_t count);

    const std::vector<float> &getBars() const { return m_bars; }
    uint32_t getNumBars() const { return m_numBars; }
private:
    void computeWindow();
    void computeBarMapping();

    uint32_t m_fftSize;
    uint32_t m_sampleRate;
    uint32_t m_numBars;

    // Hann window (precomputed)
    std::vector<float> m_window;

    // KissFFT
    kiss_fft_cfg m_fftConfig;
    std::vector<kiss_fft_cpx> m_fftInput;
    std::vector<kiss_fft_cpx> m_fftOutput;

    // frequency bin -> bar mapping
    // for each bar, which FFT bins does it cover
    std::vector<uint32_t> m_barBinStart;
    std::vector<uint32_t> m_barBinEnd;

    // output bars; smoothed, normalized, 0.0 - 0.1
    std::vector<float> m_bars;

    // previous frame bars for smoothing
    std::vector<float> m_prevBars;

    // minimum dB, below this is silence
    float m_minDb = -70.0f;

    // hard noise gate: normalized values below this snap to 0
    float m_noiseGate = 0.15f;

    // smoothing decay factor
    float m_decay = 0.9f;
};