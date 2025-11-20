#include "TapTempo.h"
#include <cmath>

bool TapTempo::processSample(float sample, double time)
{
    // Detect rising edge
    const bool isAboveThreshold = sample >= m_threshold;
    const bool risingEdge = isAboveThreshold && !m_wasAboveThreshold;
    m_wasAboveThreshold = isAboveThreshold;
    
    if (risingEdge)
    {
        if (m_lastEdgeTime > 0.0)
        {
            const double interval = time - m_lastEdgeTime;
            
            // Calculate BPM from this interval
            const float instantBPM = static_cast<float>(60.0 / interval);
            
            // Reject if outside valid range
            if (instantBPM >= m_minBPM && instantBPM <= m_maxBPM)
            {
                // Add to intervals list
                m_intervals.push_back(interval);
                
                // Keep only last MAX_INTERVALS
                if (m_intervals.size() > MAX_INTERVALS)
                    m_intervals.erase(m_intervals.begin());
                
                // Recalculate median BPM
                calculateBPM();
            }
        }
        
        m_lastEdgeTime = time;
        return true;
    }
    
    return false;
}

void TapTempo::calculateBPM()
{
    if (m_intervals.size() < 2)
    {
        m_bpm = 0.0f;
        return;
    }
    
    // Calculate median interval
    auto sorted = m_intervals;
    std::sort(sorted.begin(), sorted.end());
    const double medianInterval = sorted[sorted.size() / 2];
    
    // Convert to BPM
    m_bpm = static_cast<float>(60.0 / medianInterval);
}

void TapTempo::reset()
{
    m_intervals.clear();
    m_lastEdgeTime = 0.0;
    m_bpm = 0.0f;
    m_wasAboveThreshold = false;
}
