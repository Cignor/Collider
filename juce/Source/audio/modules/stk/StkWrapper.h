#pragma once

#include <atomic>

// Forward declaration - STK headers included in .cpp to avoid polluting global namespace
namespace stk {
    class Stk;
}

/**
 * Wrapper class for STK library initialization and utilities
 * Manages STK's global sample rate and provides helper functions
 */
class StkWrapper
{
public:
    /**
     * Initialize STK library with sample rate
     * Must be called before creating any STK instruments
     * Thread-safe: uses atomic for sample rate
     */
    static void initializeStk(double sampleRate);
    
    /**
     * Update STK's global sample rate
     * Call when audio device sample rate changes
     */
    static void setSampleRate(double sampleRate);
    
    /**
     * Get current STK sample rate
     */
    static double getSampleRate();
    
    /**
     * Check if STK is initialized
     */
    static bool isInitialized();
    
    /**
     * Shutdown STK (cleanup if needed)
     */
    static void shutdownStk();

private:
    static std::atomic<double> s_sampleRate;
    static std::atomic<bool> s_initialized;
};

