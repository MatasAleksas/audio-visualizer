#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

#include "./audio/RingBuffer.h"
#include "./audio/AudioCapture.h"

// global flag for clean shutdown on ctrl+c
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    // applications on windows can catch ctrl+c with SIGINT
    // without it, the app will terminate without calling stop(), leaking resources
    if (signal == SIGINT) {
        g_running.store(false);
    }
}

float computeRMS(const std::vector<float> &samples, size_t count) {
    if (count == 0) {
        return 0.0f;
    }

    float sumSquares = 0.0f;
    for (size_t i = 0; i < count; i++) {
        sumSquares += samples[i] * samples[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(count));
}

void printMeter(float rms, int width = 50) {
    // convert RMS to a visual level
    // RMS of normal music is typically 0.01-0.3
    // scale it up for display

    float level = rms * 5.0f;
    if (level > 1.0f) {
        level = 1.0f;
    }

    int filled = static_cast<int>(level * width);

    std::cout << "\r["; // carriage return without a newline, overwrites current lines instead of printing new ones
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            std::cout << "#";
        } else {
            std::cout << " ";
        }
    }
    std::cout << "] " << rms << " " << std::flush;
}

int main() {
    std::signal(SIGINT, signalHandler);

    std::cout << "Audio Visualizer - Part 1" << std::endl;
    std::cout << "Press CTRL+C to exit" << std::endl;
    std::cout << std::endl;

    // ring buffer: 48000 samples = 1 second at 48kHz
    RingBuffer<float> ringBuffer(48000);

    AudioCapture capture(ringBuffer);

    if (!capture.start()) {
        std::cerr << "Failed to start capture" << std::endl;
        return 1;
    }

    std::cout << "Listening... play audio..." << std::endl;
    std::cout << std::endl;

    // read buffer for pulling samples from the ring buffer
    // 4096 is roughly 85ms of audio at 48kHz, its enough samples to get a stable reading
    std::vector<float> readBuffer(4096);

    while (g_running.load()) {
        // read available samples (while consuming)
        size_t samplesRead = ringBuffer.read(readBuffer.data(), readBuffer.size());

        if (samplesRead > 0) {
            float rms = computeRMS(readBuffer, samplesRead);
            printMeter(rms);
        }

        // update roughly 30 times/second
        // fast enough to see responsive movement, slow enough to not waste CPU
        // when switching to renderer, this sleep goes away and is replaced with vsync
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    std::cout << std::endl << std::endl;
    std::cout << "Shutting down..." << std::endl;

    capture.stop();

    std::cout << "Audio capture shut down" << std::endl;
    return 0;
}

