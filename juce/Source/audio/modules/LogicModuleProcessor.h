#pragma once

#include "ModuleProcessor.h"
#include <array>
#include <atomic>
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

/**
    A logic utility module that performs boolean operations on Gate signals.

    This module takes two Gate inputs and provides outputs for various logical
    operations: AND, OR, XOR, and NOT. Useful for creating complex gate patterns
    and conditional triggers in modular patches.
*/
class LogicModuleProcessor : public ModuleProcessor
{
public:
    LogicModuleProcessor();
    ~LogicModuleProcessor() override = default;

    const juce::String getName() const override { return "logic"; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    /**
        Maps parameter IDs to their corresponding modulation bus and channel indices.
        
        @param paramId              The parameter ID to query.
        @param outBusIndex          Receives the bus index for modulation.
        @param outChannelIndexInBus Receives the channel index within the bus.
        @returns                    True if the parameter supports modulation.
    */
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    bool usesCustomPinLayout() const override { return true; }
#endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Parameters
    std::atomic<float>* operationParam { nullptr };
    std::atomic<float>* gateThresholdParam { nullptr };

#if defined(PRESET_CREATOR_UI)
    // --- Visualization Data (thread-safe, updated from audio thread) ---
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> inputAWaveform;
        std::array<std::atomic<float>, waveformPoints> inputBWaveform;
        std::array<std::atomic<float>, waveformPoints> andOutputWaveform;
        std::array<std::atomic<float>, waveformPoints> orOutputWaveform;
        std::array<std::atomic<float>, waveformPoints> xorOutputWaveform;
        std::array<std::atomic<float>, waveformPoints> notAOutputWaveform;
        std::atomic<float> currentGateThreshold { 0.5f };
        std::atomic<int> currentOperation { 0 };
        std::atomic<bool> inputAState { false };
        std::atomic<bool> inputBState { false };
        std::atomic<bool> andState { false };
        std::atomic<bool> orState { false };
        std::atomic<bool> xorState { false };
        std::atomic<bool> notAState { false };

        VizData()
        {
            for (auto& v : inputAWaveform) v.store(0.0f);
            for (auto& v : inputBWaveform) v.store(0.0f);
            for (auto& v : andOutputWaveform) v.store(0.0f);
            for (auto& v : orOutputWaveform) v.store(0.0f);
            for (auto& v : xorOutputWaveform) v.store(0.0f);
            for (auto& v : notAOutputWaveform) v.store(0.0f);
        }
    };
    VizData vizData;

    // Circular buffers for waveform capture
    juce::AudioBuffer<float> vizInputABuffer;
    juce::AudioBuffer<float> vizInputBBuffer;
    juce::AudioBuffer<float> vizAndOutputBuffer;
    juce::AudioBuffer<float> vizOrOutputBuffer;
    juce::AudioBuffer<float> vizXorOutputBuffer;
    juce::AudioBuffer<float> vizNotAOutputBuffer;
    int vizWritePos { 0 };
    static constexpr int vizBufferSize = 2048; // ~43ms at 48kHz
#endif
};