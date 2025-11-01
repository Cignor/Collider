#include "BPMMonitorModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"

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
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f), 30.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("detMaxBPM", "Det Max BPM", 
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f), 300.0f));
    
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
    
    // Reset all tap tempo analyzers
    for (auto& analyzer : m_tapAnalyzers)
        analyzer.reset();
    
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
    const juce::ScopedLock lock(m_sourcesLock);
    m_detectedSources.clear();
    
    const int numInputs = apvts.getRawParameterValue("numInputs")->load();
    const float sensitivity = apvts.getRawParameterValue("sensitivity")->load();
    const float detMinBPM = apvts.getRawParameterValue("detMinBPM")->load();
    const float detMaxBPM = apvts.getRawParameterValue("detMaxBPM")->load();
    
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    
    // Process each active detection input
    for (int ch = 0; ch < std::min(numInputs, MAX_DETECTION_INPUTS); ++ch)
    {
        if (ch >= numChannels)
            break;
        
        // Configure analyzer for this channel
        auto& analyzer = m_tapAnalyzers[ch];
        analyzer.setSensitivity(sensitivity);
        analyzer.setMinBPM(detMinBPM);
        analyzer.setMaxBPM(detMaxBPM);
        
        // Process all samples in this block
        const float* inputData = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            analyzer.processSample(inputData[i], m_sampleRate);
        
        // If analyzer is active (stable detection), add to detected sources
        if (analyzer.isActive())
        {
            DetectedRhythmSource source;
            source.name = "Input " + juce::String(ch + 1) + " (Detected)";
            source.inputChannel = ch;
            source.detectedBPM = analyzer.getBPM();
            source.confidence = analyzer.getConfidence();
            source.isActive = true;
            m_detectedSources.push_back(source);
        }
    }
}

float BPMMonitorModuleProcessor::normalizeBPM(float bpm, float minBPM, float maxBPM) const
{
    return juce::jmap(bpm, minBPM, maxBPM, 0.0f, 1.0f);
}

void BPMMonitorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
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
        processDetection(buffer);
    else
    {
        const juce::ScopedLock lock(m_sourcesLock);
        m_detectedSources.clear();
    }
    
    // === OUTPUT GENERATION ===
    buffer.clear();
    
    int channelIndex = 0;
    const int numSamples = buffer.getNumSamples();
    
    // Copy sources for safe iteration
    std::vector<IntrospectedSource> introspected;
    std::vector<DetectedRhythmSource> detected;
    {
        const juce::ScopedLock lock(m_sourcesLock);
        introspected = m_introspectedSources;
        detected = m_detectedSources;
    }
    
    // Introspected sources first (fast, accurate)
    for (const auto& source : introspected)
    {
        if (channelIndex + 2 >= buffer.getNumChannels())
            break;
        
        // Channel 0: BPM Raw (absolute value)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), 
                                         source.bpm, numSamples);
        
        // Channel 1: BPM CV (normalized 0-1)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), 
                                         normalizeBPM(source.bpm, minBPM, maxBPM), numSamples);
        
        // Channel 2: Active gate (0.0 or 1.0)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), 
                                         source.isActive ? 1.0f : 0.0f, numSamples);
    }
    
    // Detected sources next (universal fallback)
    for (const auto& source : detected)
    {
        if (channelIndex + 2 >= buffer.getNumChannels())
            break;
        
        // Channel 0: Detected BPM Raw
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), 
                                         source.detectedBPM, numSamples);
        
        // Channel 1: Detected BPM CV (normalized)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), 
                                         normalizeBPM(source.detectedBPM, minBPM, maxBPM), numSamples);
        
        // Channel 2: Confidence level (0-1)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channelIndex++), 
                                         source.confidence, numSamples);
    }
    
    // Update output telemetry for tooltips
    updateOutputTelemetry(buffer);
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
    
    ImGui::PushItemWidth(itemWidth);
    
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "BPM MONITOR");
    ImGui::Separator();
    
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
    ImGui::Separator();
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
        ImGui::Separator();
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
        
        float detMinBPM = apvts.getRawParameterValue("detMinBPM")->load();
        if (ImGui::SliderFloat("Det Min BPM", &detMinBPM, 20.0f, 300.0f, "%.0f"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("detMinBPM")))
                *p = detMinBPM;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        
        float detMaxBPM = apvts.getRawParameterValue("detMaxBPM")->load();
        if (ImGui::SliderFloat("Det Max BPM", &detMaxBPM, 20.0f, 300.0f, "%.0f"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("detMaxBPM")))
                *p = detMaxBPM;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    }
    
    // Display detected rhythm sources
    ImGui::Separator();
    ImGui::Text("Detected Rhythm Sources:");
    
    {
        const juce::ScopedLock lock(m_sourcesLock);
        if (m_introspectedSources.empty() && m_detectedSources.empty())
        {
            ImGui::TextDisabled("  None");
        }
        else
        {
            // Introspected sources
            if (!m_introspectedSources.empty())
            {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Introspected:");
                for (const auto& source : m_introspectedSources)
                {
                    ImGui::BulletText("%s: %.1f BPM %s", 
                                     source.name.toRawUTF8(), 
                                     source.bpm,
                                     source.isActive ? "[ACTIVE]" : "[STOPPED]");
                }
            }
            
            // Detected sources
            if (!m_detectedSources.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Detected:");
                for (const auto& source : m_detectedSources)
                {
                    ImGui::BulletText("%s: %.1f BPM (%.0f%% conf)", 
                                     source.name.toRawUTF8(), 
                                     source.detectedBPM,
                                     source.confidence * 100.0f);
                }
            }
        }
    }
    
    ImGui::PopItemWidth();
}
#endif

