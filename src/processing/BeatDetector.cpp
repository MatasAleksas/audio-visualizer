#include "BeatDetector.h"
#include <algorithm>

BeatDetector::BeatDetector(size_t historyFrames)
    : m_history(historyFrames, 0.0f)
{}

bool BeatDetector::update(const std::vector<float>& bars) {
    m_beat = false;
    m_strength = 0.0f;

    if (bars.empty()) return false;

    // bass-band energy: sum first N bars
    int n = std::min((int)bars.size(), m_bassBars);
    float energy = 0.0f;
    for (int i = 0; i < n; ++i) energy += bars[i];
    energy /= (float)n;
    m_currentEnergy = energy;

    // rolling average over history
    float avg = 0.0f;
    size_t valid = m_filled ? m_history.size() : m_historyIndex;
    if (valid > 0) {
        for (size_t i = 0; i < valid; ++i) avg += m_history[i];
        avg /= (float)valid;
    }
    m_averageEnergy = avg;

    if (m_cooldown > 0) --m_cooldown;

    if (energy > m_minEnergy &&
        energy > avg * m_threshold &&
        m_cooldown == 0)
    {
        m_beat = true;
        m_strength = (avg > 1e-4f) ? (energy / avg - 1.0f) : 1.0f;
        m_strength = std::clamp(m_strength, 0.0f, 2.0f);
        m_cooldown = m_cooldownFrames;
    }

    // write into history AFTER the comparison, so the current spike doesn't poison its own avg
    m_history[m_historyIndex] = energy;
    m_historyIndex = (m_historyIndex + 1) % m_history.size();
    if (m_historyIndex == 0) m_filled = true;

    return m_beat;
}
