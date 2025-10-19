#include "GraphicEQModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout GraphicEQModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Create a gain parameter for each of the 8 bands
    // FIX: Expand the range to allow for a more effective mute.
    // JUCE's decibelsToGain handles large negative numbers gracefully.
    for (int i = 0; i < 8; ++i)
    {
        juce::String paramID = "gainBand" + juce::String(i + 1);
        juce::String paramName = "Gain " + juce::String(centerFrequencies[i], 0) + " Hz";
        // Change the range from -12.0f to -60.0f. The default remains 0.0f.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(paramID, paramName, -60.0f, 12.0f, 0.0f));
    }

    // FIX: Ensure correct default values are set for all parameters.
    params.push_back(std::make_unique<juce::AudioParameterFloat>("outputLevel", "Output Level", -24.0f, 24.0f, 0.0f));

    // --- NEW PARAMETERS ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateThreshold", "Gate Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -30.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "triggerThreshold", "Trigger Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -6.0f));
    // --- END OF NEW PARAMETERS ---

    return { params.begin(), params.end() };
}

GraphicEQModuleProcessor::GraphicEQModuleProcessor()
    : ModuleProcessor(BusesProperties()
          // Single unified input bus: Channels 0-1 (Audio In L/R), 2-9 (Band Mods), 10-11 (Gate/Trig Thresh Mods)
          .withInput("Audio In", juce::AudioChannelSet::discreteChannels(12), true)
          // --- THE FIX: Define TWO separate output busses ---
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)
          .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(TotalCVOutputs), true)),
      apvts(*this, nullptr, "GraphicEQParams", createParameterLayout())
{
    for (int i = 0; i < 8; ++i)
    {
        bandGainParams[i] = apvts.getRawParameterValue("gainBand" + juce::String(i + 1));
    }
    outputLevelParam = apvts.getRawParameterValue("outputLevel");
    gateThresholdParam = apvts.getRawParameterValue("gateThreshold");
    triggerThresholdParam = apvts.getRawParameterValue("triggerThreshold");

    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void GraphicEQModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 2; // Process in stereo

    processorChain.prepare(spec);
    processorChain.reset();

    // Reset gate/trigger state
    lastTriggerState = false;
    triggerPulseSamplesRemaining = 0;
}

void GraphicEQModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // --- THE FIX: Get the two separate output busses by their index ---
    auto inBus = getBusBuffer(buffer, true, 0);
    auto audioOutBus = getBusBuffer(buffer, false, 0); // Audio is on bus 0
    auto cvOutBus = getBusBuffer(buffer, false, 1);    // CV is on bus 1

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // --- 1. Gate/Trigger Analysis (on clean INPUT signal) ---
    // Read threshold parameters and check for modulation CVs
    float gateThreshDb = gateThresholdParam->load();
    float trigThreshDb = triggerThresholdParam->load();

    // Check for modulation on gate threshold (channel 10)
    if (isParamInputConnected("gateThreshold") && inBus.getNumChannels() > 10)
    {
        float modCV = inBus.getSample(10, 0);
        gateThreshDb = juce::jmap(modCV, 0.0f, 1.0f, -60.0f, 0.0f);
        setLiveParamValue("gateThreshold_live", gateThreshDb);
    }

    // Check for modulation on trigger threshold (channel 11)
    if (isParamInputConnected("triggerThreshold") && inBus.getNumChannels() > 11)
    {
        float modCV = inBus.getSample(11, 0);
        trigThreshDb = juce::jmap(modCV, 0.0f, 1.0f, -60.0f, 0.0f);
        setLiveParamValue("triggerThreshold_live", trigThreshDb);
    }

    const float gateThreshLin = juce::Decibels::decibelsToGain(gateThreshDb);
    const float trigThreshLin = juce::Decibels::decibelsToGain(trigThreshDb);

    // --- THE FIX: Write CV outputs to the dedicated cvOutBus ---
    if (cvOutBus.getNumChannels() >= 2)
    {
        float* gateOut = cvOutBus.getWritePointer(GateOut);
        float* trigOut = cvOutBus.getWritePointer(TrigOut);

        for (int i = 0; i < numSamples; ++i)
        {
            // Compute mono signal from stereo input (channels 0-1)
            float monoSample = (inBus.getSample(0, i) + inBus.getSample(inBus.getNumChannels() > 1 ? 1 : 0, i)) * 0.5f;
            float sampleAbs = std::abs(monoSample);

            // Gate output
            gateOut[i] = sampleAbs > gateThreshLin ? 1.0f : 0.0f;

            // Trigger output (edge detection with pulse)
            bool isAboveTrig = sampleAbs > trigThreshLin;
            if (isAboveTrig && !lastTriggerState) {
                triggerPulseSamplesRemaining = (int)(getSampleRate() * 0.001); // 1ms pulse
            }
            lastTriggerState = isAboveTrig;

            trigOut[i] = (triggerPulseSamplesRemaining > 0) ? 1.0f : 0.0f;
            if (triggerPulseSamplesRemaining > 0) triggerPulseSamplesRemaining--;
        }
    }

    // --- 2. Copy Input Audio to AUDIO Output Bus ---
    if (audioOutBus.getNumChannels() >= 2)
    {
        audioOutBus.copyFrom(0, 0, inBus, 0, 0, numSamples);
        if (inBus.getNumChannels() > 1)
            audioOutBus.copyFrom(1, 0, inBus, 1, 0, numSamples);
        else
            audioOutBus.copyFrom(1, 0, inBus, 0, 0, numSamples);
    }
    else
    {
        audioOutBus.clear();
    }

    // --- 3. Update Filter Coefficients ---
    double sampleRate = getSampleRate();
    const float q = 1.414f;

    for (int bandIndex = 0; bandIndex < 8; ++bandIndex)
    {
        float gainDb = bandGainParams[bandIndex]->load();
        juce::String paramId = "gainBand" + juce::String(bandIndex + 1);

        // Check for modulation CV on this band (channels 2-9)
        int modChannel = 2 + bandIndex;
        if (isParamInputConnected(paramId) && inBus.getNumChannels() > modChannel)
        {
            float modCV = inBus.getSample(modChannel, 0);
            gainDb = juce::jmap(modCV, 0.0f, 1.0f, -60.0f, 12.0f);
            setLiveParamValue(paramId + "_live", gainDb);
        }

        float gainLinear = juce::Decibels::decibelsToGain(gainDb);

        // CRITICAL FIX: Use independent 'if' statements (not 'else if') so ALL filters update every block
        if (bandIndex == 0)
        {
            processorChain.get<0>().state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, centerFrequencies[bandIndex], q, gainLinear);
        }
        
        if (bandIndex > 0 && bandIndex < 7) // Peak filters for bands 1-6
        {
            auto newCoefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, centerFrequencies[bandIndex], q, gainLinear);
            if      (bandIndex == 1) { processorChain.get<1>().state = newCoefficients; }
            else if (bandIndex == 2) { processorChain.get<2>().state = newCoefficients; }
            else if (bandIndex == 3) { processorChain.get<3>().state = newCoefficients; }
            else if (bandIndex == 4) { processorChain.get<4>().state = newCoefficients; }
            else if (bandIndex == 5) { processorChain.get<5>().state = newCoefficients; }
            else if (bandIndex == 6) { processorChain.get<6>().state = newCoefficients; }
        }
        
        if (bandIndex == 7)
        {
            processorChain.get<7>().state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, centerFrequencies[bandIndex], q, gainLinear);
        }
    }

    // --- 4. Process Entire Chain in Series (in-place on output buffer) ---
    // SAFETY FIX: Only process if we have a valid output bus
    if (outBus.getNumChannels() > 0)
    {
        juce::dsp::AudioBlock<float> audioBlock(outBus.getArrayOfWritePointers(), 
                                                 std::min(2, outBus.getNumChannels()), 
                                                 numSamples);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        processorChain.process(context);

        // --- 5. Apply Output Gain ---
        float outputGain = juce::Decibels::decibelsToGain(outputLevelParam->load());
        outBus.applyGain(0, 0, numSamples, outputGain);
        if (outBus.getNumChannels() > 1)
            outBus.applyGain(1, 0, numSamples, outputGain);
    }
}

bool GraphicEQModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    // All inputs are now on Bus 0 with the following layout:
    // Channels 0-1: Audio In L/R
    // Channels 2-9: Band 1-8 Gain Mods
    // Channel 10: Gate Threshold Mod
    // Channel 11: Trigger Threshold Mod

    if (paramId.startsWith("gainBand"))
    {
        int bandIndex = paramId.substring(8).getIntValue() - 1;
        if (bandIndex >= 0 && bandIndex < 8)
        {
            outBusIndex = 0;
            outChannelIndexInBus = 2 + bandIndex; // Start at channel 2
            return true;
        }
    }

    if (paramId == "gateThreshold") { outBusIndex = 0; outChannelIndexInBus = 10; return true; }
    if (paramId == "triggerThreshold") { outBusIndex = 0; outChannelIndexInBus = 11; return true; }

    return false;
}

juce::String GraphicEQModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    if (channel >= 2 && channel < 10) return "Band " + juce::String(channel - 1) + " Mod";
    // --- NEW ---
    if (channel == 10) return "Gate Thr Mod";
    if (channel == 11) return "Trig Thr Mod";
    return {};
}

juce::String GraphicEQModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    // --- NEW ---
    if (channel == 2) return "Gate Out";
    if (channel == 3) return "Trig Out";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void GraphicEQModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();

    // --- 1. Draw EQ Band Sliders ---
    const int numBands = 8;
    const float sliderWidth = itemWidth / (float)numBands * 0.9f;
    const float sliderHeight = 100.0f;

    ImGui::PushItemWidth(sliderWidth);
    for (int i = 0; i < numBands; ++i)
    {
        if (i > 0) ImGui::SameLine();
        ImGui::PushID(i);
        ImGui::BeginGroup();

        juce::String paramId = "gainBand" + juce::String(i + 1);
        const bool isMod = isParamModulated(paramId);
        float gainDb = isMod ? getLiveParamValueFor(paramId, "gainBand" + juce::String(i + 1) + "_live", bandGainParams[i]->load())
                             : bandGainParams[i]->load();

        if (isMod) ImGui::BeginDisabled();
        if (ImGui::VSliderFloat("##eq", ImVec2(sliderWidth, sliderHeight), &gainDb, -60.0f, 12.0f, ""))
        {
            if (!isMod)
            {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)))
                {
                    *p = gainDb;
                }
            }
        }
        if (!isMod) adjustParamOnWheel(apvts.getParameter(paramId), paramId, gainDb);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isMod) ImGui::EndDisabled();

        juce::String label = (centerFrequencies[i] < 1000) ? juce::String(centerFrequencies[i], 0) : juce::String(centerFrequencies[i] / 1000.0f, 1) + "k";
        float labelWidth = ImGui::CalcTextSize(label.toRawUTF8()).x;
        float offset = (sliderWidth - labelWidth) * 0.5f;
        if (offset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
        ImGui::TextUnformatted(label.toRawUTF8());

        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::PopItemWidth();

    // --- 2. Draw Control Parameters ---
    ImGui::PushItemWidth(itemWidth);

    // Gate Threshold (modulatable)
    const bool isGateMod = isParamModulated("gateThreshold");
    float gateThresh = isGateMod ? getLiveParamValueFor("gateThreshold", "gateThreshold_live", gateThresholdParam->load())
                                 : gateThresholdParam->load();
    if (isGateMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Gate Threshold", &gateThresh, -60.0f, 0.0f, "%.1f dB")) {
        if (!isGateMod) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("gateThreshold")))
            {
                *p = gateThresh;
            }
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isGateMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Trigger Threshold (modulatable)
    const bool isTrigMod = isParamModulated("triggerThreshold");
    float trigThresh = isTrigMod ? getLiveParamValueFor("triggerThreshold", "triggerThreshold_live", triggerThresholdParam->load())
                                 : triggerThresholdParam->load();
    if (isTrigMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Trigger Threshold", &trigThresh, -60.0f, 0.0f, "%.1f dB")) {
        if (!isTrigMod) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("triggerThreshold")))
            {
                *p = trigThresh;
            }
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isTrigMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Output Level
    float outLevel = outputLevelParam->load();
    if (ImGui::SliderFloat("Output Level", &outLevel, -24.0f, 24.0f, "%.1f dB"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outputLevel")))
        {
            *p = outLevel;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }

    ImGui::PopItemWidth();
}

void GraphicEQModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // All inputs on Bus 0 (channels 0-11)
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);

    // Band gain modulation inputs (channels 2-9)
    for (int i = 0; i < 8; ++i)
    {
        helpers.drawAudioInputPin(("Band " + juce::String(i + 1) + " Mod").toRawUTF8(), 2 + i);
    }

    // Threshold modulation inputs (channels 10-11)
    helpers.drawAudioInputPin("Gate Thr Mod", 10);
    helpers.drawAudioInputPin("Trig Thr Mod", 11);

    // Output pins
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
    helpers.drawAudioOutputPin("Gate Out", 2);
    helpers.drawAudioOutputPin("Trig Out", 3);
}
#endif

void GraphicEQModuleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Get the current state of all parameters from the APVTS.
    auto state = apvts.copyState();
    // Create an XML representation of the state.
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    // Copy the XML data to the destination memory block.
    copyXmlToBinary(*xml, destData);
}

void GraphicEQModuleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Try to get an XML representation from the raw data.
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    // If the XML is valid and has the correct tag...
    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            // ...replace the current APVTS state with the new one.
            apvts.repla