#include "ScopeModuleProcessor.h"
#include "../../preset_creator/theme/ThemeManager.h"
#include <cmath>

ScopeModuleProcessor::ScopeModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::mono(), true)
                        .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "ScopeParams", createParameterLayout())
{
    monitorSecondsParam = apvts.getRawParameterValue ("monitorSeconds");
    for (auto& sample : vizData.waveform)
        sample.store (0.0f);
    vizData.peakMin.store (0.0f);
    vizData.peakMax.store (0.0f);
    // Inspector value tracking
    lastOutputValues.clear();
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout ScopeModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("monitorSeconds", "Monitor Seconds", juce::NormalisableRange<float>(0.5f, 20.0f, 0.1f), 5.0f));
    return { params.begin(), params.end() };
}

void ScopeModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    // Scope ring for UI drawing
    scopeBuffer.setSize (1, 1024);
    scopeBuffer.clear();
    writePos = 0;
    // Rolling min/max history at ~1 kHz
    decimation = juce::jmax (1, (int) std::round (currentSampleRate / 1000.0));
    resetHistoryState (computeHistoryCapacity());
}

void ScopeModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    // True pass-through: copy input to output, channel-matched, clear extras
    auto in  = getBusBuffer (buffer, true, 0);
    auto out = getBusBuffer (buffer, false, 0);
    const int n = buffer.getNumSamples();
    // Mono passthrough; avoid aliasing
    if (in.getNumChannels() > 0 && out.getNumChannels() > 0)
    {
        auto* srcCh = in.getReadPointer (0);
        auto* dst = out.getWritePointer (0);
        if (dst != srcCh) juce::FloatVectorOperations::copy (dst, srcCh, n);
    }

    // Update inspector with block peak of output
    if (! lastOutputValues.empty())
    {
        const float* p = out.getNumChannels() > 0 ? out.getReadPointer(0) : nullptr;
        float m = 0.0f; if (p) { for (int i=0;i<n;++i) m = juce::jmax(m, std::abs(p[i])); }
        lastOutputValues[0]->store(m);
    }

    // Copy first channel into scope buffer
    const float* src = (in.getNumChannels() > 0 ? in.getReadPointer (0) : nullptr);
    const int bufferSamples = scopeBuffer.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const float s = (src ? src[i] : 0.0f);
        if (bufferSamples > 0)
        {
            scopeBuffer.setSample (0, writePos, s);
            writePos = (writePos + 1) % bufferSamples;
        }

        // Decimate and push into history (for rolling min/max over ~5s)
        if (++decimCounter >= decimation)
        {
            decimCounter = 0;
            pushDecimatedSample (s);
        }
    }

    refreshVizWaveform();
}

#if defined(PRESET_CREATOR_UI)
void ScopeModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused (isParamModulated);
    auto& ap = getAPVTS();
    ImGui::PushID (this);
    
    // Helper for tooltips
    auto HelpMarkerScope = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };
    
    float seconds = monitorSecondsParam ? monitorSecondsParam->load() : 5.0f;
    ImGui::PushItemWidth (itemWidth);
    
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();

    auto themeText = [](const juce::String& text, const ImVec4& colour)
    {
        ThemeText(text.toRawUTF8(), colour);
    };

    auto pickColor = [](ImU32 candidate, ImU32 fallback)
    {
        return candidate != 0 ? candidate : fallback;
    };

    // === SCOPE SETTINGS SECTION ===
    themeText("Scope Settings", theme.modules.scope_section_header);
    ImGui::Spacing();
    
    const bool isSecondsModulated = isParamModulated("monitorSeconds");
    if (isSecondsModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat ("Seconds", &seconds, 0.5f, 20.0f, "%.1f s"))
    {
        if (!isSecondsModulated)
        {
            if (auto* p = ap.getParameter ("monitorSeconds"))
                p->setValueNotifyingHost (ap.getParameterRange("monitorSeconds").convertTo0to1 (seconds));
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !isSecondsModulated) { onModificationEnded(); }
    if (!isSecondsModulated) adjustParamOnWheel (ap.getParameter ("monitorSeconds"), "monitorSeconds", seconds);
    if (isSecondsModulated) ImGui::EndDisabled();
    ImGui::SameLine();
    HelpMarkerScope("Time window for waveform display (0.5-20 seconds)\nAlso affects min/max monitoring period");
    
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Spacing();

    // === LIVE WAVEFORM SECTION ===
    themeText("Live Waveform", theme.modules.scope_section_header);
    ImGui::Spacing();

    float waveform[ VizData::waveformPoints ];
    for (int i = 0; i < VizData::waveformPoints; ++i)
        waveform[i] = vizData.waveform[i].load();
    const float currentMin = vizData.peakMin.load();
    const float currentMax = vizData.peakMax.load();

    const float waveHeight = 100.0f;
    const ImVec2 graphSize (itemWidth, waveHeight);
    const ImU32 bg     = pickColor(theme.modules.scope_plot_bg, IM_COL32(30,30,30,255));
    const ImU32 fg     = pickColor(theme.modules.scope_plot_fg, IM_COL32(100,200,255,255));
    const ImU32 colMax = pickColor(theme.modules.scope_plot_max, IM_COL32(255,80,80,255));
    const ImU32 colMin = pickColor(theme.modules.scope_plot_min, IM_COL32(255,220,80,255));
    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar
                                      | ImGuiWindowFlags_NoScrollWithMouse
                                      | ImGuiWindowFlags_NoNav;

    if (ImGui::BeginChild ("ScopeWaveform", graphSize, false, childFlags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2 (p0.x + graphSize.x, p0.y + graphSize.y);

        dl->AddRectFilled (p0, p1, bg, 4.0f);
        dl->PushClipRect (p0, p1, true);

        const float midY = p0.y + graphSize.y * 0.5f;
        const float scaleY = graphSize.y * 0.45f;
        const float stepX = (VizData::waveformPoints > 1)
                                ? graphSize.x / static_cast<float> (VizData::waveformPoints - 1)
                                : graphSize.x;

        float prevX = p0.x;
        float prevY = midY;
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const float sample = juce::jlimit (-1.5f, 1.5f, waveform[i]);
            const float x = p0.x + static_cast<float> (i) * stepX;
            const float y = juce::jlimit (p0.y, p1.y, midY - sample * scaleY);

            if (i > 0)
                dl->AddLine (ImVec2 (prevX, prevY), ImVec2 (x, y), fg, 1.5f);

            prevX = x;
            prevY = y;
        }

        const float yMax = juce::jlimit (p0.y, p1.y, midY - juce::jlimit (-1.5f, 1.5f, currentMax) * scaleY);
        const float yMin = juce::jlimit (p0.y, p1.y, midY - juce::jlimit (-1.5f, 1.5f, currentMin) * scaleY);
        dl->AddLine (ImVec2 (p0.x, yMax), ImVec2 (p1.x, yMax), colMax, 1.0f);
        dl->AddLine (ImVec2 (p0.x, yMin), ImVec2 (p1.x, yMin), colMin, 1.0f);

        dl->PopClipRect ();

        ImGui::SetCursorPos (ImVec2 (0.0f, 0.0f));
        ImGui::InvisibleButton ("ScopeWaveformDrag", graphSize);
    }
    ImGui::EndChild ();

    ImGui::Spacing();
    ImGui::Spacing();

    // === SIGNAL STATISTICS SECTION ===
    themeText("Signal Statistics", theme.modules.scope_section_header);
    ImGui::Spacing();

    // Display min/max values with color coding
    themeText(juce::String::formatted("Peak Max: %.3f", currentMax), theme.modules.scope_text_max);
    themeText(juce::String::formatted("Peak Min: %.3f", currentMin), theme.modules.scope_text_min);
    
    // Peak-to-peak
    float peakToPeak = currentMax - currentMin;
    ImGui::Text("P-P: %.3f", peakToPeak);
    
    // dBFS conversion for max
    float dBMax = currentMax > 0.0001f ? 20.0f * std::log10(currentMax) : -100.0f;
    ImGui::Text("Max dBFS: %.1f", dBMax);

    ImGui::PopID();
}

void ScopeModuleProcessor::getStatistics(float& outMin, float& outMax) const
{
    outMin = vizData.peakMin.load();
    outMax = vizData.peakMax.load();
}

#endif

int ScopeModuleProcessor::computeHistoryCapacity() const
{
    const float seconds = (monitorSecondsParam != nullptr ? monitorSecondsParam->load() : 5.0f);
    const float samplesPerSecond = static_cast<float> (currentSampleRate) / static_cast<float> (juce::jmax (1, decimation));
    const int desired = static_cast<int> (std::round (seconds * samplesPerSecond));
    return juce::jlimit (100, 50000, juce::jmax (1, desired));
}

void ScopeModuleProcessor::resetHistoryState (int newCapacity)
{
    histCapacity = juce::jmax (1, newCapacity);
    histSampleCounter = -1;
    historyMinDeque.clear();
    historyMaxDeque.clear();
    vizData.peakMin.store (0.0f);
    vizData.peakMax.store (0.0f);
}

void ScopeModuleProcessor::pushDecimatedSample (float sample)
{
    const int desired = computeHistoryCapacity();
    if (desired != histCapacity)
        resetHistoryState (desired);

    ++histSampleCounter;
    const std::int64_t currentIndex = histSampleCounter;
    const std::int64_t windowStart = currentIndex - static_cast<std::int64_t> (histCapacity) + 1;

    while (! historyMinDeque.empty() && sample <= historyMinDeque.back().second)
        historyMinDeque.pop_back();
    historyMinDeque.emplace_back (currentIndex, sample);
    while (! historyMinDeque.empty() && historyMinDeque.front().first < windowStart)
        historyMinDeque.pop_front();

    while (! historyMaxDeque.empty() && sample >= historyMaxDeque.back().second)
        historyMaxDeque.pop_back();
    historyMaxDeque.emplace_back (currentIndex, sample);
    while (! historyMaxDeque.empty() && historyMaxDeque.front().first < windowStart)
        historyMaxDeque.pop_front();

    const float minValue = historyMinDeque.empty() ? sample : historyMinDeque.front().second;
    const float maxValue = historyMaxDeque.empty() ? sample : historyMaxDeque.front().second;
    vizData.peakMin.store (minValue);
    vizData.peakMax.store (maxValue);
}

void ScopeModuleProcessor::refreshVizWaveform()
{
#if defined(PRESET_CREATOR_UI)
    const int numChannels = scopeBuffer.getNumChannels();
    const int numSamples = scopeBuffer.getNumSamples();

    if (numChannels == 0 || numSamples <= 0)
    {
        for (auto& sample : vizData.waveform)
            sample.store (0.0f);
        return;
    }

    const float stride = static_cast<float> (numSamples) / static_cast<float> (VizData::waveformPoints);
    float readIndex = static_cast<float> (writePos);
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        readIndex += stride;
        while (readIndex >= static_cast<float> (numSamples))
            readIndex -= static_cast<float> (numSamples);

        const int bufferIndex = static_cast<int> (readIndex);
        const float value = scopeBuffer.getSample (0, bufferIndex);
        vizData.waveform[i].store (value);
    }
#endif
}

