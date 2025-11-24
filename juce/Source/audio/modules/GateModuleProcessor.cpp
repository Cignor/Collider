#include "GateModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout GateModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdThreshold, "Threshold", -80.0f, 0.0f, -40.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdAttack, "Attack", 0.1f, 100.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdRelease, "Release", 5.0f, 1000.0f, 50.0f));
    
    return { params.begin(), params.end() };
}

GateModuleProcessor::GateModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Audio In", juce::AudioChannelSet::stereo(), true)
          // For now, no modulation inputs. Can be added later if desired.
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GateParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue(paramIdThreshold);
    attackParam = apvts.getRawParameterValue(paramIdAttack);
    releaseParam = apvts.getRawParameterValue(paramIdRelease);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
}

void GateModuleProcessor::prepareToPlay(double newSampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = newSampleRate;
    envelope = 0.0f;

#if defined(PRESET_CREATOR_UI)
    vizData.gateAmount.store(0.0f);
    vizData.currentThresholdDb.store(thresholdParam ? thresholdParam->load() : -40.0f);
    vizData.currentAttackMs.store(attackParam ? attackParam->load() : 1.0f);
    vizData.currentReleaseMs.store(releaseParam ? releaseParam->load() : 50.0f);
    vizData.writeIndex.store(0);
    for (auto& v : vizData.inputHistory) v.store(-80.0f);
    for (auto& v : vizData.envelopeHistory) v.store(-80.0f);
    for (auto& v : vizData.gateHistory) v.store(0.0f);
#endif
}

void GateModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // Copy input to output
    const int numInputChannels = inBus.getNumChannels();
    const int numOutputChannels = outBus.getNumChannels();

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

    // Get parameters
    const float thresholdDb = thresholdParam != nullptr ? thresholdParam->load() : -40.0f;
    const float thresholdLinear = juce::Decibels::decibelsToGain(thresholdDb);
    // Convert attack/release times from ms to a per-sample coefficient
    const float attackMs = juce::jmax(0.1f, attackParam != nullptr ? attackParam->load() : 1.0f);
    const float releaseMs = juce::jmax(1.0f, releaseParam != nullptr ? releaseParam->load() : 50.0f);
    const float attackCoeff = 1.0f - std::exp(-1.0f / (attackMs * 0.001f * (float)currentSampleRate));
    const float releaseCoeff = 1.0f - std::exp(-1.0f / (releaseMs * 0.001f * (float)currentSampleRate));

    auto* leftData = outBus.getWritePointer(0);
    auto* rightData = numChannels > 1 ? outBus.getWritePointer(1) : nullptr;

    float peakInput = 0.0f;
    float peakEnvelope = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        // Get the magnitude of the input signal (mono or stereo)
        float magnitude = std::abs(leftData[i]);
        if (rightData)
            magnitude = std::max(magnitude, std::abs(rightData[i]));

        peakInput = std::max(peakInput, magnitude);

        // Determine if the gate should be open or closed
        float target = (magnitude >= thresholdLinear) ? 1.0f : 0.0f;

        // Move the envelope towards the target using the appropriate attack or release time
        if (target > envelope)
            envelope += (target - envelope) * attackCoeff;
        else
            envelope += (target - envelope) * releaseCoeff;

        peakEnvelope = std::max(peakEnvelope, envelope);
        
        // Apply the envelope as a gain to the signal
        leftData[i] *= envelope;
        if (rightData)
            rightData[i] *= envelope;
    }

#if defined(PRESET_CREATOR_UI)
    const float inputDb = juce::Decibels::gainToDecibels(juce::jmax(peakInput, 1.0e-6f), -80.0f);
    const float envelopeDb = juce::Decibels::gainToDecibels(juce::jmax(peakEnvelope, 1.0e-6f), -80.0f);
    int writeIdx = vizData.writeIndex.load();
    vizData.inputHistory[writeIdx].store(inputDb);
    vizData.envelopeHistory[writeIdx].store(envelopeDb);
    vizData.gateHistory[writeIdx].store(peakEnvelope);
    writeIdx = (writeIdx + 1) % VizData::historyPoints;
    vizData.writeIndex.store(writeIdx);
    vizData.currentThresholdDb.store(thresholdDb);
    vizData.currentAttackMs.store(attackMs);
    vizData.currentReleaseMs.store(releaseMs);
    vizData.gateAmount.store(envelope);
#endif

    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(leftData[numSamples - 1]);
        if (lastOutputValues[1] && rightData) lastOutputValues[1]->store(rightData[numSamples - 1]);
    }
}

bool GateModuleProcessor::getParamRouting(const juce::String& /*paramId*/, int& /*outBusIndex*/, int& /*outChannelIndexInBus*/) const
{
    // No modulation inputs in this version
    return false;
}

juce::String GateModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In L";
    if (channel == 1) return "In R";
    return {};
}

juce::String GateModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void GateModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    ImGui::PushID(this);
    ImGui::PushItemWidth(itemWidth);

    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    auto* drawList = ImGui::GetWindowDrawList();

    ImGui::Spacing();
    ImGui::Text("Gate Visualizer");
    ImGui::Spacing();

    const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
    const ImU32 inputColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);
    const ImU32 envelopeColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre);
    const ImU32 gateColor = ImGui::ColorConvertFloat4ToU32(theme.accent);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float vizHeight = 90.0f;
    const ImVec2 rectMax = ImVec2(origin.x + itemWidth, origin.y + vizHeight);
    drawList->AddRectFilled(origin, rectMax, bgColor, 4.0f);
    ImGui::PushClipRect(origin, rectMax, true);

    float inputHistory[VizData::historyPoints];
    float envelopeHistory[VizData::historyPoints];
    float gateHistory[VizData::historyPoints];
    const int writeIdx = vizData.writeIndex.load();
    for (int i = 0; i < VizData::historyPoints; ++i)
    {
        const int idx = (writeIdx + i) % VizData::historyPoints;
        inputHistory[i] = vizData.inputHistory[idx].load();
        envelopeHistory[i] = vizData.envelopeHistory[idx].load();
        gateHistory[i] = vizData.gateHistory[idx].load();
    }

    auto mapDbToNorm = [](float db)
    {
        return juce::jlimit(0.0f, 1.0f, (db + 80.0f) / 80.0f);
    };

    const float stepX = itemWidth / (float)(VizData::historyPoints - 1);
    float prevX = origin.x;
    float prevY = rectMax.y;
    for (int i = 0; i < VizData::historyPoints; ++i)
    {
        const float norm = mapDbToNorm(inputHistory[i]);
        const float y = rectMax.y - norm * (vizHeight - 8.0f) - 4.0f;
        const float x = origin.x + i * stepX;
        if (i > 0)
            drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), inputColor, 2.0f);
        prevX = x;
        prevY = y;
    }

    prevX = origin.x;
    prevY = rectMax.y;
    for (int i = 0; i < VizData::historyPoints; ++i)
    {
        const float norm = mapDbToNorm(envelopeHistory[i]);
        const float y = rectMax.y - norm * (vizHeight - 8.0f) - 4.0f;
        const float x = origin.x + i * stepX;
        if (i > 0)
            drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), envelopeColor, 2.2f);
        prevX = x;
        prevY = y;
    }

    for (int i = 0; i < VizData::historyPoints; ++i)
    {
        const float state = gateHistory[i];
        if (state > 0.01f)
        {
            const float x = origin.x + i * stepX;
            const float yTop = origin.y + 4.0f;
            const float yBottom = origin.y + 12.0f + (1.0f - state) * 10.0f;
            drawList->AddLine(ImVec2(x, yTop), ImVec2(x, yBottom), gateColor, 1.2f);
        }
    }

    const float thresholdDb = vizData.currentThresholdDb.load();
    const float thresholdNorm = mapDbToNorm(thresholdDb);
    const float thresholdY = rectMax.y - thresholdNorm * (vizHeight - 8.0f) - 4.0f;
    drawList->AddLine(ImVec2(origin.x, thresholdY), ImVec2(rectMax.x, thresholdY), IM_COL32(255, 255, 255, 120), 1.5f);
    const juce::String threshText = juce::String(thresholdDb, 1) + " dB";
    drawList->AddText(ImVec2(origin.x + 6.0f, thresholdY - ImGui::GetTextLineHeight()), IM_COL32(255, 255, 255, 160), threshText.toRawUTF8());

    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, rectMax.y));
    ImGui::Dummy(ImVec2(itemWidth, 0));

    ImGui::Spacing();
    ImGui::Text("Gate State");
    const float gateAmt = vizData.gateAmount.load();
    const char* gateLabel = gateAmt > 0.5f ? "OPEN" : (gateAmt > 0.1f ? "TRANSIENT" : "CLOSED");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, gateColor);
    ImGui::ProgressBar(gateAmt, ImVec2(itemWidth * 0.6f, 0), gateLabel);
    ImGui::PopStyleColor();

    ImGui::Spacing();

    auto drawSlider = [&](const char* label, const juce::String& paramId, float min, float max, const char* format) {
        float value = ap.getRawParameterValue(paramId)->load();
        if (ImGui::SliderFloat(label, &value, min, max, format))
            *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)) = value;
        adjustParamOnWheel(ap.getParameter(paramId), paramId, value);
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    };

    drawSlider("Threshold", paramIdThreshold, -80.0f, 0.0f, "%.1f dB");
    drawSlider("Attack", paramIdAttack, 0.1f, 100.0f, "%.1f ms");
    drawSlider("Release", paramIdRelease, 5.0f, 1000.0f, "%.0f ms");

    ImGui::PopItemWidth();
    ImGui::PopID();
}

void GateModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("In L", 0, "Out L", 0);
    helpers.drawParallelPins("In R", 1, "Out R", 1);
}
#endif

