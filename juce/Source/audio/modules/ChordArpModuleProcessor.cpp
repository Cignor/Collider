#include "ChordArpModuleProcessor.h"
#include "../../preset_creator/ImGuiNodeEditorComponent.h" // For NodeWidth if needed
#include "../../preset_creator/theme/ThemeManager.h" // For ThemeText
#include <random>

//==============================================================================
ChordArpModuleProcessor::ChordArpModuleProcessor()
    : ModuleProcessor(
          BusesProperties()
              .withInput(
                  "Inputs",
                  juce::AudioChannelSet::discreteChannels(10),
                  true) // 10 CV inputs: Degree, Root CV, Chord Mode Mod, Arp Rate Mod, Scale Mod, Key Mod, Voicing Mod, Arp Mode Mod, Range Mode Mod, NumVoices Mod
              .withOutput(
                  "Outputs",
                  juce::AudioChannelSet::discreteChannels(10),
                  true)), // 10 outputs: 4 Pitch + 4 Gate pairs (0-7) + Arp Pitch/Gate (8-9)
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Cache parameter pointers
    scaleParam = apvts.getRawParameterValue(paramIdScale);
    keyParam = apvts.getRawParameterValue(paramIdKey);
    chordModeParam = apvts.getRawParameterValue(paramIdChordMode);
    voicingParam = apvts.getRawParameterValue(paramIdVoicing);
    arpModeParam = apvts.getRawParameterValue(paramIdArpMode);
    arpDivisionParam = apvts.getRawParameterValue(paramIdArpDivision);
    useExtClockParam = apvts.getRawParameterValue(paramIdUseExtClock);
    numVoicesParam = apvts.getRawParameterValue(paramIdNumVoices);

    degreeRangeMinParam = apvts.getRawParameterValue(paramIdDegreeRangeMin);
    degreeRangeMaxParam = apvts.getRawParameterValue(paramIdDegreeRangeMax);
    degreeRangeModeParam = apvts.getRawParameterValue(paramIdDegreeRangeMode);

    // Initialize output values for visualization (8 outputs: 4 pitch + 4 gate)
    lastOutputValues.resize(8);
    for (auto& v : lastOutputValues)
        v = std::make_unique<std::atomic<float>>(0.0f);
}

ChordArpModuleProcessor::~ChordArpModuleProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ChordArpModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 1. Scale / Key
    juce::StringArray scales = {
        "Major",
        "Minor",
        "Dorian",
        "Phrygian",
        "Lydian",
        "Mixolydian",
        "Locrian",
        "Harmonic Minor",
        "Melodic Minor",
        "Chromatic"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(paramIdScale, "Scale", scales, 0));

    juce::StringArray keys = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    params.push_back(std::make_unique<juce::AudioParameterChoice>(paramIdKey, "Key", keys, 0));

    // 2. Chord Generation
    juce::StringArray chordModes = {"Triad", "Seventh", "Ninth", "Cluster", "Open", "Power"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdChordMode, "Chord Mode", chordModes, 0));

    juce::StringArray voicings = {"Root", "1st Inv", "2nd Inv", "3rd Inv", "Drop 2", "Spread"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(paramIdVoicing, "Voicing", voicings, 0));

    // 3. Arpeggiator
    juce::StringArray arpModes = {"Off", "Up", "Down", "Up/Down", "Random", "Poly"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdArpMode, "Arp Mode", arpModes, 1)); // Default Up

    juce::StringArray divisions = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdArpDivision, "Division", divisions, 4)); // Default 1/16

    params.push_back(
        std::make_unique<juce::AudioParameterBool>(paramIdUseExtClock, "Ext Clock", false));

    // Number of voices (for Poly mode)
    params.push_back(
        std::make_unique<juce::AudioParameterInt>(paramIdNumVoices, "Voices", 1, 4, 4));

    // 4. Degree Range
    params.push_back(
        std::make_unique<juce::AudioParameterInt>(paramIdDegreeRangeMin, "Degree Min", 1, 7, 1));
    params.push_back(
        std::make_unique<juce::AudioParameterInt>(paramIdDegreeRangeMax, "Degree Max", 1, 7, 7));

    juce::StringArray rangeModes = {"All", "Sequential", "Random"};
    params.push_back(
        std::make_unique<juce::AudioParameterChoice>(
            paramIdDegreeRangeMode, "Range Mode", rangeModes, 0));

    return {params.begin(), params.end()};
}

//==============================================================================
void ChordArpModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    arpState = ArpState();
}

void ChordArpModuleProcessor::setTimingInfo(const TransportState& state)
{
    currentTransport = state;
}

std::optional<RhythmInfo> ChordArpModuleProcessor::getRhythmInfo() const
{
    // Report rhythm if Arp is active
    if (arpModeParam && arpModeParam->load() > 0)
    {
        return RhythmInfo(
            "Chord Arp", currentTransport.bpm, currentTransport.isPlaying, true, "sequencer");
    }
    return std::nullopt;
}

//==============================================================================
void ChordArpModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    // Get input bus (read BEFORE clearing outputs to avoid aliasing)
    const auto& inputBus = getBusBuffer(buffer, true, 0);
    const int   numInputChannels = inputBus.getNumChannels();

    // 1. Read Inputs (CV) - use first sample for CV values
    float degreeIn = 0.0f;
    float rootCvIn = 0.0f;
    float chordModeModIn = 0.0f;
    float arpRateModIn = 0.0f;
    float scaleModIn = 0.0f;
    float keyModIn = 0.0f;
    float voicingModIn = 0.0f;
    float arpModeModIn = 0.0f;
    float rangeModeModIn = 0.0f;
    float numVoicesModIn = 0.0f;

    if (numInputChannels > 0)
        degreeIn = inputBus.getReadPointer(0)[0];
    if (numInputChannels > 1)
        rootCvIn = inputBus.getReadPointer(1)[0];
    if (numInputChannels > 2)
        chordModeModIn = inputBus.getReadPointer(2)[0];
    if (numInputChannels > 3)
        arpRateModIn = inputBus.getReadPointer(3)[0];
    if (numInputChannels > 4)
        scaleModIn = inputBus.getReadPointer(4)[0];
    if (numInputChannels > 5)
        keyModIn = inputBus.getReadPointer(5)[0];
    if (numInputChannels > 6)
        voicingModIn = inputBus.getReadPointer(6)[0];
    if (numInputChannels > 7)
        arpModeModIn = inputBus.getReadPointer(7)[0];
    if (numInputChannels > 8)
        rangeModeModIn = inputBus.getReadPointer(8)[0];
    if (numInputChannels > 9)
        numVoicesModIn = inputBus.getReadPointer(9)[0];

    // Clamp inputs to valid range
    degreeIn = juce::jlimit(0.0f, 1.0f, degreeIn);
    rootCvIn = juce::jlimit(0.0f, 1.0f, rootCvIn);
    chordModeModIn = juce::jlimit(0.0f, 1.0f, chordModeModIn);
    arpRateModIn = juce::jlimit(0.0f, 1.0f, arpRateModIn);
    scaleModIn = juce::jlimit(0.0f, 1.0f, scaleModIn);
    keyModIn = juce::jlimit(0.0f, 1.0f, keyModIn);
    voicingModIn = juce::jlimit(0.0f, 1.0f, voicingModIn);
    arpModeModIn = juce::jlimit(0.0f, 1.0f, arpModeModIn);
    rangeModeModIn = juce::jlimit(0.0f, 1.0f, rangeModeModIn);
    numVoicesModIn = juce::jlimit(0.0f, 1.0f, numVoicesModIn);

    // Clear output buffer
    buffer.clear();

    // 2. Read Parameters
    // Safety checks for cached parameters
    if (!scaleParam || !keyParam || !chordModeParam || !voicingParam || !arpModeParam ||
        !arpDivisionParam || !useExtClockParam || !numVoicesParam || !degreeRangeMinParam ||
        !degreeRangeMaxParam || !degreeRangeModeParam)
    {
        return;
    }

    int scaleIdx = (int)scaleParam->load();
    int keyIdx = (int)keyParam->load();
    int chordModeIdx = (int)chordModeParam->load();
    int voicingIdx = (int)voicingParam->load();
    int arpModeIdx = (int)arpModeParam->load();
    int divisionIdx = (int)arpDivisionParam->load();
    const bool useExtClock = (float)useExtClockParam->load() > 0.5f;
    int numVoices = (int)numVoicesParam->load();

    int       degreeMin = (int)degreeRangeMinParam->load();
    int       degreeMax = (int)degreeRangeMaxParam->load();
    int rangeMode = (int)degreeRangeModeParam->load(); // 0=All, 1=Seq, 2=Rnd

    // Ensure Min <= Max
    if (degreeMin > degreeMax)
        std::swap(degreeMin, degreeMax);

    // --- Telemetry: Update Live Values & Apply Modulation ---
    // Scale Mod
    if (isParamInputConnected(paramIdScaleMod))
    {
        // Map 0..1 CV to scale index range (0-5)
        int modOffset = (int)(scaleModIn * 5.0f);
        scaleIdx = juce::jlimit(0, 5, modOffset);
        setLiveParamValue("scale_live", (float)scaleIdx);
    }
    else
    {
        setLiveParamValue("scale_live", (float)scaleIdx);
    }

    // Key Mod
    if (isParamInputConnected(paramIdKeyMod))
    {
        // Map 0..1 CV to key index range (0-11)
        int modOffset = (int)(keyModIn * 11.0f);
        keyIdx = juce::jlimit(0, 11, modOffset);
        setLiveParamValue("key_live", (float)keyIdx);
    }
    else
    {
        setLiveParamValue("key_live", (float)keyIdx);
    }

    // Chord Mode Mod
    if (isParamInputConnected(paramIdChordModeMod))
    {
        // Map 0..1 CV to mode index range (0-5)
        int modOffset = (int)(chordModeModIn * 5.0f);
        chordModeIdx = juce::jlimit(0, 5, modOffset);
        setLiveParamValue("chordMode_live", (float)chordModeIdx);
    }
    else
    {
        setLiveParamValue("chordMode_live", (float)chordModeIdx);
    }

    // Voicing Mod
    if (isParamInputConnected(paramIdVoicingMod))
    {
        // Map 0..1 CV to voicing index range (0-3)
        int modOffset = (int)(voicingModIn * 3.0f);
        voicingIdx = juce::jlimit(0, 3, modOffset);
        setLiveParamValue("voicing_live", (float)voicingIdx);
    }
    else
    {
        setLiveParamValue("voicing_live", (float)voicingIdx);
    }

    // Arp Mode Mod
    if (isParamInputConnected(paramIdArpModeMod))
    {
        // Map 0..1 CV to arp mode index range (0-3)
        int modOffset = (int)(arpModeModIn * 3.0f);
        arpModeIdx = juce::jlimit(0, 3, modOffset);
        setLiveParamValue("arpMode_live", (float)arpModeIdx);
    }
    else
    {
        setLiveParamValue("arpMode_live", (float)arpModeIdx);
    }

    // Arp Rate Mod (Division)
    if (isParamInputConnected(paramIdArpRateMod))
    {
        // Map 0..1 CV to division index range (0-6)
        int modOffset = (int)(arpRateModIn * 6.0f);
        divisionIdx = juce::jlimit(0, 6, modOffset);
        setLiveParamValue("arpDivision_live", (float)divisionIdx);
    }
    else
    {
        setLiveParamValue("arpDivision_live", (float)divisionIdx);
    }

    // Range Mode Mod
    if (isParamInputConnected(paramIdRangeModeMod))
    {
        // Map 0..1 CV to range mode index range (0-2)
        int modOffset = (int)(rangeModeModIn * 2.0f);
        rangeMode = juce::jlimit(0, 2, modOffset);
        setLiveParamValue("rangeMode_live", (float)rangeMode);
    }
    else
    {
        setLiveParamValue("rangeMode_live", (float)rangeMode);
    }

    // NumVoices Mod
    if (isParamInputConnected(paramIdNumVoicesMod))
    {
        // Map 0..1 CV to voices range (1-4)
        int modOffset = 1 + (int)(numVoicesModIn * 3.0f);
        numVoices = juce::jlimit(1, 4, modOffset);
        setLiveParamValue("numVoices_live", (float)numVoices);
    }
    else
    {
        setLiveParamValue("numVoices_live", (float)numVoices);
    }

    // 3. Compute Harmony: Generate chord pitches from inputs
    // Helper function to compute chord voice offsets (semitone offsets from root)
    auto computeChordVoiceOffsets = [](int degreeIdx, int scaleIndex, int chordIndex, int voicingIndex, int numVoices) -> std::vector<float> {
        // Scale intervals (semitone offsets from root)
        static const std::vector<std::vector<int>> scaleIntervals = {
            {0, 2, 4, 5, 7, 9, 11}, // Major
            {0, 2, 3, 5, 7, 8, 10}, // Minor
            {0, 2, 4, 6, 8, 10, 11}, // Diminished
            {0, 2, 4, 5, 7, 9, 10}, // Mixolydian
            {0, 2, 3, 5, 7, 9, 10}, // Dorian
            {0, 1, 3, 5, 6, 8, 10}, // Harmonic Minor
        };

        // Chord intervals (semitone offsets for each chord type)
        static const std::vector<std::vector<int>> chordIntervals = {
            {0, 4, 7},   // Triad (Major)
            {0, 3, 7},   // Triad (Minor)
            {0, 4, 7, 11}, // Seventh (Major 7th)
            {0, 3, 7, 10}, // Seventh (Minor 7th)
            {0, 4, 7, 10}, // Seventh (Dominant 7th)
            {0, 3, 6, 10}, // Seventh (Half-diminished)
        };

        // Voicing offsets (octave shifts for spread voicings)
        static const std::vector<std::vector<int>> voicingOffsets = {
            {0, 0, 0, 0},   // Close
            {0, 0, 12, 12}, // Spread
            {0, 12, 0, 12}, // Drop 2
            {0, 12, 12, 0}, // Drop 3
        };

        std::vector<float> offsets;
        if (scaleIndex < 0 || scaleIndex >= (int)scaleIntervals.size())
            scaleIndex = 0;
        if (chordIndex < 0 || chordIndex >= (int)chordIntervals.size())
            chordIndex = 0;
        if (voicingIndex < 0 || voicingIndex >= (int)voicingOffsets.size())
            voicingIndex = 0;

        const auto& scale = scaleIntervals[scaleIndex];
        const auto& chord = chordIntervals[chordIndex];
        const auto& voicing = voicingOffsets[voicingIndex];

        // Map degreeIdx (0-6) to scale semitone
        int rootSemitone = scale[juce::jlimit(0, 6, degreeIdx)];

        // Generate chord notes
        for (int i = 0; i < numVoices && i < 4; ++i)
        {
            if (i < (int)chord.size())
            {
                float semitone = rootSemitone + chord[i] + (i < (int)voicing.size() ? voicing[i] : 0);
                offsets.push_back(semitone / 12.0f); // Convert to V/Oct
            }
        }

        return offsets;
    };

    // Compute chord voice offsets
    int degreeVal = juce::jlimit(0, 6, (int)(degreeIn * 6.0f));
    std::vector<float> voiceOffsets = computeChordVoiceOffsets(degreeVal, scaleIdx, chordModeIdx, voicingIdx, numVoices);

    // Convert to absolute pitches (V/Oct) by adding root CV
    std::vector<float> chordPitches;
    std::vector<int> chordScaleDegrees;
    for (size_t i = 0; i < voiceOffsets.size(); ++i)
    {
        chordPitches.push_back(rootCvIn + voiceOffsets[i]);
        chordScaleDegrees.push_back(degreeVal);
    }

    // 4. Calculate Timing
    double beatsPerSecond = currentTransport.bpm / 60.0;
    double samplesPerBeat = currentSampleRate / beatsPerSecond;

    // Division map: 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64
    // divisionIdx may have been modified by modulation above
    float divMult = 4.0f / std::pow(2.0f, (float)divisionIdx); // 1/16 (idx 4) -> 4/16 = 0.25 beats
    double samplesPerStep = samplesPerBeat * divMult;

    // Apply Arp Rate Mod as multiplier (0.5x to 2.0x)
    float rateMult = 0.5f + (arpRateModIn * 1.5f);
    samplesPerStep *= rateMult;

    // Update arp state
    arpState.samplesPerStep = samplesPerStep;

    // 5. Filter Active Indices based on Degree Range
    std::vector<size_t> activeIndices;
    if (rangeMode == 0) // All
    {
        for (size_t i = 0; i < chordPitches.size(); ++i)
            activeIndices.push_back(i);
    }
    else // Sequential or Random
    {
        for (size_t i = 0; i < chordPitches.size(); ++i)
        {
            // degreeMin/Max are 1-based (1-7). chordScaleDegrees is 0-6.
            int deg = chordScaleDegrees[i] + 1;
            if (deg >= degreeMin && deg <= degreeMax)
            {
                activeIndices.push_back(i);
            }
        }
    }

    // Fallback if no notes allowed: use all (or silence?) -> Use all to avoid silence
    if (activeIndices.empty())
    {
        for (size_t i = 0; i < chordPitches.size(); ++i)
            activeIndices.push_back(i);
    }

    // Get output bus
    auto      outputBus = getBusBuffer(buffer, false, 0);
    const int numOutputChannels = outputBus.getNumChannels();

    // 6. Arpeggiator Logic
    float currentArpPitch = 0.0f;
    float currentArpGate = 0.0f;
    int   currentVizDegree = -1;

    if (arpModeIdx == 0) // Off (Poly Chord)
    {
        // Output up to 4 voices (Pitch/Gate pairs)
        for (int i = 0; i < 4; ++i)
        {
            float p = (i < (int)chordPitches.size()) ? chordPitches[i] : 0.0f;
            float g = (i < (int)chordPitches.size()) ? 1.0f : 0.0f; // Gate On if note exists

            // Write pitch (channels 0, 2, 4, 6)
            if (numOutputChannels > i * 2)
            {
                auto* pitchOut = outputBus.getWritePointer(i * 2);
                if (pitchOut)
                    juce::FloatVectorOperations::fill(pitchOut, p, numSamples);
            }
            // Write gate (channels 1, 3, 5, 7)
            if (numOutputChannels > i * 2 + 1)
            {
                auto* gateOut = outputBus.getWritePointer(i * 2 + 1);
                if (gateOut)
                    juce::FloatVectorOperations::fill(gateOut, g, numSamples);
            }
        }
    }
    else // Arp On
    {
        // Advance Step
        if (currentTransport.isPlaying)
        {
            arpState.phase += numSamples;
            if (arpState.phase >= arpState.samplesPerStep)
            {
                arpState.phase -= arpState.samplesPerStep;

                // Advance Index based on Mode
                if (rangeMode == 2 && !activeIndices.empty()) // Random Range
                {
                    // Pick random index from activeIndices
                    static std::mt19937                rng(std::random_device{}());
                    std::uniform_int_distribution<int> dist(0, (int)activeIndices.size() - 1);
                    arpState.currentIndex = activeIndices[dist(rng)];
                }
                else if (!activeIndices.empty()) // Sequential
                {
                    // Simple cycle through active indices
                    static int stepCounter = 0;
                    stepCounter++;
                    arpState.currentIndex = activeIndices[stepCounter % activeIndices.size()];
                }
                else
                {
                    arpState.currentIndex = 0;
                }

                arpState.gateOn = true; // Trigger
            }
            else if (arpState.phase > arpState.samplesPerStep * 0.5f) // 50% Gate length
            {
                arpState.gateOn = false;
            }
        }
        else
        {
            arpState.gateOn = false;
        }

        // Output Arp (to channels 8 and 9: Arp Pitch and Arp Gate)
        if (arpState.currentIndex >= 0 && arpState.currentIndex < (int)chordPitches.size())
        {
            currentArpPitch = chordPitches[arpState.currentIndex];
            currentVizDegree = (arpState.currentIndex < (int)chordScaleDegrees.size())
                                   ? chordScaleDegrees[arpState.currentIndex]
                                   : -1;
        }

        currentArpGate = arpState.gateOn ? 1.0f : 0.0f;

        // Write to output channels 8 (Arp Pitch) and 9 (Arp Gate)
        if (numOutputChannels > 8)
        {
            auto* pitchOut = outputBus.getWritePointer(8);
            if (pitchOut)
                juce::FloatVectorOperations::fill(pitchOut, currentArpPitch, numSamples);
        }
        if (numOutputChannels > 9)
        {
            auto* gateOut = outputBus.getWritePointer(9);
            if (gateOut)
                juce::FloatVectorOperations::fill(gateOut, currentArpGate, numSamples);
        }
        
        // Also output chord voices when Arp is on (channels 0-7)
        for (int i = 0; i < 4; ++i)
        {
            float p = (i < (int)chordPitches.size()) ? chordPitches[i] : 0.0f;
            float g = (i < (int)chordPitches.size()) ? 1.0f : 0.0f; // Gate On if note exists

            // Write pitch (channels 0, 2, 4, 6)
            if (numOutputChannels > i * 2)
            {
                auto* pitchOut = outputBus.getWritePointer(i * 2);
                if (pitchOut)
                    juce::FloatVectorOperations::fill(pitchOut, p, numSamples);
            }
            // Write gate (channels 1, 3, 5, 7)
            if (numOutputChannels > i * 2 + 1)
            {
                auto* gateOut = outputBus.getWritePointer(i * 2 + 1);
                if (gateOut)
                    juce::FloatVectorOperations::fill(gateOut, g, numSamples);
            }
        }
    }

    // 7. Update Visualization Data
#if defined(PRESET_CREATOR_UI)
    vizData.degreeIn.store(degreeVal, std::memory_order_relaxed);
    vizData.rootCvIn.store(rootCvIn, std::memory_order_relaxed);

    // Store pitches for viz
    if (chordPitches.size() > 0)
        vizData.pitch1.store(chordPitches[0], std::memory_order_relaxed);
    if (chordPitches.size() > 1)
        vizData.pitch2.store(chordPitches[1], std::memory_order_relaxed);
    if (chordPitches.size() > 2)
        vizData.pitch3.store(chordPitches[2], std::memory_order_relaxed);
    if (chordPitches.size() > 3)
        vizData.pitch4.store(chordPitches[3], std::memory_order_relaxed);

    vizData.arpPitch.store(currentArpPitch, std::memory_order_relaxed);
    vizData.arpGate.store(currentArpGate, std::memory_order_relaxed);

    // Update Active Degree Range Viz
    for (int i = 0; i < 7; ++i)
    {
        int  deg = i + 1;
        bool isActive = (deg >= degreeMin && deg <= degreeMax);
        vizData.activeDegreeRange[i].store(isActive ? 1 : 0, std::memory_order_relaxed);
    }

    vizData.currentDegreeHighlight.store((float)currentVizDegree, std::memory_order_relaxed);
#endif

    // Update Output Telemetry
    updateOutputTelemetry(buffer);
}

//==============================================================================
bool ChordArpModuleProcessor::getParamRouting(
    const juce::String& paramId,
    int&                outBusIndex,
    int&                outChannelIndexInBus) const
{
    // All modulation inputs are on bus 0 (the single input bus)
    outBusIndex = 0;

    // Map virtual mod params to channel indices within bus 0
    if (paramId == paramIdDegreeMod)
    {
        outChannelIndexInBus = 0;
        return true;
    }
    if (paramId == paramIdRootCvMod)
    {
        outChannelIndexInBus = 1;
        return true;
    }
    if (paramId == paramIdChordModeMod)
    {
        outChannelIndexInBus = 2;
        return true;
    }
    if (paramId == paramIdArpRateMod)
    {
        outChannelIndexInBus = 3;
        return true;
    }
    if (paramId == paramIdScaleMod)
    {
        outChannelIndexInBus = 4;
        return true;
    }
    if (paramId == paramIdKeyMod)
    {
        outChannelIndexInBus = 5;
        return true;
    }
    if (paramId == paramIdVoicingMod)
    {
        outChannelIndexInBus = 6;
        return true;
    }
    if (paramId == paramIdArpModeMod)
    {
        outChannelIndexInBus = 7;
        return true;
    }
    if (paramId == paramIdRangeModeMod)
    {
        outChannelIndexInBus = 8;
        return true;
    }
    if (paramId == paramIdNumVoicesMod)
    {
        outChannelIndexInBus = 9;
        return true;
    }
    return false;
}

juce::String ChordArpModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
    case 0:
        return "Degree";
    case 1:
        return "Root CV";
    case 2:
        return "Mode Mod";
    case 3:
        return "Rate Mod";
    case 4:
        return "Scale Mod";
    case 5:
        return "Key Mod";
    case 6:
        return "Voicing Mod";
    case 7:
        return "Arp Mode Mod";
    case 8:
        return "Range Mode Mod";
    case 9:
        return "Voices Mod";
    default:
        return "In " + juce::String(channel + 1);
    }
}

juce::String ChordArpModuleProcessor::getAudioOutputLabel(int channel) const
{
    // Outputs: 0=Pitch1, 1=Gate1, 2=Pitch2, 3=Gate2, 4=Pitch3, 5=Gate3, 6=Pitch4, 7=Gate4
    if (channel < 8)
    {
        int  voice = channel / 2;
        bool isGate = (channel % 2) != 0;
        return "Voice " + juce::String(voice + 1) + (isGate ? " Gate" : " Pitch");
    }
    return "Out " + juce::String(channel + 1);
}

void ChordArpModuleProcessor::forceStop()
{
    arpState.gateOn = false;
    arpState.phase = 0.0;
    arpState.currentIndex = 0;
}

//==============================================================================
#if defined(PRESET_CREATOR_UI)

// Helper to convert StringArray to C-style array for ImGui
static std::vector<std::string> stringArrayToStd(const juce::StringArray& arr)
{
    std::vector<std::string> res;
    for (const auto& s : arr)
        res.push_back(s.toStdString());
    return res;
}

// Helper to create null-separated string for ImGui::Combo
static std::string stringArrayToComboString(const juce::StringArray& arr)
{
    std::string res;
    for (const auto& s : arr)
    {
        res += s.toStdString();
        res += '\0';
    }
    res += '\0';
    return res;
}

void ChordArpModuleProcessor::drawParametersInNode(
    float                                                   itemWidth,
    const std::function<bool(const juce::String& paramId)>& isParamModulated,
    const std::function<void()>&                            onModificationEnded)
{
    try
    {
        ImGui::PushID(this); // Unique ID for this node's UI - MUST be at the start
        
        // Root CV Visualization - at the top for maximum visibility
        const auto& theme = ThemeManager::getInstance().getCurrentTheme();
        ThemeText("Root CV Input", theme.text.section_header);
        ImGui::Spacing();
        
        // Read visualization data (thread-safe) - BEFORE BeginChild
        const float rootCvValue = vizData.rootCvIn.load();
        
        // Calculate note name from root CV (V/Oct: 0V = C-2, 1V = C-1, etc.)
        auto getNoteFromRootCv = [](float cv) -> juce::String
        {
            // V/Oct: 0V = C-2 (MIDI 0), 1V = C-1 (MIDI 12), etc.
            // Clamp to reasonable range: -2 to 8 octaves (MIDI 0-120)
            float midiNote = cv * 12.0f; // 0V = 0, 1V = 12, 2V = 24, etc.
            midiNote = juce::jlimit(0.0f, 120.0f, midiNote);
            int noteNum = (int)std::round(midiNote);
            const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            int octave = (noteNum / 12) - 2; // -2 to 8
            return juce::String(notes[noteNum % 12]) + juce::String(octave);
        };
        
        // CV visualization in child window
        const float cvVizHeight = 80.0f;
        const ImVec2 graphSize(itemWidth, cvVizHeight);
        const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        
        if (ImGui::BeginChild("RootCvViz", graphSize, false, childFlags))
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetWindowPos();
            const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
            
            // Background
            ImU32 bgColor = IM_COL32(18, 20, 24, 255);
            ImU32 frameColor = IM_COL32(150, 150, 150, 255);
            try
            {
                bgColor = ThemeManager::getInstance().getCanvasBackground();
                const auto& canvasTheme = ThemeManager::getInstance().getCurrentTheme().canvas;
                if (canvasTheme.node_frame != 0)
                    frameColor = canvasTheme.node_frame;
            }
            catch (...) {}
            drawList->AddRectFilled(p0, p1, bgColor, 4.0f);
            drawList->AddRect(p0, p1, frameColor, 4.0f, 0, 1.0f);
            
            // Clip to graph area
            drawList->PushClipRect(p0, p1, true);
            
            // Define colors
            const ImU32 gridColor = IM_COL32(50, 55, 65, 255);
            
            // Draw CV bar (horizontal, left to right, 0-1 range)
            const float normalizedCv = juce::jlimit(0.0f, 1.0f, rootCvValue);
            const float barWidth = graphSize.x * normalizedCv;
            const ImU32 barColor = ImGui::ColorConvertFloat4ToU32(theme.accent);
            const float barY = p0.y + graphSize.y * 0.5f - 10.0f;
            const float barHeight = 20.0f;
            
            // Always draw bar background (even when value is 0)
            const ImU32 barBgColor = IM_COL32(40, 40, 40, 255);
            drawList->AddRectFilled(
                ImVec2(p0.x, barY),
                ImVec2(p0.x + graphSize.x, barY + barHeight),
                barBgColor, 2.0f);
            
            // Draw filled portion if value > 0
            if (barWidth > 0.0f)
            {
                drawList->AddRectFilled(
                    ImVec2(p0.x, barY),
                    ImVec2(p0.x + barWidth, barY + barHeight),
                    barColor, 2.0f);
            }
            
            // Draw border around bar
            drawList->AddRect(
                ImVec2(p0.x, barY),
                ImVec2(p0.x + graphSize.x, barY + barHeight),
                gridColor, 2.0f, 0, 1.0f);
            
            // Draw center line (0.5V mark)
            const float centerX = p0.x + graphSize.x * 0.5f;
            drawList->AddLine(
                ImVec2(centerX, p0.y),
                ImVec2(centerX, p1.y),
                gridColor, 1.0f);
            
            // Draw scale markers (0V, 1V, 2V, etc.)
            for (int i = 0; i <= 4; ++i)
            {
                const float markerX = p0.x + (graphSize.x * i / 4.0f);
                drawList->AddLine(
                    ImVec2(markerX, p1.y - 8.0f),
                    ImVec2(markerX, p1.y),
                    gridColor, 1.0f);
            }
            
            drawList->PopClipRect();
            
            // Text overlay: CV value and note name (drawn using ImGui widgets, not drawList)
            ImGui::SetCursorPos(ImVec2(8, 8));
            juce::String cvText = "Root CV: " + juce::String(rootCvValue, 3) + "V";
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.9f), cvText.toRawUTF8());
            
            ImGui::SetCursorPos(ImVec2(8, 24));
            juce::String noteText = "Note: " + getNoteFromRootCv(rootCvValue);
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 0.8f), noteText.toRawUTF8());
        }
        ImGui::EndChild();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Scale & Key
        ImGui::PushItemWidth(itemWidth * 0.5f - 4);

        // Scale (Modulatable)
        auto* scaleP = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdScale));
        if (scaleP)
        {
            bool scaleMod = isParamModulated(paramIdScaleMod);
            if (scaleMod)
                ImGui::BeginDisabled();
            int scaleVal = scaleMod
                               ? (int)getLiveParamValueFor(
                                     paramIdScaleMod, "scale_live", (float)scaleP->getIndex())
                               : scaleP->getIndex();
            std::string scalesStr = stringArrayToComboString(scaleP->choices);
            if (ImGui::Combo("##scale", &scaleVal, scalesStr.c_str()))
            {
                if (!scaleMod)
                {
                    *scaleP = scaleVal;
                    onModificationEnded();
                }
            }
            if (!scaleMod)
                adjustParamOnWheel(scaleP, paramIdScale, (float)scaleVal);
            if (scaleMod)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted("(mod)");
            }
        }
        ImGui::SameLine();

        // Key (Modulatable)
        auto* keyP = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdKey));
        if (keyP)
        {
            bool keyMod = isParamModulated(paramIdKeyMod);
            if (keyMod)
                ImGui::BeginDisabled();
            int keyVal = keyMod
                             ? (int)getLiveParamValueFor(
                                   paramIdKeyMod, "key_live", (float)keyP->getIndex())
                             : keyP->getIndex();
            std::string keysStr = stringArrayToComboString(keyP->choices);
            if (ImGui::Combo("##key", &keyVal, keysStr.c_str()))
            {
                if (!keyMod)
                {
                    *keyP = keyVal;
                    onModificationEnded();
                }
            }
            if (!keyMod)
                adjustParamOnWheel(keyP, paramIdKey, (float)keyVal);
            if (keyMod)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted("(mod)");
            }
        }
        ImGui::PopItemWidth();

        // 2. Chord Mode (Modulatable)
        auto* modeP =
            dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdChordMode));
        if (modeP)
        {
            bool modeMod = isParamModulated(paramIdChordModeMod);

            if (modeMod)
                ImGui::BeginDisabled();
            int modeVal = modeMod
                              ? (int)getLiveParamValueFor(
                                    paramIdChordModeMod, "chordMode_live", (float)modeP->getIndex())
                              : modeP->getIndex();

            ImGui::PushItemWidth(itemWidth);
            std::string modesStr = stringArrayToComboString(modeP->choices);
            if (ImGui::Combo("Mode", &modeVal, modesStr.c_str()))
            {
                *modeP = modeVal;
                onModificationEnded();
            }
            if (modeMod)
                ImGui::EndDisabled();
            if (!modeMod)
                adjustParamOnWheel(modeP, paramIdChordMode, (float)modeVal);
        }

        // 3. Voicing (Modulatable)
        auto* voiceP =
            dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdVoicing));
        if (voiceP)
        {
            bool voicingMod = isParamModulated(paramIdVoicingMod);
            if (voicingMod)
                ImGui::BeginDisabled();
            int voiceVal = voicingMod
                               ? (int)getLiveParamValueFor(
                                     paramIdVoicingMod, "voicing_live", (float)voiceP->getIndex())
                               : voiceP->getIndex();
            std::string voicingsStr = stringArrayToComboString(voiceP->choices);
            if (ImGui::Combo("Voicing", &voiceVal, voicingsStr.c_str()))
            {
                if (!voicingMod)
                {
                    *voiceP = voiceVal;
                    onModificationEnded();
                }
            }
            if (!voicingMod)
                adjustParamOnWheel(voiceP, paramIdVoicing, (float)voiceVal);
            if (voicingMod)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted("(mod)");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // 4. Arp Controls
        auto* arpModeP =
            dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdArpMode));
        if (arpModeP)
        {
            bool arpModeMod = isParamModulated(paramIdArpModeMod);
            if (arpModeMod)
                ImGui::BeginDisabled();
            int arpModeVal = arpModeMod
                                 ? (int)getLiveParamValueFor(
                                       paramIdArpModeMod, "arpMode_live", (float)arpModeP->getIndex())
                                 : arpModeP->getIndex();
            std::string arpModesStr = stringArrayToComboString(arpModeP->choices);
            if (ImGui::Combo("Arp", &arpModeVal, arpModesStr.c_str()))
            {
                if (!arpModeMod)
                {
                    *arpModeP = arpModeVal;
                    onModificationEnded();
                }
            }
            if (!arpModeMod)
                adjustParamOnWheel(arpModeP, paramIdArpMode, (float)arpModeVal);
            if (arpModeMod)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted("(mod)");
            }
        }

        // Division (Modulatable)
        auto* divP =
            dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdArpDivision));
        if (divP)
        {
            bool rateMod = isParamModulated(paramIdArpRateMod);

            if (rateMod)
                ImGui::BeginDisabled();
            int divVal = rateMod
                             ? (int)getLiveParamValueFor(
                                   paramIdArpRateMod, "arpDivision_live", (float)divP->getIndex())
                             : divP->getIndex();

            std::string divsStr = stringArrayToComboString(divP->choices);
            if (ImGui::Combo("Rate", &divVal, divsStr.c_str()))
            {
                *divP = divVal;
                onModificationEnded();
            }
            if (rateMod)
                ImGui::EndDisabled();
            if (!rateMod)
                adjustParamOnWheel(divP, paramIdArpDivision, (float)divVal);
        }

        ImGui::PopItemWidth();

        // 5. Degree Range Visualization & Control
        ImGui::Text("Active Degrees:");

        // Safety check: ensure itemWidth is valid
        if (itemWidth <= 0.0f)
            return;

        // Safety check: ensure parameters exist
        if (!degreeRangeMinParam || !degreeRangeMaxParam)
            return;

        // Draw 7 clickable slots
        float slotWidth = itemWidth / 7.0f;
        float slotHeight = 20.0f;

        // Use raw parameter values (thread-safe) - already checked for null above
        int currentMin = juce::jlimit(1, 7, (int)degreeRangeMinParam->load());
        int currentMax = juce::jlimit(1, 7, (int)degreeRangeMaxParam->load());

        // Get parameter pointers for setting values
        auto* minP =
            dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(paramIdDegreeRangeMin));
        auto* maxP =
            dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(paramIdDegreeRangeMax));

        if (!minP || !maxP)
            return;

        // Safe theme access with fallback (OUTSIDE loop)
        ImU32 bgColor = IM_COL32(30, 30, 30, 255); // Default dark gray
        ImVec4 accentColor = ImVec4(0.8f, 0.6f, 0.2f, 1.0f); // Default orange
        
        try
        {
            bgColor = ThemeManager::getInstance().getCanvasBackground();
            accentColor = ThemeManager::getInstance().getCurrentTheme().accent;
        }
        catch (...)
        {
            // Use defaults
        }

        float highlightDegFloat = vizData.currentDegreeHighlight.load();
        int   highlightDeg = (highlightDegFloat >= 0.0f && highlightDegFloat < 7.0f)
                                 ? (int)highlightDegFloat
                                 : -1;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        for (int i = 0; i < 7; ++i)
        {
            int  deg = i + 1;
            bool isActive = (deg >= currentMin && deg <= currentMax);
            bool isHighlight = (i == highlightDeg);

            // Convert ImU32 (ARGB) to ImU32 color for button
            ImU32 baseColorU32;
            if (isActive)
            {
                baseColorU32 = ImGui::ColorConvertFloat4ToU32(accentColor);
            }
                else
                {
                    // Extract ARGB components from bgColor and make brighter
                    uint8_t a = (bgColor >> 24) & 0xFF;
                    uint8_t r = (bgColor >> 16) & 0xFF;
                    uint8_t g = (bgColor >> 8) & 0xFF;
                    uint8_t b = bgColor & 0xFF;
                    // Make brighter (simple approach: increase RGB values)
                    r = juce::jmin(255, (int)r + 25);
                    g = juce::jmin(255, (int)g + 25);
                    b = juce::jmin(255, (int)b + 25);
                    baseColorU32 = IM_COL32(r, g, b, a);
                }

                if (isHighlight)
                    baseColorU32 = IM_COL32(255, 255, 255, 255); // White

                ImGui::PushStyleColor(ImGuiCol_Button, baseColorU32);

                juce::String label = juce::String(deg);
                if (ImGui::Button(label.toRawUTF8(), ImVec2(slotWidth, slotHeight)))
                {
                    int distMin = std::abs(deg - currentMin);
                    int distMax = std::abs(deg - currentMax);

                    if (distMin < distMax)
                    {
                        minP->beginChangeGesture();
                        *minP = deg;
                        minP->endChangeGesture();
                    }
                    else
                    {
                        maxP->beginChangeGesture();
                        *maxP = deg;
                        maxP->endChangeGesture();
                    }

                    onModificationEnded();
                }
                ImGui::PopStyleColor();

                if (i < 6)
                    ImGui::SameLine();
        }
        ImGui::PopStyleVar();

        // Range Sliders & Mode
        ImGui::PushItemWidth(itemWidth);

        // Range Mode (Modulatable)
        auto* rangeModeP =
            dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdDegreeRangeMode));
        if (rangeModeP)
        {
            bool rangeModeMod = isParamModulated(paramIdRangeModeMod);
            if (rangeModeMod)
                ImGui::BeginDisabled();
            int rangeModeVal = rangeModeMod
                                   ? (int)getLiveParamValueFor(
                                         paramIdRangeModeMod, "rangeMode_live", (float)rangeModeP->getIndex())
                                   : rangeModeP->getIndex();
            std::string rangeModesStr = stringArrayToComboString(rangeModeP->choices);
            if (ImGui::Combo("Range Mode", &rangeModeVal, rangeModesStr.c_str()))
            {
                if (!rangeModeMod)
                {
                    *rangeModeP = rangeModeVal;
                    onModificationEnded();
                }
            }
            if (!rangeModeMod)
                adjustParamOnWheel(rangeModeP, paramIdDegreeRangeMode, (float)rangeModeVal);
            if (rangeModeMod)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted("(mod)");
            }
        }
        
        // NumVoices (Modulatable)
        auto* voicesP = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(paramIdNumVoices));
        if (voicesP)
        {
            bool voicesMod = isParamModulated(paramIdNumVoicesMod);
            if (voicesMod)
                ImGui::BeginDisabled();
            int voicesVal = voicesMod
                                ? (int)getLiveParamValueFor(
                                      paramIdNumVoicesMod, "numVoices_live", (float)voicesP->get())
                                : voicesP->get();
            if (ImGui::SliderInt("Voices", &voicesVal, 1, 4))
            {
                if (!voicesMod)
                {
                    *voicesP = voicesVal;
                    onModificationEnded();
                }
            }
            if (!voicesMod)
                adjustParamOnWheel(voicesP, paramIdNumVoices, (float)voicesVal);
            if (voicesMod)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextUnformatted("(mod)");
            }
        }

        ImGui::PopItemWidth();

        ImGui::Separator();

        // 6. Auto-Connect Button
        if (ImGui::Button("Connect to PolyVCO", ImVec2(itemWidth, 0)))
        {
            autoConnectVCOTriggered.store(true);
        }
        
        ImGui::PopID(); // Close ID scope at the end of the function
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog(
            "[ChordArp] Exception in drawParametersInNode: " + juce::String(e.what()));
    }
    catch (...)
    {
        juce::Logger::writeToLog("[ChordArp] Unknown exception in drawParametersInNode");
    }
}

void ChordArpModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Draw pins using drawParallelPins for input/output pairs
    // Inputs paired with outputs
    helpers.drawParallelPins("Degree In", 0, "Pitch 1", 0);
    helpers.drawParallelPins("Root CV In", 1, "Gate 1", 1);
    helpers.drawParallelPins("Chord Mode Mod", 2, "Pitch 2", 2);
    helpers.drawParallelPins("Arp Rate Mod", 3, "Gate 2", 3);
    helpers.drawParallelPins("Scale Mod", 4, "Pitch 3", 4);
    helpers.drawParallelPins("Key Mod", 5, "Gate 3", 5);
    helpers.drawParallelPins("Voicing Mod", 6, "Pitch 4", 6);
    helpers.drawParallelPins("Arp Mode Mod", 7, "Gate 4", 7);
    helpers.drawParallelPins("Range Mode Mod", 8, "Arp Pitch", 8);
    helpers.drawParallelPins("Voices Mod", 9, "Arp Gate", 9);
}

#endif
