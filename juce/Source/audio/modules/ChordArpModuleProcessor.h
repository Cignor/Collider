#pragma once

#include "ModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

/**
 * ChordArpModuleProcessor
 *
 * Harmony brain node that generates chord pitches and arpeggios from simple CV inputs.
 * This is an initial skeleton implementation following the BestPractice / Modulation Trinity
 * patterns – behaviour will be expanded in later passes.
 */
class ChordArpModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs for APVTS
    static constexpr auto paramIdScale          = "scale";
    static constexpr auto paramIdKey            = "key";
    static constexpr auto paramIdChordMode      = "chordMode";
    static constexpr auto paramIdVoicing        = "voicing";
    static constexpr auto paramIdArpMode        = "arpMode";
    static constexpr auto paramIdArpDivision    = "arpDivision";
    static constexpr auto paramIdUseExtClock    = "useExternalClock";
    static constexpr auto paramIdNumVoices      = "numVoices";

    // Virtual modulation target IDs (no APVTS parameters)
    static constexpr auto paramIdDegreeMod      = "degree_mod";
    static constexpr auto paramIdRootCvMod      = "root_cv_mod";
    static constexpr auto paramIdChordModeMod   = "chordMode_mod";
    static constexpr auto paramIdArpRateMod     = "arpRate_mod";

    ChordArpModuleProcessor();
    ~ChordArpModuleProcessor() override = default;

    const juce::String getName() const override { return "chord_arp"; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // Transport / rhythm integration
    void setTimingInfo (const TransportState& state) override;
    std::optional<RhythmInfo> getRhythmInfo() const override;
    void forceStop() override;

    // Parameter bus contract implementation
    bool getParamRouting (const juce::String& paramId,
                          int& outBusIndex,
                          int& outChannelIndexInBus) const override;

    // Pin label overrides
    juce::String getAudioInputLabel (int channel) const override;
    juce::String getAudioOutputLabel (int channel) const override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Cached parameter pointers
    std::atomic<float>* scaleParam       { nullptr };
    std::atomic<float>* keyParam         { nullptr };
    std::atomic<float>* chordModeParam   { nullptr };
    std::atomic<float>* voicingParam     { nullptr };
    std::atomic<float>* arpModeParam     { nullptr };
    std::atomic<float>* arpDivisionParam { nullptr };
    std::atomic<float>* useExtClockParam { nullptr };
    std::atomic<float>* numVoicesParam   { nullptr };

    // Simple arp state (stub, to be expanded)
    struct ArpState
    {
        int    currentIndex     { 0 };
        double phase            { 0.0 };
        double samplesPerStep   { 0.0 };
        bool   gateOn           { false };
    };

    ArpState     arpState;
    double       currentSampleRate { 44100.0 };
    TransportState currentTransport {};

#if defined(PRESET_CREATOR_UI)
    // Lightweight visualization snapshot shared with the UI thread.
    // All fields are atomics and written only from the audio thread in processBlock().
    struct VizData
    {
        std::atomic<float> degreeIn      { 0.0f };
        std::atomic<float> rootCvIn      { 0.0f };
        std::atomic<float> pitch1        { 0.0f };
        std::atomic<float> pitch2        { 0.0f };
        std::atomic<float> pitch3        { 0.0f };
        std::atomic<float> pitch4        { 0.0f };
        std::atomic<float> arpPitch      { 0.0f };
        std::atomic<float> arpGate       { 0.0f };
    };

    VizData vizData;

    // Custom node size and pin layout
    bool usesCustomPinLayout() const override { return true; }
    ImVec2 getCustomNodeSize() const override { return ImVec2 (320.0f, 0.0f); }

    void drawParametersInNode (float itemWidth,
                               const std::function<bool (const juce::String& paramId)>& isParamModulated,
                               const std::function<void()>& onModificationEnded) override;
    void drawIoPins (const NodePinHelpers& helpers) override;
#endif
};


