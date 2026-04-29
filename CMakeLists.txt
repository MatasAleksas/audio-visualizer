cmake_minimum_required(VERSION 3.16)
project(AudioVisualizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(AudioVisualizer
        src/main.cpp
        src/audio/AudioCapture.cpp
        src/audio/AudioCapture.h
        src/audio/RingBuffer.h
)

# WASAPI and COM require these Windows libraries
target_link_libraries(AudioVisualizer PRIVATE
        ole32
        uuid
        propsys
)

# make sure we can find our headers
target_include_directories(AudioVisualizer PRIVATE src)