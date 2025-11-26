#pragma once

#include "../graph/ModularSynthProcessor.h"
#include "../assets/SampleBank.h"
#include "../voices/SampleVoiceProcessor.h"
#include "../dsp/TimePitchProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>

class SampleSfxModuleProcessor : public ModuleProcessor
{
public:
    SampleSfxModuleProcessor();
    ~SampleSfxModuleProcessor() override = default;

    const juce::String getName() const override { return "sample_sfx"; }

    // --- Audio Processing ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void reset() override;
    void forceStop() override;

    // --- State Management ---
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Required by ModuleProcessor ---
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // --- Sample Folder Management ---
    void setSampleFolder(const juce::File& folder);
    void loadSample(const juce::File& file);
    juce::String getCurrentSampleName() const;
    bool hasSampleLoaded() const;
    int getFolderSampleCount() const { return folderSamples.size(); }
    int getCurrentSampleIndex() const { return currentSampleIndex; }

    // --- Parameter Layout ---
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    // Parameter bus contract implementation
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    bool usesCustomPinLayout() const override { return true; }
    ImVec2 getCustomNodeSize() const override { return ImVec2(320.0f, 0.0f); }

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Pitch Var Mod";
            case 1: return "Gate Mod";
            case 2: return "Trigger";
            case 3: return "Range Start Mod";
            case 4: return "Range End Mod";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Out L";
            case 1: return "Out R";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }

    std::vector<DynamicPinInfo> getDynamicOutputPins() const override
    {
        return {
            { "Out L", 0, PinDataType::Audio },
            { "Out R", 1, PinDataType::Audio }
        };
    }

    // Accept multi-bus layout
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        if (layouts.getMainInputChannelSet().isDisabled())
            return false;
        if (layouts.getMainOutputChannelSet().isDisabled())
            return false;
        return true;
    }

    // Extra state for folder path persistence
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& tree) override;

private:
    // --- APVTS ---
    juce::AudioProcessorValueTreeState apvts;

    // --- Sample Management ---
    std::shared_ptr<SampleBank::Sample> currentSample;
    std::unique_ptr<SampleVoiceProcessor> sampleProcessor;
    std::atomic<SampleVoiceProcessor*> newSampleProcessor { nullptr };
    juce::CriticalSection processorSwapLock;
    std::unique_ptr<SampleVoiceProcessor> processorToDelete;
    juce::String currentSampleName;
    juce::String currentSamplePath;

    // --- Folder Management ---
    juce::String currentFolderPath;
    juce::Array<juce::File> folderSamples;
    int currentSampleIndex { 0 };
    juce::CriticalSection folderLock;  // Protects folderSamples and currentSampleIndex

    // Thread-safe sample metadata
    std::atomic<double> sampleDurationSeconds { 0.0 };
    std::atomic<int> sampleSampleRate { 0 };

    // Trigger edge detection
    bool lastTriggerHigh { false };
    bool sampleEndDetected { false };  // Flag to prevent multiple triggers on playback end
    
    // Playlist queue for triggers (prevents missed triggers)
    juce::Array<int> triggerQueue;
    juce::CriticalSection queueLock;  // Protects triggerQueue

    // Parameter references
    std::atomic<float>* pitchVariationParam { nullptr };
    std::atomic<float>* pitchVariationModParam { nullptr };
    std::atomic<float>* gateParam { nullptr };
    std::atomic<float>* gateModParam { nullptr };
    std::atomic<float>* selectionModeParam { nullptr };  // 0 = Sequential, 1 = Random
    std::atomic<float>* rangeStartParam { nullptr };
    std::atomic<float>* rangeEndParam { nullptr };
    std::atomic<float>* rangeStartModParam { nullptr };
    std::atomic<float>* rangeEndModParam { nullptr };

#if defined(PRESET_CREATOR_UI)
    // Keep a persistent chooser so async callback remains valid
    std::unique_ptr<juce::FileChooser> folderChooser;
    
    // --- Visualization Data (thread-safe, updated when sample loads) ---
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> waveformPreview;
    };
    VizData vizData;
#endif

    // --- Internal Methods ---
    void createSampleProcessor();
    void switchToNextSample();
    void queueNextSample();  // Queue next sample for playback (don't switch immediately)
    void processTriggerQueue();  // Process queued samples when current playback ends
    juce::File getLastFolder() const;
    
#if defined(PRESET_CREATOR_UI)
    void generateWaveformPreview();
#endif
};

