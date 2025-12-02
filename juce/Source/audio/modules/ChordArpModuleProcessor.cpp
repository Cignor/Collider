#include "ChordArpModuleProcessor.h"

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
APVTS::ParameterLayout ChordArpModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Basic harmony parameters
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        paramIdScale, "Scale",
        juce::StringArray { "Major", "Natural Minor", "Harmonic Minor", "Dorian", "Mixolydian", "Pent Maj", "Pent Min" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        paramIdKey, "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        paramIdChordMode, "Chord Mode",
        juce::StringArray { "Triad", "Seventh" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        paramIdVoicing, "Voicing",
        juce::StringArray { "Close", "Spread" },
        0));

    // Arp parameters
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        paramIdArpMode, "Arp Mode",
        juce::StringArray { "Off", "Up", "Down", "UpDown", "Random" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        paramIdArpDivision, "Arp Division",
        juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16" },
        3)); // default 1/8

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        paramIdUseExtClock, "Use External Clock", false));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        paramIdNumVoices, "Voices", 1, 4, 4));

    return { params.begin(), params.end() };
}

//==============================================================================
ChordArpModuleProcessor::ChordArpModuleProcessor()
    : ModuleProcessor (BusesProperties()
                           // Single unified input bus for all CV/Gate inputs
                           .withInput ("Inputs", juce::AudioChannelSet::discreteChannels (4), true)
                           // Single output bus: 4 voices (pitch/gate pairs) + arp pitch/gate = 10 channels
                           .withOutput ("Outputs", juce::AudioChannelSet::discreteChannels (10), true)),
      apvts (*this, nullptr, "ChordArpParams", createParameterLayout())
{
    currentSampleRate = 44100.0;

    scaleParam       = apvts.getRawParameterValue (paramIdScale);
    keyParam         = apvts.getRawParameterValue (paramIdKey);
    chordModeParam   = apvts.getRawParameterValue (paramIdChordMode);
    voicingParam     = apvts.getRawParameterValue (paramIdVoicing);
    arpModeParam     = apvts.getRawParameterValue (paramIdArpMode);
    arpDivisionParam = apvts.getRawParameterValue (paramIdArpDivision);
    useExtClockParam = apvts.getRawParameterValue (paramIdUseExtClock);
    numVoicesParam   = apvts.getRawParameterValue (paramIdNumVoices);
}

//==============================================================================
void ChordArpModuleProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    arpState.phase          = 0.0;
    arpState.currentIndex   = 0;
    arpState.gateOn         = false;
    arpState.samplesPerStep = currentSampleRate * 0.25; // default ~4 steps per second
}

void ChordArpModuleProcessor::setTimingInfo (const TransportState& state)
{
    currentTransport = state;
}

//==============================================================================
void ChordArpModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    // --- INPUT BUS (read BEFORE touching outputs to avoid aliasing issues) ---
    const auto& inputBus = getBusBuffer (buffer, true, 0);

    const int totalInCh = inputBus.getNumChannels();

    // Inputs are interpreted as CV/Gate:
    // ch0: Degree In (Raw/CV 0..1)
    // ch1: Root CV In (pitch CV, 0..1 in this skeleton)
    // ch2: Chord Mode Mod (0..1)
    // ch3: Arp Rate Mod   (0..1)

    float degreeIn   = 0.0f;
    float rootCvIn   = 0.0f;
    float chordModCv = 0.0f;
    float arpRateCv  = 0.0f;

    if (totalInCh > 0)
        degreeIn = inputBus.getReadPointer (0)[0];
    if (totalInCh > 1)
        rootCvIn = inputBus.getReadPointer (1)[0];
    if (totalInCh > 2)
        chordModCv = inputBus.getReadPointer (2)[0];
    if (totalInCh > 3)
        arpRateCv = inputBus.getReadPointer (3)[0];

    // Clamp to sane ranges
    degreeIn   = juce::jlimit (0.0f, 1.0f, degreeIn);
    rootCvIn   = juce::jlimit (0.0f, 1.0f, rootCvIn);
    chordModCv = juce::jlimit (0.0f, 1.0f, chordModCv);
    arpRateCv  = juce::jlimit (0.0f, 1.0f, arpRateCv);

    // Expose basic telemetry for eventual UI use
    setLiveParamValue ("degree_live", degreeIn);
    setLiveParamValue ("rootCv_live", rootCvIn);

    // --- CLEAR OUTPUTS ---
    buffer.clear();

    // --- SIMPLE, SAFE STUB BEHAVIOUR ---
    //
    // For now we implement a trivial mapping:
    // - Compute a base "root pitch" from Root CV (0..1)
    // - Add small offsets per voice to simulate a chord
    // - Arp output simply cycles through voices at a rate influenced by arpRateCv.

    const int numChannels = buffer.getNumChannels();

    auto* pitch1 = numChannels > 0 ? buffer.getWritePointer (0) : nullptr;
    auto* gate1  = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    auto* pitch2 = numChannels > 2 ? buffer.getWritePointer (2) : nullptr;
    auto* gate2  = numChannels > 3 ? buffer.getWritePointer (3) : nullptr;
    auto* pitch3 = numChannels > 4 ? buffer.getWritePointer (4) : nullptr;
    auto* gate3  = numChannels > 5 ? buffer.getWritePointer (5) : nullptr;
    auto* pitch4 = numChannels > 6 ? buffer.getWritePointer (6) : nullptr;
    auto* gate4  = numChannels > 7 ? buffer.getWritePointer (7) : nullptr;
    auto* arpP   = numChannels > 8 ? buffer.getWritePointer (8) : nullptr;
    auto* arpG   = numChannels > 9 ? buffer.getWritePointer (9) : nullptr;

    const int numVoices = numVoicesParam != nullptr ? juce::jlimit (1, 4, (int) numVoicesParam->load()) : 4;

    // Simple static chord offsets (0, +3, +7, +10 as arbitrary intervals in normalized units)
    const float base = rootCvIn;
    const float offsets[4] { 0.0f, 0.1f, 0.2f, 0.3f };

    // Configure a very rough arp step duration based on arpRateCv (0..1)
    const float minHz = 0.5f;
    const float maxHz = 12.0f;
    const float rateHz = juce::jmap (arpRateCv, 0.0f, 1.0f, minHz, maxHz);
    arpState.samplesPerStep = (rateHz > 0.0f ? currentSampleRate / rateHz : currentSampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        // Per-sample voice pitches
        const float v1 = juce::jlimit (0.0f, 1.0f, base + offsets[0]);
        const float v2 = juce::jlimit (0.0f, 1.0f, base + offsets[1]);
        const float v3 = juce::jlimit (0.0f, 1.0f, base + offsets[2]);
        const float v4 = juce::jlimit (0.0f, 1.0f, base + offsets[3]);

        if (pitch1) pitch1[i] = v1;
        if (pitch2 && numVoices > 1) pitch2[i] = v2;
        if (pitch3 && numVoices > 2) pitch3[i] = v3;
        if (pitch4 && numVoices > 3) pitch4[i] = v4;

        // Hold all gates high while transport is playing
        const float g = currentTransport.isPlaying ? 1.0f : 0.0f;
        if (gate1) gate1[i] = g;
        if (gate2 && numVoices > 1) gate2[i] = g;
        if (gate3 && numVoices > 2) gate3[i] = g;
        if (gate4 && numVoices > 3) gate4[i] = g;

        // --- Extremely simple arp: step index based on phase ---
        if (arpP || arpG)
        {
            if (currentTransport.isPlaying)
            {
                arpState.phase += 1.0;
                if (arpState.phase >= arpState.samplesPerStep)
                {
                    arpState.phase -= arpState.samplesPerStep;
                    arpState.currentIndex = (arpState.currentIndex + 1) % numVoices;
                    arpState.gateOn = true;
                }
            }
            else
            {
                // When stopped, reset arp phase and gate
                arpState.phase    = 0.0;
                arpState.gateOn   = false;
                arpState.currentIndex = 0;
            }

            float currentPitch = v1;
            switch (arpState.currentIndex)
            {
                case 0: currentPitch = v1; break;
                case 1: currentPitch = v2; break;
                case 2: currentPitch = v3; break;
                case 3: currentPitch = v4; break;
                default: break;
            }

            if (arpP) arpP[i] = currentPitch;
            if (arpG) arpG[i] = (arpState.gateOn ? 1.0f : 0.0f);

            // simple short pulse: one sample wide
            arpState.gateOn = false;
        }
    }

#if defined(PRESET_CREATOR_UI)
    // Capture a lightweight snapshot for node visualization (last computed block state).
    {
        const float v1 = juce::jlimit (0.0f, 1.0f, base + offsets[0]);
        const float v2 = juce::jlimit (0.0f, 1.0f, base + offsets[1]);
        const float v3 = juce::jlimit (0.0f, 1.0f, base + offsets[2]);
        const float v4 = juce::jlimit (0.0f, 1.0f, base + offsets[3]);

        float currentPitch = v1;
        switch (arpState.currentIndex)
        {
            case 0: currentPitch = v1; break;
            case 1: currentPitch = v2; break;
            case 2: currentPitch = v3; break;
            case 3: currentPitch = v4; break;
            default: break;
        }

        vizData.degreeIn.store (degreeIn);
        vizData.rootCvIn.store (rootCvIn);
        vizData.pitch1.store (v1);
        vizData.pitch2.store (v2);
        vizData.pitch3.store (v3);
        vizData.pitch4.store (v4);
        vizData.arpPitch.store (currentPitch);
        vizData.arpGate.store (currentTransport.isPlaying ? 1.0f : 0.0f);
    }
#endif
}

//==============================================================================
bool ChordArpModuleProcessor::getParamRouting (const juce::String& paramId,
                                               int& outBusIndex,
                                               int& outChannelIndexInBus) const
{
    // All modulation is on input bus 0.
    outBusIndex = 0;

    // Map virtual IDs to physical channel indices on the single input bus.
    if (paramId == paramIdDegreeMod)    { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdRootCvMod)    { outChannelIndexInBus = 1; return true; }
    if (paramId == paramIdChordModeMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdArpRateMod)   { outChannelIndexInBus = 3; return true; }

    return false;
}

//==============================================================================
juce::String ChordArpModuleProcessor::getAudioInputLabel (int channel) const
{
    switch (channel)
    {
        case 0: return "Degree In";
        case 1: return "Root CV In";
        case 2: return "Chord Mode Mod";
        case 3: return "Arp Rate Mod";
        default: return juce::String ("In ") + juce::String (channel + 1);
    }
}

juce::String ChordArpModuleProcessor::getAudioOutputLabel (int channel) const
{
    switch (channel)
    {
        case 0: return "Pitch 1";
        case 1: return "Gate 1";
        case 2: return "Pitch 2";
        case 3: return "Gate 2";
        case 4: return "Pitch 3";
        case 5: return "Gate 3";
        case 6: return "Pitch 4";
        case 7: return "Gate 4";
        case 8: return "Arp Pitch";
        case 9: return "Arp Gate";
        default: return juce::String ("Out ") + juce::String (channel + 1);
    }
}

//==============================================================================
std::optional<RhythmInfo> ChordArpModuleProcessor::getRhythmInfo() const
{
    RhythmInfo info;

    info.displayName = "Chord Arp #" + juce::String (getLogicalId());
    info.sourceType  = "chord_arp";

    // Consider node active whenever transport is playing
    info.isActive = currentTransport.isPlaying;

    // Very rough BPM estimate based on arpRateCv (we don't have its live value exposed yet,
    // so just approximate from samplesPerStep)
    if (currentTransport.isPlaying && arpState.samplesPerStep > 0.0 && currentSampleRate > 0.0)
    {
        const double stepsPerSecond = currentSampleRate / arpState.samplesPerStep;
        info.bpm = static_cast<float> (stepsPerSecond * 60.0);
    }
    else
    {
        info.bpm = 0.0f;
    }

    info.isSynced = false; // this initial stub does not yet sync to transport divisions

    return info;
}

void ChordArpModuleProcessor::forceStop()
{
    arpState.currentIndex   = 0;
    arpState.phase          = 0.0;
    arpState.gateOn         = false;
}

//==============================================================================
#if defined(PRESET_CREATOR_UI)

void ChordArpModuleProcessor::drawParametersInNode (
    float itemWidth,
    const std::function<bool (const juce::String& paramId)>& isParamModulated,
    const std::function<void()>& onModificationEnded)
{
    ImGui::PushID (this);
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();

    // --- Visualization panel -------------------------------------------------
    {
        // Read snapshot data from atomics
        const float degree   = vizData.degreeIn.load();
        const float rootCv   = vizData.rootCvIn.load();
        const float v1       = vizData.pitch1.load();
        const float v2       = vizData.pitch2.load();
        const float v3       = vizData.pitch3.load();
        const float v4       = vizData.pitch4.load();
        const float arpPitch = vizData.arpPitch.load();
        const float arpGate  = vizData.arpGate.load();

        const float vizHeight = 80.0f;
        const ImVec2 graphSize (itemWidth, vizHeight);
        const ImGuiWindowFlags childFlags =
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::BeginChild ("ChordArpViz", graphSize, false, childFlags))
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetWindowPos();
            const ImVec2 p1 = ImVec2 (p0.x + graphSize.x, p0.y + graphSize.y);

            const ImU32 bgColor   = ThemeManager::getInstance().getCanvasBackground();
            const ImU32 voiceColor = ImGui::GetColorU32 (theme.modules.sequencer_step_active_grab);
            const ImU32 arpColor   = ImGui::GetColorU32 (theme.accent);

            drawList->AddRectFilled (p0, p1, bgColor, 4.0f);
            drawList->PushClipRect (p0, p1, true);

            // Draw four voice pitch bars across the width
            const float padding = 6.0f;
            const float width  = graphSize.x - padding * 2.0f;
            const float height = graphSize.y - padding * 2.0f;
            const float barWidth = width / 6.0f;

            auto drawVoiceBar = [&] (int index, float value, ImU32 color)
            {
                const float x0 = p0.x + padding + index * (barWidth + 4.0f);
                const float x1 = x0 + barWidth;
                const float clamped = juce::jlimit (0.0f, 1.0f, value);
                const float y1 = p0.y + padding + height;
                const float y0 = y1 - clamped * height;

                const ImVec2 a (x0, juce::jlimit (p0.y + padding, p1.y - padding, y0));
                const ImVec2 b (x1, juce::jlimit (p0.y + padding, p1.y - padding, y1));
                drawList->AddRectFilled (a, b, color, 3.0f);
            };

            drawVoiceBar (0, v1, voiceColor);
            drawVoiceBar (1, v2, voiceColor);
            drawVoiceBar (2, v3, voiceColor);
            drawVoiceBar (3, v4, voiceColor);

            // Arp pitch indicator on the right
            const float arpX0 = p0.x + padding + 4.0f * (barWidth + 4.0f);
            const float arpX1 = arpX0 + barWidth * 1.2f;
            const float arpClamped = juce::jlimit (0.0f, 1.0f, arpPitch);
            const float arpY1 = p0.y + padding + height;
            const float arpY0 = arpY1 - arpClamped * height;
            const ImVec2 arpA (arpX0, juce::jlimit (p0.y + padding, p1.y - padding, arpY0));
            const ImVec2 arpB (arpX1, juce::jlimit (p0.y + padding, p1.y - padding, arpY1));
            drawList->AddRectFilled (arpA, arpB, arpColor, 3.0f);

            // Simple gate overlay (border highlight when gate is high)
            if (arpGate > 0.5f)
                drawList->AddRect (p0, p1, arpColor, 4.0f, 0, 2.0f);

            drawList->PopClipRect();

            // Text overlay
            ImGui::SetCursorPos (ImVec2 (4.0f, 4.0f));
            ImGui::TextColored (ImVec4 (1.0f, 1.0f, 1.0f, 0.9f),
                                "Degree %.2f | Root CV %.2f", degree, rootCv);

            ImGui::SetCursorPos (ImVec2 (0.0f, 0.0f));
            ImGui::InvisibleButton ("##chordArpVizDrag", graphSize);
        }
        ImGui::EndChild();
    }

    ImGui::PushItemWidth (itemWidth);

    // Scale
    {
        int idx = scaleParam ? (int) scaleParam->load() : 0;
        if (ImGui::Combo ("Scale", &idx, "Major\0Natural Minor\0Harmonic Minor\0Dorian\0Mixolydian\0Pent Maj\0Pent Min\0\0"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (ap.getParameter (paramIdScale)))
                *p = idx;
            onModificationEnded();
        }
    }

    // Key
    {
        int idx = keyParam ? (int) keyParam->load() : 0;
        if (ImGui::Combo ("Key", &idx, "C\0C#\0D\0D#\0E\0F\0F#\0G\0G#\0A\0A#\0B\0\0"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (ap.getParameter (paramIdKey)))
                *p = idx;
            onModificationEnded();
        }
    }

    // Chord mode
    {
        int idx = chordModeParam ? (int) chordModeParam->load() : 0;
        if (ImGui::Combo ("Chord", &idx, "Triad\0Seventh\0\0"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (ap.getParameter (paramIdChordMode)))
                *p = idx;
            onModificationEnded();
        }
    }

    // Arp mode
    {
        int idx = arpModeParam ? (int) arpModeParam->load() : 0;
        if (ImGui::Combo ("Arp", &idx, "Off\0Up\0Down\0UpDown\0Random\0\0"))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (ap.getParameter (paramIdArpMode)))
                *p = idx;
            onModificationEnded();
        }
    }

    // Voices (simple slider)
    {
        int voices = numVoicesParam ? (int) numVoicesParam->load() : 4;
        if (ImGui::SliderInt ("Voices", &voices, 1, 4))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterInt*> (ap.getParameter (paramIdNumVoices)))
                *p = voices;
            onModificationEnded();
        }
    }

    ImGui::PopItemWidth();
    ImGui::PopID();
}

void ChordArpModuleProcessor::drawIoPins (const NodePinHelpers& helpers)
{
    // Inputs: Degree, Root CV, Chord Mode Mod, Arp Rate Mod
    helpers.drawParallelPins ("Degree In", 0, "Pitch 1", 0);
    helpers.drawParallelPins ("Root CV In", 1, "Gate 1", 1);
    helpers.drawParallelPins ("Chord Mode Mod", 2, "Pitch 2", 2);
    helpers.drawParallelPins ("Arp Rate Mod", 3, "Gate 2", 3);

    // Remaining outputs without paired inputs
    helpers.drawParallelPins (nullptr, -1, "Pitch 3", 4);
    helpers.drawParallelPins (nullptr, -1, "Gate 3", 5);
    helpers.drawParallelPins (nullptr, -1, "Pitch 4", 6);
    helpers.drawParallelPins (nullptr, -1, "Gate 4", 7);
    helpers.drawParallelPins (nullptr, -1, "Arp Pitch", 8);
    helpers.drawParallelPins (nullptr, -1, "Arp Gate", 9);
}

#endif


