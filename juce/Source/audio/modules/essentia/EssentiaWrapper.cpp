#include "EssentiaWrapper.h"
#include <juce_core/juce_core.h>

#ifdef ESSENTIA_FOUND
#include <essentia/essentia.h>
#include <essentia/algorithmfactory.h>
#include <essentia/streaming/algorithmfactory.h>
#endif

std::atomic<bool> EssentiaWrapper::s_initialized{false};

void EssentiaWrapper::initializeEssentia()
{
#ifdef ESSENTIA_FOUND
    if (!s_initialized.load())
    {
        try
        {
            essentia::init();
            s_initialized.store(true);
            juce::Logger::writeToLog("[Essentia] Library initialized successfully");
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("[Essentia] ERROR: Failed to initialize: " + juce::String(e.what()));
            s_initialized.store(false);
        }
    }
#else
    juce::Logger::writeToLog("[Essentia] WARNING: ESSENTIA_FOUND not defined - Essentia library not available!");
#endif
}

void EssentiaWrapper::shutdownEssentia()
{
#ifdef ESSENTIA_FOUND
    if (s_initialized.load())
    {
        try
        {
            essentia::shutdown();
            s_initialized.store(false);
            juce::Logger::writeToLog("[Essentia] Library shutdown");
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("[Essentia] ERROR during shutdown: " + juce::String(e.what()));
        }
    }
#endif
}

bool EssentiaWrapper::isInitialized()
{
    return s_initialized.load();
}

