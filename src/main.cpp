#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <windows.h>

#include "./audio/RingBuffer.h"
#include "./audio/AudioCapture.h"
#include "processing/FFTProcessor.h"

// global flag for clean shutdown on ctrl+c
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    // applications on windows can catch ctrl+c with SIGINT
    // without it, the app will terminate without calling stop(), leaking resources
    if (signal == SIGINT) {
        g_running.store(false);
    }
}

void setCursorToTop() {
    COORD topLeft = {0, 0};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), topLeft);
}

void hideCursor() {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 1;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(console, &info);
}

void printBars(const std::vector<float>& bars, int barHeight = 20) {
    setCursorToTop();

    int numBars = (int)bars.size();

    // build the entire frame as one string, then output at once
    std::string frame;
    frame.reserve(numBars * 4 * barHeight);

    for (int row = barHeight; row >= 1; row--) {
        float threshold = (float)row / barHeight;

        for (int i = 0; i < numBars; i++) {
            if (bars[i] >= threshold) {
                frame += "##";
            } else {
                frame += "  ";
            }
            frame += ' ';  // space between bars
        }
        frame += '\n';
    }

    // base line
    for (int i = 0; i < numBars; i++) {
        frame += "---";
    }
    frame += '\n';

    std::cout << frame << std::flush;
}

int main() {
    hideCursor();

    std::signal(SIGINT, signalHandler);

    std::cout << "Audio Visualizer - Part 2" << std::endl;
    std::cout << "Press CTRL+C to exit" << std::endl;
    std::cout << std::endl;

    // ring buffer: 48000 samples = 1 second at 48kHz
    RingBuffer<float> ringBuffer(48000);

    // start audio capture
    AudioCapture capture(ringBuffer);
    if (!capture.start()) {
        std::cerr << "Failed to start capture" << std::endl;
        return 1;
    }

    // FFT setup
    const uint32_t FFT_SIZE = 2048;
    const uint32_t NUM_BARS = 32;
    FFTProcessor fft(FFT_SIZE, capture.getSampleRate(), NUM_BARS);

    // buffer to read samples from ring buffer
    std::vector<float> sampleBuffer(FFT_SIZE);

    std::cout << "Listening... play audio..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    while (g_running.load()) {
        // use peek so we can do overlapping windows
        size_t available = ringBuffer.availibleSamples();

        if (available >= FFT_SIZE) {
            // read FFT_SIZE samples, consuming them
            ringBuffer.read(sampleBuffer.data(), FFT_SIZE);

            // process through FFT
            fft.process(sampleBuffer.data(), FFT_SIZE);

            // display
            printBars(fft.getBars());
        }

        // 30 fps
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    std::cout << "Shutting down..." << std::endl;
    capture.stop();
    std::cout << "Audio capture shut down" << std::endl;

    return 0;
}

