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
public:
    /**
     * Get the cached CUDA device count.
     * First call will query CUDA (thread-safe), subsequent calls return cached value.
     * Returns 0 if CUDA not compiled or no devices available.
     */
    static int getDeviceCount()
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
                cachedCount = 0;
                juce::Logger::writeToLog("[CudaCache] CUDA query failed - no NVIDIA GPU or CUDA runtime");
            }
#else
            cachedCount = 0;
#endif
        });

        return cachedCount;
    }

    /**
     * Check if CUDA is available (device count > 0)
     */
    static bool isAvailable()
    {
        return getDeviceCount() > 0;
    }
};

