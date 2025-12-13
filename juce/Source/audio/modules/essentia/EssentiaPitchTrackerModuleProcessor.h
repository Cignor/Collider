#pragma once

#include "../ModuleProcessor.h"
#include "EssentiaWrapper.h"
#include <memory>
#include <atomic>
#include <vector>

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

class EssentiaPitchTrackerModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdMinFrequency = "min_frequency";
    static constexpr auto paramIdMaxFrequency = "max_frequency";
    static constexpr auto paramIdMethod = "method";
    
    // CV modulation inputs (virtual targets for routing)
    static constexpr auto paramIdMinFrequencyMod = "min_frequency_mod";
    static constexpr auto paramIdMaxFrequencyMod = "max_frequency_mod";

    EssentiaPitchTrackerModuleProcessor();
    ~EssentiaPitchTrackerModuleProcessor() override;

    const juce::String getName() const override { return "essentia_pitch_tracker"; }

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
    essentia::standard::Algorithm* frameCutter { nullptr };
    essentia::standard::Algorithm* windowing { nullptr };
    essentia::standard::Algorithm* spectrum { nullptr };
    essentia::standard::Algorithm* pitchDetector { nullptr };
    essentia::Pool pool;
#endif
    
    double currentSampleRate = 44100.0;
    int currentMethod = 0;
    
    // Analysis buffer
    std::vector<float> analysisBuffer;
    static constexpr int FRAME_SIZE = 2048;
    static constexpr int HOP_SIZE = 256;
    int bufferWritePos = 0;
    int samplesSinceAnalysis = 0;
    
    // Current pitch output (smoothed)
    float currentPitchHz = 0.0f;
    float currentConfidence = 0.0f;
    float smoothedPitchCV = 0.0f;
    float smoothedConfidence = 0.0f;
    
    // Cached parameter pointers
    std::atomic<float>* minFrequencyParam { nullptr };
    std::atomic<float>* maxFrequencyParam { nullptr };
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
        std::atomic<float> pitchCV{0.0f};
        std::atomic<float> confidence{0.0f};
        std::atomic<float> detectedPitchHz{0.0f};
        
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EssentiaPitchTrackerModuleProcessor)
};

