#include "ReverbModuleProcessor.h"
#include <cmath> // For std::exp, std::sqrt, std::pow

ReverbModuleProcessor::ReverbModuleProcessor()
    : ModuleProcessor (BusesProperties()
        .withInput ("Inputs", juce::AudioChannelSet::discreteChannels(5), true) // 0-1: Audio In, 2: Size Mod, 3: Damp Mod, 4: Mix Mod
        .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ReverbParams", createParameterLayout())
{
    sizeParam = apvts.getRawParameterValue ("size");
    dampParam = apvts.getRawParameterValue ("damp");
    mixParam  = apvts.getRawParameterValue ("mix");
    relativeSizeModParam = apvts.getRawParameterValue("relativeSizeMod");
    relativeDampModParam = apvts.getRawParameterValue("relativeDampMod");
    relativeMixModParam = apvts.getRawParameterValue("relativeMixMod");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
    
    // Initialize visualization data
    for (auto& w : vizData.inputWaveformL) w.store(0.0f);
    for (auto& w : vizData.inputWaveformR) w.store(0.0f);
    for (auto& w : vizData.outputWaveformL) w.store(0.0f);
    for (auto& w : vizData.outputWaveformR) w.store(0.0f);
    for (auto& d : vizData.decayCurve) d.store(0.0f);
    for (auto& s : vizData.frequencySpectrum) s.store(0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout ReverbModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("size", "Size", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("damp", "Damp", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("mix",  "Mix",  juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
    
    p.push_back (std::make_unique<juce::AudioParameterBool>("relativeSizeMod", "Relative Size Mod", true));
    p.push_back (std::make_unique<juce::AudioParameterBool>("relativeDampMod", "Relative Damp Mod", true));
    p.push_back (std::make_unique<juce::AudioParameterBool>("relativeMixMod", "Relative Mix Mod", true));
    
    return { p.begin(), p.end() };
}

void ReverbModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    reverb.prepare (spec);
    
    // Reset reverb state
    reverb.reset();
    
    // Initialize visualization buffers
    vizInputBuffer.setSize(2, vizBufferSize);
    vizOutputBuffer.setSize(2, vizBufferSize);
    vizDryBuffer.setSize(2, vizBufferSize);
    dryBlockTemp.setSize(2, samplesPerBlock);
    vizInputBuffer.clear();
    vizOutputBuffer.clear();
    vizDryBuffer.clear();
    vizWritePos = 0;
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
    const int numInputChannels = inBus.getNumChannels();
    const int numOutputChannels = outBus.getNumChannels();
    const int numSamples = juce::jmin(inBus.getNumSamples(), outBus.getNumSamples());

    if (numInputChannels > 0)
    {
        // If input is mono, copy it to both left and right outputs.
        if (numInputChannels == 1 && numOutputChannels > 1)
        {
            outBus.copyFrom(0, 0, inBus, 0, 0, numSamples);
            outBus.copyFrom(1, 0, inBus, 0, 0, numSamples);
        }
        // Otherwise, perform a standard stereo copy.
        else
        {
            const int channelsToCopy = juce::jmin(numInputChannels, numOutputChannels);
            for (int ch = 0; ch < channelsToCopy; ++ch)
            {
                outBus.copyFrom(ch, 0, inBus, ch, 0, numSamples);
            }
        }
    }
    else
    {
        // If no input is connected, ensure the output is silent.
        outBus.clear();
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

    // Get base parameter values
    const float baseSize = sizeParam != nullptr ? sizeParam->load() : 0.5f;
    const float baseDamp = dampParam != nullptr ? dampParam->load() : 0.3f;
    const float baseMix = mixParam != nullptr ? mixParam->load() : 0.8f;
    const bool relativeSizeMode = relativeSizeModParam && relativeSizeModParam->load() > 0.5f;
    const bool relativeDampMode = relativeDampModParam && relativeDampModParam->load() > 0.5f;
    const bool relativeMixMode = relativeMixModParam && relativeMixModParam->load() > 0.5f;
    
    // Apply modulation or use parameter values
    float size = baseSize;
    if (isParamInputConnected("size"))
    {
        const float cv = juce::jlimit(0.0f, 1.0f, sizeModCV);
        if (relativeSizeMode) {
            // RELATIVE: CV adds offset to base size (±0.5)
            const float offset = (cv - 0.5f) * 1.0f;
            size = baseSize + offset;
        } else {
            // ABSOLUTE: CV directly sets size
            size = cv;
        }
        size = juce::jlimit(0.0f, 1.0f, size);
    }
    
    float damp = baseDamp;
    if (isParamInputConnected("damp"))
    {
        const float cv = juce::jlimit(0.0f, 1.0f, dampModCV);
        if (relativeDampMode) {
            // RELATIVE: CV adds offset to base damp (±0.5)
            const float offset = (cv - 0.5f) * 1.0f;
            damp = baseDamp + offset;
        } else {
            // ABSOLUTE: CV directly sets damp
            damp = cv;
        }
        damp = juce::jlimit(0.0f, 1.0f, damp);
    }
    
    float mix = baseMix;
    if (isParamInputConnected("mix"))
    {
        const float cv = juce::jlimit(0.0f, 1.0f, mixModCV);
        if (relativeMixMode) {
            // RELATIVE: CV adds offset to base mix (±0.5)
            const float offset = (cv - 0.5f) * 1.0f;
            mix = baseMix + offset;
        } else {
            // ABSOLUTE: CV directly sets mix
            mix = cv;
        }
        mix = juce::jlimit(0.0f, 1.0f, mix);
    }
    
    // Clamp parameters to valid ranges
    size = juce::jlimit(0.0f, 1.0f, size);
    damp = juce::jlimit(0.0f, 1.0f, damp);
    mix = juce::jlimit(0.0f, 1.0f, mix);
    
    // Store dry signal for visualization (before reverb processing)
    if (dryBlockTemp.getNumSamples() < numSamples)
        dryBlockTemp.setSize(2, numSamples, false, false, true);
    dryBlockTemp.clear();
    const int dryChannelsToCopy = juce::jmin(2, numOutputChannels);
    for (int ch = 0; ch < dryChannelsToCopy; ++ch)
        dryBlockTemp.copyFrom(ch, 0, outBus, ch, 0, numSamples);
    if (dryChannelsToCopy == 1 && dryBlockTemp.getNumChannels() > 1)
        dryBlockTemp.copyFrom(1, 0, dryBlockTemp, 0, 0, numSamples);
    
    juce::dsp::Reverb::Parameters par;
    par.roomSize = size;
    par.damping  = damp;
    par.wetLevel = mix;
    par.dryLevel = 1.0f - par.wetLevel;
    reverb.setParameters (par);
    
    // Now, process the output buffer, which we have just filled with the input signal.
    juce::dsp::AudioBlock<float> block (outBus);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb.process (ctx);
    
    // Record waveforms to visualization buffers (circular)
    for (int i = 0; i < numSamples; ++i)
    {
        // Record input (dry signal before reverb)
        vizInputBuffer.setSample(0, vizWritePos, dryBlockTemp.getSample(0, i));
        // Record output (after reverb)
        vizOutputBuffer.setSample(0, vizWritePos, outBus.getSample(0, i));
        vizDryBuffer.setSample(0, vizWritePos, dryBlockTemp.getSample(0, i));
        
        if (vizInputBuffer.getNumChannels() > 1 && vizOutputBuffer.getNumChannels() > 1)
        {
            const float inR = numInputChannels > 1 ? dryBlockTemp.getSample(1, i) : dryBlockTemp.getSample(0, i);
            vizInputBuffer.setSample(1, vizWritePos, inR);
            vizOutputBuffer.setSample(1, vizWritePos, outBus.getSample(1, i));
            vizDryBuffer.setSample(1, vizWritePos, dryBlockTemp.getSample(1, i));
        }
        
        vizWritePos = (vizWritePos + 1) % vizBufferSize;
        
        // Update visualization data (throttled - every 64 samples, like BitCrusher/Delay)
        if ((i & 0x3F) == 0)
        {
            // Update current parameter state
            vizData.currentSize.store(size);
            vizData.currentDamp.store(damp);
            vizData.currentMix.store(mix);
            
            // Calculate reverb activity (RMS of output - input difference)
            float activitySum = 0.0f;
            int activityCount = 0;
            const int lookback = juce::jmin(256, vizBufferSize);
            for (int j = 0; j < lookback; ++j)
            {
                int idx = (vizWritePos - lookback + j + vizBufferSize) % vizBufferSize;
                const float diff = std::abs(vizOutputBuffer.getSample(0, idx) - vizInputBuffer.getSample(0, idx));
                activitySum += diff * diff;
                activityCount++;
            }
            const float rms = activityCount > 0 ? std::sqrt(activitySum / (float)activityCount) : 0.0f;
            vizData.reverbActivity.store(rms);
            
            // Update waveform snapshots (downsampled from circular buffer)
            const int step = vizBufferSize / VizData::waveformPoints;
            for (int j = 0; j < VizData::waveformPoints; ++j)
            {
                int idx = (vizWritePos - (VizData::waveformPoints - j) * step + vizBufferSize) % vizBufferSize;
                vizData.inputWaveformL[j].store(vizInputBuffer.getSample(0, idx));
                vizData.outputWaveformL[j].store(vizOutputBuffer.getSample(0, idx));
                if (vizInputBuffer.getNumChannels() > 1 && vizOutputBuffer.getNumChannels() > 1)
                {
                    vizData.inputWaveformR[j].store(vizInputBuffer.getSample(1, idx));
                    vizData.outputWaveformR[j].store(vizOutputBuffer.getSample(1, idx));
                }
            }
            
            // Calculate decay curve based on size and damp
            const float rt60 = size * 3.0f + 0.5f;  // Decay time in seconds (0.5-3.5s)
            const float dampFactor = 1.0f - (damp * 0.7f);  // Damping affects decay rate
            for (int j = 0; j < VizData::decayCurvePoints; ++j)
            {
                const float t = (float)j / (float)(VizData::decayCurvePoints - 1);  // 0 to 1
                const float decay = std::exp(-t * 5.0f / (rt60 * dampFactor));
                vizData.decayCurve[j].store(juce::jlimit(0.0f, 1.0f, decay));
            }
        }
    }
    
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

std::vector<DynamicPinInfo> ReverbModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio inputs (channels 0-1)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});
    
    // Modulation inputs (channels 2-4)
    pins.push_back({"Size Mod", 2, PinDataType::CV});
    pins.push_back({"Damp Mod", 3, PinDataType::CV});
    pins.push_back({"Mix Mod", 4, PinDataType::CV});
    
    return pins;
}

std::vector<DynamicPinInfo> ReverbModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio outputs (channels 0-1)
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});
    
    return pins;
}


