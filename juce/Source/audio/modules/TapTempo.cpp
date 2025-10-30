#include "TapTempo.h"
#include <numeric>
#include <algorithm>
#include <cmath>

bool TapTempo::processSample(float sample, double sampleRate)
{
    if (sampleRate <= 0.0)
        return false;
    
    // Increment current time
    m_currentTime += 1.0 / sampleRate;
    
    // Detect rising edge (transition from below threshold to above threshold)
    const bool isAboveThreshold = sample >= m_threshold;
    const bool risingEdge = isAboveThreshold && !m_wasAboveThreshold;
    m_wasAboveThreshold = isAboveThreshold;
    
    // Check for timeout (no beats detected recently - reset detection)
    if (m_currentTime - m_lastTapTime > TIMEOUT_SECONDS)
    {
        reset();
        return false;
    }
    
    // Process rising edge (beat detected)
    if (risingEdge)
    {
        const double interval = m_currentTime - m_lastTapTime;
        m_lastTapTime = m_currentTime;
        
        // Ignore first tap (no interval yet)
        if (m_tapCount == 0)
        {
            m_tapCount = 1;
            return true;
        }
        
        // Calculate instantaneous BPM from this interval
        const float instantBPM = static_cast<float>(60.0 / interval);
        
        // Reject intervals outside valid BPM range (noise filtering)
        if (instantBPM < m_minBPM || instantBPM > m_maxBPM)
            return false;
        
        // Add interval to rolling buffer
        if (m_tapCount < MAX_TAPS)
        {
            // Buffer not full yet - just append
            m_tapIntervals[m_tapCount - 1] = interval;
            m_tapCount++;
        }
        else
        {
            // Buffer full - shift left and add new interval at end
            std::shift_left(m_tapIntervals.begin(), m_tapIntervals.end(), 1);
            m_tapIntervals[MAX_TAPS - 1] = interval;
        }
        
        // Recalculate BPM with new data
        calculateBPM();
        return true;
    }
    
    return false;
}

void TapTempo::calculateBPM()
{
    // Need at least 2 taps to calculate BPM (1 interval)
    if (m_tapCount < 2)
    {
        m_detectedBPM = 0.0f;
        m_confidence = 0.0f;
        m_isActive = false;
        return;
    }
    
    // Calculate average interval
    const int validTaps = std::min(m_tapCount - 1, MAX_TAPS);
    const double avgInterval = std::accumulate(m_tapIntervals.begin(), 
                                               m_tapIntervals.begin() + validTaps, 
                                               0.0) / validTaps;
    
    // Calculate variance (for confidence metric)
    double variance = 0.0;
    for (int i = 0; i < validTaps; ++i)
    {
        const double diff = m_tapIntervals[i] - avgInterval;
        variance += diff * diff;
    }
    variance /= validTaps;
    
    // Convert average interval to BPM
    m_detectedBPM = static_cast<float>(60.0 / avgInterval);
    
    // Calculate confidence: high when variance is low (consistent timing)
    // Using coefficient of variation (standard deviation / mean)
    const double stdDev = std::sqrt(variance);
    const double coefficientOfVariation = stdDev / avgInterval;
    
    // Map to 0-1 range (lower variation = higher confidence)
    // CoV of 0.2 (20% variation) maps to ~0 confidence
    // CoV of 0.0 (perfect consistency) maps to 1.0 confidence
    m_confidence = juce::jlimit(0.0f, 1.0f, static_cast<float>(1.0 - coefficientOfVariation * 5.0));
    
    // Consider detection "active" when we have:
    // - At least 3 taps (2 intervals) for stability
    // - Confidence above 30% (reasonably consistent timing)
    m_isActive = (m_tapCount >= 3 && m_confidence > 0.3f);
}

void TapTempo::reset()
{
    m_tapCount = 0;
    m_lastTapTime = 0.0;
    m_currentTime = 0.0;
    m_detectedBPM = 0.0f;
    m_confidence = 0.0f;
    m_isActive = false;
    m_wasAboveThreshold = false;
    m_lastSample = 0.0f;
}

