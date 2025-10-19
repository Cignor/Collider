#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <chrono>

/**
    A lightweight performance monitor for tracking audio thread performance.
    
    This class provides real-time CPU usage monitoring for individual modules
    and can help identify performance bottlenecks during audio processing.
*/
class PerformanceMonitor
{
public:
    PerformanceMonitor(const juce::String& moduleName) : name(moduleName) {}
    
    /**
        Call this at the start of processBlock to begin timing.
    */
    void startTiming()
    {
        startTime = std::chrono::high_resolution_clock::now();
    }
    
    /**
        Call this at the end of processBlock to end timing and update statistics.
        
        @param numSamples The number of samples processed in this block
        @param sampleRate The current sample rate
    */
    void endTiming(int numSamples, double sampleRate)
    {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        // Calculate CPU usage percentage
        double blockTimeMs = (numSamples / sampleRate) * 1000.0;
        double actualTimeMs = duration.count() / 1000.0;
        double cpuUsage = (actualTimeMs / blockTimeMs) * 100.0;
        
        // Update running average
        updateAverage(cpuUsage);
        
        // Check for warnings
        if (cpuUsage > 50.0) // More than 50% CPU
        {
            juce::Logger::writeToLog(juce::String("WARNING: ") + name + " using " + 
                                   juce::String(cpuUsage, 1) + "% CPU");
        }
    }
    
    /**
        Get the current average CPU usage percentage.
    */
    double getAverageCpuUsage() const
    {
        return averageCpuUsage.load();
    }
    
    /**
        Get the module name being monitored.
    */
    const juce::String& getName() const { return name; }
    
    /**
        Reset the performance statistics.
    */
    void reset()
    {
        averageCpuUsage.store(0.0);
        sampleCount.store(0);
    }

private:
    void updateAverage(double newValue)
    {
        auto count = sampleCount.fetch_add(1) + 1;
        auto current = averageCpuUsage.load();
        
        // Simple running average
        double newAverage = (current * (count - 1) + newValue) / count;
        averageCpuUsage.store(newAverage);
    }
    
    const juce::String name;
    std::chrono::high_resolution_clock::time_point startTime;
    std::atomic<double> averageCpuUsage { 0.0 };
    std::atomic<int> sampleCount { 0 };
};

/**
    RAII helper for automatic performance monitoring.
    
    Usage:
    @code
    void SomeModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        PerformanceScope scope(performanceMonitor, buffer.getNumSamples(), getSampleRate());
        
        // ... audio processing code ...
    }
    @endcode
*/
class PerformanceScope
{
public:
    PerformanceScope(PerformanceMonitor& monitor, int numSamples, double sampleRate)
        : performanceMonitor(monitor), samples(numSamples), rate(sampleRate)
    {
        performanceMonitor.startTiming();
    }
    
    ~PerformanceScope()
    {
        performanceMonitor.endTiming(samples, rate);
    }

private:
    PerformanceMonitor& performanceMonitor;
    int samples;
    double rate;
};
