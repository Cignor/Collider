#include "VCFModuleProcessor.h"

VCFModuleProcessor::VCFModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(5), true) // ch0-1 audio, ch2-4 mods
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VCFParams", createParameterLayout())
{
    cutoffParam = apvts.getRawParameterValue(paramIdCutoff);
    resonanceParam = apvts.getRawParameterValue(paramIdResonance);
    typeParam = apvts.getRawParameterValue(paramIdType);
    typeModParam = apvts.getRawParameterValue(paramIdTypeMod);
    relativeCutoffModParam = apvts.getRawParameterValue("relativeCutoffMod");
    relativeResonanceModParam = apvts.getRawParameterValue("relativeResonanceMod");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
    
    // Initialize smoothed values
    cutoffSm.reset(1000.0f);
    resonanceSm.reset(1.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout VCFModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdCutoff, "Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 1000.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdResonance, "Resonance",
        juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdType, "Type",
        juce::StringArray { "Low-pass", "High-pass", "Band-pass" }, 0));
    
    // Add modulation parameter for filter type
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdTypeMod, "Type Mod", 0.0f, 1.0f, 0.0f));

    // Relative modulation modes
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "relativeCutoffMod", "Relative Cutoff Mod", true));
    
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "relativeResonanceMod", "Relative Resonance Mod", true));

    return { params.begin(), params.end() };
}

void VCFModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, 2 };
    filterA.prepare(spec);
    filterB.prepare(spec);
    
    // Set smoothing time for parameters (10ms)
    cutoffSm.reset(sampleRate, 0.01);
    resonanceSm.reset(sampleRate, 0.01);
}

void VCFModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    // Get buses
    auto inBus  = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    // Start from dry input like Reverb: copy in -> out, then filter in place
    {
        const int nCh = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels());
        const int nSm = juce::jmin(inBus.getNumSamples(),  outBus.getNumSamples());
        for (int ch = 0; ch < nCh; ++ch)
            outBus.copyFrom(ch, 0, inBus, ch, 0, nSm);
    }
    
    // PER-SAMPLE FIX: Get pointers to modulation CV inputs, if they are connected
    const bool isCutoffMod = isParamInputConnected(paramIdCutoff);
    const bool isResoMod = isParamInputConnected(paramIdResonance);
    const bool isTypeMod = isParamInputConnected(paramIdTypeMod);

    const float* cutoffCV = isCutoffMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* resoCV = isResoMod && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* typeCV = isTypeMod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;

    // Get base parameter values ONCE
    const float baseCutoff = cutoffParam != nullptr ? cutoffParam->load() : 1000.0f;
    const float baseResonance = resonanceParam != nullptr ? resonanceParam->load() : 1.0f;
    const int baseType = static_cast<int>(typeParam != nullptr ? typeParam->load() : 0);
    const bool relativeCutoffMode = relativeCutoffModParam != nullptr && relativeCutoffModParam->load() > 0.5f;
    const bool relativeResonanceMode = relativeResonanceModParam != nullptr && relativeResonanceModParam->load() > 0.5f;

    // Create a temporary buffer for single-sample processing (always 2 channels)
    juce::AudioBuffer<float> sampleBuffer(2, 1);
    juce::dsp::AudioBlock<float> block(sampleBuffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    // Ensure initial filter types
    configureFilterForType(filterA, activeType);
    configureFilterForType(filterB, activeIsA ? activeType : pendingType);

    // PER-SAMPLE FIX: Process each sample individually to respond to changing modulation
    const int numSamples = buffer.getNumSamples();
    for (int i = 0; i < numSamples; ++i)
    {
        // --- Calculate effective parameters FOR THIS SAMPLE ---
        float cutoff = baseCutoff;
        if (isCutoffMod && cutoffCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, cutoffCV[i]);
            
            if (relativeCutoffMode)
            {
                // RELATIVE MODE: CV modulates around base cutoff (±4 octaves)
                const float octaveRange = 4.0f; // CV can modulate +/- 4 octaves
                const float octaveOffset = (cv - 0.5f) * octaveRange; // Center around 0, range [-2, +2] octaves
                cutoff = baseCutoff * std::pow(2.0f, octaveOffset);
            }
            else
            {
                // ABSOLUTE MODE: CV directly maps to full cutoff range (20Hz-20kHz)
                constexpr float fMin = 20.0f;
                constexpr float fMax = 20000.0f;
                const float spanOct = std::log2(fMax / fMin);
                cutoff = fMin * std::pow(2.0f, cv * spanOct);
            }
            cutoff = juce::jlimit(20.0f, 20000.0f, cutoff);
        }
        
        // Apply smoothing to cutoff to prevent zipper noise
        cutoffSm.setTargetValue(cutoff);
        cutoff = cutoffSm.getNextValue();

        float resonance = baseResonance;
        if (isResoMod && resoCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, resoCV[i]);
            
            if (relativeResonanceMode)
            {
                // RELATIVE MODE: CV adds offset to base resonance (±5 units)
                const float resoRange = 10.0f; // CV can modulate resonance by +/- 5 units
                const float resoOffset = (cv - 0.5f) * resoRange; // Center around 0
                resonance = baseResonance + resoOffset;
            }
            else
            {
                // ABSOLUTE MODE: CV directly maps to full resonance range (0.1 - 10.0)
                resonance = juce::jmap(cv, 0.1f, 10.0f);
            }
            resonance = juce::jlimit(0.1f, 10.0f, resonance);
        }
        
        // Apply smoothing to resonance to prevent zipper noise
        resonanceSm.setTargetValue(resonance);
        resonance = resonanceSm.getNextValue();

        int type = baseType;
        if (isTypeMod && typeCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, typeCV[i]);
            int numChoices = 3;
            int modChoice = static_cast<int>(std::floor(cv * numChoices));
            type = (baseType + modChoice) % numChoices;
        }

        // --- Set filter state FOR THIS SAMPLE ---
        // Handle type change with short crossfade between two filter instances
        if (type != activeType && typeCrossfadeRemaining == 0)
        {
            pendingType = type;
            typeCrossfadeRemaining = TYPE_CROSSFADE_SAMPLES;
            // Configure the inactive filter to the new type
            if (activeIsA) configureFilterForType(filterB, pendingType);
            else           configureFilterForType(filterA, pendingType);
        }

        // Keep both filters set to same coeffs
        filterA.setCutoffFrequency(juce::jlimit(20.0f, 20000.0f, cutoff));
        filterA.setResonance(juce::jlimit(0.1f, 10.0f, resonance));
        filterB.setCutoffFrequency(juce::jlimit(20.0f, 20000.0f, cutoff));
        filterB.setResonance(juce::jlimit(0.1f, 10.0f, resonance));
        if ((i & 0x1F) == 0) // decimate telemetry writes
        {
            setLiveParamValue("cutoff_live", cutoff);
            setLiveParamValue("resonance_live", resonance);
            setLiveParamValue("type_live", (float) type);
        }

        // --- Read current output sample (dry) and process in place
        const float inL = outBus.getNumChannels() > 0 ? outBus.getSample(0, i) : 0.0f;
        const float inR = outBus.getNumChannels() > 1 ? outBus.getSample(1, i) : inL;
        sampleBuffer.setSample(0, 0, inL);
        sampleBuffer.setSample(1, 0, inR);

        // Process through both filters and fade if needed
        float yL = inL, yR = inR;
        {
            sampleBuffer.setSample(0, 0, inL);
            sampleBuffer.setSample(1, 0, inR);
            filterA.process(context);
            float aL = sampleBuffer.getSample(0, 0);
            float aR = sampleBuffer.getSample(1, 0);

            sampleBuffer.setSample(0, 0, inL);
            sampleBuffer.setSample(1, 0, inR);
            filterB.process(context);
            float bL = sampleBuffer.getSample(0, 0);
            float bR = sampleBuffer.getSample(1, 0);

            if (typeCrossfadeRemaining > 0)
            {
                const float t = 1.0f - (float) typeCrossfadeRemaining / (float) TYPE_CROSSFADE_SAMPLES;
                const float wA = activeIsA ? (1.0f - t) : t;
                const float wB = 1.0f - wA;
                yL = aL * wA + bL * wB;
                yR = aR * wA + bR * wB;
                --typeCrossfadeRemaining;
                if (typeCrossfadeRemaining == 0)
                {
                    // Switch active filter to the new type and reset states
                    activeIsA = !activeIsA;
                    activeType = pendingType;
                    filterA.reset();
                    filterB.reset();
                }
            }
            else
            {
                if (activeIsA) { yL = aL; yR = aR; }
                else           { yL = bL; yR = bR; }
            }
        }

        // --- Write filtered sample back to output bus
        if (outBus.getNumChannels() > 0) outBus.setSample(0, i, yL);
        if (outBus.getNumChannels() > 1) outBus.setSample(1, i, yR);
    }
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getNumChannels() > 0 ? outBus.getSample(0, numSamples - 1) : 0.0f);
        if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getNumChannels() > 1 ? outBus.getSample(1, numSamples - 1) : 0.0f);
    }
}

// Parameter bus contract implementation
bool VCFModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All inputs are on bus 0
    if (paramId == paramIdCutoff)    { outChannelIndexInBus = 2; return true; } // Cutoff Mod
    if (paramId == paramIdResonance) { outChannelIndexInBus = 3; return true; } // Resonance Mod
    if (paramId == paramIdTypeMod)   { outChannelIndexInBus = 4; return true; } // Type Mod
    return false;
}

std::vector<DynamicPinInfo> VCFModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio inputs (channels 0-1)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});
    
    // Modulation inputs (channels 2-4)
    pins.push_back({"Cutoff Mod", 2, PinDataType::CV});
    pins.push_back({"Resonance Mod", 3, PinDataType::CV});
    pins.push_back({"Type Mod", 4, PinDataType::CV});
    
    return pins;
}

std::vector<DynamicPinInfo> VCFModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio outputs (channels 0-1)
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});
    
    return pins;
}


