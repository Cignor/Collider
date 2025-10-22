#include "AudioEngine.h"
#include "voices/SampleVoiceProcessor.h"
#include "voices/SynthVoiceProcessor.h"
#include "voices/NoiseVoiceProcessor.h"
#include "graph/ModularSynthProcessor.h"
#include "voices/ModularVoice.h"
#include "fx/GainProcessor.h"
#include "../ipc/OscConfig.h"

AudioEngine::AudioEngine(juce::AudioDeviceManager& adm)
    : deviceManager(adm), oscServer(commandBus)
{
    mainGraph = std::make_unique<juce::AudioProcessorGraph>();

    // Ensure the main graph is configured for stereo in/out
    {
        juce::AudioProcessor::BusesLayout layout {
            juce::AudioChannelSet::stereo(),
            juce::AudioChannelSet::stereo()
        };
        if (! mainGraph->setBusesLayout (layout))
        {
            // Fallback to play-config details if layout setting is rejected
            mainGraph->setPlayConfigDetails (2, 2, 48000.0, 512);
        }
    }
    audioOutputNode = mainGraph->addNode (std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    
    // Insert master gain node and route graph: Voices -> MasterGain -> Output
    auto master = std::make_unique<GainProcessor>();
    masterGainNode = mainGraph->addNode (std::move (master));
    {
        using NodeAndChannel = juce::AudioProcessorGraph::NodeAndChannel;
        juce::AudioProcessorGraph::Connection cL { NodeAndChannel{ masterGainNode->nodeID, 0 }, NodeAndChannel{ audioOutputNode->nodeID, 0 } };
        juce::AudioProcessorGraph::Connection cR { NodeAndChannel{ masterGainNode->nodeID, 1 }, NodeAndChannel{ audioOutputNode->nodeID, 1 } };
        mainGraph->addConnection (cL);
        mainGraph->addConnection (cR);

        // Constant tone removed
    }

    // Bind OSC server (Python -> JUCE)
    oscServer.bind (OscConfig::kJuceServerPort);
    // Connect OSC client (JUCE -> Python)
    oscClient.connect (OscConfig::kPythonHost, OscConfig::kPythonPort);

    // Kick main logic loop
    startTimerHz (120);
}
static void logGraphTopology (juce::AudioProcessorGraph* g)
{
    if (g == nullptr) return;
    juce::Logger::writeToLog ("--- MAIN GRAPH TOPOLOGY ---");
    auto nodes = g->getNodes();
    for (auto* n : nodes)
    {
        if (n == nullptr) continue;
        auto* p = n->getProcessor();
        const int ins  = p ? p->getTotalNumInputChannels()  : 0;
        const int outs = p ? p->getTotalNumOutputChannels() : 0;
        juce::Logger::writeToLog ("  Node: id=" + juce::String ((int) n->nodeID.uid)
            + " name='" + (p ? p->getName() : juce::String("<null>")) + "' ins=" + juce::String (ins)
            + " outs=" + juce::String (outs));
    }
    auto conns = g->getConnections();
    for (auto& c : conns)
    {
        juce::Logger::writeToLog ("  Conn: [" + juce::String ((int)c.source.nodeID.uid) + ":" + juce::String (c.source.channelIndex)
            + "] -> [" + juce::String ((int)c.destination.nodeID.uid) + ":" + juce::String (c.destination.channelIndex) + "]");
    }
    juce::Logger::writeToLog ("---------------------------");
}

AudioEngine::~AudioEngine()
{
    stopTimer();
}

void AudioEngine::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    lastSampleRate = sampleRate;
    lastBlockSize = samplesPerBlockExpected;
    // Ensure the graph I/O configuration matches the device: 2 inputs, 2 outputs
    if (mainGraph)
    {
        mainGraph->setPlayConfigDetails (2, 2, sampleRate, samplesPerBlockExpected);
        // Do NOT override individual node play configs here; the graph manages node formats.
    }
    mainGraph->prepareToPlay (sampleRate, samplesPerBlockExpected);
    appendLog ("Engine prepared: sr=" + juce::String (sampleRate) + " block=" + juce::String (samplesPerBlockExpected));

    // DIAGNOSTIC: dump current output device and master gain
    auto* dev = deviceManager.getCurrentAudioDevice();
    appendLog ("Device: out='" + (dev ? dev->getName() : juce::String("<none>")) + "' sr="
               + juce::String (dev ? dev->getCurrentSampleRate() : 0.0)
               + " bs=" + juce::String (dev ? dev->getCurrentBufferSizeSamples() : 0));
}

void AudioEngine::sendFullInfoSnapshot()
{
    // Enumerate devices and send info to Python
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    juce::StringArray ins, outs;
    if (auto* t = deviceManager.getAvailableDeviceTypes().getFirst())
    {
        t->scanForDevices();
        ins = t->getDeviceNames(true);
        outs = t->getDeviceNames(false);
    }
    DBG ("AudioEngine::sendFullInfoSnapshot - sending devices and settings");
    oscClient.sendDeviceList ("input", ins);
    oscClient.sendDeviceList ("output", outs);
    // MIDI inputs
    {
        juce::StringArray midiIns;
        auto midiDevs = juce::MidiInput::getAvailableDevices();
        for (auto& d : midiDevs) midiIns.add (d.name);
        oscClient.sendMidiDeviceList (midiIns);
    }
    const float sr = (float) (deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 0.0f);
    const int bs  = (deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples() : 0);
    oscClient.sendCurrentSettings (setup.inputDeviceName, setup.outputDeviceName, sr, bs);
    // Send latest master gain if available
    if (masterGainNode != nullptr)
    {
        float g = 1.0f;
        if (auto* gp = dynamic_cast<GainProcessor*> (masterGainNode->getProcessor()))
        {
            if (auto* p = gp->getAPVTS().getRawParameterValue ("gain"))
                g = p->load();
        }
        oscClient.sendMasterGain (g);
    }
}

void AudioEngine::releaseResources()
{
    if (mainGraph) mainGraph->releaseResources();
}

juce::AudioProcessorGraph::Node::Ptr AudioEngine::connectAndAddVoice (std::unique_ptr<juce::AudioProcessor> processor)
{
    if (! mainGraph)
        return {};

    auto node = mainGraph->addNode (std::move (processor));
    using NodeAndChannel = juce::AudioProcessorGraph::NodeAndChannel;
    
    // Ensure bus layout on the voice processor is active and matches the graph's 0-in/2-out
    if (auto* vp = node->getProcessor())
    {
        const double sr = lastSampleRate > 0.0 ? lastSampleRate : 48000.0;
        const int    bs = lastBlockSize  > 0   ? lastBlockSize  : 512;
        vp->enableAllBuses();
        vp->setPlayConfigDetails (0, 2, sr, bs);
        vp->prepareToPlay (sr, bs);
    }

    // Connect the voice's audio output to the master gain, with diagnostics on failure
    juce::AudioProcessorGraph::Connection cL { NodeAndChannel{ node->nodeID, 0 }, NodeAndChannel{ masterGainNode->nodeID, 0 } };
    juce::AudioProcessorGraph::Connection cR { NodeAndChannel{ node->nodeID, 1 }, NodeAndChannel{ masterGainNode->nodeID, 1 } };
    const bool okL = mainGraph->addConnection (cL);
    const bool okR = mainGraph->addConnection (cR);
    if (!okL || !okR)
    {
        auto* src = node->getProcessor();
        auto* dst = masterGainNode ? masterGainNode->getProcessor() : nullptr;
        appendLog ("[ERR] Failed to connect voice->master: okL=" + juce::String (okL ? 1 : 0) +
                   " okR=" + juce::String (okR ? 1 : 0) +
                   " srcOutCh=" + juce::String (src ? src->getTotalNumOutputChannels() : -1) +
                   " dstInCh=" + juce::String (dst ? dst->getTotalNumInputChannels() : -1));
    }

    // Ensure the processing topology is updated after adding a node and connections
    mainGraph->rebuild();

    // Dump and also append summary for diagnostics
    logGraphTopology (mainGraph.get());
    appendLog ("Graph after add: nodes=" + juce::String ((int) mainGraph->getNodes().size()) +
               " conns=" + juce::String ((int) mainGraph->getConnections().size()));
    
    return node;
}

void AudioEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr)
        return;

    const int numCh = bufferToFill.buffer->getNumChannels();
    const int start = bufferToFill.startSample;
    const int num   = bufferToFill.numSamples;
    if (numCh <= 0 || num <= 0)
        return;

    // Clear just the region we're responsible for and process
    bufferToFill.buffer->clear (start, num);
    // Render graph into scratch buffer and copy back into the requested region
    // Build a zero-copy view into the requested region and process directly
    juce::HeapBlock<float*> chans (numCh);
    for (int ch = 0; ch < numCh; ++ch)
        chans[ch] = bufferToFill.buffer->getWritePointer (ch) + start;
    juce::AudioBuffer<float> view (chans.getData(), numCh, num);
    juce::MidiBuffer midi;
    mainGraph->processBlock (view, midi);
    // Per-callback quick peek at level before master output
    if (masterGainNode)
    {
        auto* proc = masterGainNode->getProcessor();
        juce::ignoreUnused (proc); // reserved for future deeper probes
    }

    // DIAGNOSTIC: if silent, log once per second-ish (no safety tone injection)
    const float pk = bufferToFill.buffer->getMagnitude (start, num);
    lastOutputPeak.store (pk);
    static int silentCounter = 0;
    if (pk < 1.0e-6f && (++silentCounter % 120) == 0)
        appendLog ("WARN: Output magnitude near zero for recent callbacks");
}

void AudioEngine::timerCallback()
{
    // Drain commands
    for (int i = 0; i < 4096; ++i)
    {
        Command c; if (! commandBus.tryDequeue (c)) break;
        if (c.type == Command::Type::Create)          handleCreateVoice (c);
        else if (c.type == Command::Type::Destroy)    handleDestroyVoice (c.voiceId);
        else if (c.type == Command::Type::Update)     handleUpdateParam (c);
        else if (c.type == Command::Type::DebugDump)  dumpCurrentStateToLog();
        // ADD THIS ENTIRE NEW BLOCK:
        else if (c.type == Command::Type::LoadPreset)
        {
            // a. Find the target voice by its unique ID
            auto it = activeVoices.find(c.voiceId);
            if (it != activeVoices.end())
            {
                // b. Safely check if this voice is a ModularVoice wrapper
                if (auto* mv = dynamic_cast<ModularVoice*>(it->second->getProcessor()))
                {
                    if (auto* msp = mv->getModularSynth()) // Get the internal synth
                    {
                        // c. Convert the XML string to a MemoryBlock and load the state
                        juce::MemoryBlock mb(c.presetData.toRawUTF8(), c.presetData.getNumBytesAsUTF8());
                        msp->setStateInformation(mb.getData(), (int)mb.getSize());
                        appendLog("Loaded preset onto Modular Synth voice ID: " + juce::String((juce::int64)c.voiceId));
                    }
                }
            }
        }
        else if (c.type == Command::Type::LoadPatchState)
        {
            // Load a snapshot from the Snapshot Sequencer
            auto it = activeVoices.find(c.voiceId);
            if (it != activeVoices.end())
            {
                if (auto* mv = dynamic_cast<ModularVoice*>(it->second->getProcessor()))
                {
                    if (auto* msp = mv->getModularSynth())
                    {
                        // The patchState is already a MemoryBlock, ready to load
                        msp->setStateInformation(c.patchState.getData(), (int)c.patchState.getSize());
                        appendLog("[SnapshotSeq] Loaded patch state for voice ID: " + juce::String((juce::int64)c.voiceId) +
                                " (size: " + juce::String((int)c.patchState.getSize()) + " bytes)");
                    }
                }
            }
        }
        else if (c.type == Command::Type::ResetFx)
        {
            auto it = activeVoices.find(c.voiceId);
            if (it != activeVoices.end())
            {
                if (auto* vp = dynamic_cast<VoiceProcessor*>(it->second->getProcessor()))
                {
                    resetVoiceParamsToDefaults(vp);
                    appendLog("Reset FX for voice ID: " + juce::String((juce::int64)c.voiceId));
                }
            }
        }
        else if (c.type == Command::Type::RandomizePitch)
        {
            auto it = activeVoices.find(c.voiceId);
            if (it != activeVoices.end())
            {
                if (auto* vp = dynamic_cast<VoiceProcessor*>(it->second->getProcessor()))
                {
                    if (auto* p = vp->getAPVTS().getParameter("pitchSemitones"))
                    {
                        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
                        {
                            float randomPitch = juce::Random::getSystemRandom().nextFloat() * 24.0f - 12.0f; // -12 to +12 semitones
                            r->setValueNotifyingHost(r->getNormalisableRange().convertTo0to1(randomPitch));
                            appendLog("Randomized pitch for voice ID: " + juce::String((juce::int64)c.voiceId) + " to " + juce::String(randomPitch) + " semitones");
                        }
                    }
                }
            }
        }
        else if (c.type == Command::Type::RandomizeTime)
        {
            auto it = activeVoices.find(c.voiceId);
            if (it != activeVoices.end())
            {
                if (auto* vp = dynamic_cast<VoiceProcessor*>(it->second->getProcessor()))
                {
                    if (auto* p = vp->getAPVTS().getParameter("timeStretchRatio"))
                    {
                        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
                        {
                            float randomRatio = juce::Random::getSystemRandom().nextFloat() * 4.0f + 0.25f; // 0.25 to 4.25 ratio
                            r->setValueNotifyingHost(r->getNormalisableRange().convertTo0to1(randomRatio));
                            appendLog("Randomized time stretch for voice ID: " + juce::String((juce::int64)c.voiceId) + " to " + juce::String(randomRatio) + " ratio");
                        }
                    }
                }
            }
        }
        else if (c.type == Command::Type::SetChaosMode)
        {
            chaosModeEnabled = c.chaosModeEnabled;
            appendLog("Chaos mode " + juce::String(chaosModeEnabled ? "enabled" : "disabled"));
        }
    }

    // Chaos Mode: Periodically randomize parameters if enabled
    static int chaosCounter = 0;
    if (chaosModeEnabled && ++chaosCounter % 100 == 0) // Every 100 timer calls
    {
        if (!activeVoices.empty())
        {
            auto& rng = juce::Random::getSystemRandom();
            auto it = activeVoices.begin();
            std::advance(it, rng.nextInt((int)activeVoices.size()));
            
            if (auto* vp = dynamic_cast<VoiceProcessor*>(it->second->getProcessor()))
            {
                // Random parameter list
                juce::StringArray params = {"filterCutoff", "filterResonance", "chorusRate", "chorusDepth", 
                                           "phaserRate", "phaserDepth", "reverbRoom", "reverbDamp", 
                                           "delayTimeMs", "delayFeedback", "driveAmount"};
                
                juce::String paramName = params[rng.nextInt(params.size())];
                if (auto* p = vp->getAPVTS().getParameter(paramName))
                {
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
                    {
                        float randomValue = rng.nextFloat(); // 0.0 to 1.0
                        r->setValueNotifyingHost(randomValue);
                    }
                }
            }
        }
    }

    // Bridge diagnostics to UI (minimal: just listener; voices left empty)
    {
        const juce::ScopedLock sl (visualiserLock);
        visualiserState.voices.clearQuick();
        visualiserState.listenerPosition = { listenerX, listenerY };
    }

    // CPU load reporting disabled to avoid repeated device manager init cost.
}

void AudioEngine::dumpCurrentStateToLog()
{
    juce::Logger::writeToLog ("--- JUCE STATE DUMP TRIGGERED ---");
    // Current audio device settings
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    auto* dev = deviceManager.getCurrentAudioDevice();
    const juce::String inName = setup.inputDeviceName;
    const juce::String outName = setup.outputDeviceName;
    const double sr = dev ? dev->getCurrentSampleRate() : 0.0;
    const int bs = dev ? dev->getCurrentBufferSizeSamples() : 0;
    juce::Logger::writeToLog ("[AUDIO] input='" + inName + "' output='" + outName + "' sr=" + juce::String (sr) + " bs=" + juce::String (bs));

    // Graph stats
    const int numNodes = (int) (mainGraph ? mainGraph->getNodes().size() : 0);
    juce::Logger::writeToLog ("[GRAPH] nodes=" + juce::String (numNodes));

    // Master gain param
    float masterGain = -1.0f;
    if (masterGainNode != nullptr)
    {
        if (auto* gp = dynamic_cast<GainProcessor*> (masterGainNode->getProcessor()))
        {
            if (auto* p = gp->getAPVTS().getRawParameterValue ("gain"))
                masterGain = p->load();
        }
    }
    juce::Logger::writeToLog ("[GRAPH] masterGainParam=" + juce::String (masterGain));

    // Voices (log minimal APVTS values)
    juce::Logger::writeToLog ("[VOICES] count=" + juce::String ((int) activeVoices.size()));
    for (auto& kv : activeVoices)
    {
        const juce::uint64 vid = kv.first;
        auto* proc = kv.second ? kv.second->getProcessor() : nullptr;
        float apGain = -1.0f, apPan = 0.0f, apFreq = 0.0f;
        if (auto* vp = dynamic_cast<VoiceProcessor*> (proc))
        {
            if (auto* pG = vp->getAPVTS().getRawParameterValue ("gain")) apGain = pG->load();
            if (auto* pP = vp->getAPVTS().getRawParameterValue ("pan"))  apPan  = pP->load();
            if (auto* pF = vp->getAPVTS().getRawParameterValue ("frequency")) apFreq = pF->load();
        }
        juce::Logger::writeToLog ("[VOICE] id=" + juce::String ((juce::int64) vid) +
                                   " gain=" + juce::String (apGain) +
                                   " pan=" + juce::String (apPan) +
                                   " freq=" + juce::String (apFreq));
    }
}

juce::Array<AudioEngine::VoiceInfo> AudioEngine::getActiveVoicesInfo() const
{
    juce::Array<VoiceInfo> result;
    
    for (auto& kv : activeVoices)
    {
        const juce::uint64 voiceId = kv.first;
        auto node = kv.second;
        if (!node) continue;
        
        auto* proc = node->getProcessor();
        if (!proc) continue;
        
        VoiceInfo info;
        info.voiceId = voiceId;
        
        // Determine voice type and display name
        if (dynamic_cast<SynthVoiceProcessor*>(proc))
        {
            info.voiceType = "Synth";
            info.displayName = "Synth Voice " + juce::String((juce::int64)voiceId);
        }
        else if (dynamic_cast<NoiseVoiceProcessor*>(proc))
        {
            info.voiceType = "Noise";
            info.displayName = "Noise Voice " + juce::String((juce::int64)voiceId);
        }
        else if (auto* sampleProc = dynamic_cast<SampleVoiceProcessor*>(proc))
        {
            info.voiceType = "Sample";
            info.displayName = "Sample: " + sampleProc->getSourceName();
        }
        else if (dynamic_cast<ModularVoice*>(proc))
        {
            info.voiceType = "Modular";
            info.displayName = "Modular Synth " + juce::String((juce::int64)voiceId);
        }
        else
        {
            info.voiceType = "Unknown";
            info.displayName = "Unknown Voice " + juce::String((juce::int64)voiceId);
        }
        
        result.add(info);
    }
    
    return result;
}

float AudioEngine::getVoiceParameterValue(juce::uint64 voiceId, const juce::String& paramName) const
{
    auto it = activeVoices.find(voiceId);
    if (it != activeVoices.end())
    {
        if (auto* vp = dynamic_cast<VoiceProcessor*>(it->second->getProcessor()))
        {
            if (auto* p = vp->getAPVTS().getParameter(paramName))
            {
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
                {
                    return r->getNormalisableRange().convertFrom0to1(p->getValue());
                }
            }
        }
    }
    return 0.0f; // Default value if parameter not found
}

juce::StringArray AudioEngine::getAvailableInputChannelNames() const
{
    if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
    {
        return currentDevice->getInputChannelNames();
    }

    return {}; // Return empty array if no device
}

juce::String AudioEngine::getCurrentInputDeviceName() const
{
    if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
    {
        return currentDevice->getName();
    }
    return "No Device";
}

juce::StringArray AudioEngine::getAvailableInputDeviceNames() const
{
    juce::StringArray names;
    // Get the first available device type (e.g., ASIO, CoreAudio, WASAPI)
    if (auto* deviceType = deviceManager.getAvailableDeviceTypes().getFirst())
    {
        deviceType->scanForDevices(); // Rescan to get the most up-to-date list
        names = deviceType->getDeviceNames(true); // 'true' for input devices
    }

    return names;
}

void AudioEngine::setInputDevice(const juce::String& deviceName)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);

    if (setup.inputDeviceName != deviceName)
    {
        setup.inputDeviceName = deviceName;
        // This will restart the audio device with the new settings
        deviceManager.setAudioDeviceSetup(setup, true);
        appendLog("[AudioEngine] Changed input device to: " + deviceName);
    }
}

void AudioEngine::handleCreateVoice (const Command& cmd)
{
    if (!mainGraph) return;

    std::unique_ptr<VoiceProcessor> proc;
    // --- 1. Create the processor based on type ---
    if (cmd.voiceType.equalsIgnoreCase("synth")) {
        proc = std::make_unique<SynthVoiceProcessor>();
    } else if (cmd.voiceType.equalsIgnoreCase("noise")) {
        proc = std::make_unique<NoiseVoiceProcessor>();
    } else if (cmd.voiceType.equalsIgnoreCase("modular")) {
        proc = std::make_unique<ModularVoice>();
    } else if (cmd.voiceType.equalsIgnoreCase("sample")) {
        juce::File f(cmd.resourceName);
        auto smp = sampleBank.getOrLoad(f);
        if (!smp) {
            // FAILSAFE: Generate a 1-second sine wave if sample not found
            appendLog("WARNING: Sample not found: " + cmd.resourceName + " - generating sine wave failsafe");
            smp = sampleBank.generateSineWaveFailsafe(44100, 1.0); // 1 second at 44.1kHz
            if (!smp) {
                appendLog("ERROR: Failed to generate failsafe sample");
                return;
            }
        }
        auto sampleProc = std::make_unique<SampleVoiceProcessor>(smp);
        sampleProc->setSourceName(f.getFileName());
        sampleProc->setLooping(true); // Set looping by default
        activeSampleRefs[cmd.voiceId] = smp;
        proc = std::move(sampleProc);
    }

    if (proc == nullptr) {
        appendLog("ERROR: Unknown voice type for create command: " + cmd.voiceType);
        return;
    }

    // --- 2. Perform ALL necessary initialization steps BEFORE adding to the graph ---
    proc->uniqueId = cmd.voiceId;
    resetVoiceParamsToDefaults(proc.get());

    // CRITICAL FIX for "no sound": Set a non-zero default gain and centered pan.
    if (auto* p = proc->getAPVTS().getParameter("gain"))
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
            r->setValueNotifyingHost(r->getNormalisableRange().convertTo0to1(0.7f));
    
    if (auto* p = proc->getAPVTS().getParameter("pan"))
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
            r->setValueNotifyingHost(r->getNormalisableRange().convertTo0to1(0.0f));
    
    // --- 3. Add the fully prepared processor to the graph and connect it ---
    auto node = connectAndAddVoice(std::move(proc));
    // After the node is in the graph, prepare it with current runtime format
    if (node && node->getProcessor())
    {
        const double sr = lastSampleRate > 0.0 ? lastSampleRate : 48000.0;
        const int    bs = lastBlockSize  > 0   ? lastBlockSize  : 512;
        node->getProcessor()->setPlayConfigDetails (0, 2, sr, bs);
        node->getProcessor()->prepareToPlay (sr, bs);
    }
    activeVoices[cmd.voiceId] = node;

    // --- 4. MIDI connection is handled automatically in connectAndAddVoice ---
    
    // --- 5. Perform type-specific post-creation setup ---
    if (auto* mv = dynamic_cast<ModularVoice*>(node->getProcessor())) {
        if (auto* msp = mv->getModularSynth()) {
            auto vco = msp->addModule("VCO");
            auto out = msp->getOutputNodeID();
            
            // SIMPLER, BETTER DEFAULT PATCH: VCO directly to output (no VCA needed for basic sound)
            msp->connect(vco, 0, out, 0); // VCO -> Left Out
            msp->connect(vco, 0, out, 1); // VCO -> Right Out
            msp->commitChanges();
            
            appendLog("Created default modular patch: VCO -> Output (stereo)");
        }
    }

    appendLog("Successfully created voice '" + cmd.voiceType + "' with ID: " + juce::String((juce::int64)cmd.voiceId));
}

void AudioEngine::handleDestroyVoice (juce::uint64 voiceId)
{
    if (! mainGraph) return;
    auto it = activeVoices.find (voiceId);
    if (it == activeVoices.end()) return;
    auto node = it->second;
    mainGraph->removeNode (node->nodeID);
    activeVoices.erase (it);
    activeSampleRefs.erase (voiceId);
}

void AudioEngine::handleUpdateParam (const Command& cmd)
{
    // ADD THIS LINE:
    appendLog("[ENGINE LOG] Received UPDATE command for '" + cmd.paramName + "'.");
    
    if (cmd.voiceId == 0)
    {
        if (cmd.paramName == "listener.posX") listenerX = cmd.paramValue;
        else if (cmd.paramName == "listener.posY") listenerY = cmd.paramValue;
        else if (cmd.paramName == "listener.radius") listenerRadius = juce::jmax (0.0f, cmd.paramValue);
        else if (cmd.paramName == "listener.near") listenerNear = juce::jlimit (0.0f, 1.0f, cmd.paramValue);
        else if (cmd.paramName == "master.gain") setMasterGain (cmd.paramValue);
        else if (cmd.paramName == "device.set")
        {
            // cmd.voiceType packed as "type\nname"
            auto parts = juce::StringArray::fromLines (cmd.voiceType);
            if (parts.size() >= 2)
            {
                const juce::String kind = parts[0];
                const juce::String name = parts[1];
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                deviceManager.getAudioDeviceSetup(setup);
                if (kind.equalsIgnoreCase ("input")) setup.inputDeviceName = name; else setup.outputDeviceName = name;
                deviceManager.setAudioDeviceSetup(setup, true);
                static OscClient client; client.connect ("127.0.0.1", 9002);
                const float sr = (float) (deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 0.0);
                const int bs = (deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples() : 0);
                client.sendCurrentSettings (setup.inputDeviceName, setup.outputDeviceName, sr, bs);
            }
        }
        else if (cmd.paramName == "device.bufferSize")
        {
            const int newBS = juce::jmax (16, (int) std::round (cmd.paramValue));
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup(setup);
            setup.bufferSize = newBS;
            deviceManager.setAudioDeviceSetup(setup, true);
            static OscClient client; client.connect ("127.0.0.1", 9002);
            const float sr = (float) (deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 0.0);
            const int bs  = (deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples() : 0);
            client.sendCurrentSettings (setup.inputDeviceName, setup.outputDeviceName, sr, bs);
        }
        else if (cmd.paramName == "engine.requestInfo")
        {
            sendFullInfoSnapshot();
        }
        return;
    }

    auto it = activeVoices.find (cmd.voiceId);
    if (it == activeVoices.end()) return;
    if (auto* vp = dynamic_cast<VoiceProcessor*> (it->second->getProcessor()))
    {
        // Fast-path engine switches for SampleVoiceProcessor (bypass APVTS latency)
        if (auto* svp = dynamic_cast<SampleVoiceProcessor*>(vp))
        {
            if (cmd.paramName == "engine")
            {
                const bool useNaive = (cmd.paramValue >= 0.5f);
                svp->setEngine(useNaive ? SampleVoiceProcessor::Engine::Naive
                                        : SampleVoiceProcessor::Engine::RubberBand);
                return;
            }
        }
        // APVTS-based updates only
        auto setParam = [vp] (const juce::String& id, float val)
        {
            if (auto* p = vp->getAPVTS().getParameter (id))
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                    r->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, r->getNormalisableRange().convertTo0to1 (val)));
        };
        // Generic parameter update - handle any valid parameter ID
        setParam (cmd.paramName, cmd.paramValue);
    }
}

void AudioEngine::handleListenerUpdate (float x, float y, float radius, float nearRatio)
{
    listenerX = x; listenerY = y; listenerRadius = radius; listenerNear = nearRatio;
}

VisualiserState AudioEngine::getVisualiserState() const
{
    const juce::ScopedLock sl (visualiserLock);
    return visualiserState;
}

void AudioEngine::setMasterGain (float newGain)
{
    if (masterGainNode != nullptr)
    {
        if (auto* gp = dynamic_cast<GainProcessor*> (masterGainNode->getProcessor()))
            gp->setLinearGain (newGain);
    }
}

juce::uint64 AudioEngine::test_createVoice (const juce::String& voiceType)
{
    if (! mainGraph)
        return 0;

    juce::uint64 newId = (juce::uint64) juce::Time::getMillisecondCounterHiRes();

    std::unique_ptr<juce::AudioProcessor> proc;
    if (voiceType.equalsIgnoreCase ("sample"))
    {
        // Try to load any available audio file from ./audio/samples or ./assets
        std::shared_ptr<SampleBank::Sample> smp;
        {
            juce::File cwd = juce::File::getCurrentWorkingDirectory();
            juce::File candidates[] = {
                cwd.getChildFile ("audio").getChildFile ("samples").getChildFile ("test.wav"),
                cwd.getChildFile ("assets").getChildFile ("test.wav")
            };
            for (auto& f : candidates)
            {
                if (f.existsAsFile()) { smp = sampleBank.getOrLoad (f); break; }
            }
        }
        if (! smp)
        {
            // Failsafe: synthesize a 1s sine tone into a temp buffer
            auto sample = std::make_shared<SampleBank::Sample>();
            const double sr = lastSampleRate > 0.0 ? lastSampleRate : 48000.0;
            const int n = (int) (sr);
            sample->stereo.setSize (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float s = std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / sr);
                sample->stereo.setSample (0, i, s);
                sample->stereo.setSample (1, i, s);
            }
            sample->buffer.makeCopyOf (sample->stereo, true);
            sample->sampleRate = sr;
            smp = sample;
        }
        auto p = std::make_unique<SampleVoiceProcessor> (smp);
        proc = std::move (p);
        activeSampleRefs[newId] = smp;
    }
    else if (voiceType.equalsIgnoreCase ("synth"))
    {
        auto p = std::make_unique<SynthVoiceProcessor>();
        proc = std::move (p);
    }
    else if (voiceType.equalsIgnoreCase ("noise"))
    {
        proc = std::make_unique<NoiseVoiceProcessor>();
    }
    else
    {
        return 0;
    }

    auto node = mainGraph->addNode (std::move (proc));
    using NodeAndChannel = juce::AudioProcessorGraph::NodeAndChannel;
    juce::AudioProcessorGraph::Connection cL { NodeAndChannel{ node->nodeID, 0 }, NodeAndChannel{ masterGainNode->nodeID, 0 } };
    juce::AudioProcessorGraph::Connection cR { NodeAndChannel{ node->nodeID, 1 }, NodeAndChannel{ masterGainNode->nodeID, 1 } };
    mainGraph->addConnection (cL);
    mainGraph->addConnection (cR);

    // Prepare the processor with current runtime format, then mark prepared
    if (auto* ap = node->getProcessor())
        ap->prepareToPlay (lastSampleRate > 0.0 ? lastSampleRate : 48000.0, lastBlockSize > 0 ? lastBlockSize : 512);
    if (auto* vp = dynamic_cast<VoiceProcessor*> (node->getProcessor()))
    {
        // Reasonable defaults via APVTS
        if (auto* p = vp->getAPVTS().getParameter ("gain"))
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) r->setValueNotifyingHost (r->getNormalisableRange().convertTo0to1 (0.7f));
        if (auto* p2 = vp->getAPVTS().getParameter ("pan"))
            if (auto* r2 = dynamic_cast<juce::RangedAudioParameter*> (p2)) r2->setValueNotifyingHost (r2->getNormalisableRange().convertTo0to1 (0.0f));
    }

    activeVoices[newId] = node;
    appendLog ("Created voice '" + voiceType + "' id=" + juce::String ((juce::int64) newId));
    return newId;
}

void AudioEngine::test_updateVoiceParameter (juce::uint64 voiceId, const juce::String& paramId, float value)
{
    auto it = activeVoices.find (voiceId);
    if (it == activeVoices.end()) return;
    auto* proc = it->second->getProcessor();
    if (! proc) return;
    // Update APVTS parameter by ID
    const int numParams = proc->getNumParameters();
    for (int i = 0; i < numParams; ++i)
    {
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (proc->getParameters()[i]))
        {
            if (p->paramID.equalsIgnoreCase (paramId))
            {
                // Map linear value to normalized
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
                if (ranged != nullptr)
                {
                    const float norm = ranged->getNormalisableRange().convertTo0to1 (value);
                    ranged->beginChangeGesture();
                    ranged->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
                    ranged->endChangeGesture();
                    appendLog ("Set param '" + paramId + "'=" + juce::String (value) + " on voiceId=" + juce::String ((juce::int64) voiceId));
                }
                return;
            }
        }
    }
}

void AudioEngine::test_destroyVoice (juce::uint64 voiceId)
{
    handleDestroyVoice (voiceId);
}

void AudioEngine::resetVoiceParamsToDefaults (VoiceProcessor* v)
{
    if (v == nullptr) return;
    auto set = [v] (const char* id, float val)
    {
        if (auto* p = v->getAPVTS().getParameter (id))
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                r->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, r->getNormalisableRange().convertTo0to1 (val)));
    };
    set ("filterCutoff", 20000.0f);
    set ("filterResonance", 1.0f);
    set ("chorusRate", 1.0f);
    set ("chorusDepth", 0.0f);
    set ("chorusMix", 0.0f);
    set ("phaserRate", 0.5f);
    set ("phaserDepth", 0.0f);
    set ("phaserCentre", 1000.0f);
    set ("phaserFeedback", 0.0f);
    set ("phaserMix", 0.0f);
    set ("reverbRoom", 0.0f);
    set ("reverbDamp", 0.5f);
    set ("reverbWidth", 1.0f);
    set ("reverbMix", 0.0f);
    set ("delayTimeMs", 0.0f);
    set ("delayFeedback", 0.0f);
    set ("delayMix", 0.0f);
    set ("compThreshold", 0.0f);
    set ("compRatio", 1.0f);
    set ("compAttackMs", 10.0f);
    set ("compReleaseMs", 100.0f);
    set ("compMakeup", 0.0f);
    set ("limitThreshold", 0.0f);
    set ("limitReleaseMs", 10.0f);
    set ("driveAmount", 0.0f);
    set ("driveMix", 0.0f);
    set ("gateThreshold", -100.0f);
    set ("gateAttackMs", 1.0f);
    set ("gateReleaseMs", 50.0f);
    set ("timeStretchRatio", 1.0f);
    set ("pitchSemitones", 0.0f);
    set ("pitchRatio", 1.0f);
}
