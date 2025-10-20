#include "ReverbModuleProcessor.h"

ReverbModuleProcessor::ReverbModuleProcessor()
    : ModuleProcessor (BusesProperties()
        .withInput ("Inputs", juce::AudioChannelSet::discreteChannels(5), true) // 0-1: Audio In, 2: Size Mod, 3: Damp Mod, 4: Mix Mod
        .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ReverbParams", createParameterLayout())
{
    sizeParam = apvts.getRawParameterValue ("size");
    dampParam = apvts.getRawParameterValue ("damp");
    mixParam  = apvts.getRawParameterValue ("mix");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
}

juce::AudioProcessorValueTreeState::ParameterLayout ReverbModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("size", "Size", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("damp", "Damp", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("mix",  "Mix",  juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
    return { p.begin(), p.end() };
}

void ReverbModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    reverb.prepare (spec);
    
    // Reset reverb state
    reverb.reset();
}

void ReverbModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    
    // --- COPY INPUT AUDIO TO OUTPUT BUFFER ---
    // Get separate handles to the input and output buses
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
    // Explicitly copy the dry input audio to the output buffer.
    // This ensures the reverb has a signal to process.
    const int numChannels = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels());
    const int numSamples = juce::jmin(inBus.getNumSamples(), outBus.getNumSamples());
    for (int ch = 0; ch < numChannels; ++ch)
    {
        outBus.copyFrom(ch, 0, inBus, ch, 0, numSamples);
    }
    // --- END OF INPUT COPYING ---
    
    // Read CV from the unified input bus
    float sizeModCV = 0.0f;
    float dampModCV = 0.0f;
    float mixModCV = 0.0f;
    
    // Check if size mod is connected and read CV from channel 2
    if (isParamInputConnected("size") && inBus.getNumChannels() > 2)
    {
        sizeModCV = inBus.getReadPointer(2)[0]; // Read first sample from channel 2
    }
    
    // Check if damp mod is connected and read CV from channel 3
    if (isParamInputConnected("damp") && inBus.getNumChannels() > 3)
    {
        dampModCV = inBus.getReadPointer(3)[0]; // Read first sample from channel 3
    }
    
    // Check if mix mod is connected and read CV from channel 4
    if (isParamInputConnected("mix") && inBus.getNumChannels() > 4)
    {
        mixModCV = inBus.getReadPointer(4)[0]; // Read first sample from channel 4
    }

    // Apply modulation or use parameter values
    float size = 0.0f;
    if (isParamInputConnected("size")) // Size Mod bus connected
    {
        // Map CV [0,1] to size [0, 1]
        size = sizeModCV;
    }
    else
    {
        size = sizeParam != nullptr ? sizeParam->load() : 0.5f;
    }
    
    float damp = 0.0f;
    if (isParamInputConnected("damp")) // Damp Mod bus connected
    {
        // Map CV [0,1] to damp [0, 1]
        damp = dampModCV;
    }
    else
    {
        damp = dampParam != nullptr ? dampParam->load() : 0.3f;
    }
    
    float mix = 0.0f;
    if (isParamInputConnected("mix")) // Mix Mod bus connected
    {
        // Map CV [0,1] to mix [0, 1]
        mix = mixModCV;
    }
    else
    {
        mix = mixParam != nullptr ? mixParam->load() : 0.8f;
    }
    
    // Clamp parameters to valid ranges
    size = juce::jlimit(0.0f, 1.0f, size);
    damp = juce::jlimit(0.0f, 1.0f, damp);
    mix = juce::jlimit(0.0f, 1.0f, mix);
    
    juce::dsp::Reverb::Parameters par;
    par.roomSize = size;
    par.damping  = damp;
    par.wetLevel = mix;
    par.dryLevel = 1.0f - par.wetLevel;
    reverb.setParameters (par);
    
    // Debug: Print parameter values occasionally
    static int debugCounter = 0;
    if (++debugCounter > 44100) // Print every second at 44.1kHz
    {
        debugCounter = 0;
        juce::Logger::writeToLog("Reverb - Size: " + juce::String(size) + ", Damp: " + juce::String(damp) + ", Mix: " + juce::String(mix));
    }
    // Now, process the output buffer, which we have just filled with the input signal.
    juce::dsp::AudioBlock<float> block (outBus);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb.process (ctx);
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, outBus.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, outBus.getNumSamples() - 1));
    }

    // Store live modulated values for UI display
    setLiveParamValue("size_live", size);
    setLiveParamValue("damp_live", damp);
    setLiveParamValue("mix_live", mix);
}

// Parameter bus contract implementation
bool ReverbModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == "size") { outChannelIndexInBus = 2; return true; }
    if (paramId == "damp") { outChannelIndexInBus = 3; return true; }
    if (paramId == "mix")  { outChannelIndexInBus = 4; return true; }
    return false;
}


