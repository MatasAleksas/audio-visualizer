#pragma once

#include <vector>
#include <cstddef>

// Energy-based beat detector.
// Tracks rolling bass-band energy and fires a beat when the current frame
// exceeds the recent average by m_threshold.
class BeatDetector {
public:
    explicit BeatDetector(size_t historyFrames = 43);

    // Feed bar data each frame. Returns true if a beat fires this frame.
    bool update(const std::vector<float>& bars);

    bool  beatThisFrame() const { return m_beat; }
    float beatStrength()  const { return m_strength; }   // 0..~2, peak ~1 on a clean kick
    float currentEnergy() const { return m_currentEnergy; }
    float averageEnergy() const { return m_averageEnergy; }

private:
    std::vector<float> m_history;
    size_t m_historyIndex = 0;
    bool   m_filled = false;

    float m_currentEnergy = 0.0f;
    float m_averageEnergy = 0.0f;
    bool  m_beat = false;
    float m_strength = 0.0f;
    int   m_cooldown = 0;

    // tunables
    float m_threshold      = 1.35f;  // beat when energy > avg * threshold
    float m_minEnergy      = 0.05f;  // ignore quiet sections
    int   m_cooldownFrames = 6;      // ~100ms at 60fps, blocks double-triggers
    int   m_bassBars       = 6;      // first N bars (~20Hz - 200Hz with 64 bars)
};
