#include "ShapingOscillatorModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout ShapingOscillatorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdFrequency, "Frequency",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 440.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdWaveform, "Waveform",
        juce::StringArray { "Sine", "Saw", "Square" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdDrive, "Drive",
        juce::NormalisableRange<float>(1.0f, 50.0f, 0.01f, 0.5f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "relativeFreqMod", "Relative Freq Mod", true));
    
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "relativeDriveMod", "Relative Drive Mod", true));

    return { params.begin(), params.end() };
}

ShapingOscillatorModuleProcessor::ShapingOscillatorModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(5), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ShapingOscillatorParams", createParameterLayout())
{
    frequencyParam = apvts.getRawParameterValue(paramIdFrequency);
    waveformParam  = apvts.getRawParameterValue(paramIdWaveform);
    driveParam     = apvts.getRawParameterValue(paramIdDrive);
    relativeFreqModParam = apvts.getRawParameterValue("relativeFreqMod");
    relativeDriveModParam = apvts.getRawParameterValue("relativeDriveMod");

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));

    oscillator.initialise([](float x){ return std::sin(x); }, 128);
}

void ShapingOscillatorModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    oscillator.prepare(spec);

    smoothedFrequency.reset(sampleRate, 0.01);
    smoothedDrive.reset(sampleRate, 0.01);

#if defined(PRESET_CREATOR_UI)
    vizRawOscBuffer.setSize(1, vizBufferSize, false, true, true);
    vizShapedBuffer.setSize(1, vizBufferSize, false, true, true);
    vizWritePos = 0;
    for (auto& v : vizData.rawOscWaveform) v.store(0.0f);
    for (auto& v : vizData.shapedWaveform) v.store(0.0f);
    vizData.currentFrequency.store(440.0f);
    vizData.currentDrive.store(1.0f);
    vizData.currentWaveform.store(0);
    vizData.outputLevel.store(0.0f);
#endif
}

void ShapingOscillatorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto inBus  = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const bool isFreqMod  = isParamInputConnected(paramIdFrequencyMod);
    const bool isWaveMod  = isParamInputConnected(paramIdWaveformMod);
    const bool isDriveMod = isParamInputConnected(paramIdDriveMod);

    const float* audioInL = inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;
    const float* audioInR = inBus.getNumChannels() > 1 ? inBus.getReadPointer(1) : nullptr;
    const float* freqCV   = isFreqMod  && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* waveCV   = isWaveMod  && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* driveCV  = isDriveMod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;

    auto* outL = outBus.getWritePointer(0);
    auto* outR = outBus.getNumChannels() > 1 ? outBus.getWritePointer(1) : outL;

    const float baseFrequency = frequencyParam != nullptr ? frequencyParam->load() : 440.0f;
    const int   baseWaveform  = waveformParam  != nullptr ? (int) waveformParam->load()  : 0;
    const float baseDrive     = driveParam     != nullptr ? driveParam->load()     : 1.0f;
    const bool relativeFreqMode = relativeFreqModParam != nullptr && relativeFreqModParam->load() > 0.5f;
    const bool relativeDriveMode = relativeDriveModParam != nullptr && relativeDriveModParam->load() > 0.5f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float currentFreq = baseFrequency;
        if (isFreqMod && freqCV)
        {
            const float cv = juce::jlimit(0.0f, 1.0f, freqCV[i]);
            
            if (relativeFreqMode)
            {
                // RELATIVE: CV modulates around base frequency (±4 octaves)
                const float octaveOffset = (cv - 0.5f) * 8.0f;
                currentFreq = baseFrequency * std::pow(2.0f, octaveOffset);
            }
            else
            {
                // ABSOLUTE: CV directly maps to full frequency range
                constexpr float fMin = 20.0f;
                constexpr float fMax = 20000.0f;
                const float spanOct = std::log2(fMax / fMin);
                currentFreq = fMin * std::pow(2.0f, cv * spanOct);
            }
        }

        int currentWave = baseWaveform;
        if (isWaveMod && waveCV)
        {
            const float cv = juce::jlimit(0.0f, 1.0f, waveCV[i]);
            currentWave = (int) (cv * 2.99f);
        }

        float currentDrive = baseDrive;
        if (isDriveMod && driveCV)
        {
            const float cv = juce::jlimit(0.0f, 1.0f, driveCV[i]);
            
            if (relativeDriveMode)
            {
                // RELATIVE: CV modulates around base drive (±2x multiplier)
                const float driveMultiplier = std::pow(4.0f, cv - 0.5f); // 0.25x to 4x range
                currentDrive = baseDrive * driveMultiplier;
            }
            else
            {
                // ABSOLUTE: CV directly maps to full drive range
                currentDrive = juce::jmap(cv, 1.0f, 50.0f);
            }
        }

        smoothedFrequency.setTargetValue(currentFreq);
        smoothedDrive.setTargetValue(currentDrive);

        if (currentWaveform != currentWave)
        {
            if (currentWave == 0)      oscillator.initialise([](float x){ return std::sin(x); }, 128);
            else if (currentWave == 1) oscillator.initialise([](float x){ return (x / juce::MathConstants<float>::pi); }, 128);
            else                       oscillator.initialise([](float x){ return x < 0.0f ? -1.0f : 1.0f; }, 128);
            currentWaveform = currentWave;
        }

        oscillator.setFrequency(smoothedFrequency.getNextValue(), false);
        const float osc = oscillator.processSample(0.0f);
        const float drive = smoothedDrive.getNextValue();
        const float shaped = std::tanh(osc * drive);

        // Process stereo: use audio input if present, otherwise use 1.0 (full amplitude)
        const float inL = audioInL ? audioInL[i] : 1.0f;
        const float inR = audioInR ? audioInR[i] : (audioInL ? inL : 1.0f); // Use L if only L exists, otherwise 1.0
        
        outL[i] = shaped * inL;
        outR[i] = shaped * inR;

#if defined(PRESET_CREATOR_UI)
        // Capture raw oscillator and shaped signals into circular buffers
        if (vizRawOscBuffer.getNumSamples() > 0 && vizShapedBuffer.getNumSamples() > 0)
        {
            vizRawOscBuffer.setSample(0, vizWritePos, osc);
            vizShapedBuffer.setSample(0, vizWritePos, shaped);
            vizWritePos = (vizWritePos + 1) % vizBufferSize;
        }
#endif

        if ((i & 0x3F) == 0)
        {
            setLiveParamValue("frequency_live", smoothedFrequency.getCurrentValue());
            setLiveParamValue("waveform_live", (float) currentWave);
            setLiveParamValue("drive_live", smoothedDrive.getCurrentValue());
        }
    }

    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1] && outBus.getNumChannels() > 1) 
            lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
        else if (lastOutputValues[1])
            lastOutputValues[1]->store(outBus.getSample(0, buffer.getNumSamples() - 1)); // Duplicate if mono
    }

#if defined(PRESET_CREATOR_UI)
    // Update visualization data (thread-safe)
    // Downsample waveforms from circular buffers
    const int stride = vizBufferSize / VizData::waveformPoints;
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        const int readIdx = (vizWritePos - VizData::waveformPoints * stride + i * stride + vizBufferSize) % vizBufferSize;
        const float rawSample = vizRawOscBuffer.getSample(0, readIdx);
        const float shapedSample = vizShapedBuffer.getSample(0, readIdx);
        vizData.rawOscWaveform[i].store(rawSample);
        vizData.shapedWaveform[i].store(shapedSample);
    }

    // Update current state
    const float lastSample = outBus.getNumSamples() > 0 ? outBus.getSample(0, outBus.getNumSamples() - 1) : 0.0f;
    vizData.outputLevel.store(lastSample);
    vizData.currentFrequency.store(smoothedFrequency.getCurrentValue());
    vizData.currentDrive.store(smoothedDrive.getCurrentValue());
    vizData.currentWaveform.store(currentWaveform);
#endif
}

#if defined(PRESET_CREATOR_UI)
void ShapingOscillatorModuleProcessor::drawParametersInNode (float itemWidth,
                                                      const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                      const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();

    const bool freqIsMod = isParamModulated(paramIdFrequencyMod);
    float freq = freqIsMod ? getLiveParamValueFor(paramIdFrequencyMod, "frequency_live", frequencyParam ? frequencyParam->load() : 440.0f)
                           : (frequencyParam ? frequencyParam->load() : 440.0f);

    int wave = (int) (waveformParam ? waveformParam->load() : 0.0f);
    if (isParamModulated(paramIdWaveformMod))
        wave = (int) getLiveParamValueFor(paramIdWaveformMod, "waveform_live", (float) wave);

    const bool driveIsMod = isParamModulated(paramIdDriveMod);
    float drive = driveIsMod ? getLiveParamValueFor(paramIdDriveMod, "drive_live", driveParam ? driveParam->load() : 1.0f)
                             : (driveParam ? driveParam->load() : 1.0f);

    ImGui::PushItemWidth(itemWidth);

    // === SECTION: Oscillator ===
    ThemeText("OSCILLATOR", theme.text.section_header);

    if (freqIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Frequency", &freq, 20.0f, 20000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic))
    {
        if (!freqIsMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = freq;
    }
    if (!freqIsMod) adjustParamOnWheel(ap.getParameter(paramIdFrequency), "frequencyHz", freq);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (freqIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Base oscillator frequency");

    const bool waveIsMod = isParamModulated(paramIdWaveformMod);
    if (waveIsMod) ImGui::BeginDisabled();
    if (ImGui::Combo("Waveform", &wave, "Sine\0Saw\0Square\0\0"))
    {
        if (!waveIsMod) if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdWaveform))) *p = wave;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (waveIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Oscillator waveform shape");

    ImGui::Spacing();
    ImGui::Spacing();

    // === SECTION: Modulation Mode ===
    ThemeText("MODULATION MODE", theme.text.section_header);
    
    bool relativeFreqMod = relativeFreqModParam ? (relativeFreqModParam->load() > 0.5f) : true;
    if (ImGui::Checkbox("Relative Frequency Mod", &relativeFreqMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeFreqMod")))
        {
            *p = relativeFreqMod;
            juce::Logger::writeToLog("[ShapingOsc UI] Relative Freq Mod changed to: " + juce::String(relativeFreqMod ? "TRUE" : "FALSE"));
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Relative: CV modulates around slider frequency\nAbsolute: CV directly controls frequency");

    bool relativeDriveMod = relativeDriveModParam ? (relativeDriveModParam->load() > 0.5f) : true;
    if (ImGui::Checkbox("Relative Drive Mod", &relativeDriveMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeDriveMod")))
        {
            *p = relativeDriveMod;
            juce::Logger::writeToLog("[ShapingOsc UI] Relative Drive Mod changed to: " + juce::String(relativeDriveMod ? "TRUE" : "FALSE"));
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Relative: CV modulates around slider drive\nAbsolute: CV directly controls drive");

    ImGui::Spacing();
    ImGui::Spacing();

    // === SECTION: Waveshaping ===
    ThemeText("WAVESHAPING", theme.text.section_header);

    if (driveIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Drive", &drive, 1.0f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
    {
        if (!driveIsMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdDrive))) *p = drive;
    }
    if (!driveIsMod) adjustParamOnWheel(ap.getParameter(paramIdDrive), "drive", drive);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (driveIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Waveshaping amount (1=clean, 50=extreme)");

    ImGui::Spacing();
    ImGui::Spacing();

    // === SECTION: Oscillator Visualization ===
    ThemeText("OSCILLATOR OUTPUT", theme.text.section_header);
    ImGui::Spacing();

    ImGui::PushID(this); // Unique ID for this node's UI
    auto* drawList = ImGui::GetWindowDrawList();
    const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
    const ImU32 rawOscColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);
    const ImU32 shapedColor = ImGui::ColorConvertFloat4ToU32(theme.accent);
    const ImU32 centerLineColor = IM_COL32(150, 150, 150, 100);

    // Real-time waveform visualization
    const ImVec2 waveOrigin = ImGui::GetCursorScreenPos();
    const float waveHeight = 140.0f;
    const ImVec2 waveMax = ImVec2(waveOrigin.x + itemWidth, waveOrigin.y + waveHeight);
    drawList->AddRectFilled(waveOrigin, waveMax, bgColor, 4.0f);
    ImGui::PushClipRect(waveOrigin, waveMax, true);

    // Read visualization data (thread-safe)
    float rawWaveform[VizData::waveformPoints];
    float shapedWaveform[VizData::waveformPoints];
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        rawWaveform[i] = vizData.rawOscWaveform[i].load();
        shapedWaveform[i] = vizData.shapedWaveform[i].load();
    }

    const float currentFreq = vizData.currentFrequency.load();
    const float currentDrive = vizData.currentDrive.load();
    const int currentWave = vizData.currentWaveform.load();
    const float outputLevel = vizData.outputLevel.load();

    const float midY = waveOrigin.y + waveHeight * 0.5f;
    const float scaleY = waveHeight * 0.45f;
    const float stepX = itemWidth / (float)(VizData::waveformPoints - 1);

    // Draw center line (zero reference)
    drawList->AddLine(ImVec2(waveOrigin.x, midY), ImVec2(waveMax.x, midY), centerLineColor, 1.0f);

    // Draw raw oscillator waveform (thinner, more subtle - shows original shape)
    float prevX = waveOrigin.x;
    float prevY = midY;
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        const float sample = juce::jlimit(-1.0f, 1.0f, rawWaveform[i]);
        const float x = waveOrigin.x + i * stepX;
        const float y = midY - sample * scaleY;
        if (i > 0)
        {
            ImVec4 colorVec4 = ImGui::ColorConvertU32ToFloat4(rawOscColor);
            colorVec4.w = 0.4f; // More transparent for background
            drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(colorVec4), 1.8f);
        }
        prevX = x;
        prevY = y;
    }

    // Draw shaped waveform (thicker, more prominent - shows tanh distortion)
    prevX = waveOrigin.x;
    prevY = midY;
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        const float sample = juce::jlimit(-1.0f, 1.0f, shapedWaveform[i]);
        const float x = waveOrigin.x + i * stepX;
        const float y = midY - sample * scaleY;
        if (i > 0)
            drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), shapedColor, 3.0f);
        prevX = x;
        prevY = y;
    }

    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(waveOrigin.x, waveMax.y));
    ImGui::Dummy(ImVec2(itemWidth, 0));

    ImGui::Spacing();

    // Live parameter readouts
    const char* waveNames[] = { "Sine", "Saw", "Square" };
    const char* waveName = (currentWave >= 0 && currentWave < 3) ? waveNames[currentWave] : "Unknown";
    
    ImGui::Text("Freq: %.1f Hz", currentFreq);
    ImGui::SameLine();
    ImGui::Text("| Drive: %.2f", currentDrive);
    ImGui::SameLine();
    ImGui::Text("| %s", waveName);

    ImGui::Spacing();

    // Output level meter
    float normalizedLevel = juce::jlimit(0.0f, 1.0f, (outputLevel + 1.0f) / 2.0f);
    ImGui::Text("Level: %.3f", outputLevel);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, shapedColor);
    ImGui::ProgressBar(normalizedLevel, ImVec2(itemWidth * 0.5f, 0), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%.0f%%", normalizedLevel * 100.0f);

    ImGui::PopID(); // End unique ID
    ImGui::PopItemWidth();
}

void ShapingOscillatorModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioInputPin("Freq Mod", 2);
    helpers.drawAudioInputPin("Wave Mod", 3);
    helpers.drawAudioInputPin("Drive Mod", 4);

    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}

juce::String ShapingOscillatorModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "In L";
        case 1: return "In R";
        case 2: return "Freq Mod";
        case 3: return "Wave Mod";
        case 4: return "Drive Mod";
        default: return {};
    }
}

juce::String ShapingOscillatorModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Out L";
        case 1: return "Out R";
        default: return {};
    }
}

#endif

bool ShapingOscillatorModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == paramIdFrequencyMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdWaveformMod)  { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdDriveMod)     { outChannelIndexInBus = 4; return true; }
    return false;
}
