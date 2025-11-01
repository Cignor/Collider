#include "TimelineModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"
#include <cmath>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout TimelineModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Record and Play button parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("record", "Record", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("play", "Play", false));
    
    return { params.begin(), params.end() };
}

TimelineModuleProcessor::TimelineModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Inputs", juce::AudioChannelSet::discreteChannels(32), true)    // Max 32 input channels for dynamic routing
          .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(32), true)), // Max 32 output channels for passthrough
      apvts(*this, nullptr, "TimelineParams", createParameterLayout())
{
    recordParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("record"));
    playParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("play"));
}

void TimelineModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    m_sampleRate = sampleRate;
    
    // Reset playback state
    m_lastKeyframeIndexHints.clear();
    m_wasPlaying = false;
    m_lastPositionBeats = 0.0;
    m_internalPositionBeats = 0.0;
}

void TimelineModuleProcessor::setTimingInfo(const TransportState& state)
{
    if (state.isPlaying && !m_wasPlaying)
    {
        // Transport has just started, reset our search hints
        for (auto& hint : m_lastKeyframeIndexHints)
            hint = 0;
    }
    m_wasPlaying = state.isPlaying;
    m_currentTransport = state;
}

void TimelineModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Get input and output bus buffers
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;
    
    // Get recording and playback state
    bool isRecording = recordParam && recordParam->get();
    bool isPlayingBack = playParam && playParam->get();
    
    // Telemetry for UI
    setLiveParamValue("is_recording_live", isRecording ? 1.0f : 0.0f);
    setLiveParamValue("is_playing_live", isPlayingBack ? 1.0f : 0.0f);
    
    // Calculate timing information
    double blockStartBeats = m_currentTransport.songPositionBeats;
    double samplesPerBeat = (60.0 / m_currentTransport.bpm) * m_sampleRate;
    double beatsPerSample = (samplesPerBeat > 0.0) ? (1.0 / samplesPerBeat) : 0.0;
    
    // Mode-driven processing
    if (!m_currentTransport.isPlaying)
    {
        // Transport stopped - output silence and passthrough
        outBus.clear();
        setLiveParamValue("song_position_beats_live", 0.0f);
        return;
    }
    else if (isPlayingBack)
    {
        // PLAYBACK MODE: Output recorded automation data
        const juce::ScopedLock lock(automationLock);
        
        // Clear output buffer first
        outBus.clear();
        
        if (m_automationChannels.empty())
        {
            // No channels - output remains silent
            m_internalPositionBeats = blockStartBeats + (numSamples * beatsPerSample);
            setLiveParamValue("song_position_beats_live", (float)m_internalPositionBeats);
            return;
        }
        
        // Ensure hints vector is sized correctly
        if (m_lastKeyframeIndexHints.size() != m_automationChannels.size())
        {
            m_lastKeyframeIndexHints.resize(m_automationChannels.size(), 0);
        }
        
        // Reset search hints on loop/seek
        if (m_internalPositionBeats < m_lastPositionBeats)
        {
            for (auto& hint : m_lastKeyframeIndexHints)
                hint = 0;
        }
        
        // Process each channel
        for (size_t ch = 0; ch < m_automationChannels.size(); ++ch)
        {
            const auto& keyframes = m_automationChannels[ch].keyframes;
            if (keyframes.empty())
                continue;
            
            float* outputData = outBus.getWritePointer((int)ch);
            if (!outputData)
                continue;
            
            int p0Index = m_lastKeyframeIndexHints[ch];
            const int numKeyframes = (int)keyframes.size();
            
            for (int i = 0; i < numSamples; ++i)
            {
                // Calculate precise position for this sample
                double samplePosition = blockStartBeats + (i * beatsPerSample);
                
                // Find surrounding keyframes
                while (p0Index < numKeyframes - 1 && keyframes[p0Index + 1].positionBeats <= samplePosition)
                {
                    ++p0Index;
                }
                
                // Handle edge cases
                float outputValue = 0.0f;
                
                if (samplePosition < keyframes[0].positionBeats)
                {
                    // Before first keyframe - use first value
                    outputValue = keyframes[0].value;
                }
                else if (samplePosition >= keyframes.back().positionBeats)
                {
                    // After last keyframe - use last value
                    outputValue = keyframes.back().value;
                }
                else if (p0Index < numKeyframes - 1)
                {
                    // Between two keyframes - linear interpolation
                    const double p0 = keyframes[p0Index].positionBeats;
                    const double p1 = keyframes[p0Index + 1].positionBeats;
                    const float v0 = keyframes[p0Index].value;
                    const float v1 = keyframes[p0Index + 1].value;
                    
                    double t = (p1 > p0) ? (samplePosition - p0) / (p1 - p0) : 0.0;
                    outputValue = v0 + (float)(t * (v1 - v0));
                }
                else
                {
                    // Last keyframe
                    outputValue = keyframes.back().value;
                }
                
                outputData[i] = outputValue;
            }
            
            // Update hint for next block
            m_lastKeyframeIndexHints[ch] = p0Index;
        }
        
        m_internalPositionBeats = blockStartBeats + (numSamples * beatsPerSample);
        m_lastPositionBeats = m_internalPositionBeats;
        setLiveParamValue("song_position_beats_live", (float)m_internalPositionBeats);
    }
    else if (isRecording)
    {
        // RECORDING MODE: Capture input while passthrough
        // Perform passthrough first
        const int numChannels = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels(), 32);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (inBus.getReadPointer(ch) && outBus.getWritePointer(ch))
            {
                buffer.copyFrom(ch, 0, inBus.getReadPointer(ch), numSamples);
            }
        }
        
        // Record keyframes
        const juce::ScopedLock lock(automationLock);
        
        // Ensure we have at least one channel to record to
        if (m_automationChannels.empty())
        {
            m_automationChannels.resize(1);
            m_automationChannels[0].name = "Channel 1";
            m_automationChannels[0].type = SignalType::CV;
            m_lastKeyframeIndexHints.resize(1, 0);
        }
        
        // Ensure hints vector is sized correctly
        if (m_lastKeyframeIndexHints.size() != m_automationChannels.size())
        {
            m_lastKeyframeIndexHints.resize(m_automationChannels.size(), 0);
        }
        
        // Record each channel
        for (size_t ch = 0; ch < m_automationChannels.size(); ++ch)
        {
            const float* inputData = ((int)ch < inBus.getNumChannels()) ? inBus.getReadPointer((int)ch) : nullptr;
            if (!inputData)
                continue;
            
            for (int i = 0; i < numSamples; ++i)
            {
                // Calculate precise position for this sample
                double samplePosition = blockStartBeats + (i * beatsPerSample);
                float currentValue = inputData[i];
                
                // Change detection: only record if value changed significantly
                bool shouldRecord = m_automationChannels[ch].keyframes.empty();
                
                if (!shouldRecord)
                {
                    const float lastValue = m_automationChannels[ch].keyframes.back().value;
                    shouldRecord = (std::abs(currentValue - lastValue) > 0.001f);
                }
                
                if (shouldRecord)
                {
                    m_automationChannels[ch].keyframes.push_back({ samplePosition, currentValue });
                }
            }
        }
        
        // Update position for UI display
        m_internalPositionBeats = blockStartBeats + (numSamples * beatsPerSample);
        setLiveParamValue("song_position_beats_live", (float)m_internalPositionBeats);
    }
    else
    {
        // PASSTHROUGH MODE: No recording or playback, just pass through
        const int numChannels = juce::jmin(inBus.getNumChannels(), outBus.getNumChannels(), 32);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (inBus.getReadPointer(ch) && outBus.getWritePointer(ch))
            {
                buffer.copyFrom(ch, 0, inBus.getReadPointer(ch), numSamples);
            }
        }
        
        // Update position for UI display
        m_internalPositionBeats = blockStartBeats + (numSamples * beatsPerSample);
        setLiveParamValue("song_position_beats_live", (float)m_internalPositionBeats);
    }
}

std::vector<DynamicPinInfo> TimelineModuleProcessor::getDynamicInputPins() const
{
    const juce::ScopedLock lock(automationLock);
    std::vector<DynamicPinInfo> pins;
    
    for (size_t i = 0; i < m_automationChannels.size(); ++i)
    {
        juce::String name = juce::String(m_automationChannels[i].name) + " In";
        pins.emplace_back(name, (int)i, PinDataType::CV);
    }
    
    return pins;
}

std::vector<DynamicPinInfo> TimelineModuleProcessor::getDynamicOutputPins() const
{
    const juce::ScopedLock lock(automationLock);
    std::vector<DynamicPinInfo> pins;
    
    for (size_t i = 0; i < m_automationChannels.size(); ++i)
    {
        juce::String name = juce::String(m_automationChannels[i].name) + " Out";
        pins.emplace_back(name, (int)i, PinDataType::CV);
    }
    
    return pins;
}

juce::ValueTree TimelineModuleProcessor::getExtraStateTree() const
{
    const juce::ScopedLock lock(automationLock);
    juce::ValueTree root("TimelineState");
    
    for (const auto& channel : m_automationChannels)
    {
        juce::ValueTree channelNode("Channel");
        channelNode.setProperty("name", juce::String(channel.name), nullptr);
        channelNode.setProperty("type", (int)channel.type, nullptr);
        
        juce::ValueTree keyframesNode("Keyframes");
        for (const auto& keyframe : channel.keyframes)
        {
            juce::ValueTree keyNode("Key");
            keyNode.setProperty("pos", keyframe.positionBeats, nullptr);
            keyNode.setProperty("val", keyframe.value, nullptr);
            keyframesNode.appendChild(keyNode, nullptr);
        }
        
        channelNode.appendChild(keyframesNode, nullptr);
        root.appendChild(channelNode, nullptr);
    }
    
    return root;
}

void TimelineModuleProcessor::setExtraStateTree(const juce::ValueTree& state)
{
    if (!state.isValid())
        return;
    
    const juce::ScopedLock lock(automationLock);
    
    m_automationChannels.clear();
    m_lastKeyframeIndexHints.clear();
    
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto channelNode = state.getChild(i);
        if (channelNode.hasType("Channel"))
        {
            ChannelData channel;
            channel.name = channelNode.getProperty("name", "Channel " + juce::String(i + 1)).toString().toStdString();
            channel.type = (SignalType)(int)channelNode.getProperty("type", 0);
            
            auto keyframesNode = channelNode.getChildWithName("Keyframes");
            if (keyframesNode.isValid())
            {
                for (int k = 0; k < keyframesNode.getNumChildren(); ++k)
                {
                    auto keyNode = keyframesNode.getChild(k);
                    if (keyNode.hasType("Key"))
                    {
                        AutomationKeyframe keyframe;
                        keyframe.positionBeats = keyNode.getProperty("pos", 0.0);
                        keyframe.value = keyNode.getProperty("val", 0.0f);
                        channel.keyframes.push_back(keyframe);
                    }
                }
            }
            
            m_automationChannels.push_back(channel);
        }
    }
    
    // Resize hints to match channel count
    m_lastKeyframeIndexHints.resize(m_automationChannels.size(), 0);
}

#if defined(PRESET_CREATOR_UI)
void TimelineModuleProcessor::drawParametersInNode(float itemWidth,
                                                    const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                    const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated, onModificationEnded);
    
    ImGui::PushItemWidth(itemWidth);
    
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "TIMELINE");
    ImGui::Separator();
    
    // Record and Play buttons (mutually exclusive)
    bool isRecording = recordParam && recordParam->get();
    bool isPlayingBack = playParam && playParam->get();
    
    // Record button
    if (isRecording)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    else
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    
    if (ImGui::Button(isRecording ? "● REC" : "REC", ImVec2(itemWidth * 0.48f, 40)))
    {
        if (recordParam)
        {
            *recordParam = !isRecording;
            // Make mutually exclusive
            if (!isRecording && playParam)
                *playParam = false;
        }
        onModificationEnded();
    }
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    
    // Play button
    if (isPlayingBack)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
    else
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    
    if (ImGui::Button(isPlayingBack ? "▶ PLAY" : "PLAY", ImVec2(itemWidth * 0.48f, 40)))
    {
        if (playParam)
        {
            *playParam = !isPlayingBack;
            // Make mutually exclusive
            if (!isPlayingBack && recordParam)
                *recordParam = false;
        }
        onModificationEnded();
    }
    ImGui::PopStyleColor();
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Channel management
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.7f, 1.0f), "Channels");
    
    // Add/Remove buttons
    if (ImGui::Button("+ Add", ImVec2(itemWidth * 0.48f, 25)))
    {
        const juce::ScopedLock lock(automationLock);
        ChannelData newChannel;
        newChannel.name = "Channel " + juce::String(m_automationChannels.size() + 1).toStdString();
        newChannel.type = SignalType::CV;
        m_automationChannels.push_back(newChannel);
        m_lastKeyframeIndexHints.resize(m_automationChannels.size(), 0);
        onModificationEnded();
    }
    ImGui::SameLine();
    {
        const juce::ScopedLock lock(automationLock);
        bool canRemove = m_automationChannels.size() > 1;
        if (!canRemove)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        
        if (ImGui::Button("- Remove", ImVec2(itemWidth * 0.48f, 25)) && canRemove)
        {
            m_automationChannels.pop_back();
            m_lastKeyframeIndexHints.resize(m_automationChannels.size(), 0);
            if (m_selectedChannelIndex >= (int)m_automationChannels.size())
                m_selectedChannelIndex = (int)m_automationChannels.size() - 1;
            onModificationEnded();
        }
        
        if (!canRemove)
            ImGui::PopStyleVar();
    }
    
    ImGui::Spacing();
    
    // Channel list
    {
        const juce::ScopedLock lock(automationLock);
        for (size_t i = 0; i < m_automationChannels.size(); ++i)
        {
            int keyframeCount = (int)m_automationChannels[i].keyframes.size();
            juce::String label = juce::String(m_automationChannels[i].name) + " (" + juce::String(keyframeCount) + " keys)";
            
            bool isSelected = (m_selectedChannelIndex == (int)i);
            if (ImGui::Selectable(label.toRawUTF8(), isSelected))
            {
                m_selectedChannelIndex = (int)i;
            }
        }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Automation visualization
    if (m_selectedChannelIndex >= 0)
    {
        const juce::ScopedLock lock(automationLock);
        if (m_selectedChannelIndex < (int)m_automationChannels.size())
        {
            const auto& channel = m_automationChannels[m_selectedChannelIndex];
            if (!channel.keyframes.empty())
            {
                std::vector<float> plotData;
                plotData.reserve(channel.keyframes.size());
                for (const auto& kf : channel.keyframes)
                {
                    plotData.push_back(kf.value);
                }
                
                ImGui::PlotLines("##automation", plotData.data(), (int)plotData.size(), 0, nullptr, -1.0f, 1.0f, ImVec2(itemWidth, 80));
            }
        }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.7f, 1.0f), "Transport Status");
    ImGui::Spacing();
    
    float currentPos = getLiveParamValue("song_position_beats_live", 0.0f);
    int bar = static_cast<int>(currentPos / 4.0) + 1;
    int beat = static_cast<int>(currentPos) % 4 + 1;
    int tick = static_cast<int>((currentPos - std::floor(currentPos)) * 960.0); // Standard MIDI ticks
    
    ImGui::Text("Position: %04d:%02d:%03d", bar, beat, tick);
    ImGui::Text("Beats: %.4f", currentPos);
    ImGui::Text("BPM: %.2f", m_currentTransport.bpm);
    
    ImGui::PopItemWidth();
}

void TimelineModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Draw dynamic pins based on channels
    const juce::ScopedLock lock(automationLock);
    for (size_t i = 0; i < m_automationChannels.size(); ++i)
    {
        juce::String inputName = juce::String(m_automationChannels[i].name) + " In";
        juce::String outputName = juce::String(m_automationChannels[i].name) + " Out";
        helpers.drawAudioInputPin(inputName.toRawUTF8(), (int)i);
        helpers.drawAudioOutputPin(outputName.toRawUTF8(), (int)i);
    }
}

juce::String TimelineModuleProcessor::getAudioInputLabel(int channel) const
{
    const juce::ScopedLock lock(automationLock);
    if (channel >= 0 && channel < (int)m_automationChannels.size())
        return juce::String(m_automationChannels[channel].name) + " In";
    return "In " + juce::String(channel + 1);
}

juce::String TimelineModuleProcessor::getAudioOutputLabel(int channel) const
{
    const juce::ScopedLock lock(automationLock);
    if (channel >= 0 && channel < (int)m_automationChannels.size())
        return juce::String(m_automationChannels[channel].name) + " Out";
    return "Out " + juce::String(channel + 1);
}
#endif

