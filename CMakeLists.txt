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
        src/rendering/Renderer.h
        src/rendering/Renderer.cpp
        external/kissfft/kiss_fft.c
        external/kissfft/kiss_fft.h
        external/kissfft/kiss_fft_log.h
        external/kissfft/_kiss_fft_guts.h
        external/glad/src/glad.c
)

# header search paths
target_include_directories(AudioVisualizer PRIVATE
        src
        external/kissfft
        external/glad/include
        external/glfw/include
)

# link libraries
target_link_libraries(AudioVisualizer PRIVATE
        # WASAPI / COM
        ole32
        uuid
        propsys
        # OpenGL on Windows
        opengl32
        # prebuilt GLFW static library
        ${CMAKE_SOURCE_DIR}/external/glfw/lib/glfw3.lib
)
