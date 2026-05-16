#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(const std::string& title, int width, int height);
    void shutdown();

    // returns false if user closed the window
    bool pollEvents();

    // draw a frame from current bar magnitudes (0..1).
    // beatThisFrame=true triggers a visible pulse this frame.
    void drawFrame(const std::vector<float>& bars,
                   bool beatThisFrame = false,
                   float beatStrength = 0.0f);

private:
    void updatePeaks(const std::vector<float>& bars);
    bool buildShaders();
    bool createFramebuffers(int w, int h);
    void destroyFramebuffers();

    GLFWwindow* m_window = nullptr;
    int m_width  = 0;
    int m_height = 0;
    int m_fbWidth  = 0; // current FBO width
    int m_fbHeight = 0;

    // per-bar floating peak marker, falls slowly
    std::vector<float> m_peaks;
    std::vector<float> m_peakVelocity;

    double m_lastTime = 0.0;

    // beat-driven pulse, decays each frame
    float m_beatPulse = 0.0f;

    // bar shader (pixel-space verts, per-vertex color)
    GLuint m_barProgram = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLsizeiptr m_vboCapacity = 0;

    // fullscreen quad for post-process passes
    GLuint m_fsVao = 0;
    GLuint m_fsVbo = 0;

    // post-process shaders
    GLuint m_brightProgram   = 0;
    GLuint m_blurProgram     = 0;
    GLuint m_compositeProgram = 0;

    // scene FBO (full res) and ping-pong blur FBOs (half res)
    GLuint m_sceneFBO = 0;
    GLuint m_sceneTex = 0;
    GLuint m_blurFBO[2] = {0, 0};
    GLuint m_blurTex[2] = {0, 0};

    // CPU-side vertex scratch buffer (x, y, r, g, b, a)
    std::vector<float> m_vertices;
};
