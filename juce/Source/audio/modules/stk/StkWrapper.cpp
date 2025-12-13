#include "StkWrapper.h"
#include <juce_core/juce_core.h>

#ifdef STK_FOUND
#include <Stk.h>
#endif

std::atomic<double> StkWrapper::s_sampleRate{44100.0};
std::atomic<bool> StkWrapper::s_initialized{false};

void StkWrapper::initializeStk(double sampleRate)
{
#ifdef STK_FOUND
    if (!s_initialized.load())
    {
        stk::Stk::setSampleRate(sampleRate);
        s_sampleRate.store(sampleRate);
        
        // Set rawwaves path to executable directory/rawwaves
        try
        {
            auto exeFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            auto appDir = exeFile.getParentDirectory();
            auto rawwavesDir = appDir.getChildFile("rawwaves");
            
            if (rawwavesDir.exists())
            {
                juce::String rawwavesPath = rawwavesDir.getFullPathName();
                // STK requires trailing slash
                if (!rawwavesPath.endsWithChar('/') && !rawwavesPath.endsWithChar('\\'))
                {
                    #if JUCE_WINDOWS
                    rawwavesPath += "\\";
                    #else
                    rawwavesPath += "/";
                    #endif
                }
                stk::Stk::setRawwavePath(rawwavesPath.toStdString());
                juce::Logger::writeToLog("[StkWrapper] Set rawwaves path to: " + rawwavesPath);
            }
            else
            {
                juce::Logger::writeToLog("[StkWrapper] WARNING: rawwaves directory not found at: " + rawwavesDir.getFullPathName());
            }
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("[StkWrapper] ERROR setting rawwaves path: " + juce::String(e.what()));
        }
        
        s_initialized.store(true);
        juce::Logger::writeToLog("[StkWrapper] STK initialized with sample rate: " + juce::String(sampleRate));
    }
    else
    {
        setSampleRate(sampleRate);
    }
#else
    juce::ignoreUnused(sampleRate);
    juce::Logger::writeToLog("[StkWrapper] WARNING: STK_FOUND not defined - STK library not available!");
#endif
}

void StkWrapper::setSampleRate(double sampleRate)
{
#ifdef STK_FOUND
    stk::Stk::setSampleRate(sampleRate);
    s_sampleRate.store(sampleRate);
#else
    juce::ignoreUnused(sampleRate);
#endif
}

double StkWrapper::getSampleRate()
{
    return s_sampleRate.load();
}

bool StkWrapper::isInitialized()
{
    return s_initialized.load();
}

void StkWrapper::shutdownStk()
{
    // STK doesn't require explicit shutdown, but reset state
    s_initialized.store(false);
}

