#include "ScopeModuleProcessor.h"

ScopeModuleProcessor::ScopeModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::mono(), true)
                        .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "ScopeParams", createParameterLayout())
{
    monitorSecondsParam = apvts.getRawParameterValue ("monitorSeconds");
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
    histCapacity = juce::jlimit (100, 50000, (int) std::round ((monitorSecondsParam ? monitorSecondsParam->load() : 5.0f) * (currentSampleRate / decimation)));
    history.assign (histCapacity, 0.0f);
    histWrite = 0; histCount = 0; rollMin = 0.0f; rollMax = 0.0f;
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
    for (int i = 0; i < n; ++i)
    {
        const float s = (src ? src[i] : 0.0f);
        scopeBuffer.setSample (0, writePos, s);
        writePos = (writePos + 1) % scopeBuffer.getNumSamples();

        // Decimate and push into history (for rolling min/max over ~5s)
        if (++decimCounter >= decimation)
        {
            decimCounter = 0;
            history[histWrite] = s;
            histWrite = (histWrite + 1) % histCapacity;
            histCount = juce::jmin (histCount + 1, histCapacity);
        }
    }
}

#if defined(PRESET_CREATOR_UI)
void ScopeModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused (isParamModulated);
    auto& ap = getAPVTS();
    float seconds = monitorSecondsParam ? monitorSecondsParam->load() : 5.0f;
    ImGui::PushItemWidth (itemWidth);
    if (ImGui::SliderFloat ("Seconds", &seconds, 0.5f, 20.0f, "%.1f s"))
    {
        if (auto* p = ap.getParameter ("monitorSeconds"))
            p->setValueNotifyingHost (ap.getParameterRange("monitorSeconds").convertTo0to1 (seconds));
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    adjustParamOnWheel (ap.getParameter ("monitorSeconds"), "monitorSeconds", seconds);
    ImGui::PopItemWidth();

    // Draw waveform using ImGui draw list
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = 240.0f; const float height = 80.0f;
    const ImU32 bg = IM_COL32(30,30,30,255);
    const ImU32 fg = IM_COL32(100,200,255,255);
    const ImU32 colMax = IM_COL32(255,80,80,255);
    const ImU32 colMin = IM_COL32(255,220,80,255);
    const ImVec2 rectMax = ImVec2(origin.x + width, origin.y + height);
    dl->AddRectFilled (origin, rectMax, bg, 4.0f);
    ImGui::PushClipRect(origin, rectMax, true);
    const int N = scopeBuffer.getNumSamples();
    const float midY = origin.y + height * 0.5f;
    const float scaleY = height * 0.45f;
    const float stepX = width / (float) (N - 1);
    int idx = writePos; // newest
    float prevX = origin.x, prevY = midY;
    for (int i = 0; i < N; ++i)
    {
        idx = (idx + 1) % N;
        float s = scopeBuffer.getSample (0, idx);
        if (s < -1.5f) s = -1.5f; else if (s > 1.5f) s = 1.5f; // guard against runaway values
        const float x = origin.x + i * stepX;
        const float y = midY - s * scaleY;
        if (i > 0) dl->AddLine (ImVec2(prevX, prevY), ImVec2(x, y), fg, 1.5f);
        prevX = x; prevY = y;
    }

    // Rolling min/max over ~5 seconds (compute once per frame)
    if (histCount > 0)
    {
        float hmin = std::numeric_limits<float>::infinity();
        float hmax = -std::numeric_limits<float>::infinity();
        // Recompute capacity when user changes seconds
        const int desired = juce::jlimit (100, 50000, (int) std::round ((monitorSecondsParam ? monitorSecondsParam->load() : 5.0f) * (currentSampleRate / decimation)));
        if (desired != histCapacity)
        {
            histCapacity = desired;
            history.assign (histCapacity, 0.0f);
            histWrite = 0; histCount = 0; hmin = 0.0f; hmax = 0.0f;
        }
        for (int i = 0; i < histCount; ++i)
        {
            const float v = history[i];
            hmin = std::min(hmin, v);
            hmax = std::max(hmax, v);
        }
        rollMin = hmin; rollMax = hmax;
        // Draw lines for min/max across the panel
        const float yMax = midY - rollMax * scaleY;
        const float yMin = midY - rollMin * scaleY;
        dl->AddLine (ImVec2(origin.x, yMax), ImVec2(rectMax.x, yMax), colMax, 1.0f);
        dl->AddLine (ImVec2(origin.x, yMin), ImVec2(rectMax.x, yMin), colMin, 1.0f);
    }
    ImGui::PopClipRect();
    ImGui::Dummy (ImVec2 (width, height));
}
#endif


