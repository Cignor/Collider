#pragma once

#include <atomic>

#ifdef ESSENTIA_FOUND
// Forward declarations - Essentia headers included in .cpp to avoid polluting global namespace
namespace essentia {
    namespace standard {
        class AlgorithmFactory;
    }
    namespace streaming {
        class AlgorithmFactory;
    }
}
#endif

/**
 * Wrapper class for Essentia library initialization and utilities
 * Manages Essentia's initialization and provides helper functions
 */
class EssentiaWrapper
{
public:
    /**
     * Initialize Essentia library
     * Must be called before creating any Essentia algorithms
     * Thread-safe: uses atomic for initialization state
     */
    static void initializeEssentia();
    
    /**
     * Shutdown Essentia (cleanup if needed)
     */
    static void shutdownEssentia();
    
    /**
     * Check if Essentia is initialized
     */
    static bool isInitialized();

private:
    static std::atomic<bool> s_initialized;
};

