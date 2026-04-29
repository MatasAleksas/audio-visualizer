cmake_minimum_required(VERSION 3.16)
project(AudioVisualizer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AudioVisualizer
        src/main.cpp
        src/audio/AudioCapture.cpp
        src/audio/AudioCapture.h
        src/audio/RingBuffer.h
        src/processing/FFTProcessor.h
        src/processing/FFTProcessor.cpp
        external/kissfft/kiss_fft.c
        external/kissfft/kiss_fft.h
        external/kissfft/kiss_fft_log.h
        external/kissfft/_kiss_fft_guts.h
)

# WASAPI and COM require these Windows libraries
target_link_libraries(AudioVisualizer PRIVATE
        ole32
        uuid
        propsys
)

# make sure we can find our headers
target_include_directories(AudioVisualizer PRIVATE
        src
        external/kissfft
)