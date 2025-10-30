#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

/**
 * Beat detection and BPM calculation from gate/trigger signals
 * 
 * Uses tap tempo algorithm with rolling average for stable BPM detection
 * Analyzes rising edges of input signals to measure time intervals between beats
 */
class TapTempo
{
public:
    TapTempo() = default;
    
    /**
     * Process a single sample
     * @param sample Input sample value (gate/trigger signal)
     * @param sampleRate Current audio sample rate
     * @return true if a beat was detected this sample
     */
    bool processSample(float sample, double sampleRate);
    
    /**
     * Get current detected BPM
     * @return BPM value, or 0.0 if no stable detection yet
     */
    float getBPM() const { return m_detectedBPM; }
    
    /**
     * Get confidence level (0-1, based on variance of intervals)
     * Higher confidence means more consistent timing between beats
     */
    float getConfidence() const { return m_confidence; }
    
    /**
     * Is actively detecting beats?
     * Returns true when enough beats have been detected with sufficient confidence
     */
    bool isActive() const { return m_isActive; }
    
    /**
     * Reset detection state (clears all tap history)
     */
    void reset();
    
    // === Configuration ===
    
    /**
     * Set detection threshold (0-1)
     * Rising edge detected when signal crosses from below to above this value
     */
    void setSensitivity(float threshold) { m_threshold = juce::jlimit(0.0f, 1.0f, threshold); }
    
    /**
     * Set minimum valid BPM (rejects intervals outside this range)
     */
    void setMinBPM(float minBPM) { m_minBPM = juce::jlimit(10.0f, 500.0f, minBPM); }
    
    /**
     * Set maximum valid BPM (rejects intervals outside this range)
     */
    void setMaxBPM(float maxBPM) { m_maxBPM = juce::jlimit(10.0f, 500.0f, maxBPM); }
    
private:
    static constexpr int MAX_TAPS = 8;              // Number of intervals to average
    static constexpr double TIMEOUT_SECONDS = 3.0;  // Reset if no beat for this long
    
    // Configuration
    float m_threshold = 0.5f;
    float m_minBPM = 30.0f;
    float m_maxBPM = 300.0f;
    
    // Edge detection state
    float m_lastSample = 0.0f;
    bool m_wasAboveThreshold = false;
    
    // Tap tempo buffer
    std::array<double, MAX_TAPS> m_tapIntervals{};
    int m_tapCount = 0;
    
    // Timing
    double m_lastTapTime = 0.0;
    double m_currentTime = 0.0;
    
    // Results
    float m_detectedBPM = 0.0f;
    float m_confidence = 0.0f;
    bool m_isActive = false;
    
    // Calculate BPM from current tap buffer
    void calculateBPM();
};

