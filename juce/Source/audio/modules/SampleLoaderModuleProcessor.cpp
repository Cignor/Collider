#include "SampleLoaderModuleProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../utils/RtLogger.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

SampleLoaderModuleProcessor::SampleLoaderModuleProcessor()
    : ModuleProcessor(BusesProperties()
        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(7), true) // 7 modulation inputs
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
        buffer.clear();
        return;
    }
    auto inBus = getBusBuffer(buffer, true, 0);
    const int numSamples = buffer.getNumSamples();

    // --- 1. TRIGGER DETECTION ---
    // Check for a rising edge on the trigger input to start playback.
    if (isParamInputConnected("trigger_mod") && inBus.getNumChannels() > 3)
    {
        const float* trigSignal = inBus.getReadPointer(3);
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
        if (numSamples > 0) lastTriggerHigh = (inBus.getReadPointer(3)[numSamples - 1] > 0.5f);
    }

    // --- Randomize Trigger ---
    if (isParamInputConnected("randomize_mod") && inBus.getNumChannels() > 6)
    {
        const float* randTrigSignal = inBus.getReadPointer(6);
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
        if (numSamples > 0) lastRandomizeTriggerHigh = (inBus.getReadPointer(6)[numSamples - 1] > 0.5f);
    }

    // --- 2. CONDITIONAL AUDIO RENDERING ---
    // Only generate audio if the internal voice is in a playing state.
    if (currentProcessor->isPlaying)
    {
        // Update block-rate params like speed and pitch
        float speedNow = apvts.getRawParameterValue("speed")->load();
        if (isParamInputConnected("speed_mod") && inBus.getNumChannels() > 1)
            speedNow = juce::jmap(inBus.getReadPointer(1)[0], 0.0f, 1.0f, 0.25f, 4.0f);
        
        float pitchNow = apvts.getRawParameterValue("pitch")->load();
        if (isParamInputConnected("pitch_mod") && inBus.getNumChannels() > 0)
        {
            // Absolute control from CV with robust scaling
            const float raw = inBus.getReadPointer(0)[0];
            // Heuristic: accept either unipolar [0..1] or bipolar [-1..1]
            const bool looksUnipolar = (raw >= 0.0f && raw <= 1.0f);
            const float cv01 = looksUnipolar ? juce::jlimit(0.0f, 1.0f, raw)
                                             : juce::jlimit(0.0f, 1.0f, (juce::jlimit(-1.0f, 1.0f, raw) + 1.0f) * 0.5f);
            pitchNow = juce::jmap(cv01, 0.0f, 1.0f, -24.0f, 24.0f);
        }

        float startNorm = rangeStartParam->load();
        if (isParamInputConnected("rangeStart_mod") && inBus.getNumChannels() > 4)
            startNorm = juce::jlimit(0.0f, 1.0f, inBus.getReadPointer(4)[0]);

        float endNorm = rangeEndParam->load();
        if (isParamInputConnected("rangeEnd_mod") && inBus.getNumChannels() > 5)
            endNorm = juce::jlimit(0.0f, 1.0f, inBus.getReadPointer(5)[0]);

        // Ensure valid range (start < end)
        if (startNorm >= endNorm)
        {
            if (isParamInputConnected("rangeStart_mod"))
                startNorm = juce::jmax(0.0f, endNorm - 0.001f);
            else if (isParamInputConnected("rangeEnd_mod"))
                endNorm = juce::jmin(1.0f, startNorm + 0.001f);
        }

        currentProcessor->setZoneTimeStretchRatio(speedNow);
        currentProcessor->setBasePitchSemitones(pitchNow);
        const int sourceLength = currentSample->stereo.getNumSamples();
        currentProcessor->setPlaybackRange(startNorm * sourceLength, endNorm * sourceLength);

        // Update telemetry for live UI feedback
        setLiveParamValue("speed_live", speedNow);
        setLiveParamValue("pitch_live", pitchNow);
        setLiveParamValue("rangeStart_live", startNorm);
        setLiveParamValue("rangeEnd_live", endNorm);

        // Update APVTS parameters for UI feedback (especially spectrogram handles)
        *rangeStartParam = startNorm;
        apvts.getParameter("rangeStart")->sendValueChangedMessageToListeners(startNorm);
        *rangeEndParam = endNorm;
        apvts.getParameter("rangeEnd")->sendValueChangedMessageToListeners(endNorm);

        const int engineIdx = (int) apvts.getRawParameterValue("engine")->load();
        currentProcessor->setEngine(engineIdx == 0 ? SampleVoiceProcessor::Engine::RubberBand : SampleVoiceProcessor::Engine::Naive);
        currentProcessor->setRubberBandOptions(apvts.getRawParameterValue("rbWindowShort")->load() > 0.5f, apvts.getRawParameterValue("rbPhaseInd")->load() > 0.5f);
        currentProcessor->setLooping(apvts.getRawParameterValue("loop")->load() > 0.5f);

        // Generate the sample's audio into the buffer. This might set isPlaying to false if the sample ends.
        try {
            currentProcessor->renderBlock(buffer, midiMessages);
        } catch (...) {
            RtLogger::postf("[SampleLoader][FATAL] renderBlock exception");
            buffer.clear();
        }

        // --- 3. GATE (VCA) APPLICATION ---
        // If a gate is connected, use it to shape the volume of the audio we just generated.
        if (isParamInputConnected("gate_mod") && inBus.getNumChannels() > 2)
        {
            const float* gateCV = inBus.getReadPointer(2);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                float* channelData = buffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    channelData[i] *= juce::jlimit(0.0f, 1.0f, gateCV[i]);
                }
            }
        }
        
        // Apply main gate knob last
        buffer.applyGain(apvts.getRawParameterValue("gate")->load());
    }
    else
    {
        // If not playing, the buffer must be silent.
        buffer.clear();
    }
    
    // Update output values for cable inspector using block peak
    if (lastOutputValues.size() >= 2)
    {
        auto peakAbs = [&](int ch){ if (ch >= buffer.getNumChannels()) return 0.0f; const float* p = buffer.getReadPointer(ch); float m=0.0f; for (int i=0;i<buffer.getNumSamples();++i) m = juce::jmax(m, std::abs(p[i])); return m; };
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

    // 2) Create a private copy and convert to mono
    auto privateCopy = std::make_shared<SampleBank::Sample>();
    privateCopy->sampleRate = original->sampleRate;
    const int numSamples = original->stereo.getNumSamples();
    privateCopy->stereo.setSize(1, numSamples);

    if (original->stereo.getNumChannels() <= 1)
    {
        privateCopy->stereo.copyFrom(0, 0, original->stereo, 0, 0, numSamples);
    }
    else
    {
        privateCopy->stereo.clear();
        privateCopy->stereo.addFrom(0, 0, original->stereo, 0, 0, numSamples, 0.5f);
        privateCopy->stereo.addFrom(0, 0, original->stereo, 1, 0, numSamples, 0.5f);
    }

    // 3) Atomically assign our private copy for this module
    currentSample = privateCopy;
    DBG("[Sample Loader] Loaded and created private mono copy of: " << currentSampleName);
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
    newProcessor->reset(); // This will set the read position to startSample
    
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
    ImGui::PushItemWidth(itemWidth);

    // Make the node a drop target for samples
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SAMPLE_PATH"))
        {
            // A sample was dropped on this node
            const char* path = (const char*)payload->Data;
            loadSample(juce::File(path));
            onModificationEnded(); // Create an undo state for the change
        }
        ImGui::EndDragDropTarget();
    }

    if (hasSampleLoaded()) { ImGui::Text("Sample: %s", currentSampleName.toRawUTF8()); }
    else { ImGui::Text("No sample loaded"); }

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
    float gate = apvts.getRawParameterValue("gate")->load();
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
    if (ImGui::SliderFloat("Range Start", &rangeStart, 0.0f, 1.0f, "%.3f"))
    {
        apvts.getParameter("rangeStart")->setValueNotifyingHost(apvts.getParameterRange("rangeStart").convertTo0to1(rangeStart));
        onModificationEnded();
    }
    if (! rangeStartModulated)
        ModuleProcessor::adjustParamOnWheel(apvts.getParameter("rangeStart"), "rangeStart", rangeStart);
    if (rangeStartModulated) { ImGui::PopStyleColor(); ImGui::EndDisabled(); }
    
    bool rangeEndModulated = isParamModulated("rangeEnd_mod");
    if (rangeEndModulated) { ImGui::BeginDisabled(); ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 0.0f, 0.3f)); }
    float rangeEnd = rangeEndModulated ? getLiveParamValueFor("rangeEnd_mod", "rangeEnd_live", rangeEndParam->load()) 
                                       : rangeEndParam->load();
    if (ImGui::SliderFloat("Range End", &rangeEnd, 0.0f, 1.0f, "%.3f"))
    {
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
    // Audio output
    helpers.drawAudioOutputPin("Audio Output", 0);
}
#endif

// Parameter bus contract implementation
bool SampleLoaderModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All inputs are on bus 0
    if (paramId == "pitch_mod") { outChannelIndexInBus = 0; return true; }      // Pitch Mod
    if (paramId == "speed_mod") { outChannelIndexInBus = 1; return true; }      // Speed Mod
    if (paramId == "gate_mod") { outChannelIndexInBus = 2; return true; }       // Gate Mod
    if (paramId == "trigger_mod") { outChannelIndexInBus = 3; return true; }    // Trigger Mod
    if (paramId == "rangeStart_mod") { outChannelIndexInBus = 4; return true; } // Range Start Mod
    if (paramId == "rangeEnd_mod") { outChannelIndexInBus = 5; return true; }   // Range End Mod
    if (paramId == "randomize_mod") { outChannelIndexInBus = 6; return true; }  // Randomize Trig
    return false;
}