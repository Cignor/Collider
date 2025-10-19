#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class VoiceProcessor : public juce::AudioProcessor
{
public:
    VoiceProcessor();
    ~VoiceProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override final; // Final - cannot be overridden
    void releaseResources() override {}

    // Pure virtual method for subclasses to implement sound generation
    virtual void renderBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;

    const juce::String getName() const override { return "VoiceProcessor"; }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        // Allow layouts where the input and output channel sets match (common for effects)
        // or where there is no input (for pure generators).
        if (layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet())
            return true;
            
        if (layouts.getMainInputChannelSet().isDisabled() && (layouts.getMainOutputChannelSet().size() == 1 || layouts.getMainOutputChannelSet().size() == 2))
            return true;
            
        return false;
    }
    // Implement all other necessary pure virtuals with empty bodies
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return ""; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    juce::uint64 uniqueId { 0 };

protected:
    juce::AudioProcessorValueTreeState apvts;

private:
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::Chorus<float>                 chorus;
    juce::dsp::Phaser<float>                 phaser;
    juce::dsp::Reverb                        reverb;
    juce::dsp::Compressor<float>             compressor;
    juce::dsp::Limiter<float>                limiter;
    juce::dsp::WaveShaper<float>             waveshaper;

    using DelayLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    DelayLine                                   delayL { 192000 }, delayR { 192000 }; // up to ~2s @96k

    float gateEnvL { 1.0f }, gateEnvR { 1.0f };
    double currentSampleRate { 48000.0 };
    bool fxPrepared { false };
    juce::uint32 preparedChannels { 0 };
    juce::AudioBuffer<float>                 tempBuffer;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceProcessor)
};
