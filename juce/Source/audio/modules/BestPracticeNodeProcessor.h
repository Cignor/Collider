#pragma once

/**
 * @file BestPracticeNodeProcessor.h
 * @brief Reference implementation demonstrating proper module naming conventions
 *
 * ## IMPORTANT: Module Naming Convention Standard
 *
 * This file serves as the definitive reference for the correct module naming convention
 * used throughout the Collider modular synthesizer system.
 *
 * ### The Problem We Solved
 * Previously, the system had inconsistent naming conventions:
 * - Module factory registered modules with lowercase names (e.g., "polyvco", "compressor")
 * - Module getName() methods returned PascalCase names (e.g., "PolyVCO", "Compressor")
 * - Module pin database used a mix of lowercase and PascalCase keys
 *
 * This caused pin color-coding failures because the UI couldn't find modules in the database.
 *
 * ### The Solution: Unified Lowercase Convention
 * We standardized on a **lowercase with spaces** naming convention:
 *
 * 1. **Module Factory Registration**: All modules registered as lowercase (e.g., "polyvco")
 * 2. **getName() Return Values**: All modules return lowercase names (e.g., "polyvco")
 * 3. **Pin Database Keys**: All keys are lowercase (e.g., "polyvco")
 * 4. **No Aliases Needed**: Single source of truth eliminates ambiguity
 *
 * ### Pattern for New Modules
 * When creating new modules, follow this exact pattern:
 *
 * ```cpp
 * class NewModuleProcessor : public ModuleProcessor
 * {
 * public:
 *     const juce::String getName() const override { return "new module"; }
 *     // ... rest of implementation
 * };
 * ```
 *
 * Register in ModularSynthProcessor.cpp:
 * ```cpp
 * reg("new module", []{ return std::make_unique<NewModuleProcessor>(); });
 * ```
 *
 * Add to pin database in ImGuiNodeEditorComponent.cpp:
 * ```cpp
 * modulePinDatabase["new module"] = ModulePinInfo(...);
 * ```
 *
 * ### Why This Works
 * - Module factory uses lowercase names as the canonical "type"
 * - getName() returns the same lowercase name for consistency
 * - UI looks up modules by their lowercase type in the pin database
 * - All lookups succeed because keys are standardized
 * - Pin colors display correctly (green for audio, blue for CV, yellow for gate)
 *
 * This pattern ensures robust, maintainable code and eliminates naming-related bugs.
 */

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

class BestPracticeNodeProcessor : public ModuleProcessor
{
public:
    // Parameter IDs
    static constexpr auto paramIdFrequency    = "frequency";
    static constexpr auto paramIdWaveform     = "waveform";
    static constexpr auto paramIdDrive        = "drive";
    // Virtual modulation target IDs (no APVTS parameters required)
    static constexpr auto paramIdFrequencyMod = "frequency_mod";
    static constexpr auto paramIdWaveformMod  = "waveform_mod";
    static constexpr auto paramIdDriveMod     = "drive_mod";

    BestPracticeNodeProcessor();
    ~BestPracticeNodeProcessor() override = default;

    const juce::String getName() const override { return "best_practice"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode (float itemWidth,
                               const std::function<bool(const juce::String& paramId)>& isParamModulated,
                               const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
#endif

    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    juce::dsp::Oscillator<float> oscillator;

    // Cached parameter pointers
    std::atomic<float>* frequencyParam { nullptr };
    std::atomic<float>* waveformParam  { nullptr };
    std::atomic<float>* driveParam     { nullptr };

    // Smoothed values to prevent zipper noise
    juce::SmoothedValue<float> smoothedFrequency;
    juce::SmoothedValue<float> smoothedDrive;

    int currentWaveform = -1;
};


