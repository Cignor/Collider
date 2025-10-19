#include "SAndHModuleProcessor.h"

SAndHModuleProcessor::SAndHModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::discreteChannels(4), true) // 0-1=signal, 2-3=trigger
                        .withInput ("Threshold Mod", juce::AudioChannelSet::mono(), true)
                        .withInput ("Edge Mod", juce::AudioChannelSet::mono(), true)
                        .withInput ("Slew Mod", juce::AudioChannelSet::mono(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SAndHParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue ("threshold");
    slewMsParam = apvts.getRawParameterValue ("slewMs");
    edgeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter ("edge"));
    thresholdModParam = apvts.getRawParameterValue ("threshold_mod");
    slewMsModParam = apvts.getRawParameterValue ("slewMs_mod");
    edgeModParam = apvts.getRawParameterValue ("edge_mod");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
}

juce::AudioProcessorValueTreeState::ParameterLayout SAndHModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("threshold", "Threshold", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("edge", "Edge", juce::StringArray { "Rising", "Falling", "Both" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("slewMs", "Slew (ms)", juce::NormalisableRange<float> (0.0f, 2000.0f, 0.01f, 0.35f), 0.0f));
    
    // Add modulation parameters
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("threshold_mod", "Threshold Mod", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("slewMs_mod", "Slew Mod", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("edge_mod", "Edge Mod", 0.0f, 1.0f, 0.0f));
    return { params.begin(), params.end() };
}

void SAndHModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sr = sampleRate;
    lastTrigL = lastTrigR = 0.0f;
    heldL = heldR = 0.0f;
    outL = outR = 0.0f;
}

void SAndHModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    // Single input bus with 4 channels: 0-1 signal, 2-3 trigger
    auto in  = getBusBuffer (buffer, true, 0);
    auto out = getBusBuffer (buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    
    // --- CORRECTED POINTER LOGIC ---
    const float* sigL = in.getReadPointer(0);
    // If input is mono, sigR should be the same as sigL.
    const float* sigR = in.getNumChannels() > 1 ? in.getReadPointer(1) : sigL;
    
    const float* trgL = in.getNumChannels() > 2 ? in.getReadPointer(2) : sigL;
    // If trigger input is mono, trgR should be the same as trgL, NOT sigR.
    const float* trgR = in.getNumChannels() > 3 ? in.getReadPointer(3) : trgL;
    // --- END OF CORRECTION ---
    float* outLw = out.getWritePointer (0);
    float* outRw = out.getNumChannels() > 1 ? out.getWritePointer (1) : out.getWritePointer (0);

    // PER-SAMPLE FIX: Get pointers to modulation CV inputs, if they are connected
    const bool isThresholdMod = isParamInputConnected("threshold_mod");
    const bool isEdgeMod = isParamInputConnected("edge_mod");
    const bool isSlewMod = isParamInputConnected("slewMs_mod");

    const float* thresholdCV = isThresholdMod ? getBusBuffer(buffer, true, 1).getReadPointer(0) : nullptr;
    const float* edgeCV = isEdgeMod ? getBusBuffer(buffer, true, 2).getReadPointer(0) : nullptr;
    const float* slewCV = isSlewMod ? getBusBuffer(buffer, true, 3).getReadPointer(0) : nullptr;

    // Get base parameter values ONCE
    const float baseThreshold = thresholdParam != nullptr ? thresholdParam->load() : 0.5f;
    const float baseSlewMs = slewMsParam != nullptr ? slewMsParam->load() : 0.0f;
    const int baseEdge = edgeParam != nullptr ? edgeParam->getIndex() : 0;

    // Variables to store final modulated values for UI telemetry
    float finalThreshold = baseThreshold;
    float finalSlewMs = baseSlewMs;
    int finalEdge = baseEdge;

    for (int i = 0; i < numSamples; ++i)
    {
        // PER-SAMPLE FIX: Calculate effective parameters FOR THIS SAMPLE
        float thr = baseThreshold;
        if (isThresholdMod && thresholdCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, thresholdCV[i]);
            // ADDITIVE MODULATION FIX: Add CV offset to base threshold
            const float thresholdRange = 0.3f; // CV can modulate threshold by +/- 0.3
            const float thresholdOffset = (cv - 0.5f) * thresholdRange; // Center around 0
            thr = baseThreshold + thresholdOffset;
            thr = juce::jlimit(0.0f, 1.0f, thr);
        }
        
        float slewMs = baseSlewMs;
        if (isSlewMod && slewCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, slewCV[i]);
            // ADDITIVE MODULATION FIX: Add CV offset to base slew time
            const float slewRange = 500.0f; // CV can modulate slew by +/- 500ms
            const float slewOffset = (cv - 0.5f) * slewRange; // Center around 0
            slewMs = baseSlewMs + slewOffset;
            slewMs = juce::jlimit(0.0f, 2000.0f, slewMs);
        }
        
        int edge = baseEdge;
        if (isEdgeMod && edgeCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, edgeCV[i]);
            // ADDITIVE MODULATION FIX: Add CV offset to base edge setting
            const int edgeOffset = static_cast<int>((cv - 0.5f) * 3.0f); // Range [-1, +1]
            edge = (baseEdge + edgeOffset + 3) % 3; // Wrap around (0,1,2)
        }

        // Store final values for telemetry (use last sample's values)
        finalThreshold = thr;
        finalSlewMs = slewMs;
        finalEdge = edge;
        
        const float slewCoeff = (slewMs <= 0.0f) ? 1.0f : (float) (1.0 - std::exp(-1.0 / (0.001 * slewMs * sr)));
        
        const float tL = trgL[i];
        const float tR = trgR[i];
        const bool riseL = (tL > thr && lastTrigL <= thr);
        const bool fallL = (tL < thr && lastTrigL >= thr);
        const bool riseR = (tR > thr && lastTrigR <= thr);
        const bool fallR = (tR < thr && lastTrigR >= thr);

        const bool doL = (edge == 0 && riseL) || (edge == 1 && fallL) || (edge == 2 && (riseL || fallL));
        const bool doR = (edge == 0 && riseR) || (edge == 1 && fallR) || (edge == 2 && (riseR || fallR));

        if (doL) heldL = sigL[i];
        if (doR) heldR = sigR[i];
        lastTrigL = tL; lastTrigR = tR;
        // Slew limiting toward target
        outL = outL + slewCoeff * (heldL - outL);
        outR = outR + slewCoeff * (heldR - outR);
        outLw[i] = outL;
        outRw[i] = outR;
    }
    
    // Store live modulated values for UI display (use last sample values)
    setLiveParamValue("threshold_live", finalThreshold);
    setLiveParamValue("edge_live", static_cast<float>(finalEdge));
    setLiveParamValue("slewMs_live", finalSlewMs);

    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outLw[numSamples - 1]);
        if (lastOutputValues[1]) lastOutputValues[1]->store(outRw[numSamples - 1]);
    }
}

// Parameter bus contract implementation
bool SAndHModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == "threshold_mod") { outBusIndex = 1; outChannelIndexInBus = 0; return true; }
    if (paramId == "edge_mod") { outBusIndex = 2; outChannelIndexInBus = 0; return true; }
    if (paramId == "slewMs_mod") { outBusIndex = 3; outChannelIndexInBus = 0; return true; }
    return false;
}


