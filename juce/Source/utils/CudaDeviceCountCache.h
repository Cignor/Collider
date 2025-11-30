#pragma once

#include <mutex>
#include <juce_core/juce_core.h>

#if WITH_CUDA_SUPPORT
#include <opencv2/core/cuda.hpp>
#endif

/**
 * Global singleton cache for CUDA device count.
 * Queries CUDA ONCE at first access, then caches the result.
 * Thread-safe - safe to call from any thread.
 */
class CudaDeviceCountCache
{
private:
    static int getCachedCount()
    {
        static int cachedCount = -1;
        static std::once_flag initFlag;

        std::call_once(initFlag, []() {
#if WITH_CUDA_SUPPORT
            try
            {
                cachedCount = cv::cuda::getCudaEnabledDeviceCount();
                juce::Logger::writeToLog("[CudaCache] CUDA device count queried: " + juce::String(cachedCount));
            }
            catch (...)
            {
                cachedCount = -1; // -1 indicates query failed
                juce::Logger::writeToLog("[CudaCache] CUDA query failed - no NVIDIA GPU or CUDA runtime");
            }
#else
            cachedCount = -1; // -1 indicates CUDA not compiled
            juce::Logger::writeToLog("[CudaCache] CUDA not compiled (WITH_CUDA_SUPPORT not defined)");
#endif
        });

        return cachedCount;
    }

public:
    /**
     * Get the cached CUDA device count.
     * First call will query CUDA (thread-safe), subsequent calls return cached value.
     * Returns 0 if no devices available, -1 if query failed or CUDA not compiled.
     */
    static int getDeviceCount()
    {
        int count = getCachedCount();
        return (count >= 0) ? count : 0; // Return 0 if query failed
    }

    /**
     * Check if CUDA query was successful (doesn't mean devices are available)
     */
    static bool querySucceeded()
    {
        return getCachedCount() >= 0;
    }

    /**
     * Check if CUDA is available (device count > 0)
     */
    static bool isAvailable()
    {
        int count = getCachedCount();
        return count > 0;
    }
};

