#include "Renderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace {

// ---- bar shader: pixel-space verts -> NDC, per-vertex color ----
const char* kBarVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform vec2 uViewport;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(
        (aPos.x / uViewport.x) * 2.0 - 1.0,
        1.0 - (aPos.y / uViewport.y) * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
)GLSL";

const char* kBarFS = R"GLSL(
#version 330 core
in  vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)GLSL";

// ---- fullscreen quad shader (used by all post passes) ----
const char* kFsVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// bright-pass: keep pixels above threshold, scale down to ease blur
const char* kBrightFS = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float k = max(luma - uThreshold, 0.0) / max(luma, 1e-4);
    FragColor = vec4(c * k, 1.0);
}
)GLSL";

// separable Gaussian blur, 9-tap
const char* kBlurFS = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSrc;
uniform vec2 uTexelDir; // (1/w, 0) or (0, 1/h)
void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = texture(uSrc, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = uTexelDir * float(i);
        result += texture(uSrc, vUV + off).rgb * w[i];
        result += texture(uSrc, vUV - off).rgb * w[i];
    }
    FragColor = vec4(result, 1.0);
}
)GLSL";

// composite: scene + bloom * intensity (additive, soft-clamped at the top)
const char* kCompositeFS = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uIntensity;
void main() {
    vec3 scene = texture(uScene, vUV).rgb;
    vec3 bloom = texture(uBloom, vUV).rgb;
    vec3 c = scene + bloom * uIntensity;
    // soft clip only the parts that overshoot 1.0, leave the rest untouched
    c = c - (c * c * c) * 0.05; // gentle rolloff above 1.0
    c = clamp(c, 0.0, 1.0);
    FragColor = vec4(c, 1.0);
}
)GLSL";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER,   vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link failed: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void pushRect(std::vector<float>& v,
              float x, float y, float w, float h,
              float r, float g, float b, float a)
{
    auto push = [&](float px, float py){
        v.push_back(px); v.push_back(py);
        v.push_back(r);  v.push_back(g);  v.push_back(b); v.push_back(a);
    };
    push(x,     y);
    push(x + w, y);
    push(x + w, y + h);
    push(x,     y);
    push(x + w, y + h);
    push(x,     y + h);
}

void glfwErrorCallback(int code, const char* msg) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, msg);
}

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() { shutdown(); }

bool Renderer::buildShaders() {
    m_barProgram       = link(kBarVS, kBarFS);
    m_brightProgram    = link(kFsVS,  kBrightFS);
    m_blurProgram      = link(kFsVS,  kBlurFS);
    m_compositeProgram = link(kFsVS,  kCompositeFS);
    return m_barProgram && m_brightProgram && m_blurProgram && m_compositeProgram;
}

bool Renderer::createFramebuffers(int w, int h) {
    destroyFramebuffers();

    m_fbWidth  = w;
    m_fbHeight = h;
    int hw = std::max(1, w / 2);
    int hh = std::max(1, h / 2);

    // scene at full res
    glGenFramebuffers(1, &m_sceneFBO);
    glGenTextures(1, &m_sceneTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "Scene FBO incomplete\n");
        return false;
    }

    // two half-res ping-pong FBOs for blur
    glGenFramebuffers(2, m_blurFBO);
    glGenTextures(2, m_blurTex);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_blurTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hw, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, m_blurFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blurTex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "Blur FBO %d incomplete\n", i);
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Renderer::destroyFramebuffers() {
    if (m_sceneFBO) { glDeleteFramebuffers(1, &m_sceneFBO); m_sceneFBO = 0; }
    if (m_sceneTex) { glDeleteTextures(1, &m_sceneTex);     m_sceneTex = 0; }
    if (m_blurFBO[0] || m_blurFBO[1]) {
        glDeleteFramebuffers(2, m_blurFBO);
        m_blurFBO[0] = m_blurFBO[1] = 0;
    }
    if (m_blurTex[0] || m_blurTex[1]) {
        glDeleteTextures(2, m_blurTex);
        m_blurTex[0] = m_blurTex[1] = 0;
    }
}

bool Renderer::init(const std::string& title, int width, int height) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "gladLoadGLLoader failed\n");
        return false;
    }

    glfwGetFramebufferSize(m_window, &m_width, &m_height);

    if (!buildShaders()) return false;

    // bar VBO/VAO
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizei stride = sizeof(float) * 6;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float) * 2));

    // fullscreen quad: two triangles in NDC
    const float fsVerts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };
    glGenVertexArrays(1, &m_fsVao);
    glGenBuffers(1, &m_fsVbo);
    glBindVertexArray(m_fsVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_fsVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fsVerts), fsVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!createFramebuffers(m_width, m_height)) return false;

    m_lastTime = glfwGetTime();
    return true;
}

void Renderer::shutdown() {
    destroyFramebuffers();
    if (m_fsVbo)            { glDeleteBuffers(1, &m_fsVbo);            m_fsVbo = 0; }
    if (m_fsVao)            { glDeleteVertexArrays(1, &m_fsVao);       m_fsVao = 0; }
    if (m_vbo)              { glDeleteBuffers(1, &m_vbo);              m_vbo = 0; }
    if (m_vao)              { glDeleteVertexArrays(1, &m_vao);         m_vao = 0; }
    if (m_barProgram)       { glDeleteProgram(m_barProgram);           m_barProgram = 0; }
    if (m_brightProgram)    { glDeleteProgram(m_brightProgram);        m_brightProgram = 0; }
    if (m_blurProgram)      { glDeleteProgram(m_blurProgram);          m_blurProgram = 0; }
    if (m_compositeProgram) { glDeleteProgram(m_compositeProgram);     m_compositeProgram = 0; }
    if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; }
    glfwTerminate();
}

bool Renderer::pollEvents() {
    glfwPollEvents();
    if (glfwWindowShouldClose(m_window)) return false;
    if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) return false;
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    if (w != m_width || h != m_height) {
        m_width = w; m_height = h;
        if (w > 0 && h > 0) createFramebuffers(w, h);
    }
    return true;
}

void Renderer::updatePeaks(const std::vector<float>& bars) {
    if (m_peaks.size() != bars.size()) {
        m_peaks.assign(bars.size(), 0.0f);
        m_peakVelocity.assign(bars.size(), 0.0f);
    }
    double now = glfwGetTime();
    float dt = (float)(now - m_lastTime);
    m_lastTime = now;
    if (dt > 0.1f) dt = 0.1f;

    const float gravity = 1.2f;
    for (size_t i = 0; i < bars.size(); ++i) {
        if (bars[i] >= m_peaks[i]) {
            m_peaks[i] = bars[i];
            m_peakVelocity[i] = 0.0f;
        } else {
            m_peakVelocity[i] += gravity * dt;
            m_peaks[i] -= m_peakVelocity[i] * dt;
            if (m_peaks[i] < 0.0f) m_peaks[i] = 0.0f;
        }
    }
}

void Renderer::drawFrame(const std::vector<float>& bars,
                         bool beatThisFrame, float beatStrength) {
    if (bars.empty()) return;
    updatePeaks(bars);

    // beat pulse: jump on beat, decay each frame
    if (beatThisFrame) {
        float bump = 0.6f + std::clamp(beatStrength, 0.0f, 1.5f) * 0.5f;
        m_beatPulse = std::min(1.5f, m_beatPulse + bump);
    }
    m_beatPulse *= 0.88f;
    if (m_beatPulse < 0.001f) m_beatPulse = 0.0f;

    // ---- pass 1: draw bars into scene FBO ----
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);
    glViewport(0, 0, m_fbWidth, m_fbHeight);
    glClearColor(0.02f, 0.03f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_vertices.clear();

    // subtle background gradient
    {
        const int strips = 32;
        for (int i = 0; i < strips; ++i) {
            float t0 = (float)i       / strips;
            float t1 = (float)(i + 1) / strips;
            auto col = [](float t){
                return std::array<float,4>{
                    0.04f * (1.0f - t),
                    0.06f * (1.0f - t),
                    0.18f * (1.0f - t) + 0.02f,
                    1.0f
                };
            };
            auto c0 = col(t0);
            float y0 = t0 * m_height;
            float y1 = t1 * m_height;
            pushRect(m_vertices, 0, y0, (float)m_width, y1 - y0,
                     c0[0], c0[1], c0[2], c0[3]);
        }
    }

    const int   numBars = (int)bars.size();
    const float gap     = 2.0f;
    const float spacing = (float)m_width / numBars;
    const float barW    = std::max(2.0f, spacing - gap);
    const float baseY   = (float)m_height - 20.0f;
    const float maxH    = baseY - 20.0f;

    for (int i = 0; i < numBars; ++i) {
        float v = std::clamp(bars[i], 0.0f, 1.0f);
        float h = v * maxH;
        float x = i * spacing + gap * 0.5f;

        const int subStrips = 8;
        for (int s = 0; s < subStrips; ++s) {
            float t0 = (float)s       / subStrips;
            float t1 = (float)(s + 1) / subStrips;
            float ys = baseY - h * t1;
            float ye = baseY - h * t0;
            float tMid = (t0 + t1) * 0.5f;
            float r = (40  + 215 * tMid) / 255.0f;
            float g = (220 - 180 * tMid) / 255.0f;
            float b = (255 - 60  * tMid) / 255.0f;
            pushRect(m_vertices, x, ys, barW, ye - ys, r, g, b, 1.0f);
        }

        float peakY = baseY - std::clamp(m_peaks[i], 0.0f, 1.0f) * maxH;
        pushRect(m_vertices, x, peakY - 2.0f, barW, 2.0f, 1.0f, 1.0f, 1.0f, 0.9f);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    GLsizeiptr bytes = (GLsizeiptr)(m_vertices.size() * sizeof(float));
    if (bytes > m_vboCapacity) {
        glBufferData(GL_ARRAY_BUFFER, bytes, m_vertices.data(), GL_DYNAMIC_DRAW);
        m_vboCapacity = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, m_vertices.data());
    }
    glUseProgram(m_barProgram);
    glUniform2f(glGetUniformLocation(m_barProgram, "uViewport"),
                (float)m_width, (float)m_height);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(m_vertices.size() / 6));

    // pass 2: bright-pass into blur[0] at half res
    int hw = std::max(1, m_fbWidth  / 2);
    int hh = std::max(1, m_fbHeight / 2);

    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFBO[0]);
    glViewport(0, 0, hw, hh);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_brightProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glUniform1i(glGetUniformLocation(m_brightProgram, "uScene"), 0);
    glUniform1f(glGetUniformLocation(m_brightProgram, "uThreshold"), 0.30f);
    glBindVertexArray(m_fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // pass 3: separable Gaussian blur, multiple iterations
    const int blurIterations = 4; // 4 = soft, 8 = very glowy
    int srcIdx = 0;
    for (int i = 0; i < blurIterations; ++i) {
        int dstIdx = 1 - srcIdx;
        glBindFramebuffer(GL_FRAMEBUFFER, m_blurFBO[dstIdx]);
        glViewport(0, 0, hw, hh);
        glUseProgram(m_blurProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_blurTex[srcIdx]);
        glUniform1i(glGetUniformLocation(m_blurProgram, "uSrc"), 0);
        // alternate horizontal / vertical
        if ((i & 1) == 0) {
            glUniform2f(glGetUniformLocation(m_blurProgram, "uTexelDir"), 1.0f / hw, 0.0f);
        } else {
            glUniform2f(glGetUniformLocation(m_blurProgram, "uTexelDir"), 0.0f, 1.0f / hh);
        }
        glBindVertexArray(m_fsVao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        srcIdx = dstIdx;
    }

    // pass 4: composite scene + bloom to default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_fbWidth, m_fbHeight);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_compositeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glUniform1i(glGetUniformLocation(m_compositeProgram, "uScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_blurTex[srcIdx]);
    glUniform1i(glGetUniformLocation(m_compositeProgram, "uBloom"), 1);
    // base intensity + extra punch on beats
    glUniform1f(glGetUniformLocation(m_compositeProgram, "uIntensity"),
                2.2f + m_beatPulse * 2.5f);
    glBindVertexArray(m_fsVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glEnable(GL_BLEND);
    glfwSwapBuffers(m_window);
}
