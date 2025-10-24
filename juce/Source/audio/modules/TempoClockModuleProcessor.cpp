#include "TempoClockModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"

TempoClockModuleProcessor::TempoClockModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Mods", juce::AudioChannelSet::discreteChannels(8), true)    // bpm,tap,nudge+,nudge-,play,stop,reset,swing
          .withOutput("Clock", juce::AudioChannelSet::discreteChannels(7), true)), // clock, beatTrig, barTrig, beatGate, phase, bpmCv, downbeat
      apvts(*this, nullptr, "TempoClockParams", createParameterLayout())
{
    bpmParam = apvts.getRawParameterValue("bpm");
    swingParam = apvts.getRawParameterValue("swing");
    divisionParam = apvts.getRawParameterValue("division");
    gateWidthParam = apvts.getRawParameterValue("gateWidth");
    takeoverParam = apvts.getRawParameterValue("takeover");
}

juce::AudioProcessorValueTreeState::ParameterLayout TempoClockModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("bpm", "BPM", juce::NormalisableRange<float>(20.0f, 300.0f, 0.01f, 0.3f), 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", juce::NormalisableRange<float>(0.0f, 0.75f, 0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("division", "Division", juce::StringArray{"1/32","1/16","1/8","1/4","1/2","1","2","4"}, 3));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gateWidth", "Gate Width", juce::NormalisableRange<float>(0.01f, 0.99f, 0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("takeover", "External Takeover", false));
    return { params.begin(), params.end() };
}

void TempoClockModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    sampleRateHz = sampleRate;
}

void TempoClockModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto in = getBusBuffer(buffer, true, 0);
    auto out = getBusBuffer(buffer, false, 0);
    out.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || sampleRateHz <= 0.0)
        return;

    // Read CV inputs ONLY if connected (BestPractice/TTS pattern)
    const bool bpmMod = isParamInputConnected("bpm_mod");
    const bool tapMod = isParamInputConnected("tap_mod");
    const bool nudgeUpMod = isParamInputConnected("nudge_up_mod");
    const bool nudgeDownMod = isParamInputConnected("nudge_down_mod");
    const bool playMod = isParamInputConnected("play_mod");
    const bool stopMod = isParamInputConnected("stop_mod");
    const bool resetMod = isParamInputConnected("reset_mod");
    const bool swingMod = isParamInputConnected("swing_mod");

    const float* bpmCV       = (bpmMod       && in.getNumChannels() > 0) ? in.getReadPointer(0) : nullptr;
    const float* tapCV       = (tapMod       && in.getNumChannels() > 1) ? in.getReadPointer(1) : nullptr;
    const float* nudgeUpCV   = (nudgeUpMod   && in.getNumChannels() > 2) ? in.getReadPointer(2) : nullptr;
    const float* nudgeDownCV = (nudgeDownMod && in.getNumChannels() > 3) ? in.getReadPointer(3) : nullptr;
    const float* playCV      = (playMod      && in.getNumChannels() > 4) ? in.getReadPointer(4) : nullptr;
    const float* stopCV      = (stopMod      && in.getNumChannels() > 5) ? in.getReadPointer(5) : nullptr;
    const float* resetCV     = (resetMod     && in.getNumChannels() > 6) ? in.getReadPointer(6) : nullptr;
    const float* swingCV     = (swingMod     && in.getNumChannels() > 7) ? in.getReadPointer(7) : nullptr;

    float bpm = bpmParam->load();
    if (bpmCV)
    {
        const float cv = juce::jlimit(0.0f, 1.0f, bpmCV[0]);
        // Map 0..1 -> 20..300 with perceptual curve
        bpm = juce::jmap(std::pow(cv, 0.3f), 0.0f, 1.0f, 20.0f, 300.0f);
    }

    float swing = swingParam ? swingParam->load() : 0.0f;
    if (swingCV)
        swing = juce::jlimit(0.0f, 0.75f, swingCV[0]);

    // Publish live telemetry
    setLiveParamValue("bpm_live", bpm);
    setLiveParamValue("swing_live", swing);

    // Handle edge controls (play/stop/reset/tap/nudge)
    auto edge = [&](const float* cv, bool& last){ bool now = (cv && cv[0] > 0.5f); bool rising = now && !last; last = now; return rising; };
    if (edge(playCV, lastPlayHigh))   if (auto* p = getParent()) p->setPlaying(true);
    if (edge(stopCV, lastStopHigh))   if (auto* p = getParent()) p->setPlaying(false);
    if (edge(resetCV, lastResetHigh)) if (auto* p = getParent()) p->resetTransportPosition();
    if (edge(tapCV, lastTapHigh))   { samplesSinceLastTap = 0.0; }
    if (edge(nudgeUpCV, lastNudgeUpHigh))   { bpm = juce::jlimit(20.0f, 300.0f, bpm + 0.5f); }
    if (edge(nudgeDownCV, lastNudgeDownHigh)) { bpm = juce::jlimit(20.0f, 300.0f, bpm - 0.5f); }

    // External takeover: write BPM to parent transport AFTER nudges
    if (takeoverParam && takeoverParam->load() > 0.5f)
    {
        if (auto* parent = getParent())
            parent->setBPM(bpm);
    }

    // Compute outputs
    float* clockOut = out.getNumChannels() > 0 ? out.getWritePointer(0) : nullptr;
    float* beatTrig = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;
    float* barTrig  = out.getNumChannels() > 2 ? out.getWritePointer(2) : nullptr;
    float* beatGate = out.getNumChannels() > 3 ? out.getWritePointer(3) : nullptr;
    float* phaseOut = out.getNumChannels() > 4 ? out.getWritePointer(4) : nullptr;
    float* bpmOut   = out.getNumChannels() > 5 ? out.getWritePointer(5) : nullptr;
    float* downbeat = out.getNumChannels() > 6 ? out.getWritePointer(6) : nullptr;

    int divisionIdx = divisionParam ? (int)divisionParam->load() : 3; // default 1/4
    // Broadcast division to transport so sync-enabled modules can follow
    if (auto* parent = getParent())
        parent->setGlobalDivisionIndex(divisionIdx);
    static const double divisions[] = { 1.0/32.0, 1.0/16.0, 1.0/8.0, 1.0/4.0, 1.0/2.0, 1.0, 2.0, 4.0 };
    const double div = divisions[juce::jlimit(0, 7, divisionIdx)];

    // Use transport position + per-sample advancement to produce stable clock
    double sr = juce::jmax(1.0, sampleRateHz);
    double localBeatsStart = m_currentTransport.songPositionBeats;
    double phaseBeats = localBeatsStart;

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance beats using current bpm
        phaseBeats += (1.0 / sr) * (bpm / 60.0);

        // Subdivision phase
        const double scaled = phaseBeats * div;
        const double frac = scaled - std::floor(scaled);

        if (phaseOut) phaseOut[i] = (float) frac;
        if (clockOut) clockOut[i] = frac < 0.01 ? 1.0f : 0.0f;
        if (bpmOut) bpmOut[i] = juce::jmap(bpm, 20.0f, 300.0f, 0.0f, 1.0f);

        // Beat/bar triggers from integer boundaries
        const int beatIndex = (int) std::floor(phaseBeats);
        const int barIndex = beatIndex / 4;
        if (beatTrig) beatTrig[i] = (beatIndex > lastBeatIndex) ? 1.0f : 0.0f;
        if (barTrig)  barTrig[i]  = (barIndex > lastBarIndex)   ? 1.0f : 0.0f;
        if (downbeat) downbeat[i] = (beatIndex > lastBeatIndex && (beatIndex % 4) == 0) ? 1.0f : 0.0f;

        // Gate width within subdivision
        const float gw = gateWidthParam ? gateWidthParam->load() : 0.5f;
        if (beatGate) beatGate[i] = (float)(frac < gw ? 1.0 : 0.0);

        lastBeatIndex = beatIndex;
        lastBarIndex = barIndex;
    }

    // Telemetry and meter
    setLiveParamValue("phase_live", (float)(phaseBeats - std::floor(phaseBeats)));
    if (!lastOutputValues.empty())
    {
        if (!lastOutputValues[0]) lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
        if (lastOutputValues[0]) lastOutputValues[0]->store(out.getNumChannels() > 0 ? out.getSample(0, numSamples - 1) : 0.0f);
    }
}

// Parameter routing: virtual IDs on single input bus
bool TempoClockModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == "bpm_mod") { outChannelIndexInBus = 0; return true; }
    if (paramId == "tap_mod") { outChannelIndexInBus = 1; return true; }
    if (paramId == "nudge_up_mod") { outChannelIndexInBus = 2; return true; }
    if (paramId == "nudge_down_mod") { outChannelIndexInBus = 3; return true; }
    if (paramId == "play_mod") { outChannelIndexInBus = 4; return true; }
    if (paramId == "stop_mod") { outChannelIndexInBus = 5; return true; }
    if (paramId == "reset_mod") { outChannelIndexInBus = 6; return true; }
    if (paramId == "swing_mod") { outChannelIndexInBus = 7; return true; }
    return false;
}

#if defined(PRESET_CREATOR_UI)
void TempoClockModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);

    // Title row with EXT badge and status
    const bool ext = takeoverParam && takeoverParam->load() > 0.5f;
    if (ext)
    {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.8f, 1.0f), "EXT TEMPO");
        ImGui::SameLine();
    }
    ImGui::Text("Clock");

    // BPM slider with live display
    bool bpmMod = isParamModulated("bpm_mod");
    float bpm = bpmMod ? getLiveParamValueFor("bpm_mod", "bpm_live", bpmParam->load()) : bpmParam->load();
    if (bpmMod) { ImGui::BeginDisabled(); }
    if (ImGui::SliderFloat("BPM", &bpm, 20.0f, 300.0f, "%.1f"))
    {
        if (!bpmMod)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("bpm"))) *p = bpm;
            if (ext) if (auto* parent = getParent()) parent->setBPM(bpm);
        }
        onModificationEnded();
    }
    if (!bpmMod) adjustParamOnWheel(apvts.getParameter("bpm"), "bpm", bpm);
    if (bpmMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Swing
    bool swingM = isParamModulated("swing_mod");
    float swing = swingM ? getLiveParamValueFor("swing_mod", "swing_live", swingParam->load()) : swingParam->load();
    if (swingM) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Swing", &swing, 0.0f, 0.75f, "%.2f"))
    {
        if (!swingM)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("swing"))) *p = swing;
        }
        onModificationEnded();
    }
    if (!swingM) adjustParamOnWheel(apvts.getParameter("swing"), "swing", swing);
    if (swingM) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Division + Gate width in-line
    int div = divisionParam ? (int)divisionParam->load() : 3;
    const char* items[] = { "1/32","1/16","1/8","1/4","1/2","1","2","4" };
    ImGui::SetNextItemWidth(itemWidth * 0.5f);
    if (ImGui::Combo("Division", &div, items, 8))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("division"))) *p = div;
        onModificationEnded();
    }

    float gw = gateWidthParam ? gateWidthParam->load() : 0.5f;
    ImGui::SetNextItemWidth(itemWidth * 0.45f);
    if (ImGui::SliderFloat("Gate Width", &gw, 0.01f, 0.99f, "%.2f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("gateWidth"))) *p = gw;
        onModificationEnded();
    }

    // Takeover toggle
    bool tk = takeoverParam && takeoverParam->load() > 0.5f;
    if (ImGui::Checkbox("External Takeover", &tk))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("takeover"))) *p = tk;
        onModificationEnded();
    }

    // Live readouts row (phase, bpm)
    ImGui::Text("Phase: %.2f  |  BPM: %.1f", getLiveParamValue("phase_live", 0.0f), getLiveParamValue("bpm_live", bpmParam->load()));

    ImGui::PopItemWidth();
}

void TempoClockModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("BPM Mod", 0);
    helpers.drawAudioInputPin("Tap", 1);
    helpers.drawAudioInputPin("Nudge+", 2);
    helpers.drawAudioInputPin("Nudge-", 3);
    helpers.drawAudioInputPin("Play", 4);
    helpers.drawAudioInputPin("Stop", 5);
    helpers.drawAudioInputPin("Reset", 6);
    helpers.drawAudioInputPin("Swing Mod", 7);

    helpers.drawAudioOutputPin("Clock", 0);
    helpers.drawAudioOutputPin("Beat Trig", 1);
    helpers.drawAudioOutputPin("Bar Trig", 2);
    helpers.drawAudioOutputPin("Beat Gate", 3);
    helpers.drawAudioOutputPin("Phase", 4);
    helpers.drawAudioOutputPin("BPM CV", 5);
    helpers.drawAudioOutputPin("Downbeat", 6);
}
#endif


