#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

#include "./audio/RingBuffer.h"
#include "./audio/AudioCapture.h"
#include "processing/FFTProcessor.h"
#include "rendering/Renderer.h"

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "Audio Visualizer - OpenGL window" << std::endl;
    std::cout << "Close window or press ESC to exit" << std::endl;

    RingBuffer<float> ringBuffer(48000);

    AudioCapture capture(ringBuffer);
    if (!capture.start()) {
        std::cerr << "Failed to start capture" << std::endl;
        return 1;
    }

    const uint32_t FFT_SIZE = 2048;
    const uint32_t NUM_BARS = 64;
    FFTProcessor fft(FFT_SIZE, capture.getSampleRate(), NUM_BARS);

    Renderer renderer;
    if (!renderer.init("Audio Visualizer", 1280, 540)) {
        capture.stop();
        return 1;
    }

    std::vector<float> sampleBuffer(FFT_SIZE);
    std::vector<float> displayBars(NUM_BARS, 0.0f);

    bool running = true;
    while (running) {
        running = renderer.pollEvents();

        size_t available = ringBuffer.availibleSamples();
        if (available >= FFT_SIZE) {
            ringBuffer.read(sampleBuffer.data(), FFT_SIZE);
            fft.process(sampleBuffer.data(), FFT_SIZE);
            displayBars = fft.getBars();
        }

        renderer.drawFrame(displayBars);
        // vsync paces us; no manual sleep needed
    }

    std::cout << "Shutting down..." << std::endl;
    capture.stop();
    renderer.shutdown();
    return 0;
}
