#include "BPMMonitorModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif
#include <limits>
#include <cmath>

namespace
{
constexpr bool kForceBpmMonitorDebugLogging = false;
}

juce::AudioProcessorValueTreeState::ParameterLayout BPMMonitorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Operation mode selector
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Mode",
        juce::StringArray{"Auto", "Introspection Only", "Detection Only"}, 0));
    
    // BPM normalization range for CV outputs
    params.push_back(std::make_unique<juce::AudioParameterFloat>("minBPM", "Min BPM", 
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f), 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("maxBPM", "Max BPM", 
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f), 240.0f));
    
    // Beat detection settings
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sensitivity", "Detection Sensitivity", 
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("detMinBPM", "Det Min BPM", 
        juce::NormalisableRange<float>(5.0f, 1000.0f, 1.0f), 20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("detMaxBPM", "Det Max BPM", 
        juce::NormalisableRange<float>(5.0f, 1000.0f, 1.0f), 480.0f));
    
    // Number of active detection inputs (0-16)
    params.push_back(std::make_unique<juce::AudioParameterInt>("numInputs", "Num Detection Inputs", 
        0, MAX_DETECTION_INPUTS, 4));
    
    return { params.begin(), params.end() };
}

BPMMonitorModuleProcessor::BPMMonitorModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Detection Inputs", juce::AudioChannelSet::discreteChannels(MAX_DETECTION_INPUTS), true)
                        .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(96), true)), // Max 32 sources * 3 outputs each
      apvts(*this, nullptr, "BPMMonitorParams", createParameterLayout())
{
    // Initialize output telemetry for tooltips
    for (int i = 0; i < 96; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void BPMMonitorModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    m_sampleRate = sampleRate;
    
    // Reset all tap tempo analyzers and channel times
    for (auto& analyzer : m_tapAnalyzers)
        analyzer.reset();
    m_channelTime.fill(0.0);
    
    // Reset scan counter
    m_scanCounter = 0;
}

void BPMMonitorModuleProcessor::scanGraphForRhythmSources()
{
    const juce::ScopedLock lock(m_sourcesLock);
    m_introspectedSources.clear();
    
    // Get parent synth
    auto* synth = getParent();
    if (!synth)
        return;
    
    // Iterate through all modules in the graph
    auto modules = synth->getModulesInfo();
    for (const auto& [logicalId, moduleType] : modules)
    {
        // Skip ourselves
        if (logicalId == getLogicalId())
            continue;
        
        // Get the module processor
        auto* module = synth->getModuleForLogical(logicalId);
        if (!module)
            continue;
        
        // Query for rhythm info
        auto rhythmInfo = module->getRhythmInfo();
        if (rhythmInfo.has_value())
        {
            IntrospectedSource source;
            source.name = rhythmInfo->displayName;
            source.type = rhythmInfo->sourceType;
            source.bpm = rhythmInfo->bpm;
            source.isActive = rhythmInfo->isActive;
            source.isSynced = rhythmInfo->isSynced;
            m_introspectedSources.push_back(source);
        }
    }
}

void BPMMonitorModuleProcessor::processDetection(const juce::AudioBuffer<float>& buffer)
{
    const int numInputs = apvts.getRawParameterValue("numInputs")->load();
    const float sensitivity = apvts.getRawParameterValue("sensitivity")->load();
    const float detMinBPM = apvts.getRawParameterValue("detMinBPM")->load();
    const float detMaxBPM = apvts.getRawParameterValue("detMaxBPM")->load();
    
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int activeChannels = std::min(std::min(numInputs, numChannels), MAX_DETECTION_INPUTS);
    const double sampleTime = 1.0 / m_sampleRate;

    std::vector<DetectedRhythmSource> detected;
    
    // Process each active detection input
    for (int ch = 0; ch < activeChannels; ++ch)
    {
        const float* inputData = buffer.getReadPointer(ch);
        auto& analyzer = m_tapAnalyzers[ch];
        analyzer.setSensitivity(sensitivity);
        analyzer.setMinBPM(detMinBPM);
        analyzer.setMaxBPM(detMaxBPM);

        // Process all samples - analyzer tracks time internally
        for (int i = 0; i < numSamples; ++i)
        {
            m_channelTime[ch] += sampleTime;
            analyzer.processSample(inputData[i], m_channelTime[ch]);
        }

        // If we have a valid BPM, add it to detected sources
        if (analyzer.isActive() && analyzer.getBPM() > 0.0f)
        {
            DetectedRhythmSource source;
            source.name = "Detect In " + juce::String(ch + 1);
            source.inputChannel = ch;
            source.detectedBPM = analyzer.getBPM();
            source.confidence = 1.0f;  // Median is already stable, no need for confidence metric
            source.isActive = true;
            detected.push_back(source);
        }
    }

    // Update detected sources
    {
        const juce::ScopedLock lock(m_sourcesLock);
        m_detectedSources = detected;
    }

#if defined(PRESET_CREATOR_UI)
    // Update visualization data
    {
        const juce::ScopedLock lock(m_vizLock);
        m_vizDetected.clear();
        for (const auto& src : detected)
        {
            VizSource viz;
            viz.name = src.name;
            viz.bpm = src.detectedBPM;
            viz.confidence = src.confidence;
            viz.isActive = src.isActive;
            m_vizDetected.push_back(viz);
        }
    }
#endif
}

void BPMMonitorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto detectionInput = getBusBuffer (buffer, true, 0);
    auto outputBuffer   = getBusBuffer (buffer, false, 0);

    // Read parameters
    const int mode = apvts.getRawParameterValue("mode")->load();
    const float minBPM = apvts.getRawParameterValue("minBPM")->load();
    const float maxBPM = apvts.getRawParameterValue("maxBPM")->load();
    
    // === INTROSPECTION ENGINE ===
    // Scan graph periodically to reduce overhead (every 128 blocks ≈ 2.9ms at 44.1kHz)
    if (++m_scanCounter % 128 == 0)
    {
        if (mode == (int)OperationMode::Auto || mode == (int)OperationMode::IntrospectionOnly)
            scanGraphForRhythmSources();
        else
        {
            const juce::ScopedLock lock(m_sourcesLock);
            m_introspectedSources.clear();
        }
    }
    
    // === BEAT DETECTION ENGINE ===
    if (mode == (int)OperationMode::Auto || mode == (int)OperationMode::DetectionOnly)
        processDetection(detectionInput);
    else
    {
        const juce::ScopedLock lock(m_sourcesLock);
        m_detectedSources.clear();
    }
    
    // === OUTPUT GENERATION ===
    outputBuffer.clear();

    int channelIndex = 0;
    const int numSamples = outputBuffer.getNumSamples();
    const int numOutChannels = outputBuffer.getNumChannels();
    
    // Copy sources for safe iteration
    std::vector<IntrospectedSource> introspected;
    std::vector<DetectedRhythmSource> detected;
    {
        const juce::ScopedLock lock(m_sourcesLock);
        introspected = m_introspectedSources;
        detected = m_detectedSources;
    }

#if defined(PRESET_CREATOR_UI)
    // Update visualization data
    {
        const juce::ScopedLock lock(m_vizLock);
        m_vizIntrospected.clear();
        for (const auto& src : introspected)
        {
            VizSource viz;
            viz.name = src.name;
            viz.bpm = src.bpm;
            viz.confidence = src.isActive ? 1.0f : 0.0f;
            viz.isActive = src.isActive;
            m_vizIntrospected.push_back(viz);
        }
    }
#endif
    
    // Introspected sources first (fast, accurate)
    for (const auto& source : introspected)
    {
        if (channelIndex + 2 >= numOutChannels)
            break;
        
        // Channel 0: BPM Raw (absolute value)
        juce::FloatVectorOperations::fill(outputBuffer.getWritePointer(channelIndex++), 
                                         source.bpm, numSamples);
        
        // Channel 1: BPM CV (normalized 0-1)
        juce::FloatVectorOperations::fill(outputBuffer.getWritePointer(channelIndex++), 
                                         normalizeBPM(source.bpm, minBPM, maxBPM), numSamples);
        
        // Channel 2: Active gate (0.0 or 1.0)
        juce::FloatVectorOperations::fill(outputBuffer.getWritePointer(channelIndex++), 
                                         source.isActive ? 1.0f : 0.0f, numSamples);
    }
    
    // Detected sources next (universal fallback)
    for (const auto& source : detected)
    {
        if (channelIndex + 2 >= numOutChannels)
            break;
        
        // Channel 0: Detected BPM Raw
        juce::FloatVectorOperations::fill(outputBuffer.getWritePointer(channelIndex++), 
                                         source.detectedBPM, numSamples);
        
        // Channel 1: Detected BPM CV (normalized)
        juce::FloatVectorOperations::fill(outputBuffer.getWritePointer(channelIndex++), 
                                         normalizeBPM(source.detectedBPM, minBPM, maxBPM), numSamples);
        
        // Channel 2: Confidence level (0-1)
        juce::FloatVectorOperations::fill(outputBuffer.getWritePointer(channelIndex++), 
                                         source.confidence, numSamples);
    }
    
    // Update output telemetry for tooltips
    updateOutputTelemetry(outputBuffer);
}

std::vector<DynamicPinInfo> BPMMonitorModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    const juce::ScopedLock lock(m_sourcesLock);
    
    // Introspected sources
    for (const auto& source : m_introspectedSources)
    {
        const int baseChannel = (int)pins.size();
        pins.push_back({ source.name + " BPM", baseChannel, PinDataType::Raw });
        pins.push_back({ source.name + " CV", baseChannel + 1, PinDataType::CV });
        pins.push_back({ source.name + " Active", baseChannel + 2, PinDataType::Gate });
    }
    
    // Detected sources
    for (const auto& source : m_detectedSources)
    {
        const int baseChannel = (int)pins.size();
        pins.push_back({ source.name + " BPM", baseChannel, PinDataType::Raw });
        pins.push_back({ source.name + " CV", baseChannel + 1, PinDataType::CV });
        pins.push_back({ source.name + " Confidence", baseChannel + 2, PinDataType::CV });
    }
    
    return pins;
}

std::vector<DynamicPinInfo> BPMMonitorModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    const int numInputs = apvts.getRawParameterValue("numInputs")->load();
    for (int i = 0; i < numInputs; ++i)
    {
        pins.push_back({ "Detect In " + juce::String(i + 1), i, PinDataType::Gate });
    }
    
    return pins;
}

juce::String BPMMonitorModuleProcessor::getAudioInputLabel(int channel) const
{
    return "Detect In " + juce::String(channel + 1);
}

juce::String BPMMonitorModuleProcessor::getAudioOutputLabel(int channel) const
{
    // Outputs are dynamic - use getDynamicOutputPins() for proper names
    return "Out " + juce::String(channel + 1);
}

#if defined(PRESET_CREATOR_UI)
void BPMMonitorModuleProcessor::drawParametersInNode(float itemWidth, 
                                                      const std::function<bool(const juce::String&)>& isParamModulated,
                                                      const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated);
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();

    ImGui::PushID (this);
    ImGui::PushItemWidth(itemWidth);
    
    ThemeText("BPM MONITOR", theme.modules.sequencer_section_header);
    
    // Mode selector
    int mode = apvts.getRawParameterValue("mode")->load();
    if (ImGui::Combo("Mode", &mode, "Auto\0Introspection Only\0Detection Only\0\0"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode")))
        {
            *p = mode;
            onModificationEnded();
        }
    }
    
    // BPM Normalization Range
    ImGui::Text("CV Normalization Range:");
    
    float minBPM = apvts.getRawParameterValue("minBPM")->load();
    if (ImGui::SliderFloat("Min BPM", &minBPM, 20.0f, 300.0f, "%.0f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("minBPM")))
            *p = minBPM;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    
    float maxBPM = apvts.getRawParameterValue("maxBPM")->load();
    if (ImGui::SliderFloat("Max BPM", &maxBPM, 20.0f, 300.0f, "%.0f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("maxBPM")))
            *p = maxBPM;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    
    // Beat Detection Settings (only show if detection is enabled)
    if (mode == (int)OperationMode::Auto || mode == (int)OperationMode::DetectionOnly)
    {
        ImGui::Text("Beat Detection Settings:");
        
        int numInputs = apvts.getRawParameterValue("numInputs")->load();
        if (ImGui::SliderInt("Detection Inputs", &numInputs, 0, MAX_DETECTION_INPUTS))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numInputs")))
                *p = numInputs;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        
        float sensitivity = apvts.getRawParameterValue("sensitivity")->load();
        if (ImGui::SliderFloat("Sensitivity", &sensitivity, 0.0f, 1.0f, "%.2f"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("sensitivity")))
                *p = sensitivity;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        
        const auto detMinRange = apvts.getParameterRange("detMinBPM");
        const auto detMaxRange = apvts.getParameterRange("detMaxBPM");

        float detMinBPM = apvts.getRawParameterValue("detMinBPM")->load();
        if (ImGui::SliderFloat("Det Min BPM", &detMinBPM,
                               detMinRange.start, detMaxRange.end, "%.0f"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("detMinBPM")))
                *p = detMinBPM;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        
        float detMaxBPM = apvts.getRawParameterValue("detMaxBPM")->load();
        if (ImGui::SliderFloat("Det Max BPM", &detMaxBPM,
                               detMinRange.start, detMaxRange.end, "%.0f"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("detMaxBPM")))
                *p = detMaxBPM;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    }
    
    ImGui::Spacing();
    ImGui::Spacing();

    auto pickColor = [](ImU32 candidate, ImU32 fallback)
    {
        return candidate != 0 ? candidate : fallback;
    };

    const float vizMinBPM = minBPM;
    const float vizMaxBPM = maxBPM;
    const ImU32 cardBg      = pickColor (theme.modules.scope_plot_bg, IM_COL32 (24, 26, 34, 255));
    const ImU32 activeColor = pickColor (theme.modules.scope_plot_fg, IM_COL32 (58, 165, 255, 255));
    const ImU32 idleColor   = IM_COL32 (70, 70, 90, 180);
    const ImU32 detectColor = pickColor (theme.modules.scope_plot_min, IM_COL32 (255, 163, 72, 255));
    const ImU32 textColor   = ImGui::GetColorU32 (ImGuiCol_Text);

    auto clamp01 = [] (float value, float minV, float maxV)
    {
        if (maxV <= minV)
            return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (value - minV) / (maxV - minV));
    };

    // Read visualization data
    std::vector<VizSource> introspectedViz;
    std::vector<VizSource> detectedViz;
    {
        const juce::ScopedLock lock(m_vizLock);
        introspectedViz = m_vizIntrospected;
        detectedViz = m_vizDetected;
    }

    ThemeText("Introspected Sources", theme.modules.sequencer_section_header);
    ImGui::Spacing();

    const float introHeight = juce::jmax (60.0f, 26.0f * static_cast<float> (juce::jmax (1, (int)introspectedViz.size())) + 12.0f);
    const ImVec2 introSize (itemWidth, introHeight);
    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar
                                      | ImGuiWindowFlags_NoScrollWithMouse
                                      | ImGuiWindowFlags_NoNav;

    if (ImGui::BeginChild ("BPMIntroViz", introSize, false, childFlags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2 (p0.x + introSize.x, p0.y + introSize.y);
        dl->AddRectFilled (p0, p1, cardBg, 6.0f);
        dl->PushClipRect (p0, p1, true);

        if (introspectedViz.empty())
        {
            const char* msg = "No introspected sources";
            dl->AddText (ImGui::GetFont(), ImGui::GetFontSize(), ImVec2 (p0.x + 10.0f, p0.y + 10.0f), textColor, msg);
        }
        else
        {
            float y = p0.y + 8.0f;
            const float rowHeight = 24.0f;
            for (const auto& viz : introspectedViz)
            {
                const float norm = clamp01 (viz.bpm, vizMinBPM, vizMaxBPM);
                const float barStartX = p0.x + 10.0f;
                const float barEndX = barStartX + norm * (introSize.x - 20.0f);
                const ImU32 fill = viz.isActive ? activeColor : idleColor;

                dl->AddRectFilled (ImVec2 (barStartX, y), ImVec2 (p1.x - 10.0f, y + rowHeight - 6.0f), IM_COL32 (30, 33, 45, 180), 4.0f);
                dl->AddRectFilled (ImVec2 (barStartX, y), ImVec2 (barEndX, y + rowHeight - 6.0f), fill, 4.0f);

                juce::String text = juce::String::formatted ("%s  |  %.1f BPM  [%s]",
                                                             viz.name.toRawUTF8(),
                                                             viz.bpm,
                                                             viz.isActive ? "RUN" : "IDLE");

                dl->AddText (ImGui::GetFont(), ImGui::GetFontSize(), ImVec2 (barStartX + 4.0f, y + 3.0f), textColor, text.toRawUTF8());
                y += rowHeight;
            }
        }

        dl->PopClipRect();
        ImGui::SetCursorPos (ImVec2 (0.0f, 0.0f));
        ImGui::InvisibleButton ("BPMIntroDrag", introSize);
    }
    ImGui::EndChild ();

    ImGui::Spacing();
    ThemeText("Detection Inputs", theme.modules.sequencer_section_header);
    ImGui::Spacing();

    const float detectHeight = juce::jmax (60.0f, 30.0f * static_cast<float> (juce::jmax (1, (int)detectedViz.size())) + 12.0f);
    const ImVec2 detectSize (itemWidth, detectHeight);

    if (ImGui::BeginChild ("BPMDetectViz", detectSize, false, childFlags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2 (p0.x + detectSize.x, p0.y + detectSize.y);
        dl->AddRectFilled (p0, p1, cardBg, 6.0f);
        dl->PushClipRect (p0, p1, true);

        if (detectedViz.empty())
        {
            const char* msg = "No active detections";
            dl->AddText (ImGui::GetFont(), ImGui::GetFontSize(), ImVec2 (p0.x + 10.0f, p0.y + 10.0f), textColor, msg);
        }
        else
        {
            float y = p0.y + 8.0f;
            const float rowHeight = 26.0f;
            for (const auto& viz : detectedViz)
            {
                const float bpmNorm = clamp01 (viz.bpm, vizMinBPM, vizMaxBPM);
                const float confNorm = juce::jlimit (0.0f, 1.0f, viz.confidence);

                const float barStartX = p0.x + 10.0f;
                const float barWidth = detectSize.x - 20.0f;
                const ImU32 bpmColor = viz.isActive ? detectColor : IM_COL32 (110, 120, 170, 200);
                const ImU32 confBg = IM_COL32 (30, 33, 45, 180);
                const ImU32 confColor = viz.isActive ? IM_COL32 (255, 255, 255, 150) : IM_COL32 (150, 160, 210, 150);

                // BPM bar
                dl->AddRectFilled (ImVec2 (barStartX, y), ImVec2 (barStartX + barWidth, y + 10.0f), confBg, 3.0f);
                dl->AddRectFilled (ImVec2 (barStartX, y), ImVec2 (barStartX + barWidth * bpmNorm, y + 10.0f), bpmColor, 3.0f);

                // Confidence bar
                const float confTop = y + 14.0f;
                dl->AddRectFilled (ImVec2 (barStartX, confTop), ImVec2 (barStartX + barWidth, confTop + 6.0f), confBg, 3.0f);
                dl->AddRectFilled (ImVec2 (barStartX, confTop), ImVec2 (barStartX + barWidth * confNorm, confTop + 6.0f), confColor, 3.0f);

                // Format text
                juce::String bpmText = (viz.bpm > 0.1f && viz.bpm < 10000.0f) 
                    ? juce::String::formatted("%.1f BPM", viz.bpm)
                    : juce::String("-- BPM");
                
                juce::String confText = juce::String::formatted("%.0f%%", confNorm * 100.0f);
                juce::String displayText = viz.name + "  |  " + bpmText + "  |  " + confText;
                
                dl->AddText (ImGui::GetFont(), ImGui::GetFontSize(), ImVec2 (barStartX, y + 20.0f), textColor, displayText.toRawUTF8());
                y += rowHeight;
            }
        }

        dl->PopClipRect();
        ImGui::SetCursorPos (ImVec2 (0.0f, 0.0f));
        ImGui::InvisibleButton ("BPMDetectDrag", detectSize);
    }
    ImGui::EndChild ();

    ImGui::PopItemWidth();
    ImGui::PopID ();
}
#endif

float BPMMonitorModuleProcessor::normalizeBPM(float bpm, float minBPM, float maxBPM) const
{
    return juce::jmap(bpm, minBPM, maxBPM, 0.0f, 1.0f);
}
