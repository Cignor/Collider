#include "GranulatorModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout GranulatorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdDensity, "Density (Hz)", juce::NormalisableRange<float>(0.1f, 100.0f, 0.01f, 0.3f), 10.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdSize, "Size (ms)", juce::NormalisableRange<float>(5.0f, 500.0f, 0.01f, 0.4f), 100.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdPosition, "Position", 0.0f, 1.0f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdSpread, "Spread", 0.0f, 1.0f, 0.1f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdPitch, "Pitch (st)", -24.0f, 24.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdPitchRandom, "Pitch Rand", 0.0f, 12.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdPanRandom, "Pan Rand", 0.0f, 1.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdGate, "Gate", 0.0f, 1.0f, 1.0f));
    return { p.begin(), p.end() };
}

GranulatorModuleProcessor::GranulatorModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(8), true) // Audio L/R, Trig, Density, Size, Position, Pitch, Gate
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GranulatorParams", createParameterLayout())
{
    densityParam      = apvts.getRawParameterValue(paramIdDensity);
    sizeParam         = apvts.getRawParameterValue(paramIdSize);
    positionParam     = apvts.getRawParameterValue(paramIdPosition);
    spreadParam       = apvts.getRawParameterValue(paramIdSpread);
    pitchParam        = apvts.getRawParameterValue(paramIdPitch);
    pitchRandomParam  = apvts.getRawParameterValue(paramIdPitchRandom);
    panRandomParam    = apvts.getRawParameterValue(paramIdPanRandom);
    gateParam         = apvts.getRawParameterValue(paramIdGate);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void GranulatorModuleProcessor::prepareToPlay(double sampleRate, int)
{
    const int bufferSeconds = 2;
    sourceBuffer.setSize(2, (int)(sampleRate * bufferSeconds));
    sourceBuffer.clear();
    sourceWritePos = 0;

    smoothedDensity.reset(sampleRate, 0.05);
    smoothedSize.reset(sampleRate, 0.05);
    smoothedPosition.reset(sampleRate, 0.05);
    smoothedPitch.reset(sampleRate, 0.05);
    smoothedGate.reset(sampleRate, 0.002);

    for (auto& grain : grainPool)
        grain.isActive = false;
}

void GranulatorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    const double sr = getSampleRate();

    // Get modulation CVs
    const bool isTriggerConnected = isParamInputConnected(paramIdTriggerIn);
    const float* trigCV = inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* densityCV = isParamInputConnected(paramIdDensityMod) && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* sizeCV = isParamInputConnected(paramIdSizeMod) && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;
    const float* posCV = isParamInputConnected(paramIdPositionMod) && inBus.getNumChannels() > 5 ? inBus.getReadPointer(5) : nullptr;
    const float* pitchCV = isParamInputConnected(paramIdPitchMod) && inBus.getNumChannels() > 6 ? inBus.getReadPointer(6) : nullptr;
    const float* gateCV = isParamInputConnected(paramIdGateMod) && inBus.getNumChannels() > 7 ? inBus.getReadPointer(7) : nullptr;

    // Get base parameters
    const float baseDensity = densityParam->load();
    const float baseSize = sizeParam->load();
    const float basePos = positionParam->load();
    const float basePitch = pitchParam->load();
    const float baseGate = gateParam->load();

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Record incoming audio to circular buffer
        sourceBuffer.setSample(0, sourceWritePos, inBus.getSample(0, i));
        sourceBuffer.setSample(1, sourceWritePos, inBus.getSample(1, i));

        // 2. Handle triggers
        // Default to ON if the trigger input is not connected.
        bool isGenerating = !isTriggerConnected;
        if (isTriggerConnected && trigCV != nullptr) {
            // If connected, follow the gate signal.
            isGenerating = trigCV[i] > 0.5f;
        }
        
        // 3. Update smoothed parameters
        float density = baseDensity * (densityCV ? juce::jmap(densityCV[i], 0.0f, 1.0f, 0.5f, 2.0f) : 1.0f);
        float sizeMs = baseSize * (sizeCV ? juce::jmap(sizeCV[i], 0.0f, 1.0f, 0.1f, 2.0f) : 1.0f);
        float position = basePos + (posCV ? posCV[i] - 0.5f : 0.0f);
        float pitch = basePitch + (pitchCV ? juce::jmap(pitchCV[i], 0.0f, 1.0f, -12.0f, 12.0f) : 0.0f);
        float gate = gateCV ? juce::jlimit(0.0f, 1.0f, gateCV[i]) : baseGate;
        
        smoothedDensity.setTargetValue(density);
        smoothedSize.setTargetValue(sizeMs);
        smoothedPosition.setTargetValue(position);
        smoothedPitch.setTargetValue(pitch);
        smoothedGate.setTargetValue(gate);

        // 4. Spawn new grains
        if (isGenerating && --samplesUntilNextGrain <= 0) {
            for (int j = 0; j < (int)grainPool.size(); ++j) {
                if (!grainPool[j].isActive) {
                    launchGrain(j, smoothedDensity.getNextValue(), smoothedSize.getNextValue(),
                                smoothedPosition.getNextValue(), spreadParam->load(),
                                smoothedPitch.getNextValue(), pitchRandomParam->load(), panRandomParam->load());
                    break;
                }
            }
            float currentDensity = smoothedDensity.getCurrentValue();
            samplesUntilNextGrain = (currentDensity > 0.1f) ? (int)(sr / currentDensity) : (int)sr;
        }

        // 5. Process active grains
        float sampleL = 0.0f, sampleR = 0.0f;
        for (auto& grain : grainPool) {
            if (grain.isActive) {
                int readPosInt = (int)grain.readPosition;
                float fraction = (float)(grain.readPosition - readPosInt);
                
                // Linear interpolation
                float sL = sourceBuffer.getSample(0, readPosInt) * (1.0f - fraction) + sourceBuffer.getSample(0, (readPosInt + 1) % sourceBuffer.getNumSamples()) * fraction;
                float sR = sourceBuffer.getSample(1, readPosInt) * (1.0f - fraction) + sourceBuffer.getSample(1, (readPosInt + 1) % sourceBuffer.getNumSamples()) * fraction;

                // Hann window envelope
                float envelope = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * (float)(grain.totalLifetime - grain.samplesRemaining) / (float)grain.totalLifetime));
                
                sampleL += sL * envelope * grain.panL;
                sampleR += sR * envelope * grain.panR;

                grain.readPosition += grain.increment;
                if (grain.readPosition >= sourceBuffer.getNumSamples())
                    grain.readPosition -= sourceBuffer.getNumSamples();
                
                if (--grain.samplesRemaining <= 0)
                    grain.isActive = false;
            }
        }
        
        // 6. Apply gate and write to output
        float gateValue = smoothedGate.getNextValue();
        outBus.setSample(0, i, sampleL * gateValue);
        outBus.setSample(1, i, sampleR * gateValue);

        sourceWritePos = (sourceWritePos + 1) % sourceBuffer.getNumSamples();
    }
    
    // Update telemetry
    setLiveParamValue("density_live", smoothedDensity.getCurrentValue());
    setLiveParamValue("size_live", smoothedSize.getCurrentValue());
    setLiveParamValue("position_live", smoothedPosition.getCurrentValue());
    setLiveParamValue("pitch_live", smoothedPitch.getCurrentValue());
    setLiveParamValue("gate_live", smoothedGate.getCurrentValue());
    
    if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, numSamples - 1));
    if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, numSamples - 1));
}

void GranulatorModuleProcessor::launchGrain(int grainIndex, float density, float size, float position, float spread, float pitch, float pitchRandom, float panRandom)
{
    auto& grain = grainPool[grainIndex];
    const double sr = getSampleRate();

    grain.totalLifetime = grain.samplesRemaining = (int)((size / 1000.0f) * sr);
    if (grain.samplesRemaining == 0) return;

    float posOffset = (random.nextFloat() - 0.5f) * spread;
    grain.readPosition = (sourceWritePos - (int)(juce::jlimit(0.0f, 1.0f, position + posOffset) * sourceBuffer.getNumSamples()) + sourceBuffer.getNumSamples()) % sourceBuffer.getNumSamples();

    float pitchOffset = (random.nextFloat() - 0.5f) * pitchRandom;
    grain.increment = std::pow(2.0, (pitch + pitchOffset) / 12.0);

    float pan = (random.nextFloat() - 0.5f) * panRandom;
    grain.panL = std::cos((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
    grain.panR = std::sin((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

    grain.isActive = true;
}

#if defined(PRESET_CREATOR_UI)
void GranulatorModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    ImGui::PushItemWidth(itemWidth);

    auto drawSlider = [&](const char* label, const juce::String& paramId, const juce::String& modId, float min, float max, const char* format, int flags = 0) {
        bool isMod = isParamModulated(modId);
        float value = isMod ? getLiveParamValueFor(modId, paramId + "_live", ap.getRawParameterValue(paramId)->load())
                            : ap.getRawParameterValue(paramId)->load();
        
        if (isMod) ImGui::BeginDisabled();
        if (ImGui::SliderFloat(label, &value, min, max, format, flags))
            if (!isMod) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
        if (!isMod) adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (isMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    };

    drawSlider("Density", paramIdDensity, paramIdDensityMod, 0.1f, 100.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic);
    drawSlider("Size", paramIdSize, paramIdSizeMod, 5.0f, 500.0f, "%.0f ms", ImGuiSliderFlags_Logarithmic);
    drawSlider("Position", paramIdPosition, paramIdPositionMod, 0.0f, 1.0f, "%.2f");
    drawSlider("Spread", paramIdSpread, "", 0.0f, 1.0f, "%.2f");
    drawSlider("Pitch", paramIdPitch, paramIdPitchMod, -24.0f, 24.0f, "%.1f st");
    drawSlider("Pitch Rand", paramIdPitchRandom, "", 0.0f, 12.0f, "%.1f st");
    drawSlider("Pan Rand", paramIdPanRandom, "", 0.0f, 1.0f, "%.2f");
    drawSlider("Gate", paramIdGate, paramIdGateMod, 0.0f, 1.0f, "%.2f");

    ImGui::PopItemWidth();
}

void GranulatorModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioInputPin("Trigger In", 2);
    helpers.drawAudioInputPin("Density Mod", 3);
    helpers.drawAudioInputPin("Size Mod", 4);
    helpers.drawAudioInputPin("Position Mod", 5);
    helpers.drawAudioInputPin("Pitch Mod", 6);
    helpers.drawAudioInputPin("Gate Mod", 7);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

bool GranulatorModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus.
    if (paramId == paramIdTriggerIn)    { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdDensityMod)   { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdSizeMod)      { outChannelIndexInBus = 4; return true; }
    if (paramId == paramIdPositionMod)  { outChannelIndexInBus = 5; return true; }
    if (paramId == paramIdPitchMod)     { outChannelIndexInBus = 6; return true; }
    if (paramId == paramIdGateMod)      { outChannelIndexInBus = 7; return true; }
    return false;
}

juce::String GranulatorModuleProcessor::getAudioInputLabel(int channel) const
{
    switch(channel) {
        case 0: return "In L";
        case 1: return "In R";
        case 2: return "Trigger In";
        case 3: return "Density Mod";
        case 4: return "Size Mod";
        case 5: return "Position Mod";
        case 6: return "Pitch Mod";
        case 7: return "Gate Mod";
        default: return {};
    }
}