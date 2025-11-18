#include "VCAModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#include <imgui.h>
#endif

VCAModuleProcessor::VCAModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true) // 0-1: Audio In, 2: Gain Mod
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VCAParams", createParameterLayout())
{
    gainParam = apvts.getRawParameterValue("gain");
    relativeGainModParam = apvts.getRawParameterValue("relativeGainMod");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
}

juce::AudioProcessorValueTreeState::ParameterLayout VCAModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "relativeGainMod", "Relative Gain Mod", true));

    return { params.begin(), params.end() };
}

void VCAModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)samplesPerBlock, 2 };
    gain.prepare(spec);

#if defined(PRESET_CREATOR_UI)
    vizInputBuffer.setSize(2, vizBufferSize, false, true, true);
    vizOutputBuffer.setSize(2, vizBufferSize, false, true, true);
    vizWritePos = 0;
    for (auto& v : vizData.inputWaveformL) v.store(0.0f);
    for (auto& v : vizData.inputWaveformR) v.store(0.0f);
    for (auto& v : vizData.outputWaveformL) v.store(0.0f);
    for (auto& v : vizData.outputWaveformR) v.store(0.0f);
    vizData.currentGainDb.store(0.0f);
    vizData.inputLevelDb.store(-60.0f);
    vizData.outputLevelDb.store(-60.0f);
#endif
}

void VCAModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
#if defined(PRESET_CREATOR_UI)
    // Capture input audio for visualization (before processing)
    if (vizInputBuffer.getNumSamples() > 0 && inBus.getNumChannels() >= 2)
    {
        const int numSamples = juce::jmin(buffer.getNumSamples(), vizBufferSize);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (inBus.getNumChannels() > ch)
            {
                const float* inputData = inBus.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const int writeIdx = (vizWritePos + i) % vizBufferSize;
                    vizInputBuffer.setSample(ch, writeIdx, inputData[i]);
                }
            }
        }
    }
#endif
    
    // Read CV from unified input bus (if connected)
    float gainModCV = 0.5f; // Default to neutral (0.5 for relative mode)
    const bool isGainMod = isParamInputConnected("gain");
    
    if (isGainMod)
    {
        if (inBus.getNumChannels() > 2)
            gainModCV = inBus.getReadPointer(2)[0]; // Read first sample from channel 2
    }
    
    const float baseGainDb = gainParam != nullptr ? gainParam->load() : 0.0f;
    const bool relativeMode = relativeGainModParam && relativeGainModParam->load() > 0.5f;
    
    // Process sample by sample to apply modulation
    for (int channel = 0; channel < outBus.getNumChannels(); ++channel)
    {
        float* channelData = outBus.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float finalGainDb = baseGainDb;
            
            if (isGainMod)
            {
                const float cv = juce::jlimit(0.0f, 1.0f, gainModCV);
                
                if (relativeMode)
                {
                    // RELATIVE: CV modulates around base gain (±30dB range)
                    const float dbOffset = (cv - 0.5f) * 60.0f; // ±30dB
                    finalGainDb = baseGainDb + dbOffset;
                }
                else
                {
                    // ABSOLUTE: CV directly maps to full gain range (-60dB to +6dB)
                    finalGainDb = juce::jmap(cv, -60.0f, 6.0f);
                }
            }
            
            finalGainDb = juce::jlimit(-60.0f, 6.0f, finalGainDb);
            const float finalGain = juce::Decibels::decibelsToGain(finalGainDb);
            channelData[i] *= finalGain;
        }
    }
    
#if defined(PRESET_CREATOR_UI)
    // Capture output audio for visualization (after processing)
    if (vizOutputBuffer.getNumSamples() > 0 && outBus.getNumChannels() >= 2)
    {
        const int numSamples = juce::jmin(buffer.getNumSamples(), vizBufferSize);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (outBus.getNumChannels() > ch)
            {
                const float* outputData = outBus.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const int writeIdx = (vizWritePos + i) % vizBufferSize;
                    vizOutputBuffer.setSample(ch, writeIdx, outputData[i]);
                }
            }
        }
        vizWritePos = (vizWritePos + numSamples) % vizBufferSize;
    }
#endif
    
    // Store live modulated values for UI display
    float displayGainDb = baseGainDb;
    if (isGainMod)
    {
        const float cv = juce::jlimit(0.0f, 1.0f, gainModCV);
        if (relativeMode)
        {
            const float dbOffset = (cv - 0.5f) * 60.0f;
            displayGainDb = baseGainDb + dbOffset;
        }
        else
        {
            displayGainDb = juce::jmap(cv, -60.0f, 6.0f);
        }
    }
    setLiveParamValue("gain_live", juce::jlimit(-60.0f, 6.0f, displayGainDb));

    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
    }

#if defined(PRESET_CREATOR_UI)
    // Update visualization data (thread-safe)
    // Downsample waveforms from circular buffers
    const int stride = vizBufferSize / VizData::waveformPoints;
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        const int readIdx = (vizWritePos - VizData::waveformPoints * stride + i * stride + vizBufferSize) % vizBufferSize;
        if (vizInputBuffer.getNumChannels() > 0 && vizOutputBuffer.getNumChannels() > 0)
        {
            vizData.inputWaveformL[i].store(vizInputBuffer.getSample(0, readIdx));
            vizData.inputWaveformR[i].store(vizInputBuffer.getNumChannels() > 1 ? vizInputBuffer.getSample(1, readIdx) : vizInputBuffer.getSample(0, readIdx));
            vizData.outputWaveformL[i].store(vizOutputBuffer.getSample(0, readIdx));
            vizData.outputWaveformR[i].store(vizOutputBuffer.getNumChannels() > 1 ? vizOutputBuffer.getSample(1, readIdx) : vizOutputBuffer.getSample(0, readIdx));
        }
    }

    // Calculate input/output levels (RMS)
    float inputRms = 0.0f;
    float outputRms = 0.0f;
    const int numSamples = buffer.getNumSamples();
    if (numSamples > 0)
    {
        if (inBus.getNumChannels() > 0)
            inputRms = inBus.getRMSLevel(0, 0, numSamples);
        if (outBus.getNumChannels() > 0)
            outputRms = outBus.getRMSLevel(0, 0, numSamples);
    }
    vizData.inputLevelDb.store(juce::Decibels::gainToDecibels(inputRms, -60.0f));
    vizData.outputLevelDb.store(juce::Decibels::gainToDecibels(outputRms, -60.0f));
    vizData.currentGainDb.store(displayGainDb);
#endif
}

#if defined(PRESET_CREATOR_UI)
void VCAModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    float gainDb = gainParam != nullptr ? gainParam->load() : 0.0f;
    
    ImGui::PushID(this); // Unique ID for this node's UI - must be at start
    ImGui::PushItemWidth (itemWidth);
    
    bool isGainModulated = isParamModulated("gain");
    if (isGainModulated) {
        gainDb = getLiveParamValueFor("gain", "gain_live", gainDb);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat ("Gain dB", &gainDb, -60.0f, 6.0f)) if (!isGainModulated) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("gain"))) *p = gainDb;
    if (!isGainModulated) adjustParamOnWheel(ap.getParameter("gain"), "gain", gainDb);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isGainModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    ImGui::Spacing();
    ImGui::Spacing();

    // === SECTION: VCA Visualization ===
    ThemeText("VCA ACTIVITY", theme.text.section_header);
    ImGui::Spacing();

    // Read visualization data (thread-safe) - BEFORE BeginChild
    float inputWaveform[VizData::waveformPoints];
    float outputWaveform[VizData::waveformPoints];
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        inputWaveform[i] = vizData.inputWaveformL[i].load();
        outputWaveform[i] = vizData.outputWaveformL[i].load();
    }
    const float currentGainDb = vizData.currentGainDb.load();
    const float inputLevelDb = vizData.inputLevelDb.load();
    const float outputLevelDb = vizData.outputLevelDb.load();

    // Waveform visualization in child window
    const float waveHeight = 120.0f;
    const ImVec2 graphSize(itemWidth, waveHeight);
    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("VCAViz", graphSize, false, childFlags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
        
        // Background
        const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
        drawList->AddRectFilled(p0, p1, bgColor, 4.0f);
        
        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);
        
        const ImU32 inputColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);
        const ImU32 outputColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre);
        const ImU32 centerLineColor = IM_COL32(150, 150, 150, 100);

        // Draw output waveforms
        const float midY = p0.y + graphSize.y * 0.5f;
        const float scaleY = graphSize.y * 0.45f;
        const float stepX = graphSize.x / (float)(VizData::waveformPoints - 1);

        // Draw center line (zero reference)
        drawList->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), centerLineColor, 1.0f);

        // Draw input waveform (background, more subtle)
        float prevX = p0.x;
        float prevY = midY;
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const float sample = juce::jlimit(-1.0f, 1.0f, inputWaveform[i]);
            const float x = p0.x + i * stepX;
            const float y = midY - sample * scaleY;
            if (i > 0)
            {
                ImVec4 colorVec4 = ImGui::ColorConvertU32ToFloat4(inputColor);
                colorVec4.w = 0.4f; // More transparent for background
                drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(colorVec4), 1.8f);
            }
            prevX = x;
            prevY = y;
        }

        // Draw output waveform (foreground, shows gain effect)
        prevX = p0.x;
        prevY = midY;
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const float sample = juce::jlimit(-1.0f, 1.0f, outputWaveform[i]);
            const float x = p0.x + i * stepX;
            const float y = midY - sample * scaleY;
            if (i > 0)
                drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), outputColor, 2.5f);
            prevX = x;
            prevY = y;
        }

        drawList->PopClipRect();
        
        // Level meters overlay
        ImGui::SetCursorPos(ImVec2(4, waveHeight + 4));
        const float inputNorm = juce::jlimit(0.0f, 1.0f, (inputLevelDb + 60.0f) / 60.0f);
        const float outputNorm = juce::jlimit(0.0f, 1.0f, (outputLevelDb + 60.0f) / 60.0f);
        
        ImGui::Text("Input: %.1f dB", inputLevelDb);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, inputColor);
        ImGui::ProgressBar(inputNorm, ImVec2(graphSize.x * 0.5f, 0), "");
        ImGui::PopStyleColor();
        ImGui::SameLine(); ImGui::Text("%.0f%%", inputNorm * 100.0f);

        ImGui::Text("Output: %.1f dB", outputLevelDb);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, outputColor);
        ImGui::ProgressBar(outputNorm, ImVec2(graphSize.x * 0.5f, 0), "");
        ImGui::PopStyleColor();
        ImGui::SameLine(); ImGui::Text("%.0f%%", outputNorm * 100.0f);

        ImGui::Text("Gain: %.1f dB", currentGainDb);
        
        // Invisible drag blocker
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##vcaVizDrag", graphSize);
    }
    ImGui::EndChild();
    
    ImGui::Spacing();
    ImGui::Spacing();

    // Modulation mode
    bool relativeGainMod = relativeGainModParam ? (relativeGainModParam->load() > 0.5f) : true;
    if (ImGui::Checkbox("Relative Gain Mod", &relativeGainMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeGainMod")))
        {
            *p = relativeGainMod;
            juce::Logger::writeToLog("[VCA UI] Relative Gain Mod changed to: " + juce::String(relativeGainMod ? "TRUE" : "FALSE"));
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Relative: CV modulates around slider gain (±30dB)\nAbsolute: CV directly controls gain (-60dB to +6dB, ignores slider)");

    ImGui::PopItemWidth();
    ImGui::PopID(); // End unique ID
}
#endif

// Parameter bus contract implementation
bool VCAModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == "gain") { outChannelIndexInBus = 2; return true; }
    return false;
}


