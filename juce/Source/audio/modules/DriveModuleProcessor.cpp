#include "DriveModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif
#include <cmath> // For std::tanh

juce::AudioProcessorValueTreeState::ParameterLayout DriveModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdDrive, "Drive", 0.0f, 2.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMix, "Mix", 0.0f, 1.0f, 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeDriveMod", "Relative Drive Mod", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeMixMod", "Relative Mix Mod", true));
    
    return { params.begin(), params.end() };
}

DriveModuleProcessor::DriveModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Audio In", juce::AudioChannelSet::stereo(), true)
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DriveParams", createParameterLayout())
{
    driveParam = apvts.getRawParameterValue(paramIdDrive);
    mixParam = apvts.getRawParameterValue(paramIdMix);
    relativeDriveModParam = apvts.getRawParameterValue("relativeDriveMod");
    relativeMixModParam = apvts.getRawParameterValue("relativeMixMod");

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

void DriveModuleProcessor::prepareToPlay(double /*sampleRate*/, int samplesPerBlock)
{
    tempBuffer.setSize(2, samplesPerBlock);
#if defined(PRESET_CREATOR_UI)
    vizDryBuffer.setSize(2, samplesPerBlock);
    vizWetBuffer.setSize(2, samplesPerBlock);
    vizDryBuffer.clear();
    vizWetBuffer.clear();
    vizData.harmonicEnergy.store(0.0f);
    vizData.outputLevelDb.store(-60.0f);
#endif
}

void DriveModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
    const float driveAmount = driveParam->load();
    const float mixAmount = mixParam->load();

    // Copy input to output
    const int numInputChannels = inBus.getNumChannels();
    const int numOutputChannels = outBus.getNumChannels();
    const int numSamples = buffer.getNumSamples();

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
    
    const int numChannels = juce::jmin(numInputChannels, numOutputChannels);

#if defined(PRESET_CREATOR_UI)
    vizDryBuffer.makeCopyOf(outBus);
#endif

    // If drive is zero and mix is fully dry, we can skip processing entirely.
    if (driveAmount <= 0.001f && mixAmount <= 0.001f)
    {
        // Update output values for tooltips
        if (lastOutputValues.size() >= 2)
        {
            if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
            if (lastOutputValues[1] && numChannels > 1) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
        }
        return;
    }

    // --- Dry/Wet Mix Implementation (inspired by VoiceProcessor.cpp) ---
    // 1. Make a copy of the original (dry) signal.
    tempBuffer.makeCopyOf(outBus);

    // 2. Apply the distortion to the temporary buffer to create the wet signal.
    const float k = juce::jlimit(0.0f, 10.0f, driveAmount) * 5.0f;
    for (int ch = 0; ch < tempBuffer.getNumChannels(); ++ch)
    {
        auto* data = tempBuffer.getWritePointer(ch);
        for (int i = 0; i < tempBuffer.getNumSamples(); ++i)
        {
            data[i] = std::tanh(k * data[i]);
        }
    }

    // 3. Blend the dry and wet signals in the main output buffer.
    const float dryLevel = 1.0f - mixAmount;
    const float wetLevel = mixAmount;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        // First, scale the original (dry) signal down.
        outBus.applyGain(ch, 0, buffer.getNumSamples(), dryLevel);
        // Then, add the scaled wet signal from our temporary buffer.
        outBus.addFrom(ch, 0, tempBuffer, ch, 0, buffer.getNumSamples(), wetLevel);
    }

#if defined(PRESET_CREATOR_UI)
    vizWetBuffer.makeCopyOf(tempBuffer);

    auto captureWaveform = [&](const juce::AudioBuffer<float>& source, std::array<std::atomic<float>, VizData::waveformPoints>& dest)
    {
        const int samples = juce::jmin(source.getNumSamples(), numSamples);
        if (samples <= 0) return;
        const int stride = juce::jmax(1, samples / VizData::waveformPoints);
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const int idx = juce::jmin(samples - 1, i * stride);
            float value = source.getSample(0, idx);
            if (source.getNumChannels() > 1)
                value = 0.5f * (value + source.getSample(1, idx));
            dest[i].store(juce::jlimit(-1.0f, 1.0f, value));
        }
    };

    captureWaveform(vizDryBuffer, vizData.dryWaveform);
    captureWaveform(vizWetBuffer, vizData.wetWaveform);
    captureWaveform(outBus, vizData.mixWaveform);

    const int visualSamples = juce::jmin(numSamples, vizDryBuffer.getNumSamples());
    float diffAccum = 0.0f;
    for (int i = 0; i < visualSamples; ++i)
    {
        const float drySample = vizDryBuffer.getSample(0, i);
        const float wetSample = vizWetBuffer.getSample(0, i);
        diffAccum += std::abs(wetSample - drySample);
    }
    const float harmonicEnergy = (visualSamples > 0) ? juce::jlimit(0.0f, 1.0f, diffAccum / (float)visualSamples) : 0.0f;
    vizData.harmonicEnergy.store(harmonicEnergy);
    vizData.currentDrive.store(driveAmount);
    vizData.currentMix.store(mixAmount);

    float outputPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outputPeak = juce::jmax(outputPeak, outBus.getRMSLevel(ch, 0, numSamples));
    vizData.outputLevelDb.store(juce::Decibels::gainToDecibels(outputPeak, -60.0f));
#endif
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1] && numChannels > 1) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
    }
}

bool DriveModuleProcessor::getParamRouting(const juce::String& /*paramId*/, int& /*outBusIndex*/, int& /*outChannelIndexInBus*/) const
{
    // No modulation inputs in this version
    return false;
}

juce::String DriveModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    return {};
}

juce::String DriveModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void DriveModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    auto& ap = getAPVTS();
    ImGui::PushID(this);
    ImGui::PushItemWidth(itemWidth);

    auto HelpMarker = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f); ImGui::TextUnformatted(desc); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
    };

    auto drawSlider = [&](const char* label, const juce::String& paramId, float min, float max, const char* format, const char* tooltip) {
        float value = ap.getRawParameterValue(paramId)->load();
        if (ImGui::SliderFloat(label, &value, min, max, format))
            *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
        adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        if (tooltip) { ImGui::SameLine(); HelpMarker(tooltip); }
    };

    ImGui::Spacing();
    ImGui::Text("Drive Visualizer");
    ImGui::Spacing();

    auto* drawList = ImGui::GetWindowDrawList();
    const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
    const ImU32 dryColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);
    const ImU32 wetColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre);
    const ImU32 mixColor = ImGui::ColorConvertFloat4ToU32(theme.accent);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float vizHeight = 110.0f;
    const ImVec2 rectMax = ImVec2(origin.x + itemWidth, origin.y + vizHeight);
    drawList->AddRectFilled(origin, rectMax, bgColor, 4.0f);
    ImGui::PushClipRect(origin, rectMax, true);

    float dryWave[VizData::waveformPoints];
    float wetWave[VizData::waveformPoints];
    float mixWave[VizData::waveformPoints];
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        dryWave[i] = vizData.dryWaveform[i].load();
        wetWave[i] = vizData.wetWaveform[i].load();
        mixWave[i] = vizData.mixWaveform[i].load();
    }

    const float midY = origin.y + vizHeight * 0.5f;
    const float scaleY = vizHeight * 0.4f;
    const float stepX = itemWidth / (float)(VizData::waveformPoints - 1);

    auto drawWave = [&](float* data, ImU32 color, float thickness)
    {
        float px = origin.x;
        float py = midY;
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const float x = origin.x + i * stepX;
            const float y = midY - juce::jlimit(-1.0f, 1.0f, data[i]) * scaleY;
            if (i > 0)
                drawList->AddLine(ImVec2(px, py), ImVec2(x, y), color, thickness);
            px = x;
            py = y;
        }
    };

    drawWave(dryWave, dryColor, 1.6f);
    drawWave(wetWave, wetColor, 2.6f);
    drawWave(mixWave, mixColor, 1.2f);

    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, rectMax.y));
    ImGui::Dummy(ImVec2(itemWidth, 0));

    const float harmonic = vizData.harmonicEnergy.load();
    const float outputDb = vizData.outputLevelDb.load();
    const float driveVal = vizData.currentDrive.load();
    const float mixVal = vizData.currentMix.load();

    ImGui::Spacing();
    ImGui::Text("Harmonic Density");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, wetColor);
    ImGui::ProgressBar(harmonic, ImVec2(itemWidth * 0.5f, 0), juce::String(harmonic * 100.0f, 1).toRawUTF8());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("Output: %.1f dB", outputDb);

    ImGui::Text("Drive %.2f  |  Mix %.2f", driveVal, mixVal);

    ImGui::Spacing();
    ThemeText("Drive Parameters", theme.text.section_header);
    ImGui::Spacing();

    drawSlider("Drive", paramIdDrive, 0.0f, 2.0f, "%.2f", "Saturation amount (0-2)\n0 = clean, 2 = heavy distortion");
    drawSlider("Mix", paramIdMix, 0.0f, 1.0f, "%.2f", "Dry/wet mix (0-1)\n0 = clean, 1 = fully driven");

    ImGui::PopItemWidth();
    ImGui::PopID();
}

void DriveModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

