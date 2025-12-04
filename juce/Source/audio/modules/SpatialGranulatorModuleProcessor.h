#pragma once

#include "ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <map>

class SpatialGranulatorModuleProcessor : public ModuleProcessor
{
public:
    // Parameter IDs for APVTS
    static constexpr auto paramIdDryMix = "dryMix";
    static constexpr auto paramIdPenMix = "penMix";
    static constexpr auto paramIdSprayMix = "sprayMix";
    static constexpr auto paramIdDensity = "density";
    static constexpr auto paramIdGrainSize = "grainSize";
    static constexpr auto paramIdBufferLength = "bufferLength";
    
    // Color amount parameters
    static constexpr auto paramIdRedAmount = "redAmount";
    static constexpr auto paramIdGreenAmount = "greenAmount";
    static constexpr auto paramIdBlueAmount = "blueAmount";
    
    // CV Modulation parameter IDs (for getParamRouting) - Virtual IDs, NOT in APVTS
    static constexpr auto paramIdDryMixMod = "dryMix_mod";
    static constexpr auto paramIdPenMixMod = "penMix_mod";
    static constexpr auto paramIdSprayMixMod = "sprayMix_mod";
    static constexpr auto paramIdDensityMod = "density_mod";
    static constexpr auto paramIdGrainSizeMod = "grainSize_mod";

    // Dot types
    enum class DotType
    {
        Pen,    // Static voice (chorus-like)
        Spray   // Grain spawner (produces dynamic grains)
    };

    // Color IDs (fixed set of 3)
    enum class ColorID
    {
        Red,      // Delay time
        Green,    // Volume level
        Blue,     // Pitch shift
        COUNT = 3
    };

    // Dot structure
    struct Dot
    {
        float x;           // 0-1, pan position (static)
        float y;           // 0-1, buffer position (static) - 0=start, 1=end
        float size;        // 0-1, voice reproduction amount + color param intensity (static)
        ColorID color;     // Fixed color set (Red, Green, Blue only)
        DotType type;      // Pen (voice) or Spray (grain spawner)
    };

    SpatialGranulatorModuleProcessor();
    ~SpatialGranulatorModuleProcessor() override = default;

    const juce::String getName() const override { return "spatial_granulator"; }

    // --- JUCE AudioProcessor Overrides ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // --- State Persistence ---
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& tree) override;

    // --- UI & Routing Overrides ---
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    bool usesCustomPinLayout() const override { return true; }
#endif
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    juce::String getAudioInputLabel(int channel) const override;
    juce::String getAudioOutputLabel(int channel) const override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;

private:
    // --- Internal Implementation ---
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Audio processing helpers
    void processPenVoice(const Dot& dot, juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate);
    void processSprayGrains(const Dot& dot, juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate);
    void launchGrain(int grainIndex, const Dot& dot, double sampleRate, int currentWritePos, int currentSamplesWritten, float grainSizeMs);
    float getColorParameterValue(ColorID color, float size) const;
    
    // Color parameter mapping
    struct ColorParameterMapping
    {
        enum class ParameterType { Delay, Volume, Pitch, None };
        ParameterType paramType;
        float minValue;
        float maxValue;
        
        static ColorParameterMapping getMapping(ColorID color)
        {
            switch (color)
            {
                case ColorID::Red:    return { ParameterType::Delay, 0.0f, 2000.0f }; // 0-2000ms
                case ColorID::Green:  return { ParameterType::Volume, -12.0f, 12.0f }; // -12 to +12 dB
                case ColorID::Blue:   return { ParameterType::Pitch, -24.0f, 24.0f };  // -24 to +24 semitones
                default:              return { ParameterType::None, 0.0f, 0.0f };
            }
        }
    };

    // --- APVTS & Parameters ---
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* dryMixParam { nullptr };
    std::atomic<float>* penMixParam { nullptr };
    std::atomic<float>* sprayMixParam { nullptr };
    std::atomic<float>* densityParam { nullptr };
    std::atomic<float>* grainSizeParam { nullptr };
    std::atomic<float>* bufferLengthParam { nullptr };
    std::atomic<float>* redAmountParam { nullptr };
    std::atomic<float>* greenAmountParam { nullptr };
    std::atomic<float>* blueAmountParam { nullptr };

    // --- Grain State (for Spray tool) ---
    struct Grain
    {
        bool isActive { false };
        double readPosition { 0.0 };
        double increment { 1.0 };
        int samplesRemaining { 0 };
        int totalLifetime { 0 };
        float panL { 0.707f };
        float panR { 0.707f };
        float envelope { 0.0f };
        float envelopeIncrement { 0.0f };
        // Dynamic movement
        float movementOffset { 0.0f };
        float movementVelocity { 0.0f };
        // Color parameters (stored from dot)
        ColorID color { ColorID::Red };
        float size { 0.5f };
        float delayTimeMs { 0.0f };
        float volume { 1.0f };
        float pitchOffset { 0.0f };
    };
    std::array<Grain, 128> grainPool; // Larger pool for multiple spray dots
    juce::Random random;

    // --- Voice State (for Pen tool) ---
    struct Voice
    {
        bool isActive { false };
        double readPosition { 0.0 };
        float panL { 0.707f };
        float panR { 0.707f };
        float volume { 1.0f };
        // Delay line for Red color
        std::vector<float> delayBuffer;
        int delayWritePos { 0 };
        float delayTimeMs { 0.0f };
        // Pitch shifter for Blue color
        double pitchRatio { 1.0 };
        double pitchPhase { 0.0 };
        std::vector<float> pitchBuffer;
    };
    std::array<Voice, 64> voicePool; // Pool for Pen tool voices

    // --- Audio Buffering ---
    juce::AudioBuffer<float> sourceBuffer;
    int sourceWritePos { 0 };
    int samplesWritten { 0 }; // Track how many samples have been written to the buffer
    std::map<int, double> dotDensityPhases; // Phase accumulator per dot (using dot index as key)

    // --- Thread-Safe Dot Storage ---
    mutable juce::ReadWriteLock dotsLock;
    std::vector<Dot> dots;

    // --- UI State (only accessed from UI thread) ---
    DotType activeTool { DotType::Pen };
    ColorID activeColor { ColorID::Red };
    float defaultDotSize { 0.3f }; // Default size for new dots

    // --- Parameter Smoothing ---
    juce::SmoothedValue<float> smoothedDryMix, smoothedPenMix, smoothedSprayMix, smoothedDensity, smoothedGrainSize;

#if defined(PRESET_CREATOR_UI)
    // --- Visualization Data (thread-safe, updated from audio thread) ---
    struct VizData
    {
        static constexpr int waveformPoints = 256;
        std::array<std::atomic<float>, waveformPoints> outputWaveform;
        std::atomic<int> activeVoices { 0 };
        std::atomic<int> activeGrains { 0 };
        std::atomic<float> bufferFillLevel { 0.0f }; // 0-1, how much of buffer is filled
        std::atomic<float> outputLevel { 0.0f };

        VizData()
        {
            for (auto& v : outputWaveform) v.store(0.0f);
        }
    };
    VizData vizData;

    // Circular buffer for waveform capture
    juce::AudioBuffer<float> vizOutputBuffer;
    int vizWritePos { 0 };
    static constexpr int vizBufferSize = 2048; // ~43ms at 48kHz
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpatialGranulatorModuleProcessor)
};

