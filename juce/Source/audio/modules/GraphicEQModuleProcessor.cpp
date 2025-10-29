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

    // CRITICAL: Initialize all filter coefficients with unity gain (0 dB) to prevent uninitialized state
    const float q = 1.414f;
    *processorChain.get<0>().state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, centerFrequencies[0], q, 1.0f);
    *processorChain.get<1>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, centerFrequencies[1], q, 1.0f);
    *processorChain.get<2>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, centerFrequencies[2], q, 1.0f);
    *processorChain.get<3>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, centerFrequencies[3], q, 1.0f);
    *processorChain.get<4>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, centerFrequencies[4], q, 1.0f);
    *processorChain.get<5>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, centerFrequencies[5], q, 1.0f);
    *processorChain.get<6>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, centerFrequencies[6], q, 1.0f);
    *processorChain.get<7>().state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, centerFrequencies[7], q, 1.0f);

    // Reset gate/trigger state
    lastTriggerState = false;
    triggerPulseSamplesRemaining = 0;
}

void GraphicEQModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // CRITICAL BUFFER ALIASING WARNING:
    // This module has 2 output buses: Bus 0 (audio L/R) and Bus 1 (CV gate/trig).
    // In JUCE's AudioProcessorGraph, output buses can share the SAME memory as input channels!
    // Specifically: Output Bus 1 channels 0-1 map to Input Bus 0 channels 2-3.
    // 
    // SOLUTION: Get output bus references ONLY AFTER reading all input CVs that may be aliased.
    // Order: 1) Get inBus, 2) Read all CV inputs, 3) THEN get output busses, 4) Write outputs
    
    auto inBus = getBusBuffer(buffer, true, 0);
    // CRITICAL: Do NOT get output busses until AFTER reading all CV inputs to prevent aliasing
    // audioOutBus and cvOutBus will be obtained AFTER reading CV inputs!

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // --- Logging Setup ---
    // Log only once every 100 blocks to avoid spamming the log file.
    static int debugCounter = 0;
    const bool shouldLog = ((debugCounter++ % 100) == 0);

    // LOG POINT 1: Check input signal at entry + CV CHANNELS
    if (shouldLog && inBus.getNumChannels() > 0)
    {
        float inputRms = inBus.getRMSLevel(0, 0, numSamples);
        juce::Logger::writeToLog("[GraphicEQ Debug] At Entry - Input RMS: " + juce::String(inputRms, 6));
        
        // CRITICAL: Log the actual RAW values in channels 2, 3, 4 (Bands 1, 2, 3 mods)
        if (inBus.getNumChannels() >= 5)
        {
            float ch2_sample0 = inBus.getSample(2, 0);
            float ch3_sample0 = inBus.getSample(3, 0);
            float ch4_sample0 = inBus.getSample(4, 0);
            float ch2_rms = inBus.getRMSLevel(2, 0, numSamples);
            float ch3_rms = inBus.getRMSLevel(3, 0, numSamples);
            float ch4_rms = inBus.getRMSLevel(4, 0, numSamples);
            
            juce::Logger::writeToLog("[GraphicEQ CHANNELS] Ch2[0]=" + juce::String(ch2_sample0, 6) + 
                                   " RMS=" + juce::String(ch2_rms, 6) +
                                   ", Ch3[0]=" + juce::String(ch3_sample0, 6) + 
                                   " RMS=" + juce::String(ch3_rms, 6) +
                                   ", Ch4[0]=" + juce::String(ch4_sample0, 6) + 
                                   " RMS=" + juce::String(ch4_rms, 6));
        }
    }

    // --- CRITICAL FIX: Capture input audio BEFORE any write operations ---
    // Create a temporary buffer to hold a copy of the stereo input (true stereo path)
    juce::AudioBuffer<float> inputCopy(2, numSamples);
    if (inBus.getNumChannels() > 0)
    {
        inputCopy.copyFrom(0, 0, inBus, 0, 0, numSamples);
        if (inBus.getNumChannels() > 1)
            inputCopy.copyFrom(1, 0, inBus, 1, 0, numSamples);
        else
            inputCopy.copyFrom(1, 0, inBus, 0, 0, numSamples); // duplicate mono to R
    }
    else
    {
        inputCopy.clear();
    }
    // --- END OF CRITICAL FIX ---

    // --- 1. CRITICAL: Read ALL CV modulation inputs BEFORE any output writes ---
    // Cache threshold parameters first
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

    // --- 2. Read and cache ALL band modulation CVs BEFORE writing outputs ---
    // This prevents output writes from corrupting input CV data
    float bandGainValues[8];
    
    for (int bandIndex = 0; bandIndex < 8; ++bandIndex)
    {
        float gainDb = bandGainParams[bandIndex]->load();
        juce::String paramId = "gainBand" + juce::String(bandIndex + 1);

        // Check for modulation CV on this band (channels 2-9)
        int modChannel = 2 + bandIndex;
        bool isConnected = isParamInputConnected(paramId);
        bool hasChannels = inBus.getNumChannels() > modChannel;
        
        // EXTENSIVE DEBUG LOGGING FOR BANDS 1 & 2
        if (shouldLog && (bandIndex == 0 || bandIndex == 1))
        {
            juce::Logger::writeToLog("[GraphicEQ] Band " + juce::String(bandIndex + 1) + 
                                   " - paramId: " + paramId +
                                   ", modChannel: " + juce::String(modChannel) +
                                   ", isConnected: " + juce::String(isConnected ? "YES" : "NO") + 
                                   ", hasChannels: " + juce::String(hasChannels ? "YES" : "NO") +
                                   ", inBus.getNumChannels(): " + juce::String(inBus.getNumChannels()));
        }
        
        if (isConnected && hasChannels)
        {
            float modCV = inBus.getSample(modChannel, 0);
            gainDb = juce::jmap(modCV, 0.0f, 1.0f, -60.0f, 12.0f);
            setLiveParamValue(paramId + "_live", gainDb);
            
            // DEBUG LOGGING
            if (shouldLog && (bandIndex == 0 || bandIndex == 1))
            {
                juce::Logger::writeToLog("[GraphicEQ] Band " + juce::String(bandIndex + 1) + 
                                       " - modCV: " + juce::String(modCV, 6) + 
                                       ", mapped gainDb: " + juce::String(gainDb, 2));
            }
        }
        else if (shouldLog && (bandIndex == 0 || bandIndex == 1))
        {
            juce::Logger::writeToLog("[GraphicEQ] Band " + juce::String(bandIndex + 1) + 
                                   " - NO MODULATION (using base gainDb: " + juce::String(gainDb, 2) + ")");
        }

        // Cache the gain value for later filter update
        bandGainValues[bandIndex] = gainDb;

        // LOG POINT 3: Log filter parameters for Band 1 as a sample
        if (shouldLog && bandIndex == 0)
        {
            float gainLinear = juce::Decibels::decibelsToGain(gainDb);
            juce::Logger::writeToLog("[GraphicEQ Debug] Band 1 - Gain (dB): " + juce::String(gainDb, 2) + 
                                   ", Gain (Linear): " + juce::String(gainLinear, 6));
        }
    }

    // --- 3. NOW get output busses - all CV inputs have been read and cached ---
    auto audioOutBus = getBusBuffer(buffer, false, 0);
    auto cvOutBus = getBusBuffer(buffer, false, 1);
    
    // --- 4. Write Gate/Trigger CVs (now SAFE - all input CVs are cached) ---
    
    if (cvOutBus.getNumChannels() >= 2)
    {
        float* gateOut = cvOutBus.getWritePointer(GateOut);
        float* trigOut = cvOutBus.getWritePointer(TrigOut);

        for (int i = 0; i < numSamples; ++i)
        {
            float monoSample = 0.5f * (inputCopy.getSample(0, i) + inputCopy.getSample(1, i));
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

    // --- 5. Copy Captured Input to AUDIO Output Bus ---
    if (audioOutBus.getNumChannels() >= 2)
    {
        audioOutBus.copyFrom(0, 0, inputCopy, 0, 0, numSamples);
        audioOutBus.copyFrom(1, 0, inputCopy, 1, 0, numSamples);
    }
    else if (audioOutBus.getNumChannels() == 1)
    {
        audioOutBus.copyFrom(0, 0, inputCopy, 0, 0, numSamples);
    }
    else
    {
        audioOutBus.clear();
    }

    // LOG POINT 2: Check signal after copying
    if (shouldLog && audioOutBus.getNumChannels() > 0)
    {
        float afterCopyRms = audioOutBus.getRMSLevel(0, 0, numSamples);
        juce::Logger::writeToLog("[GraphicEQ Debug] After Copy - Audio Out RMS: " + juce::String(afterCopyRms, 6));
    }

    // --- 6. Update Filter Coefficients (using cached gain values) ---
    const double currentSampleRate = getSampleRate();
    const float filterQ = 1.414f;

    for (int bandIndex = 0; bandIndex < 8; ++bandIndex)
    {
        float gainDb = bandGainValues[bandIndex];
        float gainLinear = juce::Decibels::decibelsToGain(gainDb);

        // Update filter coefficients based on band type
        if (bandIndex == 0)
        {
            *processorChain.get<0>().state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(currentSampleRate, centerFrequencies[bandIndex], filterQ, gainLinear);
        }
        else if (bandIndex == 7)
        {
            *processorChain.get<7>().state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(currentSampleRate, centerFrequencies[bandIndex], filterQ, gainLinear);
        }
        else if (bandIndex >= 1 && bandIndex <= 6) // Peak filters for bands 1-6
        {
            auto newCoefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(currentSampleRate, centerFrequencies[bandIndex], filterQ, gainLinear);
            switch (bandIndex)
            {
                case 1: *processorChain.get<1>().state = *newCoefficients; break;
                case 2: *processorChain.get<2>().state = *newCoefficients; break;
                case 3: *processorChain.get<3>().state = *newCoefficients; break;
                case 4: *processorChain.get<4>().state = *newCoefficients; break;
                case 5: *processorChain.get<5>().state = *newCoefficients; break;
                case 6: *processorChain.get<6>().state = *newCoefficients; break;
            }
        }
    }

    // --- 7. Process Entire Chain in Series (in-place on audioOutBus) ---
    if (audioOutBus.getNumChannels() > 0)
    {
        juce::dsp::AudioBlock<float> audioBlock(audioOutBus.getArrayOfWritePointers(), 
                                                 std::min(2, audioOutBus.getNumChannels()), 
                                                 numSamples);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        processorChain.process(context);

        // LOG POINT 4: Check signal after processing through filter chain
        if (shouldLog)
        {
            float afterChainRms = audioOutBus.getRMSLevel(0, 0, numSamples);
            juce::Logger::writeToLog("[GraphicEQ Debug] After Filter Chain - Audio Out RMS: " + juce::String(afterChainRms, 6));
        }

        // --- 8. Apply Output Gain ---
        float outputGain = juce::Decibels::decibelsToGain(outputLevelParam->load());
        audioOutBus.applyGain(0, 0, numSamples, outputGain);
        if (audioOutBus.getNumChannels() > 1)
            audioOutBus.applyGain(1, 0, numSamples, outputGain);

        // LOG POINT 5: Check final signal after output gain
        if (shouldLog)
        {
            float finalRms = audioOutBus.getRMSLevel(0, 0, numSamples);
            float outputGainDb = outputLevelParam->load();
            juce::Logger::writeToLog("[GraphicEQ Debug] After Output Gain - Audio Out RMS: " + juce::String(finalRms, 6) + 
                                   ", Output Gain (dB): " + juce::String(outputGainDb, 2));
        }
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

std::vector<DynamicPinInfo> GraphicEQModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio inputs (channels 0-1)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});
    
    // Band gain modulation inputs (channels 2-9)
    for (int i = 0; i < 8; ++i)
    {
        pins.push_back({"Band " + juce::String(i + 1) + " Mod", 2 + i, PinDataType::CV});
    }
    
    // Threshold modulation inputs (channels 10-11)
    pins.push_back({"Gate Thr Mod", 10, PinDataType::CV});
    pins.push_back({"Trig Thr Mod", 11, PinDataType::CV});
    
    return pins;
}

std::vector<DynamicPinInfo> GraphicEQModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio outputs (bus 0, channels 0-1)
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});
    
    // CV/Gate outputs (bus 1, channels 0-1, but shown as output channels 2-3)
    pins.push_back({"Gate Out", 2, PinDataType::Gate});
    pins.push_back({"Trig Out", 3, PinDataType::Gate});
    
    return pins;
}

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
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

