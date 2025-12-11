#include "AutomatoModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"
#include <cmath>
#include <algorithm>

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

// Helper function for sorting chunks
static bool compareChunks(const AutomatoChunk::Ptr& a, const AutomatoChunk::Ptr& b)
{
    return a->startBeat < b->startBeat;
}

AutomatoModuleProcessor::AutomatoModuleProcessor()
    : ModuleProcessor(
          BusesProperties()
          .withInput("Mod", juce::AudioChannelSet::discreteChannels(2), true)  // X Mod, Y Mod
          .withOutput("Output", juce::AudioChannelSet::discreteChannels(7), true)),
      apvts(*this, nullptr, "AutomatoParams", createParameterLayout())
{
    // Initialize default state with one empty chunk
    auto initialState = std::make_shared<AutomatoState>();
    auto firstChunk = std::make_shared<AutomatoChunk>(0.0, 32, 256); // 32 beats, 256 samples/beat
    initialState->chunks.push_back(firstChunk);
    initialState->totalDurationBeats = 32.0;

    activeState.store(initialState);

    // Cache parameter pointers
    recordModeParam = apvts.getRawParameterValue(paramIdRecordMode);
    syncParam = apvts.getRawParameterValue(paramIdSync);
    divisionParam = apvts.getRawParameterValue(paramIdDivision);
    loopParam = apvts.getRawParameterValue(paramIdLoop);
    rateParam = apvts.getRawParameterValue(paramIdRate);

#if defined(PRESET_CREATOR_UI)
    // Initialize UI state
    lastMousePosInGrid = ImVec2(-1.0f, -1.0f);
    // Initialize node dimensions (height will be auto-calculated, but allow resize)
    if (nodeHeight <= 0.0f)
        nodeHeight = 400.0f; // Default height for grid + controls
#endif

    // Initialize output values for cable inspector
    for (int i = 0; i < 7; ++i)
    {
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AutomatoModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdRecordMode, "Record Mode", juce::StringArray{"Record", "Edit"}, 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdSync, "Sync to Transport", true));

    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(paramIdRate, "Rate (Hz)", 0.01f, 20.0f, 1.0f));

    // Speed Division choices: 1/32 to 8x
    juce::StringArray divs = {"1/32", "1/16", "1/8", "1/4", "1/2", "1x", "2x", "4x", "8x"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdDivision, "Speed", divs, 5)); // Default 1x

    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdLoop, "Loop", true));

    return {params.begin(), params.end()};
}

void AutomatoModuleProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;
}

void AutomatoModuleProcessor::setTimingInfo(const TransportState& state)
{
    ModuleProcessor::setTimingInfo(state);

    const TransportCommand command = state.lastCommand.load();
    if (command != lastTransportCommand)
    {
        if (command == TransportCommand::Stop)
        {
            // Reset phase to 0 when transport stops
            currentPhase = 0.0;
            // Stop CV recording if transport stops
            if (isCurrentlyRecording.load())
            {
                stopRecording();
            }
        }
        lastTransportCommand = command;
    }

    m_currentTransport = state;
}

void AutomatoModuleProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Get input bus for CV modulation
    auto inBus = getBusBuffer(buffer, true, 0);
    const bool xModIsConnected = isParamInputConnected(paramIdXMod);
    const bool yModIsConnected = isParamInputConnected(paramIdYMod);
    const float* xCV = (xModIsConnected && inBus.getNumChannels() > 0) ? inBus.getReadPointer(0) : nullptr;
    const float* yCV = (yModIsConnected && inBus.getNumChannels() > 1) ? inBus.getReadPointer(1) : nullptr;

    // Get output pointers
    auto* outX = (numChannels > OUTPUT_X) ? buffer.getWritePointer(OUTPUT_X) : nullptr;
    auto* outY = (numChannels > OUTPUT_Y) ? buffer.getWritePointer(OUTPUT_Y) : nullptr;
    auto* outCombined = (numChannels > OUTPUT_COMBINED) ? buffer.getWritePointer(OUTPUT_COMBINED) : nullptr;
    auto* outValue = (numChannels > OUTPUT_VALUE) ? buffer.getWritePointer(OUTPUT_VALUE) : nullptr;
    auto* outInverted = (numChannels > OUTPUT_INVERTED) ? buffer.getWritePointer(OUTPUT_INVERTED) : nullptr;
    auto* outBipolar = (numChannels > OUTPUT_BIPOLAR) ? buffer.getWritePointer(OUTPUT_BIPOLAR) : nullptr;
    auto* outPitch = (numChannels > OUTPUT_PITCH) ? buffer.getWritePointer(OUTPUT_PITCH) : nullptr;

    // Atomic load of the state
    AutomatoState::Ptr state = activeState.load();
    if (!state)
        return;

    // Null checks for parameter pointers
    if (!syncParam || !rateParam || !loopParam || !recordModeParam)
        return;

    const bool isSync = *syncParam > 0.5f;
    const float rateHz = *rateParam;
    const bool isLooping = *loopParam > 0.5f;
    const bool isRecording = isCurrentlyRecording.load();
    
    // Record CV inputs if connected and recording
    if (isRecording && (xModIsConnected || yModIsConnected))
    {
        const int samplesPerBeat = 256;
        const double samplesPerSecond = sampleRate;
        const double beatsPerSecond = m_currentTransport.isPlaying ? (m_currentTransport.bpm / 60.0) : 0.0;
        const double samplesPerBeatActual = beatsPerSecond > 0.0 ? (samplesPerSecond / beatsPerSecond) : samplesPerBeat;
        
        // Record CV at the correct rate (samples per beat)
        // Use a phase accumulator to sample at the right rate
        std::lock_guard<std::mutex> lock(recordingMutex);
        for (int i = 0; i < numSamples; ++i)
        {
            cvRecordingPhase += 1.0;
            
            // Sample CV when we've accumulated enough samples for one "beat sample"
            if (cvRecordingPhase >= samplesPerBeatActual)
            {
                cvRecordingPhase -= samplesPerBeatActual;
                
                // Get current CV values (clamped to 0-1)
                float xVal = xCV ? juce::jlimit(0.0f, 1.0f, xCV[i]) : 0.5f;
                float yVal = yCV ? juce::jlimit(0.0f, 1.0f, yCV[i]) : 0.5f;
                recordingBuffer.push_back(std::make_pair(xVal, yVal));
            }
        }
    }
    else
    {
        // Reset phase when not recording
        cvRecordingPhase = 0.0;
    }

    // Get current duration from state
    const double totalDuration = state->totalDurationBeats;

    for (int i = 0; i < numSamples; ++i)
    {
        // Check Global Reset (pulse from Timeline Master loop)
        if (m_currentTransport.forceGlobalReset.load())
        {
            currentPhase = 0.0;
        }

        double currentBeat = 0.0;

        // --- SYNC LOGIC (Matching AutomationLane) ---
        if (isSync && m_currentTransport.isPlaying)
        {
            // SYNC MODE: Use the global beat position with division
            int divisionIndex = (int)*divisionParam;

            // Use global division if a Tempo Clock has override enabled
            if (getParent())
            {
                int globalDiv = getParent()->getTransportState().globalDivisionIndex.load();
                if (globalDiv >= 0)
                    divisionIndex = globalDiv;
            }

            // Map indices to speed multipliers
            static const double speedMultipliers[] = {
                1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 8.0};

            const double speed = speedMultipliers[juce::jlimit(0, 8, divisionIndex)];

            // Calculate beat position: (GlobalBeats * Speed)
            currentBeat = m_currentTransport.songPositionBeats * speed;
        }
        else
        {
            // FREE-RUNNING MODE or TRANSPORT STOPPED
            if (m_currentTransport.isPlaying)
            {
                // Calculate phase increment based on Hz
                const double phaseInc = (sampleRate > 0.0 ? (double)rateHz / sampleRate : 0.0);
                currentPhase += phaseInc;
                if (currentPhase >= 1.0)
                    currentPhase -= 1.0;
            }

            // Map 0..1 phase to 0..Duration beats
            currentBeat = currentPhase * totalDuration;
        }

        // Loop logic
        if (isLooping && totalDuration > 0)
        {
            currentBeat = std::fmod(currentBeat, totalDuration);
        }
        else if (!isLooping && currentBeat > totalDuration)
        {
            // Clamp to end if not looping
            currentBeat = totalDuration;
        }

        // Sample lookup
        float xValue = 0.5f;
        float yValue = 0.5f;
        auto chunk = state->findChunkAt(currentBeat);
        if (chunk)
        {
            double beatInChunk = currentBeat - chunk->startBeat;
            int sampleIndex = static_cast<int>(beatInChunk * chunk->samplesPerBeat);

            // Linear interpolation for smoother playback
            if (sampleIndex >= 0 && sampleIndex < (int)chunk->samples.size() - 1)
            {
                float frac = (float)(beatInChunk * chunk->samplesPerBeat - sampleIndex);
                const auto& s0 = chunk->samples[sampleIndex];
                const auto& s1 = chunk->samples[sampleIndex + 1];
                xValue = s0.first + frac * (s1.first - s0.first);
                yValue = s0.second + frac * (s1.second - s0.second);
            }
            else if (sampleIndex >= 0 && sampleIndex < (int)chunk->samples.size())
            {
                xValue = chunk->samples[sampleIndex].first;
                yValue = chunk->samples[sampleIndex].second;
            }
        }

        // Calculate combined value
        float combined = (xValue + yValue) * 0.5f;
        float value = combined;
        float inverted = 1.0f - value;
        float bipolar = (value * 2.0f) - 1.0f;
        float pitch = value * 10.0f; // 0-10V range

        // Output
        if (outX) outX[i] = xValue;
        if (outY) outY[i] = yValue;
        if (outCombined) outCombined[i] = combined;
        if (outValue) outValue[i] = value;
        if (outInverted) outInverted[i] = inverted;
        if (outBipolar) outBipolar[i] = bipolar;
        if (outPitch) outPitch[i] = pitch;
    }

    // Store last values for cable inspector
    if (numSamples > 0)
    {
        AutomatoState::Ptr currentState = activeState.load();
        if (currentState)
        {
            double lastBeat = isSync && m_currentTransport.isPlaying ?
                m_currentTransport.songPositionBeats * (getDivisionSpeed((int)*divisionParam)) :
                currentPhase * currentState->totalDurationBeats;

            if (isLooping && currentState->totalDurationBeats > 0)
                lastBeat = std::fmod(lastBeat, currentState->totalDurationBeats);

            float lastX = 0.5f;
            float lastY = 0.5f;
            auto chunk = currentState->findChunkAt(lastBeat);
            if (chunk)
            {
                double beatInChunk = lastBeat - chunk->startBeat;
                int sampleIndex = static_cast<int>(beatInChunk * chunk->samplesPerBeat);
                if (sampleIndex >= 0 && sampleIndex < (int)chunk->samples.size())
                {
                    lastX = chunk->samples[sampleIndex].first;
                    lastY = chunk->samples[sampleIndex].second;
                }
            }

            float lastCombined = (lastX + lastY) * 0.5f;
            float lastValue = lastCombined;

            if (lastOutputValues.size() >= 7)
            {
                lastOutputValues[OUTPUT_X]->store(lastX);
                lastOutputValues[OUTPUT_Y]->store(lastY);
                lastOutputValues[OUTPUT_COMBINED]->store(lastCombined);
                lastOutputValues[OUTPUT_VALUE]->store(lastValue);
                lastOutputValues[OUTPUT_INVERTED]->store(1.0f - lastValue);
                lastOutputValues[OUTPUT_BIPOLAR]->store((lastValue * 2.0f) - 1.0f);
                lastOutputValues[OUTPUT_PITCH]->store(lastValue * 10.0f);
            }
        }
    }
}

double AutomatoModuleProcessor::getDivisionSpeed(int divisionIndex) const
{
    static const double speedMultipliers[] = {
        1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 8.0};
    return speedMultipliers[juce::jlimit(0, 8, divisionIndex)];
}

std::optional<RhythmInfo> AutomatoModuleProcessor::getRhythmInfo() const
{
    RhythmInfo info;
    info.displayName = "Automato #" + juce::String(getLogicalId());
    info.sourceType = "automato";

    const bool syncEnabled = syncParam != nullptr ? (syncParam->load() > 0.5f) : true;
    info.isSynced = syncEnabled;

    TransportState transport;
    bool hasTransport = false;
    if (getParent())
    {
        transport = getParent()->getTransportState();
        hasTransport = true;
    }

    if (syncEnabled)
    {
        info.isActive = hasTransport ? transport.isPlaying : false;
        if (info.isActive)
        {
            int divisionIndex = divisionParam != nullptr ? (int)divisionParam->load() : 5;
            int globalDiv = transport.globalDivisionIndex.load();
            if (globalDiv >= 0)
                divisionIndex = globalDiv;

            static const double multipliers[] = {
                1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 8.0};
            const double speed = multipliers[juce::jlimit(0, 8, divisionIndex)];

            info.bpm = static_cast<float>(transport.bpm * speed);
        }
    }
    else
    {
        info.isActive = true;
        const float rate = rateParam != nullptr ? rateParam->load() : 1.0f;
        AutomatoState::Ptr state = activeState.load();
        double duration = state ? state->totalDurationBeats : 32.0;
        info.bpm = (rate / static_cast<float>(duration)) * 60.0f;
    }

    return info;
}

void AutomatoModuleProcessor::forceStop()
{
    currentPhase = 0.0;
}

// --- State Management ---

juce::ValueTree AutomatoModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("AutomatoState");
    AutomatoState::Ptr state = activeState.load();

    if (state)
    {
        vt.setProperty("totalDurationBeats", state->totalDurationBeats, nullptr);

        for (const auto& chunk : state->chunks)
        {
            juce::ValueTree chunkVt("Chunk");
            chunkVt.setProperty("startBeat", chunk->startBeat, nullptr);
            chunkVt.setProperty("numBeats", chunk->numBeats, nullptr);
            chunkVt.setProperty("samplesPerBeat", chunk->samplesPerBeat, nullptr);

            if (!chunk->samples.empty())
            {
                // Store X and Y samples separately as binary data
                juce::MemoryBlock mbX, mbY;
                std::vector<float> xSamples, ySamples;
                xSamples.reserve(chunk->samples.size());
                ySamples.reserve(chunk->samples.size());
                
                for (const auto& sample : chunk->samples)
                {
                    xSamples.push_back(sample.first);
                    ySamples.push_back(sample.second);
                }
                
                mbX.append(xSamples.data(), xSamples.size() * sizeof(float));
                mbY.append(ySamples.data(), ySamples.size() * sizeof(float));
                
                chunkVt.setProperty("samplesX", mbX, nullptr);
                chunkVt.setProperty("samplesY", mbY, nullptr);
            }

            vt.addChild(chunkVt, -1, nullptr);
        }
    }

    vt.setProperty("sync", syncParam != nullptr ? syncParam->load() : 1.0f, nullptr);
    vt.setProperty("division", divisionParam != nullptr ? divisionParam->load() : 5.0f, nullptr);
    vt.setProperty("loop", loopParam != nullptr ? loopParam->load() : 1.0f, nullptr);
    vt.setProperty("rate", rateParam != nullptr ? rateParam->load() : 1.0f, nullptr);
    
    // Save node dimensions
    vt.setProperty("width", nodeWidth, nullptr);
    vt.setProperty("height", nodeHeight, nullptr);

    return vt;
}

void AutomatoModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("AutomatoState"))
    {
        auto newState = std::make_shared<AutomatoState>();
        newState->totalDurationBeats = vt.getProperty("totalDurationBeats", 32.0);

        for (const auto& chunkVt : vt)
        {
            if (chunkVt.hasType("Chunk"))
            {
                double startBeat = chunkVt.getProperty("startBeat");
                double numBeats = chunkVt.getProperty("numBeats");
                double samplesPerBeat = chunkVt.getProperty("samplesPerBeat");

                auto chunk = std::make_shared<AutomatoChunk>(
                    startBeat, static_cast<int>(numBeats), static_cast<int>(samplesPerBeat));

                if (chunkVt.hasProperty("samplesX") && chunkVt.hasProperty("samplesY"))
                {
                    juce::MemoryBlock* mbX = chunkVt.getProperty("samplesX").getBinaryData();
                    juce::MemoryBlock* mbY = chunkVt.getProperty("samplesY").getBinaryData();
                    
                    if (mbX && mbY && mbX->getSize() > 0 && mbY->getSize() > 0)
                    {
                        size_t numFloats = mbX->getSize() / sizeof(float);
                        if (numFloats == mbY->getSize() / sizeof(float))
                        {
                            chunk->samples.resize(numFloats);
                            const float* xData = static_cast<const float*>(mbX->getData());
                            const float* yData = static_cast<const float*>(mbY->getData());
                            
                            for (size_t i = 0; i < numFloats; ++i)
                            {
                                chunk->samples[i] = std::make_pair(xData[i], yData[i]);
                            }
                        }
                    }
                }

                if (chunk->samples.empty())
                {
                    chunk->samples.resize((size_t)(numBeats * samplesPerBeat), std::make_pair(0.5f, 0.5f));
                }

                newState->chunks.push_back(chunk);
            }
        }

        if (newState->chunks.empty())
        {
            auto firstChunk = std::make_shared<AutomatoChunk>(0.0, 32, 256);
            newState->chunks.push_back(firstChunk);
        }

        updateState(newState);

        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdSync)))
            *p = (float)vt.getProperty("sync", 1.0f);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdDivision)))
            *p = (int)vt.getProperty("division", 5);
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramIdLoop)))
            *p = (float)vt.getProperty("loop", 1.0f);
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdRate)))
            *p = (float)vt.getProperty("rate", 1.0f);
        
        // Load node dimensions
        nodeWidth = (float)vt.getProperty("width", 280.0);
        nodeHeight = (float)vt.getProperty("height", 400.0); // Default height if not saved
    }
}

bool AutomatoModuleProcessor::getParamRouting(
    const juce::String& paramId,
    int& outBusIndex,
    int& outChannelIndexInBus) const
{
    if (paramId == paramIdXMod)
    {
        outBusIndex = 0;  // Input bus 0
        outChannelIndexInBus = 0;  // First channel (X Mod)
        return true;
    }
    if (paramId == paramIdYMod)
    {
        outBusIndex = 0;
        outChannelIndexInBus = 1;  // Second channel (Y Mod)
        return true;
    }
    return false;
}

juce::String AutomatoModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
    case OUTPUT_X:
        return "X";
    case OUTPUT_Y:
        return "Y";
    case OUTPUT_COMBINED:
        return "Combined";
    case OUTPUT_VALUE:
        return "Value";
    case OUTPUT_INVERTED:
        return "Inverted";
    case OUTPUT_BIPOLAR:
        return "Bipolar";
    case OUTPUT_PITCH:
        return "Pitch";
    default:
        return {};
    }
}

void AutomatoModuleProcessor::updateState(AutomatoState::Ptr newState)
{
    activeState.store(newState);
}

AutomatoState::Ptr AutomatoModuleProcessor::getState() const
{
    return activeState.load();
}

void AutomatoModuleProcessor::ensureChunkExistsAt(double beat)
{
    AutomatoState::Ptr state = activeState.load();
    if (!state)
        return;

    if (state->findChunkAt(beat))
        return;

    const double chunkDuration = 32.0;
    const int samplesPerBeat = 256;
    double chunkStart = std::floor(beat / chunkDuration) * chunkDuration;

    for (const auto& chunk : state->chunks)
    {
        if (std::abs(chunk->startBeat - chunkStart) < 0.001)
            return;
    }

    auto newState = std::make_shared<AutomatoState>();
    newState->chunks = state->chunks;

    auto newChunk =
        std::make_shared<AutomatoChunk>(chunkStart, (int)chunkDuration, samplesPerBeat);
    newState->chunks.push_back(newChunk);

    std::sort(newState->chunks.begin(), newState->chunks.end(), compareChunks);

    double maxDuration = 0.0;
    if (!newState->chunks.empty())
    {
        auto lastChunk = newState->chunks.back();
        maxDuration = lastChunk->startBeat + lastChunk->numBeats;
    }
    newState->totalDurationBeats = maxDuration;

    updateState(newState);
}

void AutomatoModuleProcessor::modifyChunkSamplesThreadSafe(
    AutomatoChunk::Ptr chunk,
    int startSampleIndex,
    int endSampleIndex,
    float startX, float startY,
    float endX, float endY)
{
    if (!chunk || startSampleIndex < 0 || endSampleIndex < 0 ||
        startSampleIndex >= (int)chunk->samples.size() ||
        endSampleIndex >= (int)chunk->samples.size())
        return;

    AutomatoState::Ptr state = activeState.load();
    if (!state)
        return;

    auto newState = std::make_shared<AutomatoState>();
    newState->totalDurationBeats = state->totalDurationBeats;

    double targetStartBeat = chunk->startBeat;

    for (const auto& oldChunk : state->chunks)
    {
        if (std::abs(oldChunk->startBeat - targetStartBeat) < 0.001)
        {
            auto newChunk = std::make_shared<AutomatoChunk>(
                chunk->startBeat, chunk->numBeats, chunk->samplesPerBeat);
            newChunk->samples = chunk->samples;

            if (startSampleIndex <= endSampleIndex)
            {
                for (int i = startSampleIndex;
                     i <= endSampleIndex && i < (int)newChunk->samples.size();
                     ++i)
                {
                    float t = (endSampleIndex == startSampleIndex)
                                  ? 1.0f
                                  : (float)(i - startSampleIndex) /
                                        (float)(endSampleIndex - startSampleIndex);
                    float x = startX + t * (endX - startX);
                    float y = startY + t * (endY - startY);
                    newChunk->samples[i] = std::make_pair(x, y);
                }
            }
            newState->chunks.push_back(newChunk);
        }
        else
        {
            newState->chunks.push_back(oldChunk);
        }
    }

    updateState(newState);
}

void AutomatoModuleProcessor::startRecording()
{
    if (isCurrentlyRecording.load())
        return;

    isCurrentlyRecording.store(true);
    recordingStartTime = m_currentTransport.isPlaying ? 
        m_currentTransport.songPositionBeats : 0.0;
    
    // Clear recording buffer and reset CV recording phase
    std::lock_guard<std::mutex> lock(recordingMutex);
    recordingBuffer.clear();
    cvRecordingPhase = 0.0;
}

void AutomatoModuleProcessor::stopRecording()
{
    if (!isCurrentlyRecording.load())
        return;

    isCurrentlyRecording.store(false);

    // Convert recording buffer to chunks
    std::lock_guard<std::mutex> lock(recordingMutex);
    
    if (recordingBuffer.empty())
        return;

    // Create new state from recording
    auto newState = std::make_shared<AutomatoState>();
    const int samplesPerBeat = 256;
    const double chunkDuration = 32.0;
    
    // Calculate total duration (only the length of the recording, starting from 0)
    double totalDuration = recordingBuffer.size() / (double)samplesPerBeat;
    
    // Create chunks (always start at beat 0 for proper looping)
    double currentBeat = 0.0;
    size_t bufferIndex = 0;
    
    while (bufferIndex < recordingBuffer.size())
    {
        auto chunk = std::make_shared<AutomatoChunk>(currentBeat, (int)chunkDuration, samplesPerBeat);
        size_t samplesInChunk = (size_t)(chunkDuration * samplesPerBeat);
        
        for (size_t i = 0; i < samplesInChunk && bufferIndex < recordingBuffer.size(); ++i, ++bufferIndex)
        {
            if (i < chunk->samples.size())
            {
                chunk->samples[i] = recordingBuffer[bufferIndex];
            }
        }
        
        newState->chunks.push_back(chunk);
        currentBeat += chunkDuration;
    }
    
    newState->totalDurationBeats = totalDuration;
    updateState(newState);
    recordingBuffer.clear();
}

#if defined(PRESET_CREATOR_UI)
ImVec2 AutomatoModuleProcessor::getCustomNodeSize() const
{
    return ImVec2(nodeWidth, nodeHeight);
}
void AutomatoModuleProcessor::drawParametersInNode(
    float itemWidth,
    const std::function<bool(const juce::String&)>& isParamModulated,
    const std::function<void()>& onModificationEnded)
{
    auto HelpMarker = [](const char* desc)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    auto& ap = getAPVTS();

    if (!recordModeParam || !syncParam || !divisionParam || !loopParam || !rateParam)
    {
        ImGui::Text("Initializing...");
        return;
    }

    ImGui::PushItemWidth(itemWidth);

    // Record/Edit Mode Toggle
    bool isRecordMode = *recordModeParam < 0.5f;
    if (ImGui::Button(isRecordMode ? "REC" : "EDIT", ImVec2(itemWidth * 0.5f, 0)))
    {
        // Stop recording if switching to Edit mode
        if (isRecordMode && isCurrentlyRecording.load())
        {
            stopRecording();
        }
        float newVal = isRecordMode ? 1.0f : 0.0f;
        apvts.getParameter(paramIdRecordMode)->setValueNotifyingHost(newVal);
        onModificationEnded();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toggle Record/Edit Mode");

    ImGui::SameLine();

    // Sync Checkbox
    bool syncEnabled = *syncParam > 0.5f;
    if (ImGui::Checkbox("Sync", &syncEnabled))
    {
        float newVal = syncEnabled ? 1.0f : 0.0f;
        *syncParam = newVal;
        ap.getParameter(paramIdSync)->setValueNotifyingHost(newVal);
        onModificationEnded();
    }
    HelpMarker("Sync playback to transport");

    // Speed Division (if synced) or Rate (if free-running)
    if (syncEnabled)
    {
        ImGui::SetNextItemWidth(itemWidth);
        int globalDiv = getParent() ? getParent()->getTransportState().globalDivisionIndex.load() : -1;
        bool isGlobal = globalDiv >= 0;
        int divIndex = isGlobal ? globalDiv : (int)*divisionParam;
        const char* divs[] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1x", "2x", "4x", "8x"};

        if (isGlobal)
            ImGui::BeginDisabled();
        if (ImGui::Combo("##speed", &divIndex, divs, 9))
        {
            if (!isGlobal)
            {
                *divisionParam = (float)divIndex;
                ap.getParameter(paramIdDivision)
                    ->setValueNotifyingHost(
                        ap.getParameterRange(paramIdDivision).convertTo0to1((float)divIndex));
                onModificationEnded();
            }
        }
        if (isGlobal)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Controlled by Tempo Clock");
        }
        // Scroll wheel editing for Speed Division combo
        if (!isGlobal && ImGui::IsItemHovered())
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                const int maxIndex = 8; // 9 divisions: 0-8
                const int newIndex = juce::jlimit(0, maxIndex, divIndex + (wheel > 0.0f ? -1 : 1));
                if (newIndex != divIndex)
                {
                    *divisionParam = (float)newIndex;
                    ap.getParameter(paramIdDivision)
                        ->setValueNotifyingHost(
                            ap.getParameterRange(paramIdDivision).convertTo0to1((float)newIndex));
                    onModificationEnded();
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Playback Speed Multiplier");
    }
    else
    {
        ImGui::SetNextItemWidth(itemWidth);
        float rate = *rateParam;
        if (ImGui::DragFloat("##rate", &rate, 0.01f, 0.01f, 20.0f, "%.2f Hz"))
        {
            *rateParam = rate;
            ap.getParameter(paramIdRate)
                ->setValueNotifyingHost(ap.getParameterRange(paramIdRate).convertTo0to1(rate));
            onModificationEnded();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            onModificationEnded();
        // Scroll wheel editing for Rate drag float
        adjustParamOnWheel(ap.getParameter(paramIdRate), paramIdRate, rate);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Playback Rate in Hz");
    }

    // Loop Checkbox
    bool isLooping = *loopParam > 0.5f;
    if (ImGui::Checkbox("Loop", &isLooping))
    {
        float newVal = isLooping ? 1.0f : 0.0f;
        *loopParam = newVal;
        ap.getParameter(paramIdLoop)->setValueNotifyingHost(newVal);
        onModificationEnded();
    }
    HelpMarker("Loop playback");

    ImGui::Spacing();

    // --- 2D Grid (Similar to PanVol) ---
    // Use nodeWidth for grid sizing (allow resize), with reasonable min/max bounds
    const float minGridSize = 120.0f;
    const float maxGridSize = 600.0f;
    const float effectiveNodeWidth = juce::jmax(minGridSize, juce::jmin(maxGridSize, itemWidth));
    const float gridSize = juce::jmin(effectiveNodeWidth - 20.0f, maxGridSize);
    const float gridPadding = (itemWidth - gridSize) * 0.5f;

    ImVec2 gridPos = ImGui::GetCursorScreenPos();
    gridPos.x += gridPadding;
    gridPos.y += 2.0f;

    ImVec2 gridMin = gridPos;
    ImVec2 gridMax = ImVec2(gridPos.x + gridSize, gridPos.y + gridSize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Check CV modulation state
    const bool xModIsConnected = isParamModulated(paramIdXMod);
    const bool yModIsConnected = isParamModulated(paramIdYMod);
    const bool cvIsActive = xModIsConnected || yModIsConnected;

    // Draw grid background
    drawList->AddRectFilled(gridMin, gridMax, theme.modules.panvol_grid_background);
    drawList->AddRect(gridMin, gridMax, theme.modules.panvol_grid_border, 0.0f, 0, 2.0f);

    // Draw grid lines
    const int gridDivisions = 4;
    for (int i = 1; i < gridDivisions; ++i)
    {
        float t = (float)i / (float)gridDivisions;
        float x = gridMin.x + t * gridSize;
        drawList->AddLine(ImVec2(x, gridMin.y), ImVec2(x, gridMax.y),
                         theme.modules.panvol_grid_lines, 1.0f);
        float y = gridMin.y + t * gridSize;
        drawList->AddLine(ImVec2(gridMin.x, y), ImVec2(gridMax.x, y),
                         theme.modules.panvol_grid_lines, 1.0f);
    }

    // Draw center crosshair
    ImVec2 center(gridMin.x + gridSize * 0.5f, gridMin.y + gridSize * 0.5f);
    drawList->AddLine(ImVec2(center.x, gridMin.y), ImVec2(center.x, gridMax.y),
                     theme.modules.panvol_crosshair, 1.0f);
    drawList->AddLine(ImVec2(gridMin.x, center.y), ImVec2(gridMax.x, center.y),
                     theme.modules.panvol_crosshair, 1.0f);

    // Draw recorded path (show in both Record and Edit modes)
    AutomatoState::Ptr state = activeState.load();
    if (state)
    {
        std::vector<ImVec2> pathPoints;
        for (const auto& chunk : state->chunks)
        {
            for (size_t i = 0; i < chunk->samples.size(); i += 4) // Sample every 4th point for performance
            {
                const auto& sample = chunk->samples[i];
                float x = gridMin.x + sample.first * gridSize;
                float y = gridMin.y + (1.0f - sample.second) * gridSize; // Invert Y
                pathPoints.push_back(ImVec2(x, y));
            }
        }
        if (pathPoints.size() > 1)
        {
            // Draw existing recorded path (lighter blue)
            drawList->AddPolyline(pathPoints.data(), (int)pathPoints.size(),
                                 IM_COL32(100, 200, 255, 200), 0, 2.0f);
        }
    }
    
    // Draw current recording buffer in real-time (during active recording)
    if (isRecordMode && isCurrentlyRecording.load())
    {
        std::lock_guard<std::mutex> lock(recordingMutex);
        if (!recordingBuffer.empty())
        {
            std::vector<ImVec2> livePathPoints;
            // Sample every point for smooth real-time visualization
            for (const auto& sample : recordingBuffer)
            {
                float x = gridMin.x + sample.first * gridSize;
                float y = gridMin.y + (1.0f - sample.second) * gridSize; // Invert Y
                livePathPoints.push_back(ImVec2(x, y));
            }
            if (livePathPoints.size() > 1)
            {
                // Draw current recording in bright cyan/yellow for visibility
                drawList->AddPolyline(livePathPoints.data(), (int)livePathPoints.size(),
                                     IM_COL32(255, 255, 100, 255), 0, 3.0f); // Bright yellow, thicker line
            }
        }
    }

    // Draw playhead position (if not recording)
    if (!isRecordMode && state)
    {
        double currentBeat = 0.0;
        if (syncEnabled && m_currentTransport.isPlaying)
        {
            int divisionIndex = (int)*divisionParam;
            if (getParent())
            {
                int globalDiv = getParent()->getTransportState().globalDivisionIndex.load();
                if (globalDiv >= 0)
                    divisionIndex = globalDiv;
            }
            double speed = getDivisionSpeed(divisionIndex);
            currentBeat = m_currentTransport.songPositionBeats * speed;
        }
        else if (m_currentTransport.isPlaying)
        {
            currentBeat = currentPhase * state->totalDurationBeats;
        }

        if (*loopParam > 0.5f && state->totalDurationBeats > 0)
            currentBeat = std::fmod(currentBeat, state->totalDurationBeats);

        // Find sample at current beat
        float playheadX = 0.5f;
        float playheadY = 0.5f;
        auto chunk = state->findChunkAt(currentBeat);
        if (chunk)
        {
            double beatInChunk = currentBeat - chunk->startBeat;
            int sampleIndex = static_cast<int>(beatInChunk * chunk->samplesPerBeat);
            if (sampleIndex >= 0 && sampleIndex < (int)chunk->samples.size())
            {
                playheadX = chunk->samples[sampleIndex].first;
                playheadY = chunk->samples[sampleIndex].second;
            }
        }

        float circleX = gridMin.x + playheadX * gridSize;
        float circleY = gridMin.y + (1.0f - playheadY) * gridSize;
        ImVec2 circlePos(circleX, circleY);
        const float circleRadius = 6.0f;

        drawList->AddCircleFilled(circlePos, circleRadius, IM_COL32(255, 255, 0, 255), 16);
        drawList->AddCircle(circlePos, circleRadius, IM_COL32(255, 255, 255, 255), 16, 1.5f);
    }

    // Draw visual feedback dot during active drawing (in Record mode)
    if (isRecordMode)
    {
        if (isDrawing && lastMousePosInGrid.x >= 0.0f && lastMousePosInGrid.y >= 0.0f)
        {
            // Draw circle at current drawing position (manual drawing)
            ImVec2 drawPos = lastMousePosInGrid;
            const float drawRadius = 6.0f;
            
            // Draw shadow
            drawList->AddCircleFilled(ImVec2(drawPos.x + 1, drawPos.y + 1),
                                     drawRadius, IM_COL32(0, 0, 0, 100), 16);
            
            // Draw circle (white for manual drawing)
            drawList->AddCircleFilled(drawPos, drawRadius, IM_COL32(255, 255, 255, 255), 16);
            drawList->AddCircle(drawPos, drawRadius, IM_COL32(255, 255, 255, 255), 16, 1.5f);
        }
        else if (cvIsActive && isCurrentlyRecording.load())
        {
            // Show CV recording indicator at center when CV is recording
            ImVec2 center(gridMin.x + gridSize * 0.5f, gridMin.y + gridSize * 0.5f);
            const float indicatorRadius = 8.0f;
            
            // Draw pulsing indicator (cyan) to show CV is recording
            drawList->AddCircleFilled(center, indicatorRadius, theme.modules.panvol_circle_modulated, 16);
            drawList->AddCircle(center, indicatorRadius, IM_COL32(255, 255, 255, 255), 16, 2.0f);
        }
    }

    // Reserve space for grid
    ImGui::Dummy(ImVec2(itemWidth, gridSize + 4.0f));

    // Invisible button for interaction
    ImGui::SetCursorScreenPos(gridMin);
    ImGui::InvisibleButton("##automato_grid", ImVec2(gridSize, gridSize));

    // FIRST: Check if mouse was just released - stop recording and toggle to Edit mode
    if (isRecordMode && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (isDrawing && isCurrentlyRecording.load() && !cvIsActive)
        {
            stopRecording();
            // Automatically switch to Edit mode
            apvts.getParameter(paramIdRecordMode)->setValueNotifyingHost(1.0f); // 1.0f = Edit mode
            onModificationEnded();
        }
        isDrawing = false;
        lastMousePosInGrid = ImVec2(-1.0f, -1.0f);
    }
    // THEN: Handle mouse interaction (only in Record mode, and only if CV is not connected)
    else if (isRecordMode && !cvIsActive)
    {
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            float relX = (mousePos.x - gridMin.x) / gridSize;
            float relY = 1.0f - ((mousePos.y - gridMin.y) / gridSize); // Invert Y
            relX = juce::jlimit(0.0f, 1.0f, relX);
            relY = juce::jlimit(0.0f, 1.0f, relY);

            // Start recording if not already
            if (!isCurrentlyRecording.load())
            {
                startRecording();
            }

            // Record this point (with interpolation if mouse moved)
            if (isCurrentlyRecording.load())
            {
                std::lock_guard<std::mutex> lock(recordingMutex);
                if (lastMousePosInGrid.x >= 0.0f && lastMousePosInGrid.y >= 0.0f)
                {
                    // Interpolate between last position and current
                    float lastRelX = (lastMousePosInGrid.x - gridMin.x) / gridSize;
                    float lastRelY = 1.0f - ((lastMousePosInGrid.y - gridMin.y) / gridSize);
                    lastRelX = juce::jlimit(0.0f, 1.0f, lastRelX);
                    lastRelY = juce::jlimit(0.0f, 1.0f, lastRelY);
                    
                    // Add intermediate points for smooth recording
                    float dx = relX - lastRelX;
                    float dy = relY - lastRelY;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    int numPoints = (int)(dist * 10.0f) + 1; // Add points based on distance
                    numPoints = juce::jlimit(1, 5, numPoints); // Limit to avoid too many points
                    
                    for (int i = 1; i <= numPoints; ++i)
                    {
                        float t = (float)i / (float)(numPoints + 1);
                        float x = lastRelX + t * (relX - lastRelX);
                        float y = lastRelY + t * (relY - lastRelY);
                        recordingBuffer.push_back(std::make_pair(x, y));
                    }
                }
                recordingBuffer.push_back(std::make_pair(relX, relY));
            }

            lastMousePosInGrid = mousePos;
            isDrawing = true;
        }
        else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            ImVec2 mousePos = ImGui::GetMousePos();
            float relX = (mousePos.x - gridMin.x) / gridSize;
            float relY = 1.0f - ((mousePos.y - gridMin.y) / gridSize);
            relX = juce::jlimit(0.0f, 1.0f, relX);
            relY = juce::jlimit(0.0f, 1.0f, relY);

            // Start recording if not already
            if (!isCurrentlyRecording.load())
            {
                startRecording();
            }

            std::lock_guard<std::mutex> lock(recordingMutex);
            recordingBuffer.push_back(std::make_pair(relX, relY));

            lastMousePosInGrid = mousePos;
            isDrawing = true;
        }
    }
    else if (isRecordMode && cvIsActive)
    {
        // CV is connected - show indicator that manual drawing is disabled
        // The CV values will be recorded in processBlock
        if (!isCurrentlyRecording.load() && m_currentTransport.isPlaying)
        {
            startRecording();
        }
    }

    // Resize handle in bottom-right corner (similar to CommentModuleProcessor)
    // Position it relative to the grid's bottom-right corner
    const ImVec2 resizeHandleSize(16.0f, 16.0f);
    ImVec2 drawPos = gridMax; // Bottom-right of grid
    drawPos.x -= resizeHandleSize.x; // Align to right edge
    drawPos.y += 4.0f; // Small offset below grid

    ImGui::SetCursorScreenPos(drawPos);
    ImGui::InvisibleButton("##automato_resize", resizeHandleSize);
    const bool isResizing = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

    if (isResizing)
    {
        const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        // Clamp to reasonable bounds (matching CommentModule pattern)
        nodeWidth = juce::jlimit(200.0f, 800.0f, nodeWidth + delta.x);
        nodeHeight = juce::jlimit(200.0f, 800.0f, nodeHeight + delta.y);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        wasBeingResizedLastFrame = true;
    }
    else if (wasBeingResizedLastFrame)
    {
        // Just finished resizing, trigger undo snapshot
        wasBeingResizedLastFrame = false;
        onModificationEnded();
    }

    // Draw resize handle indicator
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(drawPos.x + 4.0f, drawPos.y + resizeHandleSize.y - 4.0f),
        ImVec2(drawPos.x + resizeHandleSize.x - 4.0f, drawPos.y + resizeHandleSize.y - 4.0f),
        ImVec2(drawPos.x + resizeHandleSize.x - 4.0f, drawPos.y + 4.0f),
        ImGui::GetColorU32(ImGuiCol_ResizeGrip));

    // Satisfy ImGui's boundary assertions (similar to CommentModuleProcessor)
    ImGui::Dummy(ImVec2(1.0f, 1.0f));

    ImGui::PopItemWidth();
}
#endif

