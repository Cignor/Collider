#pragma once

#include "../ModuleProcessor.h"
#include "EssentiaWrapper.h"
#include <memory>
#include <atomic>
#include <vector>
#include <deque>

#ifdef ESSENTIA_FOUND
#include <essentia/essentia.h>
#include <essentia/algorithmfactory.h>
#include <essentia/streaming/algorithmfactory.h>
#include <essentia/pool.h>
#endif

#if defined(PRESET_CREATOR_UI)
#include "../../../preset_creator/theme/ThemeManager.h"
#include <imgui.h>
#endif

class EssentiaOnsetDetectorModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdThreshold = "threshold";
    static constexpr auto paramIdMinInterval = "min_interval";
    static constexpr auto paramIdSensitivity = "sensitivity";
    static constexpr auto paramIdMethod = "method";
    
    // CV modulation inputs (virtual targets for routing)
    static constexpr auto paramIdThresholdMod = "threshold_mod";

    EssentiaOnsetDetectorModuleProcessor();
    ~EssentiaOnsetDetectorModuleProcessor() override;

    const juce::String getName() const override { return "essentia_onset_detector"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void setTimingInfo(const TransportState& state) override;
    void forceStop() override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    
    void drawIoPins(const NodePinHelpers& helpers) override;
    
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    void initializeEssentiaAlgorithms();
    void shutdownEssentiaAlgorithms();
    
    juce::AudioProcessorValueTreeState apvts;
    
#ifdef ESSENTIA_FOUND
    essentia::standard::Algorithm* onsetDetector { nullptr };
    essentia::Pool pool;
    std::vector<essentia::Real> onsetTimes;
    std::vector<essentia::Real> onsetValues;
#endif
    
    double currentSampleRate = 44100.0;
    int currentMethod = 0;
    
    // Analysis buffer
    std::vector<float> analysisBuffer;
    static constexpr int ANALYSIS_BUFFER_SIZE = 2048; // ~46ms at 44.1kHz
    int bufferWritePos = 0;
    int samplesSinceAnalysis = 0;
    
    // Output state
    std::deque<float> pendingOnsets; // Sample positions for pending onsets
    float lastOnsetTime = -1.0f;
    float absoluteSamplePos = 0.0f; // Track absolute sample position for onset time mapping
    
    // Cached parameter pointers
    std::atomic<float>* thresholdParam { nullptr };
    std::atomic<float>* minIntervalParam { nullptr };
    std::atomic<float>* sensitivityParam { nullptr };
    std::atomic<float>* methodParam { nullptr };
    
    // Transport state
    TransportState m_currentTransport;
    
    // Output telemetry
    std::vector<std::unique_ptr<std::atomic<float>>> lastOutputValues;
    
#if defined(PRESET_CREATOR_UI)
    // Visualization data (thread-safe)
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::atomic<float> inputWaveform[waveformPoints];
        std::atomic<float> onsetGateLevel{0.0f};
        std::atomic<float> velocityLevel{0.0f};
        std::atomic<float> confidenceLevel{0.0f};
        std::atomic<float> detectedOnsets{0.0f};
        
        VizData()
        {
            for (int i = 0; i < waveformPoints; ++i)
                inputWaveform[i].store(0.0f);
        }
    } vizData;
    
    // Circular buffer for visualization
    juce::AudioBuffer<float> vizInputBuffer;
    static constexpr int vizBufferSize = 4096;
    int vizWritePos = 0;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EssentiaOnsetDetectorModuleProcessor)
};

