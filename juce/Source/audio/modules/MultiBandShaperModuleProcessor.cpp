#include "MultiBandShaperModuleProcessor.h"
#include <cmath>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout MultiBandShaperModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Create a Drive parameter for each band (0 = mute, 100 = max drive)
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        auto paramId = "drive_" + juce::String(i + 1);
        auto paramName = "Drive " + juce::String(i + 1);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            paramId, paramName,
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f, 0.3f), 1.0f));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputGain", "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

MultiBandShaperModuleProcessor::MultiBandShaperModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(2 + NUM_BANDS + 1), true) // 0-1: Audio In, 2-9: Drive Mods, 10: Gain Mod
          .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MultiBandShaperParams", createParameterLayout())
{
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        driveParams[i] = apvts.getRawParameterValue("drive_" + juce::String(i + 1));
    }
    outputGainParam = apvts.getRawParameterValue("outputGain");

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MultiBandShaperModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, 1 }; // Mono spec for each filter

    const float q = 1.41f; // Standard Q value for reasonable band separation

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            filters[band][ch].prepare(spec);
            filters[band][ch].reset(); // Ensure clean state
            filters[band][ch].coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(
                sampleRate, centerFreqs[band], q);
        }
    }
    
    bandBuffer.setSize(2, samplesPerBlock, false, true, true); // Clear and allocate
    sumBuffer.setSize(2, samplesPerBlock, false, true, true); // Clear and allocate
}

void MultiBandShaperModuleProcessor::releaseResources()
{
    // Reset all filters to ensure clean state when stopped
    for (int band = 0; band < NUM_BANDS; ++band)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            filters[band][ch].reset();
        }
    }
}

void MultiBandShaperModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    sumBuffer.clear();

    for (int band = 0; band < NUM_BANDS; ++band)
    {
        // 1. Filter the original signal to isolate this band
        for (int ch = 0; ch < 2; ++ch)
        {
            bandBuffer.copyFrom(ch, 0, inBus, ch, 0, numSamples);
        }

        // Apply filters per channel
        for (int ch = 0; ch < 2; ++ch)
        {
            float* data = bandBuffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                data[i] = filters[band][ch].processSample(data[i]);
            }
        }

        // 2. Apply waveshaping to the filtered band
        float drive = driveParams[band]->load();
        
        // Check for modulation input from unified input bus (channels 2-9)
        if (isParamInputConnected("drive_" + juce::String(band + 1)))
        {
            int modChannel = 2 + band; // Channels 2-9 are drive mods for bands 0-7
            if (inBus.getNumChannels() > modChannel)
            {
                // Simple 0..1 CV to full drive range mapping (0-100)
                float modValue = inBus.getSample(modChannel, 0);
                drive = juce::jmap(modValue, 0.0f, 1.0f, 0.0f, 100.0f);
            }
        }
        setLiveParamValue("drive_" + juce::String(band + 1) + "_live", drive);

        // Skip processing if drive is zero (band is muted)
        if (drive > 0.001f) // Use a small threshold instead of 0.0f
        {
            // Apply tanh waveshaping
            for (int ch = 0; ch < 2; ++ch)
            {
                float* data = bandBuffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    data[i] = std::tanh(data[i] * drive);
                }
            }

            // 3. Add the shaped band to the final mix
            sumBuffer.addFrom(0, 0, bandBuffer, 0, 0, numSamples);
            sumBuffer.addFrom(1, 0, bandBuffer, 1, 0, numSamples);
        }
    }

    // 4. Apply output gain and copy to the final output bus
    float gainDb = outputGainParam->load();
    
    // Check for modulation on output gain from unified input bus
    if (isParamInputConnected("outputGain"))
    {
        int gainModChannel = 2 + NUM_BANDS; // Channel 10
        if (inBus.getNumChannels() > gainModChannel)
        {
            float modValue = inBus.getSample(gainModChannel, 0);
            gainDb = juce::jmap(modValue, 0.0f, 1.0f, -24.0f, 24.0f);
        }
    }
    setLiveParamValue("outputGain_live", gainDb);
    
    const float finalGain = juce::Decibels::decibelsToGain(gainDb);
    outBus.copyFrom(0, 0, sumBuffer, 0, 0, numSamples);
    outBus.copyFrom(1, 0, sumBuffer, 1, 0, numSamples);
    outBus.applyGain(finalGain);
    
    if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, numSamples - 1));
    if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, numSamples - 1));
}

bool MultiBandShaperModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId.startsWith("drive_"))
    {
        int bandNum = paramId.getTrailingIntValue(); // e.g., "drive_5" -> 5
        if (bandNum > 0 && bandNum <= NUM_BANDS)
        {
            outChannelIndexInBus = 2 + (bandNum - 1); // Channels 2-9 for drives 1-8
            return true;
        }
    }
    if (paramId == "outputGain")
    {
        outChannelIndexInBus = 2 + NUM_BANDS; // Channel 10 for output gain
        return true;
    }
    return false;
}

juce::String MultiBandShaperModuleProcessor::getAudioInputLabel(int channel) const
{
    // Bus 0: Audio In
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    
    // Bus 1: Mod In (channels are relative to the start of the bus)
    // Absolute channel index = channel - numChannelsInBus0
    int modChannel = channel - 2; // Bus 0 has 2 channels (stereo)
    if (modChannel >= 0 && modChannel < NUM_BANDS)
    {
        return "Drive " + juce::String(modChannel + 1) + " Mod";
    }
    if (modChannel == NUM_BANDS)
    {
        return "Gain Mod";
    }
    
    return {};
}

#if defined(PRESET_CREATOR_UI)
void MultiBandShaperModuleProcessor::drawParametersInNode(
    float itemWidth,
    const std::function<bool(const juce::String&)>& isParamModulated,
    const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const float centerFreqs[NUM_BANDS] = { 60.0f, 150.0f, 400.0f, 1000.0f, 2400.0f, 5000.0f, 10000.0f, 16000.0f };

    // --- NEW: Vertical Slider Bank Layout ---
    const float sliderWidth = itemWidth / (float)NUM_BANDS * 0.85f;
    const float sliderHeight = 80.0f;

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        if (i > 0) ImGui::SameLine();

        const auto paramId = "drive_" + juce::String(i + 1);
        auto* driveParamPtr = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId));
        if (!driveParamPtr) continue;

        const bool isDriveModulated = isParamInputConnected(paramId); // Use correct isParamInputConnected
        float drive = isDriveModulated ? getLiveParamValueFor(paramId, paramId + "_live", driveParamPtr->get()) : driveParamPtr->get();

        ImGui::PushID(i);
        ImGui::BeginGroup();

        if (isDriveModulated) ImGui::BeginDisabled();
        
        if (ImGui::VSliderFloat("##drive", ImVec2(sliderWidth, sliderHeight), &drive, 0.0f, 100.0f, "", ImGuiSliderFlags_Logarithmic))
        {
            if (!isDriveModulated) *driveParamPtr = drive;
        }
        
        if (!isDriveModulated) adjustParamOnWheel(driveParamPtr, "drive", drive);
        if (ImGui::IsItemDeactivatedAfterEdit() && !isDriveModulated) { onModificationEnded(); }
        if (isDriveModulated) ImGui::EndDisabled();

        // Draw labels below the slider
        ImGui::Text("%.1f", drive);
        ImGui::Text("%dHz", (int)centerFreqs[i]);
        if (isDriveModulated) { ImGui::SameLine(); ImGui::TextUnformatted("(m)"); }

        ImGui::EndGroup();
        ImGui::PopID();
    }


    // Output Gain Slider (remains horizontal)
    auto* gainParamPtr = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outputGain"));
    const bool isGainModulated = isParamInputConnected("outputGain");
    float gain = isGainModulated ? getLiveParamValueFor("outputGain", "outputGain_live", gainParamPtr->get()) : gainParamPtr->get();
    
    if (isGainModulated) ImGui::BeginDisabled();
    
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::SliderFloat("Output (dB)", &gain, -24.0f, 24.0f, "%.1f dB"))
    {
        if (!isGainModulated) *gainParamPtr = gain;
    }
    if (!isGainModulated) adjustParamOnWheel(gainParamPtr, "gain", gain);
    if (ImGui::IsItemDeactivatedAfterEdit() && !isGainModulated) { onModificationEnded(); }
    ImGui::PopItemWidth();
    
    if (isGainModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
}

void MultiBandShaperModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
    
    ImGui::Spacing(); // Add a little space before mod inputs

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        auto paramId = "drive_" + juce::String(i + 1);
        int busIdx, chanInBus;
        if (getParamRouting(paramId, busIdx, chanInBus))
        {
            helpers.drawAudioInputPin(("Drive " + juce::String(i + 1) + " Mod").toRawUTF8(), getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
        }
    }
    
    // Draw output gain modulation input
    int busIdx, chanInBus;
    if (getParamRouting("outputGain", busIdx, chanInBus))
    {
        helpers.drawAudioInputPin("Gain Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    }
}
#endif

