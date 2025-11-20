#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <algorithm>

/**
 * Minimal BPM detection: detect rising edges, measure intervals, calculate median BPM
 */
class TapTempo
{
public:
    TapTempo() = default;

    /**
     * Process a single sample
     * @param sample Input sample value (gate/trigger signal)
     * @param time Current time in seconds
     * @return true if a beat was detected this sample
     */
    bool processSample(float sample, double time);
    
    /**
     * Get current detected BPM (median of intervals)
     * @return BPM value, or 0.0 if not enough data
     */
    float getBPM() const { return m_bpm; }
    
    /**
     * Is actively detecting beats?
     */
    bool isActive() const { return m_intervals.size() >= 2; }

    /**
     * Reset detection state
     */
    void reset();
    
    /**
     * Set detection threshold (0-1)
     */
    void setSensitivity(float threshold) { m_threshold = juce::jlimit(0.0f, 1.0f, threshold); }
    
    /**
     * Set valid BPM range (rejects intervals outside this range)
     */
    void setMinBPM(float minBPM) { m_minBPM = juce::jlimit(5.0f, 1000.0f, minBPM); }
    void setMaxBPM(float maxBPM) { m_maxBPM = juce::jlimit(5.0f, 1000.0f, maxBPM); }
    
private:
    static constexpr int MAX_INTERVALS = 4;  // Keep last 4 intervals for median
    
    float m_threshold = 0.5f;
    float m_minBPM = 30.0f;
    float m_maxBPM = 300.0f;
    
    bool m_wasAboveThreshold = false;
    double m_lastEdgeTime = 0.0;
    std::vector<double> m_intervals;  // Last N intervals
    float m_bpm = 0.0f;
    
    void calculateBPM();
};
