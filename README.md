# Audio Visualizer

A real-time audio visualizer for Windows. It captures system audio via WASAPI loopback, runs an FFT to extract frequency bars, detects beats, and renders everything with OpenGL — bars with peak caps, a beat-driven pulse, and bloom post-processing.

## Features

- **System audio capture** via WASAPI loopback (no virtual cable needed) — [AudioCapture](src/audio/AudioCapture.cpp)
- **FFT frequency analysis** using KissFFT, binned into bars — [FFTProcessor](src/processing/FFTProcessor.cpp)
- **Beat detection** from the frequency spectrum — [BeatDetector](src/processing/BeatDetector.cpp)
- **OpenGL rendering** with per-bar peak markers, a beat pulse, and a bright-pass/blur/composite bloom pipeline — [Renderer](src/rendering/Renderer.cpp)

## Requirements

- Windows 10/11
- CMake 3.16+
- A C++17 compiler (MSVC via Visual Studio Build Tools)

Dependencies are vendored under [external/](external/): GLFW (prebuilt static lib), glad, and KissFFT.

## Building

```powershell
cmake -B build -S .
cmake --build build --config Release
```

The executable is written to `build/Release/AudioVisualizer.exe`.

## Running

```powershell
build\Release\AudioVisualizer.exe
```

The app opens a window and visualizes whatever audio is currently playing on your default output device. Press `ESC` or close the window to exit.

## Project layout

```
src/
  audio/       WASAPI capture + lock-free ring buffer
  processing/  FFT and beat detection
  rendering/   OpenGL renderer and shaders
shaders/       GLSL vertex/fragment shaders for bars and post-processing
external/      Vendored dependencies (GLFW, glad, KissFFT)
```

## License

MIT — see [LICENSE](LICENSE).
