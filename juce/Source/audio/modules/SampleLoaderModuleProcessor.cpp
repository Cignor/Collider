#include "SampleLoaderModuleProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../utils/RtLogger.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

SampleLoaderModuleProcessor::SampleLoaderModuleProcessor()
    : ModuleProcessor(BusesProperties()
        .withInput("Playback Mods", juce::AudioChannelSet::discreteChannels(2), true)  // Bus 0: Pitch, Speed (flat ch 0-1)
        .withInput("Control Mods", juce::AudioChannelSet::discreteChannels(2), true)   // Bus 1: Gate, Trigger (flat ch 2-3)
        .withInput("Range Mods", juce::AudioChannelSet::discreteChannels(2), true)     // Bus 2: Range Start, Range End (flat ch 4-5)
        .withInput("Randomize", juce::AudioChannelSet::discreteChannels(1), true)      // Bus 3: Randomize (flat ch 6)
        .withOutput("Audio Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SampleLoaderParameters", createParameterLayout())
{
    // Parameter references will be obtained when needed
    // Initialize output value tracking for cable inspector (stereo)
    lastOutputValues.clear();
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    
    // Initialize parameter pointers
    rangeStartParam = apvts.getRawParameterValue("rangeStart");
    rangeEndParam = apvts.getRawParameterValue("rangeEnd");
    rangeStartModParam = apvts.getRawParameterValue("rangeStart_mod");
    rangeEndModParam = apvts.getRawParameterValue("rangeEnd_mod");
}



juce::AudioProcessorValueTreeState::ParameterLayout SampleLoaderModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    
    // --- Basic Playback Parameters ---
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "speed", "Speed", 0.25f, 4.0f, 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitch", "Pitch (semitones)", -24.0f, 24.0f, 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gate", "Gate", 0.0f, 1.0f, 0.8f));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        "engine", "Engine", juce::StringArray { "RubberBand", "Naive" }, 1));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        "rbWindowShort", "RB Window Short", true));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        "rbPhaseInd", "RB Phase Independent", true));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
         "loop", "Loop", false));
    
    // (Removed legacy SoundTouch tuning parameters)

    // --- New Modulation Inputs (absolute control) ---
    // These live in APVTS and are fed by modulation cables; they override UI when connected.
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "pitch_mod", "Pitch Mod", -24.0f, 24.0f, 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "speed_mod", "Speed Mod", 0.25f, 4.0f, 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gate_mod", "Gate Mod", 0.0f, 1.0f, 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "trigger_mod", "Trigger Mod", 0.0f, 1.0f, 0.0f));
    
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rangeStart_mod", "Range Start Mod", 0.0f, 1.0f, 0.0f));

    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rangeEnd_mod", "Range End Mod", 0.0f, 1.0f, 1.0f));
    
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rangeStart", "Range Start", 
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rangeEnd", "Range End", 
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    
    return { parameters.begin(), parameters.end() };
}

void SampleLoaderModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    juce::Logger::writeToLog("[Sample Loader] prepareToPlay sr=" + juce::String(sampleRate) + ", block=" + juce::String(samplesPerBlock));
    
    // DEBUG: Check bus enablement status BEFORE forcing
    juce::String busStatusBefore = "[Sample Loader] Bus Status BEFORE: ";
    for (int i = 0; i < getBusCount(true); ++i)
    {
        auto* bus = getBus(true, i);
        if (bus)
            busStatusBefore += "In" + juce::String(i) + "=" + (bus->isEnabled() ? "ON" : "OFF") + "(" + juce::String(bus->getNumberOfChannels()) + "ch) ";
    }
    juce::Logger::writeToLog(busStatusBefore);
    
    // FORCE ENABLE ALL INPUT BUSES (AudioProcessorGraph might disable them)
    for (int i = 0; i < getBusCount(true); ++i)
    {
        if (auto* bus = getBus(true, i))
        {
            if (!bus->isEnabled())
            {
                enableAllBuses(); // Try to enable all
                juce::Logger::writeToLog("[Sample Loader] Forced all buses ON!");
                break;
            }
        }
    }
    
    // DEBUG: Check bus enablement status AFTER forcing
    juce::String busStatusAfter = "[Sample Loader] Bus Status AFTER: ";
    for (int i = 0; i < getBusCount(true); ++i)
    {
        auto* bus = getBus(true, i);
        if (bus)
            busStatusAfter += "In" + juce::String(i) + "=" + (bus->isEnabled() ? "ON" : "OFF") + "(" + juce::String(bus->getNumberOfChannels()) + "ch) ";
    }
    juce::Logger::writeToLog(busStatusAfter);
    
    // Auto-load sample from saved state if available
    if (currentSample == nullptr)
    {
        const auto savedPath = apvts.state.getProperty ("samplePath").toString();
        if (savedPath.isNotEmpty())
        {
            currentSamplePath = savedPath;
            loadSample (juce::File (currentSamplePath));
        }
    }
    // Create sample processor if we have a sample loaded
    if (currentSample != nullptr)
    {
        createSampleProcessor();
    }
}

void SampleLoaderModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // Get OUTPUT bus, but do NOT clear here.
    // Clearing at the start can zero input buses when buffers are aliased in AudioProcessorGraph.
    auto outBus = getBusBuffer(buffer, false, 0);
    
    // --- Setup and Safety Checks ---
    if (auto* pending = newSampleProcessor.exchange(nullptr))
    {
        const juce::ScopedLock lock(processorSwapLock);
        processorToDelete = std::move(sampleProcessor);
        sampleProcessor.reset(pending);
    }
    SampleVoiceProcessor* currentProcessor = nullptr;
    {
        const juce::ScopedLock lock(processorSwapLock);
        currentProcessor = sampleProcessor.get();
    }
    if (currentProcessor == nullptr || currentSample == nullptr)
    {
        outBus.clear();
        return;
    }
    
    // DEBUG: Check MAIN buffer first (before splitting into buses)
    static int rawDebugCounter = 0;
    if (rawDebugCounter == 0 || rawDebugCounter % 240 == 0)
    {
        juce::String mainMsg = "[Sample MAIN Buffer #" + juce::String(rawDebugCounter) + "] ";
        mainMsg += "totalCh=" + juce::String(buffer.getNumChannels()) + " samples=" + juce::String(buffer.getNumSamples()) + " | ";
        
        // Check if buffer channels have valid pointers
        bool hasData = false;
        for (int ch = 0; ch < juce::jmin(7, buffer.getNumChannels()); ++ch)
        {
            float val = buffer.getSample(ch, 0);
            mainMsg += "ch" + juce::String(ch) + "=" + juce::String(val, 3) + " ";
            if (std::abs(val) > 0.001f) hasData = true;
        }
        
        mainMsg += "| hasData=" + juce::String(hasData ? "YES" : "NO");
        
        // Check channel pointer validity
        mainMsg += " | ptrs: ";
        for (int ch = 0; ch < juce::jmin(3, buffer.getNumChannels()); ++ch)
        {
            const float* ptr = buffer.getReadPointer(ch);
            mainMsg += "ch" + juce::String(ch) + "=" + (ptr ? "OK" : "NULL") + " ";
        }
        juce::Logger::writeToLog(mainMsg);
    }
    
    // Multi-bus input architecture (like TTS Performer)
    auto playbackBus = getBusBuffer(buffer, true, 0);  // Bus 0: Pitch, Speed (flat ch 0-1)
    auto controlBus = getBusBuffer(buffer, true, 1);   // Bus 1: Gate, Trigger (flat ch 2-3)
    auto rangeBus = getBusBuffer(buffer, true, 2);     // Bus 2: Range Start, Range End (flat ch 4-5)
    auto randomizeBus = getBusBuffer(buffer, true, 3); // Bus 3: Randomize (flat ch 6)
    
    // DEBUG: Check buses after splitting
    if (rawDebugCounter == 0 || rawDebugCounter % 240 == 0)
    {
        juce::String busMsg = "[Sample Buses #" + juce::String(rawDebugCounter) + "] ";
        busMsg += "playback=" + juce::String(playbackBus.getNumChannels()) + " ";
        busMsg += "control=" + juce::String(controlBus.getNumChannels()) + " ";
        busMsg += "range=" + juce::String(rangeBus.getNumChannels()) + " ";
        busMsg += "randomize=" + juce::String(randomizeBus.getNumChannels()) + " | ";
        if (playbackBus.getNumChannels() > 0) busMsg += "pitch=" + juce::String(playbackBus.getSample(0, 0), 3) + " ";
        if (playbackBus.getNumChannels() > 1) busMsg += "speed=" + juce::String(playbackBus.getSample(1, 0), 3) + " ";
        if (controlBus.getNumChannels() > 0) busMsg += "gate=" + juce::String(controlBus.getSample(0, 0), 3) + " ";
        juce::Logger::writeToLog(busMsg);
    }
    rawDebugCounter++;
    
    const int numSamples = buffer.getNumSamples();

    // --- Compute block-rate CV-mapped values for telemetry (even when not playing) ---
    const float baseSpeed = apvts.getRawParameterValue("speed")->load();
    float speedNow = baseSpeed;
    if (isParamInputConnected("speed_mod") && playbackBus.getNumChannels() > 1)
    {
        const float cv = juce::jlimit(0.0f, 1.0f, playbackBus.getReadPointer(1)[0]);
        const float octaveRange = 4.0f;
        const float octaveOffset = (cv - 0.5f) * octaveRange;
        speedNow = juce::jlimit(0.25f, 4.0f, baseSpeed * std::pow(2.0f, octaveOffset));
    }

    const float basePitch = apvts.getRawParameterValue("pitch")->load();
    float pitchNow = basePitch;
    if (isParamInputConnected("pitch_mod") && playbackBus.getNumChannels() > 0)
    {
        const float rawCV = playbackBus.getReadPointer(0)[0];
        const float bipolarCV = (rawCV >= 0.0f && rawCV <= 1.0f) ? (rawCV * 2.0f - 1.0f) : rawCV;
        const float pitchModulationRange = 24.0f; 
        pitchNow = juce::jlimit(-24.0f, 24.0f, basePitch + (bipolarCV * pitchModulationRange));
    }

    float startNorm = rangeStartParam->load();
    if (isParamInputConnected("rangeStart_mod") && rangeBus.getNumChannels() > 0)
        startNorm = juce::jlimit(0.0f, 1.0f, rangeBus.getReadPointer(0)[0]);

    float endNorm = rangeEndParam->load();
    if (isParamInputConnected("rangeEnd_mod") && rangeBus.getNumChannels() > 1)
        endNorm = juce::jlimit(0.0f, 1.0f, rangeBus.getReadPointer(1)[0]);

    // Ensure valid range window
    {
        const float minGap = 0.001f;
        if (startNorm >= endNorm)
        {
            const float midpoint = (startNorm + endNorm) * 0.5f;
            startNorm = juce::jlimit(0.0f, 1.0f - minGap, midpoint - minGap * 0.5f);
            endNorm   = juce::jlimit(minGap, 1.0f, startNorm + minGap);
        }
    }

    // Update live telemetry regardless of play state (matches TTS pattern)
    setLiveParamValue("speed_live", speedNow);
    setLiveParamValue("pitch_live", pitchNow);
    setLiveParamValue("rangeStart_live", startNorm);
    setLiveParamValue("rangeEnd_live", endNorm);
    // Gate live (use first sample if CV present, otherwise knob)
    if (isParamInputConnected("gate_mod") && controlBus.getNumChannels() > 0)
    {
        const float g = juce::jlimit(0.0f, 1.0f, controlBus.getReadPointer(0)[0]);
        setLiveParamValue("gate_live", g);
    }
    else
    {
        setLiveParamValue("gate_live", apvts.getRawParameterValue("gate")->load());
    }

    // --- 1. TRIGGER DETECTION ---
    const bool looping = apvts.getRawParameterValue("loop")->load() > 0.5f;
    
    // If loop is enabled and not playing, start playing
    if (looping && !currentProcessor->isPlaying)
    {
        currentProcessor->reset();
    }
    
    // Check for a rising edge on the trigger input to start playback.
    if (isParamInputConnected("trigger_mod") && controlBus.getNumChannels() > 1)
    {
        const float* trigSignal = controlBus.getReadPointer(1);  // Control Bus Channel 1 = Trigger
        for (int i = 0; i < numSamples; ++i)
        {
            const bool trigHigh = trigSignal[i] > 0.5f;
            if (trigHigh && !lastTriggerHigh)
            {
                reset(); // This now sets the internal voice's isPlaying to true
                break;
            }
            lastTriggerHigh = trigHigh;
        }
        if (numSamples > 0) lastTriggerHigh = (controlBus.getReadPointer(1)[numSamples - 1] > 0.5f);
    }

    // --- Randomize Trigger ---
    if (isParamInputConnected("randomize_mod") && randomizeBus.getNumChannels() > 0)
    {
        const float* randTrigSignal = randomizeBus.getReadPointer(0);  // Randomize Bus Channel 0
        for (int i = 0; i < numSamples; ++i)
        {
            const bool trigHigh = randTrigSignal[i] > 0.5f;
            if (trigHigh && !lastRandomizeTriggerHigh)
            {
                randomizeSample(); // Call the existing randomize function
                break; // Only randomize once per block
            }
            lastRandomizeTriggerHigh = trigHigh;
        }
        if (numSamples > 0) lastRandomizeTriggerHigh = (randomizeBus.getReadPointer(0)[numSamples - 1] > 0.5f);
    }

    // --- 2. CONDITIONAL AUDIO RENDERING ---
    // Only generate audio if the internal voice is in a playing state.
    if (currentProcessor->isPlaying)
    {
        // DEBUG: Log CV values from buses (like TTS)
        static int debugFrameCounter = 0;
        if (debugFrameCounter == 0 || debugFrameCounter % 240 == 0)
        {
            juce::String dbgMsg = "[Sample CV Debug #" + juce::String(debugFrameCounter) + "] ";
            if (playbackBus.getNumChannels() > 0) dbgMsg += "pitch_cv=" + juce::String(playbackBus.getReadPointer(0)[0], 3) + " ";
            if (playbackBus.getNumChannels() > 1) dbgMsg += "speed_cv=" + juce::String(playbackBus.getReadPointer(1)[0], 3) + " ";
            if (controlBus.getNumChannels() > 0) dbgMsg += "gate_cv=" + juce::String(controlBus.getReadPointer(0)[0], 3) + " ";
            juce::Logger::writeToLog(dbgMsg);
        }
        debugFrameCounter++;
        
        currentProcessor->setZoneTimeStretchRatio(speedNow);
        currentProcessor->setBasePitchSemitones(pitchNow);
        const int sourceLength = currentSample->stereo.getNumSamples();
        currentProcessor->setPlaybackRange(startNorm * sourceLength, endNorm * sourceLength);

        // Update APVTS parameters for UI feedback (especially spectrogram handles)
        *rangeStartParam = startNorm;
        apvts.getParameter("rangeStart")->sendValueChangedMessageToListeners(startNorm);
        *rangeEndParam = endNorm;
        apvts.getParameter("rangeEnd")->sendValueChangedMessageToListeners(endNorm);

        const int engineIdx = (int) apvts.getRawParameterValue("engine")->load();
        currentProcessor->setEngine(engineIdx == 0 ? SampleVoiceProcessor::Engine::RubberBand : SampleVoiceProcessor::Engine::Naive);
        currentProcessor->setRubberBandOptions(apvts.getRawParameterValue("rbWindowShort")->load() > 0.5f, apvts.getRawParameterValue("rbPhaseInd")->load() > 0.5f);
        currentProcessor->setLooping(apvts.getRawParameterValue("loop")->load() > 0.5f);

        // Generate the sample's audio into the OUTPUT buffer. This might set isPlaying to false if the sample ends.
        try {
            // Create a temporary buffer view for just the output bus
            juce::AudioBuffer<float> outputBuffer(outBus.getArrayOfWritePointers(), 
                                                   outBus.getNumChannels(), 
                                                   outBus.getNumSamples());
            currentProcessor->renderBlock(outputBuffer, midiMessages);
        } catch (...) {
            RtLogger::postf("[SampleLoader][FATAL] renderBlock exception");
            outBus.clear();
        }

        // --- 3. GATE (VCA) APPLICATION ---
        // If a gate is connected, use it to shape the volume of the audio we just generated.
        float lastGateValue = 1.0f;
        if (isParamInputConnected("gate_mod") && controlBus.getNumChannels() > 0)
        {
            const float* gateCV = controlBus.getReadPointer(0);  // Control Bus Channel 0 = Gate
            for (int ch = 0; ch < outBus.getNumChannels(); ++ch)
            {
                float* channelData = outBus.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float gateValue = juce::jlimit(0.0f, 1.0f, gateCV[i]);
                    channelData[i] *= gateValue;
                    
                    // Update telemetry (throttled every 64 samples, only once per channel)
                    if (ch == 0 && (i & 0x3F) == 0)
                    {
                        setLiveParamValue("gate_live", gateValue);
                        lastGateValue = gateValue;
                    }
                }
            }
        }
        else
        {
            // No gate modulation - use static gate knob value
            lastGateValue = apvts.getRawParameterValue("gate")->load();
            setLiveParamValue("gate_live", lastGateValue);
        }
        
        // Apply main gate knob last
        outBus.applyGain(apvts.getRawParameterValue("gate")->load());
    }
    else
    {
        // Not playing: explicitly clear output now (safe after input analysis)
        outBus.clear();
    }
    
    // Update output values for cable inspector using block peak
    if (lastOutputValues.size() >= 2)
    {
        auto peakAbs = [&](int ch){ if (ch >= outBus.getNumChannels()) return 0.0f; const float* p = outBus.getReadPointer(ch); float m=0.0f; for (int i=0;i<outBus.getNumSamples();++i) m = juce::jmax(m, std::abs(p[i])); return m; };
        if (lastOutputValues[0]) lastOutputValues[0]->store(peakAbs(0));
        if (lastOutputValues[1]) lastOutputValues[1]->store(peakAbs(1));
    }
}

void SampleLoaderModuleProcessor::reset()
{
    if (sampleProcessor != nullptr)
    {
        sampleProcessor->reset();
    }
    
    if (currentSample != nullptr && rangeStartParam != nullptr)
    {
        readPosition = rangeStartParam->load() * currentSample->stereo.getNumSamples();
    }
    else
    {
        readPosition = 0.0;
    }
}

void SampleLoaderModuleProcessor::loadSample(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        DBG("[Sample Loader] File does not exist: " + file.getFullPathName());
        return;
    }

    // 1) Load the original shared sample from the bank
    SampleBank sampleBank;
    std::shared_ptr<SampleBank::Sample> original;
    try {
        original = sampleBank.getOrLoad(file);
    } catch (...) {
        DBG("[Sample Loader][FATAL] Exception in SampleBank::getOrLoad");
        return;
    }
    if (original == nullptr || original->stereo.getNumSamples() <= 0)
    {
        DBG("[Sample Loader] Failed to load sample or empty: " + file.getFullPathName());
        return;
    }

    currentSampleName = file.getFileName();
    currentSamplePath = file.getFullPathName();
    apvts.state.setProperty ("samplePath", currentSamplePath, nullptr);

    // --- THIS IS THE FIX ---
    // Store the sample's metadata in our new member variables.
    sampleDurationSeconds = (double)original->stereo.getNumSamples() / original->sampleRate;
    sampleSampleRate = (int)original->sampleRate;
    // --- END OF FIX ---

    // 2) Create a private STEREO copy (preserve stereo or duplicate mono)
    auto privateCopy = std::make_shared<SampleBank::Sample>();
    privateCopy->sampleRate = original->sampleRate;
    const int numSamples = original->stereo.getNumSamples();
    privateCopy->stereo.setSize(2, numSamples); // Always stereo output

    if (original->stereo.getNumChannels() <= 1)
    {
        // Mono source: duplicate to both L and R channels
        privateCopy->stereo.copyFrom(0, 0, original->stereo, 0, 0, numSamples); // L = Mono
        privateCopy->stereo.copyFrom(1, 0, original->stereo, 0, 0, numSamples); // R = Mono
        DBG("[Sample Loader] Loaded mono sample and duplicated to stereo: " << file.getFileName());
    }
    else
    {
        // Stereo (or multi-channel) source: copy L and R channels
        privateCopy->stereo.copyFrom(0, 0, original->stereo, 0, 0, numSamples); // L channel
        privateCopy->stereo.copyFrom(1, 0, original->stereo, 1, 0, numSamples); // R channel
        DBG("[Sample Loader] Loaded stereo sample: " << file.getFileName());
    }

    // 3) Atomically assign our private copy for this module
    currentSample = privateCopy;
    generateSpectrogram();

    // 4) If the module is prepared, stage a new processor
    if (getSampleRate() > 0.0 && getBlockSize() > 0)
    {
        createSampleProcessor();
    }
    else
    {
        DBG("[Sample Loader][Defer] Module not prepared yet; will create processor in prepareToPlay");
    }
}
void SampleLoaderModuleProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree vt ("SampleLoader");
    vt.setProperty ("samplePath", currentSamplePath, nullptr);
    vt.setProperty ("speed", apvts.getRawParameterValue("speed")->load(), nullptr);
    vt.setProperty ("pitch", apvts.getRawParameterValue("pitch")->load(), nullptr);
    vt.setProperty ("gate", apvts.getRawParameterValue("gate")->load(), nullptr);
    vt.setProperty ("engine", (int) apvts.getRawParameterValue("engine")->load(), nullptr);
    vt.setProperty ("rbWindowShort", apvts.getRawParameterValue("rbWindowShort")->load() > 0.5f, nullptr);
    vt.setProperty ("rbPhaseInd", apvts.getRawParameterValue("rbPhaseInd")->load() > 0.5f, nullptr);
    vt.setProperty ("loop", apvts.getRawParameterValue("loop")->load() > 0.5f, nullptr);
    if (auto xml = vt.createXml())
        copyXmlToBinary (*xml, destData);
}

void SampleLoaderModuleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (! xml) return;
    juce::ValueTree vt = juce::ValueTree::fromXml (*xml);
    if (! vt.isValid()) return;
    currentSamplePath = vt.getProperty ("samplePath").toString();
    if (currentSamplePath.isNotEmpty())
        loadSample (juce::File (currentSamplePath));
    if (auto* p = apvts.getParameter ("speed"))
        p->setValueNotifyingHost (apvts.getParameterRange("speed").convertTo0to1 ((float) vt.getProperty ("speed", 1.0f)));
    if (auto* p = apvts.getParameter ("pitch"))
        p->setValueNotifyingHost (apvts.getParameterRange("pitch").convertTo0to1 ((float) vt.getProperty ("pitch", 0.0f)));
    if (auto* p = apvts.getParameter ("gate"))
        p->setValueNotifyingHost (apvts.getParameterRange("gate").convertTo0to1 ((float) vt.getProperty ("gate", 0.8f)));
    if (auto* p = apvts.getParameter ("engine"))
        p->setValueNotifyingHost ((float) (int) vt.getProperty ("engine", 0));
    if (auto* p = apvts.getParameter ("rbWindowShort"))
        p->setValueNotifyingHost ((bool) vt.getProperty ("rbWindowShort", true) ? 1.0f : 0.0f);
    if (auto* p = apvts.getParameter ("rbPhaseInd"))
        p->setValueNotifyingHost ((bool) vt.getProperty ("rbPhaseInd", true) ? 1.0f : 0.0f);
    if (auto* p = apvts.getParameter ("loop"))
        p->setValueNotifyingHost ((bool) vt.getProperty ("loop", false) ? 1.0f : 0.0f);
}

void SampleLoaderModuleProcessor::loadSample(const juce::String& filePath)
{
    loadSample(juce::File(filePath));
}

juce::String SampleLoaderModuleProcessor::getCurrentSampleName() const
{
    return currentSampleName;
}

bool SampleLoaderModuleProcessor::hasSampleLoaded() const
{
    return currentSample != nullptr;
}

// Legacy SoundTouch setters removed

void SampleLoaderModuleProcessor::setDebugOutput(bool enabled)
{
    debugOutput = enabled;
}

void SampleLoaderModuleProcessor::logCurrentSettings() const
{
    if (debugOutput)
    {
        DBG("[Sample Loader] Current Settings:");
        DBG("  Sample: " + currentSampleName);
        DBG("  Speed: " + juce::String(apvts.getRawParameterValue("speed")->load()));
        DBG("  Pitch: " + juce::String(apvts.getRawParameterValue("pitch")->load()));
    }
}

void SampleLoaderModuleProcessor::updateSoundTouchSettings() {}

void SampleLoaderModuleProcessor::randomizeSample()
{
    if (currentSamplePath.isEmpty())
        return;
        
    juce::File currentFile(currentSamplePath);
    juce::File parentDir = currentFile.getParentDirectory();
    
    if (!parentDir.exists() || !parentDir.isDirectory())
        return;
        
    // Get all audio files in the same directory
    juce::Array<juce::File> audioFiles;
    parentDir.findChildFiles(audioFiles, juce::File::findFiles, true, "*.wav;*.mp3;*.flac;*.aiff;*.ogg");
    
    if (audioFiles.size() <= 1)
        return;
        
    // Remove current file from the list
    for (int i = audioFiles.size() - 1; i >= 0; --i)
    {
        if (audioFiles[i].getFullPathName() == currentSamplePath)
        {
            audioFiles.remove(i);
            break;
        }
    }
    
    if (audioFiles.isEmpty())
        return;
        
    // Pick a random file
    juce::Random rng(juce::Time::getMillisecondCounterHiRes());
    juce::File randomFile = audioFiles[rng.nextInt(audioFiles.size())];
    
    DBG("[Sample Loader] Randomizing to: " + randomFile.getFullPathName());
    loadSample(randomFile);
}

void SampleLoaderModuleProcessor::createSampleProcessor()
{
    if (currentSample == nullptr)
    {
        return;
    }
    // Guard against double-creation and race with audio thread: build new then swap under lock
    auto newProcessor = std::make_unique<SampleVoiceProcessor>(currentSample);
    
    // Set up the sample processor
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const int bs = getBlockSize() > 0 ? getBlockSize() : 512;
    newProcessor->prepareToPlay(sr, bs);
    
    // --- Set initial playback range ---
    const float startNorm = rangeStartParam->load();
    const float endNorm = rangeEndParam->load();
    const double startSample = startNorm * currentSample->stereo.getNumSamples();
    const double endSample = endNorm * currentSample->stereo.getNumSamples();
    newProcessor->setPlaybackRange(startSample, endSample);
    newProcessor->resetPosition(); // Reset position without starting playback - wait for trigger
    
    // Set parameters from our APVTS
    newProcessor->setZoneTimeStretchRatio(apvts.getRawParameterValue("speed")->load());
    newProcessor->setBasePitchSemitones(apvts.getRawParameterValue("pitch")->load());
    newSampleProcessor.store(newProcessor.release());
    DBG("[Sample Loader] Staged new sample processor for: " << currentSampleName);
    
    DBG("[Sample Loader] Created sample processor for: " + currentSampleName);
}

void SampleLoaderModuleProcessor::generateSpectrogram()
{
    const juce::ScopedLock lock(imageLock);
    spectrogramImage = juce::Image(); // Clear previous image

    if (currentSample == nullptr || currentSample->stereo.getNumSamples() == 0)
        return;

    const int fftOrder = 10;
    const int fftSize = 1 << fftOrder;
    const int hopSize = fftSize / 4;
    const int numHops = (currentSample->stereo.getNumSamples() - fftSize) / hopSize;

    if (numHops <= 0) return;

    // Create a mono version for analysis if necessary
    juce::AudioBuffer<float> monoBuffer;
    if (currentSample->stereo.getNumChannels() > 1)
    {
        monoBuffer.setSize(1, currentSample->stereo.getNumSamples());
        monoBuffer.copyFrom(0, 0, currentSample->stereo, 0, 0, currentSample->stereo.getNumSamples());
        monoBuffer.addFrom(0, 0, currentSample->stereo, 1, 0, currentSample->stereo.getNumSamples(), 0.5f);
        monoBuffer.applyGain(0.5f);
    }
    const float* audioData = (currentSample->stereo.getNumChannels() > 1) ? monoBuffer.getReadPointer(0) : currentSample->stereo.getReadPointer(0);

    // Use RGB so JUCE's OpenGLTexture uploads with expected format
    spectrogramImage = juce::Image(juce::Image::RGB, numHops, fftSize / 2, true);
    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> fftData(fftSize * 2);

    for (int i = 0; i < numHops; ++i)
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        memcpy(fftData.data(), audioData + (i * hopSize), fftSize * sizeof(float));

        window.multiplyWithWindowingTable(fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(fftData.data());

        for (int j = 0; j < fftSize / 2; ++j)
        {
            const float db = juce::Decibels::gainToDecibels(juce::jmax(fftData[j], 1.0e-9f), -100.0f);
            float level = juce::jmap(db, -100.0f, 0.0f, 0.0f, 1.0f);
            level = juce::jlimit(0.0f, 1.0f, level);
            spectrogramImage.setPixelAt(i, (fftSize / 2) - 1 - j, juce::Colour::fromFloatRGBA(level, level, level, 1.0f));
        }
    }
}

#if defined(PRESET_CREATOR_UI)
void SampleLoaderModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    // --- THIS IS THE DEFINITIVE FIX ---
    // 1. Draw all the parameter sliders and buttons FIRST.
    ImGui::PushItemWidth(itemWidth);

    if (ImGui::Button("Load Sample", ImVec2(itemWidth * 0.48f, 0)))
    {
        juce::File startDir;
        {
            auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            auto dir = appFile.getParentDirectory();
            for (int i = 0; i < 8 && dir.exists(); ++i)
            {
                auto candidate = dir.getSiblingFile("audio").getChildFile("samples");
                if (candidate.exists() && candidate.isDirectory()) { startDir = candidate; break; }
                dir = dir.getParentDirectory();
            }
        }
        if (! startDir.exists()) startDir = juce::File();
        fileChooser = std::make_unique<juce::FileChooser>("Select Audio Sample", startDir, "*.wav;*.mp3;*.flac;*.aiff;*.ogg");
        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
        {
            try {
                auto file = fc.getResult();
                if (file != juce::File{})
                {
                    juce::Logger::writeToLog("[Sample Loader] User selected file: " + file.getFullPathName());
                    loadSample(file);
                }
            } catch (...) {
                juce::Logger::writeToLog("[Sample Loader][FATAL] Exception during file chooser callback");
            }
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Random", ImVec2(itemWidth * 0.48f, 0))) { randomizeSample(); }

    // Range selection is now handled by the interactive spectrogram in the UI component

    ImGui::Spacing();
    // Main parameters in compact layout
    bool speedModulated = isParamModulated("speed_mod");
    if (speedModulated) { ImGui::BeginDisabled(); ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 0.0f, 0.3f)); }
    float speed = speedModulated ? getLiveParamValueFor("speed_mod", "speed_live", apvts.getRawParameterValue("speed")->load()) 
                                 : apvts.getRawParameterValue("speed")->load();
    if (ImGui::SliderFloat("Speed", &speed, 0.25f, 4.0f, "%.2fx"))
    {
        apvts.getParameter("speed")->setValueNotifyingHost(apvts.getParameterRange("speed").convertTo0to1(speed));
        onModificationEnded();
    }
    if (! speedModulated)
        ModuleProcessor::adjustParamOnWheel(apvts.getParameter("speed"), "speed", speed);
    if (speedModulated) { ImGui::PopStyleColor(); ImGui::EndDisabled(); }
    
    bool pitchModulated = isParamModulated("pitch_mod");
    if (pitchModulated) { ImGui::BeginDisabled(); ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 0.0f, 0.3f)); }
    float pitch = pitchModulated ? getLiveParamValueFor("pitch_mod", "pitch_live", apvts.getRawParameterValue("pitch")->load()) 
                                 : apvts.getRawParameterValue("pitch")->load();
    if (ImGui::SliderFloat("Pitch", &pitch, -24.0f, 24.0f, "%.1f st"))
    {
        apvts.getParameter("pitch")->setValueNotifyingHost(apvts.getParameterRange("pitch").convertTo0to1(pitch));
        onModificationEnded();
    }
    if (! pitchModulated)
        ModuleProcessor::adjustParamOnWheel(apvts.getParameter("pitch"), "pitch", pitch);
    if (pitchModulated) { ImGui::PopStyleColor(); ImGui::EndDisabled(); }
    
    // --- Gate slider (formerly volume) ---
    bool gateModulated = isParamModulated("gate_mod"); 
    if (gateModulated) { ImGui::BeginDisabled(); ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 0.0f, 0.3f)); }
    float gate = gateModulated ? getLiveParamValueFor("gate_mod", "gate_live", apvts.getRawParameterValue("gate")->load())
                               : apvts.getRawParameterValue("gate")->load();
    if (ImGui::SliderFloat("Gate", &gate, 0.0f, 1.0f, "%.2f"))
    {
        if (!gateModulated) {
            apvts.getParameter("gate")->setValueNotifyingHost(apvts.getParameterRange("gate").convertTo0to1(gate));
            onModificationEnded();
        }
    }
    if (!gateModulated)
        ModuleProcessor::adjustParamOnWheel(apvts.getParameter("gate"), "gate", gate);
    if (gateModulated) { ImGui::PopStyleColor(); ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    // Range parameters with live modulation feedback
    bool rangeStartModulated = isParamModulated("rangeStart_mod");
    if (rangeStartModulated) { ImGui::BeginDisabled(); ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 0.0f, 0.3f)); }
    float rangeStart = rangeStartModulated ? getLiveParamValueFor("rangeStart_mod", "rangeStart_live", rangeStartParam->load()) 
                                          : rangeStartParam->load();
    float rangeEnd = rangeEndParam->load();
    if (ImGui::SliderFloat("Range Start", &rangeStart, 0.0f, 1.0f, "%.3f"))
    {
        // Ensure start doesn't exceed end (leave at least 0.001 gap)
        rangeStart = juce::jmin(rangeStart, rangeEnd - 0.001f);
        apvts.getParameter("rangeStart")->setValueNotifyingHost(apvts.getParameterRange("rangeStart").convertTo0to1(rangeStart));
        onModificationEnded();
    }
    if (! rangeStartModulated)
        ModuleProcessor::adjustParamOnWheel(apvts.getParameter("rangeStart"), "rangeStart", rangeStart);
    if (rangeStartModulated) { ImGui::PopStyleColor(); ImGui::EndDisabled(); }
    
    bool rangeEndModulated = isParamModulated("rangeEnd_mod");
    if (rangeEndModulated) { ImGui::BeginDisabled(); ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 0.0f, 0.3f)); }
    rangeEnd = rangeEndModulated ? getLiveParamValueFor("rangeEnd_mod", "rangeEnd_live", rangeEndParam->load()) 
                                 : rangeEndParam->load();
    rangeStart = rangeStartParam->load(); // Refresh rangeStart for validation
    if (ImGui::SliderFloat("Range End", &rangeEnd, 0.0f, 1.0f, "%.3f"))
    {
        // Ensure end doesn't go below start (leave at least 0.001 gap)
        rangeEnd = juce::jmax(rangeEnd, rangeStart + 0.001f);
        apvts.getParameter("rangeEnd")->setValueNotifyingHost(apvts.getParameterRange("rangeEnd").convertTo0to1(rangeEnd));
        onModificationEnded();
    }
    if (! rangeEndModulated)
        ModuleProcessor::adjustParamOnWheel(apvts.getParameter("rangeEnd"), "rangeEnd", rangeEnd);
    if (rangeEndModulated) { ImGui::PopStyleColor(); ImGui::EndDisabled(); }
    
    bool loop = apvts.getRawParameterValue("loop")->load() > 0.5f;
    if (ImGui::Checkbox("Loop", &loop))
    {
        apvts.getParameter("loop")->setValueNotifyingHost(loop ? 1.0f : 0.0f);
        onModificationEnded();
    }
    
    int engineIdx = (int) apvts.getRawParameterValue("engine")->load();
    const char* items[] = { "RubberBand", "Naive" };
    if (ImGui::Combo("Engine", &engineIdx, items, 2))
    {
        apvts.getParameter("engine")->setValueNotifyingHost((float) engineIdx);
        if (sampleProcessor)
            sampleProcessor->setEngine(engineIdx == 0 ? SampleVoiceProcessor::Engine::RubberBand
                                                      : SampleVoiceProcessor::Engine::Naive);
        onModificationEnded();
    }
    
    if (engineIdx == 0)
    {
        bool winShort = apvts.getRawParameterValue("rbWindowShort")->load() > 0.5f;
        if (ImGui::Checkbox("RB Window Short", &winShort))
        {
            apvts.getParameter("rbWindowShort")->setValueNotifyingHost(winShort ? 1.0f : 0.0f);
            if (sampleProcessor) sampleProcessor->setRubberBandOptions(winShort, apvts.getRawParameterValue("rbPhaseInd")->load() > 0.5f);
            onModificationEnded();
        }
        bool phaseInd = apvts.getRawParameterValue("rbPhaseInd")->load() > 0.5f;
        if (ImGui::Checkbox("RB Phase Independent", &phaseInd))
        {
            apvts.getParameter("rbPhaseInd")->setValueNotifyingHost(phaseInd ? 1.0f : 0.0f);
            if (sampleProcessor) sampleProcessor->setRubberBandOptions(apvts.getRawParameterValue("rbWindowShort")->load() > 0.5f, phaseInd);
            onModificationEnded();
        }
    }
    
    ImGui::PopItemWidth();
    
    // 2. Now, draw the sample information and visual display AT THE END.
    if (hasSampleLoaded())
    {
        ImGui::Text("Sample: %s", currentSampleName.toRawUTF8());
        ImGui::Text("Duration: %.2f s", sampleDurationSeconds);
        ImGui::Text("Rate: %d Hz", sampleSampleRate);

        // Draw a colored button as a visible drop zone for hot-swapping
        ImVec2 swapZoneSize = ImVec2(itemWidth, 100.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 180, 180, 60));
        ImGui::Button("##dropzone_sample_swap", swapZoneSize);
        ImGui::PopStyleColor();
        
        // Draw text centered on the button
        const char* text = "Drop to Swap Sample";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 textPos = ImGui::GetItemRectMin();
        textPos.x += (swapZoneSize.x - textSize.x) * 0.5f;
        textPos.y += (swapZoneSize.y - textSize.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(200, 200, 200, 255), text);

        // 3. Make this button the drop target for hot-swapping.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SAMPLE_PATH"))
            {
                const char* path = (const char*)payload->Data;
                loadSample(juce::File(path));
                onModificationEnded();
            }
            ImGui::EndDragDropTarget();
        }
    }
    else
    {
        // If NO sample is loaded, draw a dedicated, colored dropzone.
        ImVec2 dropZoneSize = ImVec2(itemWidth, 60.0f);
        
        // Use a cyan color to match the Sample browser theme
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 180, 180, 100));
        ImGui::Button("##dropzone_sample", dropZoneSize);
        ImGui::PopStyleColor();
        
        // Draw text centered on top of the button
        const char* text = "Drop Sample Here";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 textPos = ImGui::GetItemRectMin();
        textPos.x += (dropZoneSize.x - textSize.x) * 0.5f;
        textPos.y += (dropZoneSize.y - textSize.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32_WHITE, text);

        // Make THIS BUTTON the drop target.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SAMPLE_PATH"))
            {
                const char* path = (const char*)payload->Data;
                loadSample(juce::File(path));
                onModificationEnded();
            }
            ImGui::EndDragDropTarget();
        }
    }
    // --- END OF FIX ---
}

void SampleLoaderModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Modulation inputs
    helpers.drawAudioInputPin("Pitch Mod", 0);
    helpers.drawAudioInputPin("Speed Mod", 1);
    helpers.drawAudioInputPin("Gate Mod", 2);
    helpers.drawAudioInputPin("Trigger Mod", 3);
    helpers.drawAudioInputPin("Range Start Mod", 4);
    helpers.drawAudioInputPin("Range End Mod", 5);
    helpers.drawAudioInputPin("Randomize Trig", 6);
    // Audio outputs (stereo)
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

// Parameter bus contract implementation (multi-bus architecture like TTS Performer)
bool SampleLoaderModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    // Bus 0: Playback Mods (Pitch, Speed) - flat channels 0-1
    if (paramId == "pitch_mod") { outBusIndex = 0; outChannelIndexInBus = 0; return true; }
    if (paramId == "speed_mod") { outBusIndex = 0; outChannelIndexInBus = 1; return true; }
    
    // Bus 1: Control Mods (Gate, Trigger) - flat channels 2-3
    if (paramId == "gate_mod") { outBusIndex = 1; outChannelIndexInBus = 0; return true; }
    if (paramId == "trigger_mod") { outBusIndex = 1; outChannelIndexInBus = 1; return true; }
    
    // Bus 2: Range Mods (Range Start, Range End) - flat channels 4-5
    if (paramId == "rangeStart_mod") { outBusIndex = 2; outChannelIndexInBus = 0; return true; }
    if (paramId == "rangeEnd_mod") { outBusIndex = 2; outChannelIndexInBus = 1; return true; }
    
    // Bus 3: Randomize - flat channel 6
    if (paramId == "randomize_mod") { outBusIndex = 3; outChannelIndexInBus = 0; return true; }
    
    return false;
}