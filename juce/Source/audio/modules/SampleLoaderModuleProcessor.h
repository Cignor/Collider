#pragma once

#include "../graph/ModularSynthProcessor.h"
#include "../assets/SampleBank.h"
#include "../voices/SampleVoiceProcessor.h"
#include "../dsp/TimePitchProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SampleLoaderModuleProcessor : public ModuleProcessor
{
public:
    SampleLoaderModuleProcessor();
    ~SampleLoaderModuleProcessor() override = default;

    const juce::String getName() const override { return "sample loader"; }

    // --- Audio Processing ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void reset() override;
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Required by ModuleProcessor ---
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // --- Sample Loading ---
    void loadSample(const juce::File& file);
    void loadSample(const juce::String& filePath);
    void randomizeSample();
    juce::String getCurrentSampleName() const;
    bool hasSampleLoaded() const;

    // (Removed SoundTouch controls; using Rubber Band via TimePitchProcessor)
    
    // --- Debug and Monitoring ---
    void setDebugOutput(bool enabled);
    void logCurrentSettings() const;

    // --- Parameter Layout ---
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
#endif

    // Parameter bus contract implementation (must be available in Collider too)
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

    juce::String getAudioInputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Pitch Mod";
            case 1: return "Speed Mod";
            case 2: return "Gate Mod";
            case 3: return "Trigger Mod";
            case 4: return "Range Start Mod";
            case 5: return "Range End Mod";
            case 6: return "Randomize Trig";
            default: return juce::String("In ") + juce::String(channel + 1);
        }
    }

    juce::String getAudioOutputLabel(int channel) const override
    {
        switch (channel)
        {
            case 0: return "Audio Output";
            default: return juce::String("Out ") + juce::String(channel + 1);
        }
    }
    
    // --- Spectrogram Access ---
    juce::Image getSpectrogramImage()
    {
        const juce::ScopedLock lock(imageLock);
        return spectrogramImage;
    }

private:
    // --- ADD THIS STATE VARIABLE ---
    std::atomic<bool> isPlaying { false };

    // --- APVTS ---
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* engineParam { nullptr }; // 0: rubberband, 1: naive
    
    // --- Sample Management ---
    std::shared_ptr<SampleBank::Sample> currentSample;
    std::unique_ptr<SampleVoiceProcessor> sampleProcessor;
    std::atomic<SampleVoiceProcessor*> newSampleProcessor { nullptr };
    juce::CriticalSection processorSwapLock;
    std::unique_ptr<SampleVoiceProcessor> processorToDelete;
    juce::String currentSampleName;
    juce::String currentSamplePath;
    
    // ADD THESE TWO LINES
    double sampleDurationSeconds = 0.0;
    int sampleSampleRate = 0;
    
    // Trigger edge detection for trigger_mod
    bool lastTriggerHigh { false };
    
    // --- ADD THIS LINE for the new randomize trigger ---
    bool lastRandomizeTriggerHigh { false };
    
#if defined(PRESET_CREATOR_UI)
    // Keep a persistent chooser so async callback remains valid
    std::unique_ptr<juce::FileChooser> fileChooser;
#endif
    
    // Rubber Band is configured in TimePitchProcessor; keep no per-node ST params
    
    // --- Debug ---
    bool debugOutput { false };
    
    // --- Spectrogram Data ---
    juce::Image spectrogramImage;
    juce::CriticalSection imageLock;

    // --- Range Parameters ---
    std::atomic<float>* rangeStartParam { nullptr };
    std::atomic<float>* rangeEndParam { nullptr };
    std::atomic<float>* rangeStartModParam { nullptr };
    std::atomic<float>* rangeEndModParam { nullptr };
    double readPosition { 0.0 };
    
    // --- Parameter References ---
    // Parameters are accessed directly via apvts.getRawParameterValue()
    
    // --- Internal Methods ---
    void updateSoundTouchSettings();
    void createSampleProcessor();
    void generateSpectrogram();
};
