#include "TempoClockModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"
#include <limits>

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

TempoClockModuleProcessor::TempoClockModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Mods", juce::AudioChannelSet::discreteChannels(8), true)    // bpm,tap,nudge+,nudge-,play,stop,reset,swing
          .withOutput("Clock", juce::AudioChannelSet::discreteChannels(7), true)), // clock, beatTrig, barTrig, beatGate, phase, bpmCv, downbeat
      apvts(*this, nullptr, "TempoClockParams", createParameterLayout())
{
    bpmParam = apvts.getRawParameterValue(paramIdBpm);
    swingParam = apvts.getRawParameterValue(paramIdSwing);
    divisionParam = apvts.getRawParameterValue(paramIdDivision);
    gateWidthParam = apvts.getRawParameterValue(paramIdGateWidth);
    syncToHostParam = apvts.getRawParameterValue(paramIdSyncToHost);
    divisionOverrideParam = apvts.getRawParameterValue(paramIdDivisionOverride);
    
    // Timeline sync parameters
    syncToTimelineParam = apvts.getRawParameterValue(paramIdSyncToTimeline);
    timelineSourceIdParam = apvts.getRawParameterValue(paramIdTimelineSourceId);
    enableBPMDerivationParam = apvts.getRawParameterValue(paramIdEnableBPMDerivation);
    beatsPerTimelineParam = apvts.getRawParameterValue(paramIdBeatsPerTimeline);
}

juce::AudioProcessorValueTreeState::ParameterLayout TempoClockModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdBpm, "BPM", juce::NormalisableRange<float>(20.0f, 300.0f, 0.01f, 0.3f), 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdSwing, "Swing", juce::NormalisableRange<float>(0.0f, 0.75f, 0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(paramIdDivision, "Division", juce::StringArray{"1/32","1/16","1/8","1/4","1/2","1","2","4"}, 3));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdGateWidth, "Gate Width", juce::NormalisableRange<float>(0.01f, 0.99f, 0.0f, 1.0f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdSyncToHost, "Sync to Host", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdDivisionOverride, "Division Override", false));
    
    // Timeline sync parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdSyncToTimeline, "Sync to Timeline", false));
    params.push_back(std::make_unique<juce::AudioParameterInt>(paramIdTimelineSourceId, "Timeline Source", 0, 9999, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdEnableBPMDerivation, "Derive BPM from Timeline", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdBeatsPerTimeline, "Beats per Timeline", 
        juce::NormalisableRange<float>(1.0f, 32.0f, 0.1f), 4.0f));
    
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
    
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || sampleRateHz <= 0.0)
        return;
    
    // FIX: DON'T clear output buffer yet - it might alias with input buffer!
    // We'll write to all output channels explicitly, so no need to clear.

    // Read CV inputs ONLY if connected (BestPractice/TTS pattern)
    const bool bpmMod = isParamInputConnected(paramIdBpmMod);
    const bool tapMod = isParamInputConnected(paramIdTapMod);
    const bool nudgeUpMod = isParamInputConnected(paramIdNudgeUpMod);
    const bool nudgeDownMod = isParamInputConnected(paramIdNudgeDownMod);
    const bool playMod = isParamInputConnected(paramIdPlayMod);
    const bool stopMod = isParamInputConnected(paramIdStopMod);
    const bool resetMod = isParamInputConnected(paramIdResetMod);
    const bool swingMod = isParamInputConnected(paramIdSwingMod);

    const float* bpmCV       = (bpmMod       && in.getNumChannels() > 0) ? in.getReadPointer(0) : nullptr;
    const float* tapCV       = (tapMod       && in.getNumChannels() > 1) ? in.getReadPointer(1) : nullptr;
    const float* nudgeUpCV   = (nudgeUpMod   && in.getNumChannels() > 2) ? in.getReadPointer(2) : nullptr;
    const float* nudgeDownCV = (nudgeDownMod && in.getNumChannels() > 3) ? in.getReadPointer(3) : nullptr;
    const float* playCV      = (playMod      && in.getNumChannels() > 4) ? in.getReadPointer(4) : nullptr;
    const float* stopCV      = (stopMod      && in.getNumChannels() > 5) ? in.getReadPointer(5) : nullptr;
    const float* resetCV     = (resetMod     && in.getNumChannels() > 6) ? in.getReadPointer(6) : nullptr;
    const float* swingCV     = (swingMod     && in.getNumChannels() > 7) ? in.getReadPointer(7) : nullptr;

    float bpm = bpmParam->load();
    
    // FIX: Set flag when BPM comes from CV so other sources won't override it (MultiBandShaper pattern)
    bool bpmFromCV = false;
    if (bpmCV)
    {
        const float cv = juce::jlimit(0.0f, 1.0f, bpmCV[0]);
        // Map 0..1 -> 20..300 with perceptual curve
        bpm = juce::jmap(std::pow(cv, 0.3f), 0.0f, 1.0f, 20.0f, 300.0f);
        bpmFromCV = true;
    }

    float swing = swingParam ? swingParam->load() : 0.0f;
    if (swingCV)
        swing = juce::jlimit(0.0f, 0.75f, swingCV[0]);

    // Increment tap counter each block (if we're waiting for a second tap)
    if (hasPreviousTap)
    {
        samplesSinceLastTap += numSamples;
    }

    // Handle edge controls (play/stop/reset/tap/nudge)
    // FIX: Only allow these to modify BPM if CV is NOT connected
    auto edge = [&](const float* cv, bool& last){ bool now = (cv && cv[0] > 0.5f); bool rising = now && !last; last = now; return rising; };
    if (edge(playCV, lastPlayHigh))   if (auto* p = getParent()) p->setPlaying(true);
    if (edge(stopCV, lastStopHigh))   if (auto* p = getParent()) p->setPlaying(false);
    if (edge(resetCV, lastResetHigh)) if (auto* p = getParent()) p->resetTransportPosition();
    
    // TAP TEMPO (CV Input): Calculate BPM from interval between taps (ONLY if BPM CV not connected)
    bool tapDetected = false;
    if (!bpmFromCV && edge(tapCV, lastTapHigh))
    {
        tapDetected = true;
    }
    
    // TAP TEMPO (UI Button): Detect if UI button was pressed
    if (!bpmFromCV)
    {
        const double currentUiTap = uiTapTimestamp.load();
        if (currentUiTap != lastProcessedUiTap && currentUiTap > 0.0)
        {
            tapDetected = true;
            lastProcessedUiTap = currentUiTap;
        }
    }
    
    // Process tap (from CV or UI button)
    if (tapDetected)
    {
        if (hasPreviousTap && samplesSinceLastTap > 0.0)
        {
            // Calculate BPM from time between taps
            const double secondsBetweenTaps = samplesSinceLastTap / sampleRateHz;
            
            // Sanity check: prevent extreme values (20-300 BPM range)
            // Min interval: 0.2 seconds (300 BPM), Max interval: 3.0 seconds (20 BPM)
            if (secondsBetweenTaps >= 0.2 && secondsBetweenTaps <= 3.0)
            {
                float newBPM = 60.0f / static_cast<float>(secondsBetweenTaps);
                bpm = juce::jlimit(20.0f, 300.0f, newBPM);
                
                // Update the parameter so it persists
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdBpm)))
                    *p = bpm;
                
                juce::Logger::writeToLog("[TempoClock] Tap tempo: " + 
                    juce::String(secondsBetweenTaps, 3) + "s interval = " + 
                    juce::String(bpm, 1) + " BPM");
            }
        }
        
        // Reset counter and mark that we have a valid tap
        samplesSinceLastTap = 0.0;
        hasPreviousTap = true;
    }
    
    // TAP TIMEOUT: Reset if no tap for 4 seconds
    if (hasPreviousTap && samplesSinceLastTap > sampleRateHz * 4.0)
    {
        hasPreviousTap = false;
        samplesSinceLastTap = 0.0;
    }
    
    // NUDGE: Only allow if BPM CV not connected
    if (!bpmFromCV)
    {
        if (edge(nudgeUpCV, lastNudgeUpHigh))   { bpm = juce::jlimit(20.0f, 300.0f, bpm + 0.5f); }
        if (edge(nudgeDownCV, lastNudgeDownHigh)) { bpm = juce::jlimit(20.0f, 300.0f, bpm - 0.5f); }
    }

    // === TIMELINE SYNC (HIGHEST PRIORITY for position, can also set BPM) ===
    // Timeline sync controls transport position (always) and optionally BPM (if derivation enabled)
    bool syncToTimeline = syncToTimelineParam && syncToTimelineParam->load() > 0.5f;
    bool timelineSyncActive = false;
    
    // Static counter for throttled logging (log every 44100 samples = ~1 second at 44.1kHz)
    static int timelineSyncLogCounter = 0;
    const int logThrottleSamples = 44100; // Log once per second
    
    if (syncToTimeline)
    {
        juce::uint32 targetId = timelineSourceIdParam ? (juce::uint32)timelineSourceIdParam->load() : 0;
        auto* parent = getParent();
        if (targetId > 0 && parent != nullptr)
        {
            // Get snapshot (thread-safe, lock-free)
            auto currentProcessors = parent->getActiveAudioProcessors();
            bool found = false;
            bool wasActive = false;
            
            if (currentProcessors)
            {
                // Iterate snapshot (lock-free, thread-safe)
                for (const auto& mod : *currentProcessors)
                {
                    if (mod && mod->getLogicalId() == targetId)
                    {
                        found = true;
                        wasActive = mod->canProvideTimeline() && mod->isTimelineActive();
                        
                        if (wasActive)
                        {
                            // Read atomic values directly (zero overhead)
                            double pos = mod->getTimelinePositionSeconds();
                            double dur = mod->getTimelineDurationSeconds();
                            
                            // Validate duration (edge case: zero or negative duration)
                            if (dur <= 0.0)
                            {
                                if (timelineSyncLogCounter % logThrottleSamples == 0)
                                {
                                    juce::Logger::writeToLog("[TempoClock] Timeline sync: Invalid duration (" + 
                                        juce::String(dur) + "s) for module #" + juce::String(targetId));
                                }
                                break; // Skip this block
                            }
                            
                            // Clamp position to valid range (edge case: position out of bounds)
                            double originalPos = pos;
                            pos = juce::jlimit(0.0, dur, pos);
                            if (pos != originalPos && timelineSyncLogCounter % logThrottleSamples == 0)
                            {
                                juce::Logger::writeToLog("[TempoClock] Timeline sync: Clamped position from " + 
                                    juce::String(originalPos, 3) + "s to " + juce::String(pos, 3) + "s (duration: " + 
                                    juce::String(dur, 3) + "s)");
                            }
                            
                            // Update transport position (ALWAYS, every block)
                            parent->setTransportPositionSeconds(pos);
                            
                            // Set this module as timeline master (prevents circular dependency)
                            parent->setTimelineMaster(targetId);
                            
                            // Optional: Derive BPM from timeline (only if BPM CV not connected)
                            if (!bpmFromCV && enableBPMDerivationParam && 
                                enableBPMDerivationParam->load() > 0.5f)
                            {
                                double beatsPerTimeline = beatsPerTimelineParam ? 
                                    beatsPerTimelineParam->load() : 4.0;
                                double derivedBPM = (beatsPerTimeline * 60.0) / dur;
                                derivedBPM = juce::jlimit(20.0, 300.0, derivedBPM);
                                
                                parent->setBPM(derivedBPM);
                                parent->setTempoControlledByModule(true);
                                
                                // Use derived BPM for this block
                                bpm = (float)derivedBPM;
                                bpmFromCV = false; // Prevent other sources from overriding
                                
                                // Log BPM derivation (throttled)
                                if (timelineSyncLogCounter % logThrottleSamples == 0)
                                {
                                    juce::Logger::writeToLog("[TempoClock] Timeline sync: Derived BPM " + 
                                        juce::String(derivedBPM, 1) + " from timeline (duration: " + 
                                        juce::String(dur, 3) + "s, beats: " + juce::String(beatsPerTimeline, 1) + ")");
                                }
                            }
                            
                            timelineSyncActive = true;
                            
                            // Log position update (throttled to once per second)
                            if (timelineSyncLogCounter % logThrottleSamples == 0)
                            {
                                juce::Logger::writeToLog("[TempoClock] Timeline sync: Position " + 
                                    juce::String(pos, 3) + "s / " + juce::String(dur, 3) + "s from module #" + 
                                    juce::String(targetId));
                            }
                        }
                        break;
                    }
                }
            }
            
            // Module was deleted or not found - handle gracefully
            if (!found && targetId != 0)
            {
                // Log only once to avoid spam
                static juce::uint32 lastLoggedDeletedId = 0;
                if (lastLoggedDeletedId != targetId)
                {
                    juce::Logger::writeToLog("[TempoClock] Timeline sync: Module #" + juce::String(targetId) + 
                        " not found (deleted), resetting to None");
                    lastLoggedDeletedId = targetId;
                }
                
                // Reset to "None" (atomic, safe in audio thread)
                if (timelineSourceIdParam)
                    timelineSourceIdParam->store(0.0f);
                
                // Clear timeline master flag
                parent->setTimelineMaster(0);
                
                // Queue UI update (async, safe)
                juce::MessageManager::callAsync([this]() {
                    if (auto* p = apvts.getParameter(paramIdTimelineSourceId))
                        p->setValueNotifyingHost(0.0f);
                });
            }
            else if (!timelineSyncActive && targetId > 0 && found)
            {
                // Source exists but is inactive - clear master flag
                parent->setTimelineMaster(0);
                
                // Log inactive state (throttled)
                static bool lastLoggedInactive = false;
                if (timelineSyncLogCounter % logThrottleSamples == 0 && !lastLoggedInactive)
                {
                    juce::Logger::writeToLog("[TempoClock] Timeline sync: Module #" + juce::String(targetId) + 
                        " exists but is inactive (no media loaded or not playing)");
                    lastLoggedInactive = true;
                }
                if (wasActive) lastLoggedInactive = false; // Reset when becomes active again
            }
        }
        else if (targetId == 0)
        {
            // No source selected - but timeline sync is enabled
            // Set a special marker to prevent ModularSynthProcessor from advancing transport
            // (TempoClock will control transport position based on BPM instead)
            if (auto* parent = getParent())
            {
                // Use UINT32_MAX as a special value meaning "TempoClock controls transport, don't advance by sample count"
                parent->setTimelineMaster(std::numeric_limits<juce::uint32>::max());
            }
        }
    }
    else
    {
        // Timeline sync disabled - clear master flag
        if (auto* parent = getParent())
            parent->setTimelineMaster(0);
    }
    
    // Increment log counter (wrap at throttle limit to avoid overflow)
    timelineSyncLogCounter = (timelineSyncLogCounter + numSamples) % (logThrottleSamples * 2);

    // FIX: When timeline sync is enabled but no source is selected,
    // DO NOT update transport position at all. This prevents SampleLoader from
    // constantly seeking when synced to transport. Transport stays at its current
    // position until a timeline source is selected or timeline sync is disabled.
    // The UINT32_MAX marker prevents ModularSynthProcessor from advancing it by sample count.
    // (We do NOT update transport position here - this prevents SampleLoader scrubbing)
    
    // Sync to Host: Use host transport tempo OR control it
    // FIX: BPM CV always takes priority over sync-to-host
    // NOTE: Timeline sync (if active) has already set BPM, so this won't override it
    if (auto* parent = getParent())
    {
        bool syncToHost = syncToHostParam && syncToHostParam->load() > 0.5f;
        if (syncToHost && !bpmFromCV && !timelineSyncActive)  // FIX: Only sync from host if BPM CV and Timeline sync are NOT active
        {
            // Pull tempo FROM host transport (Tempo Clock follows)
            bpm = (float)m_currentTransport.bpm;
            parent->setTempoControlledByModule(false);  // Not controlling
        }
        else if (!timelineSyncActive)  // Only push BPM if timeline sync didn't set it
        {
            // Push tempo TO host transport (Tempo Clock controls the global BPM)
            // This includes: manual BPM, BPM CV, tap tempo, and nudge
            // NOTE: Timeline sync (if active) has already set BPM, so this is skipped
            parent->setBPM(bpm);
            parent->setTempoControlledByModule(true);  // Controlling - UI should be greyed
        }
    }
    
    // Publish live telemetry AFTER all BPM sources resolved (including sync)
    setLiveParamValue("bpm_live", bpm);
    setLiveParamValue("swing_live", swing);

    // Compute outputs
    float* clockOut = out.getNumChannels() > 0 ? out.getWritePointer(0) : nullptr;
    float* beatTrig = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;
    float* barTrig  = out.getNumChannels() > 2 ? out.getWritePointer(2) : nullptr;
    float* beatGate = out.getNumChannels() > 3 ? out.getWritePointer(3) : nullptr;
    float* phaseOut = out.getNumChannels() > 4 ? out.getWritePointer(4) : nullptr;
    float* bpmOut   = out.getNumChannels() > 5 ? out.getWritePointer(5) : nullptr;
    float* downbeat = out.getNumChannels() > 6 ? out.getWritePointer(6) : nullptr;

    int divisionIdx = divisionParam ? (int)divisionParam->load() : 3; // default 1/4
    
    // Division Override: Broadcast local division to global transport OR clear it
    bool divisionOverride = divisionOverrideParam && divisionOverrideParam->load() > 0.5f;
    if (auto* parent = getParent())
    {
        if (divisionOverride)
        {
            // This clock becomes the master division source
            parent->setGlobalDivisionIndex(divisionIdx);
        }
        else
        {
            // Not overriding - clear the global division
            parent->setGlobalDivisionIndex(-1);
        }
    }
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
    if (paramId == paramIdBpmMod) { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdTapMod) { outChannelIndexInBus = 1; return true; }
    if (paramId == paramIdNudgeUpMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdNudgeDownMod) { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdPlayMod) { outChannelIndexInBus = 4; return true; }
    if (paramId == paramIdStopMod) { outChannelIndexInBus = 5; return true; }
    if (paramId == paramIdResetMod) { outChannelIndexInBus = 6; return true; }
    if (paramId == paramIdSwingMod) { outChannelIndexInBus = 7; return true; }
    return false;
}

#if defined(PRESET_CREATOR_UI)
void TempoClockModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4& sectionHeader = theme.modules.sequencer_section_header;
    const ImVec4& activeBeatColor = theme.modules.sequencer_step_active_frame;
    const ImVec4 inactiveBeatColor = style.Colors[ImGuiCol_Button];

    ImGui::PushItemWidth(itemWidth);
    
    // Helper for tooltips
    auto HelpMarkerClock = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    // === TEMPO CONTROLS SECTION ===
    ThemeText("Tempo", sectionHeader);
    ImGui::Spacing();

    // BPM slider with live display
    bool bpmMod = isParamInputConnected(paramIdBpmMod);  // FIX: Use isParamInputConnected, not isParamModulated
    float bpm = bpmMod ? getLiveParamValueFor(paramIdBpmMod, "bpm_live", bpmParam->load()) : bpmParam->load();
    bool syncToHost = syncToHostParam && syncToHostParam->load() > 0.5f;
    
    // Disable BPM control if synced to host
    if (bpmMod || syncToHost) { ImGui::BeginDisabled(); }
    if (ImGui::SliderFloat("BPM", &bpm, 20.0f, 300.0f, "%.1f"))
    {
        if (!bpmMod && !syncToHost)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdBpm))) *p = bpm;
        }
        onModificationEnded();
    }
    if (!bpmMod && !syncToHost) adjustParamOnWheel(apvts.getParameter(paramIdBpm), paramIdBpm, bpm);
    if (bpmMod) { ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    if (syncToHost) { ImGui::SameLine(); ThemeText("(synced)", theme.text.success); }
    if (bpmMod || syncToHost) { ImGui::EndDisabled(); }
    ImGui::SameLine();
    HelpMarkerClock("Beats per minute (20-300 BPM)\nDisabled when synced to host");
    
    // TAP TEMPO BUTTON
    if (bpmMod || syncToHost) { ImGui::BeginDisabled(); }
    if (ImGui::Button("TAP", ImVec2(itemWidth * 0.3f, 30)))
    {
        // Record tap timestamp (audio thread will detect the change)
        uiTapTimestamp.store(juce::Time::getMillisecondCounterHiRes() / 1000.0);
    }
    if (bpmMod || syncToHost) { ImGui::EndDisabled(); }
    ImGui::SameLine();
    HelpMarkerClock("Click repeatedly to set tempo by tapping\nTap at least twice to calculate BPM");

    // Swing
    bool swingM = isParamInputConnected(paramIdSwingMod);  // FIX: Use isParamInputConnected, not isParamModulated
    float swing = swingM ? getLiveParamValueFor(paramIdSwingMod, "swing_live", swingParam->load()) : swingParam->load();
    if (swingM) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Swing", &swing, 0.0f, 0.75f, "%.2f"))
    {
        if (!swingM)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdSwing))) *p = swing;
        }
        onModificationEnded();
    }
    if (!swingM) adjustParamOnWheel(apvts.getParameter(paramIdSwing), paramIdSwing, swing);
    if (swingM) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarkerClock("Swing amount (0-75%)\nDelays every other beat for shuffle feel");

    ImGui::Spacing();
    ImGui::Spacing();

    // === CLOCK OUTPUT SECTION ===
    ThemeText("Clock Output", sectionHeader);
    ImGui::Spacing();

    // Division + Gate width in-line
    int div = divisionParam ? (int)divisionParam->load() : 3;
    const char* items[] = { "1/32","1/16","1/8","1/4","1/2","1","2","4" };
    ImGui::SetNextItemWidth(itemWidth * 0.5f);
    if (ImGui::Combo("Division", &div, items, 8))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdDivision))) *p = div;
        onModificationEnded();
    }
    ImGui::SameLine();
    HelpMarkerClock("Clock output division\n1/4 = quarter notes, 1/16 = sixteenth notes");

    float gw = gateWidthParam ? gateWidthParam->load() : 0.5f;
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::SliderFloat("Gate Width", &gw, 0.01f, 0.99f, "%.2f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdGateWidth))) *p = gw;
        onModificationEnded();
    }
    ImGui::SameLine();
    HelpMarkerClock("Gate/trigger pulse width (1-99%)");

    ImGui::Spacing();
    ImGui::Spacing();

    // === LIVE CLOCK DISPLAY SECTION ===
    ThemeText("Clock Status", sectionHeader);
    ImGui::Spacing();

    // Animated beat indicator (4 boxes for 4/4 time)
    float phase = getLiveParamValue("phase_live", 0.0f);
    int currentBeat = (int)(phase * 4.0f) % 4;
    
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0) ImGui::SameLine();
        
        bool isCurrentBeat = (currentBeat == i);
        const ImVec4& beatColor = isCurrentBeat ? activeBeatColor : inactiveBeatColor;
        
        ImGui::PushStyleColor(ImGuiCol_Button, beatColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, beatColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, beatColor);
        ImGui::Button(juce::String(i + 1).toRawUTF8(), ImVec2(itemWidth * 0.23f, 30));
        ImGui::PopStyleColor(3);
    }

    // Current BPM display (large, colored)
    {
        juce::String bpmLabel = juce::String::formatted("♩ = %.1f BPM", getLiveParamValue("bpm_live", bpm));
        ThemeText(bpmLabel.toRawUTF8(), theme.text.active);
    }

    // Bar:Beat display
    int bar = (int)(phase / 4.0f) + 1;
    int beat = currentBeat + 1;
    ImGui::Text("Bar %d | Beat %d", bar, beat);

    ImGui::Spacing();
    ImGui::Spacing();

    // === TRANSPORT SYNC SECTION ===
    ThemeText("Transport Sync", sectionHeader);
    ImGui::Spacing();

    // Sync to Host checkbox
    bool sync = syncToHost;
    if (ImGui::Checkbox("Sync to Host", &sync))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdSyncToHost))) *p = sync;
        onModificationEnded();
    }
    ImGui::SameLine();
    HelpMarkerClock("Follow host transport tempo\nDisables manual BPM control when enabled");
    
    if (sync)
    {
        ThemeText("⚡ SYNCED TO HOST TRANSPORT", theme.text.success);
    }
    
    ImGui::Spacing();
    
    // Division Override checkbox
    bool divOverride = divisionOverrideParam && divisionOverrideParam->load() > 0.5f;
    if (ImGui::Checkbox("Division Override", &divOverride))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdDivisionOverride))) *p = divOverride;
        onModificationEnded();
    }
    ImGui::SameLine();
    HelpMarkerClock("Broadcast this clock's division globally\nForces all synced modules to follow this clock's subdivision");
    
    if (divOverride)
    {
        ThemeText("⚡ MASTER DIVISION SOURCE", theme.text.warning);
    }

    ImGui::Spacing();
    ImGui::Spacing();
    
    // === TIMELINE SYNC SECTION ===
    ThemeText("Timeline Sync", sectionHeader);
    ImGui::Spacing();
    
    // Sync to Timeline checkbox
    bool syncToTimeline = syncToTimelineParam && syncToTimelineParam->load() > 0.5f;
    if (ImGui::Checkbox("Sync to Timeline", &syncToTimeline))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdSyncToTimeline)))
            *p = syncToTimeline;
        onModificationEnded();
    }
    ImGui::SameLine();
    HelpMarkerClock("Sync transport position to a timeline source (SampleLoader/VideoLoader)\nTransport position follows the selected source");
    
    if (syncToTimeline)
    {
        // Timeline Source Dropdown
        auto* parent = getParent();
        juce::StringArray items;
        std::vector<juce::uint32> logicalIds;
        items.add("None");
        logicalIds.push_back(0); // "None" option
        
        if (parent)
        {
            // Get snapshot (thread-safe, lock-free)
            auto currentProcessors = parent->getActiveAudioProcessors();
            if (currentProcessors)
            {
                for (const auto& mod : *currentProcessors)
                {
                    if (mod && mod->canProvideTimeline())
                    {
                        double dur = mod->getTimelineDurationSeconds();
                        bool active = mod->isTimelineActive();
                        juce::uint32 id = mod->getLogicalId();
                        
                        // Build display name
                        juce::String displayName = mod->getName() + " #" + juce::String(id);
                        if (dur > 0.0)
                            displayName += " (" + juce::String(dur, 1) + "s)";
                        if (active)
                            displayName += " [Active]";
                        
                        items.add(displayName);
                        logicalIds.push_back(id);
                    }
                }
            }
        }
        
        // Find current selection index
        int currentSelection = 0;
        juce::uint32 currentId = timelineSourceIdParam ? (juce::uint32)timelineSourceIdParam->load() : 0;
        for (size_t i = 0; i < logicalIds.size(); ++i)
        {
            if (logicalIds[i] == currentId)
            {
                currentSelection = (int)i;
                break;
            }
        }
        
        // Display dropdown
        // Convert StringArray to const char* array for ImGui
        std::vector<const char*> itemsCStr;
        itemsCStr.reserve(items.size());
        std::vector<juce::String> itemsStorage; // Keep strings alive
        itemsStorage.reserve(items.size());
        for (const auto& item : items)
        {
            itemsStorage.push_back(item);
            itemsCStr.push_back(itemsStorage.back().toRawUTF8());
        }
        
        ImGui::SetNextItemWidth(itemWidth);
        if (ImGui::Combo("Timeline Source", &currentSelection, itemsCStr.data(), (int)itemsCStr.size()))
        {
            if (currentSelection >= 0 && currentSelection < (int)logicalIds.size())
            {
                juce::uint32 selectedId = logicalIds[currentSelection];
                if (auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(paramIdTimelineSourceId)))
                    *p = (int)selectedId;
                onModificationEnded();
            }
        }
        ImGui::SameLine();
        HelpMarkerClock("Select which timeline source to sync to\nOnly modules with loaded media appear here");
        
        // BPM Derivation controls (only show if timeline sync is enabled)
        bool enableBPMDeriv = enableBPMDerivationParam && enableBPMDerivationParam->load() > 0.5f;
        if (ImGui::Checkbox("Derive BPM from Timeline", &enableBPMDeriv))
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdEnableBPMDerivation)))
                *p = enableBPMDeriv;
            onModificationEnded();
        }
        ImGui::SameLine();
        HelpMarkerClock("Calculate BPM from timeline duration\nBPM = (beats per timeline * 60) / duration");
        
        if (enableBPMDeriv)
        {
            float beatsPerTimeline = beatsPerTimelineParam ? beatsPerTimelineParam->load() : 4.0f;
            if (ImGui::SliderFloat("Beats per Timeline", &beatsPerTimeline, 1.0f, 32.0f, "%.1f"))
            {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdBeatsPerTimeline)))
                    *p = beatsPerTimeline;
                onModificationEnded();
            }
            ImGui::SameLine();
            HelpMarkerClock("Number of beats in the timeline loop\nUsed to calculate BPM from timeline duration");
        }
        
        // Status indicator
        if (currentId > 0)
        {
            bool sourceFound = false;
            bool sourceActive = false;
            if (parent)
            {
                auto currentProcessors = parent->getActiveAudioProcessors();
                if (currentProcessors)
                {
                    for (const auto& mod : *currentProcessors)
                    {
                        if (mod && mod->getLogicalId() == currentId)
                        {
                            sourceFound = true;
                            if (mod->canProvideTimeline())
                                sourceActive = mod->isTimelineActive();
                            break;
                        }
                    }
                }
            }
            
            if (sourceFound && sourceActive)
            {
                ThemeText("⚡ SYNCED TO TIMELINE", theme.text.success);
            }
            else if (sourceFound && !sourceActive)
            {
                ThemeText("⚠ Timeline source inactive", theme.text.warning);
            }
            else
            {
                ThemeText("⚠ Timeline source not found", theme.text.error);
            }
        }
    }

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


