#include "ModularSynthProcessor.h"
#include <limits>
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/NotificationManager.h"
#endif
#include "../../preset_creator/PinDatabase.h"
#include "../modules/AudioInputModuleProcessor.h"
#include "../modules/RecordModuleProcessor.h"
#include "../modules/VCOModuleProcessor.h"
#include "../modules/VCFModuleProcessor.h"
#include "../modules/VCAModuleProcessor.h"
#include "../modules/NoiseModuleProcessor.h"
#include "../modules/LFOModuleProcessor.h"
#include "../modules/ADSRModuleProcessor.h"
#include "../modules/MixerModuleProcessor.h"
#include "../modules/DelayModuleProcessor.h"
#include "../modules/ReverbModuleProcessor.h"
#include "../modules/AttenuverterModuleProcessor.h"
#include "../modules/ScopeModuleProcessor.h"
#include "../modules/SAndHModuleProcessor.h"
#include "../modules/StepSequencerModuleProcessor.h"
#include "../modules/MathModuleProcessor.h"
#include "../modules/MapRangeModuleProcessor.h"
#include "../modules/RandomModuleProcessor.h"
#include "../modules/RateModuleProcessor.h"
#include "../modules/QuantizerModuleProcessor.h"
#include "../modules/SequentialSwitchModuleProcessor.h"
#include "../modules/LogicModuleProcessor.h"
#include "../modules/ValueModuleProcessor.h"
#include "../modules/ClockDividerModuleProcessor.h"
#include "../modules/WaveshaperModuleProcessor.h"
#include "../modules/MultiBandShaperModuleProcessor.h"
#include "../modules/GranulatorModuleProcessor.h"
#include "../modules/HarmonicShaperModuleProcessor.h"
#include "../modules/TrackMixerModuleProcessor.h"
#include "../modules/TTSPerformerModuleProcessor.h"
#include "../modules/ComparatorModuleProcessor.h"
#include "../modules/VocalTractFilterModuleProcessor.h"
#include "../modules/VstHostModuleProcessor.h"
#include "../modules/SampleLoaderModuleProcessor.h"
#include "../modules/FunctionGeneratorModuleProcessor.h"
#include "../modules/TimePitchModuleProcessor.h"
#include "../modules/DebugModuleProcessor.h"
#include "../modules/CommentModuleProcessor.h"
#include "../modules/RerouteModuleProcessor.h"
#include "../modules/MIDIPlayerModuleProcessor.h"
#include "../modules/PolyVCOModuleProcessor.h"
#include "../modules/TimelineModuleProcessor.h"
#include "../modules/BPMMonitorModuleProcessor.h"
#include "../modules/ShapingOscillatorModuleProcessor.h"
#include "../modules/MultiSequencerModuleProcessor.h"
#include "../modules/LagProcessorModuleProcessor.h"
#include "../modules/DeCrackleModuleProcessor.h"
#include "../modules/CVMixerModuleProcessor.h"
#include "../modules/GraphicEQModuleProcessor.h"
#include "../modules/FrequencyGraphModuleProcessor.h"
#include "../modules/ChorusModuleProcessor.h"
#include "../modules/PhaserModuleProcessor.h"
#include "../modules/CompressorModuleProcessor.h"
#include "../modules/RecordModuleProcessor.h"
#include "../modules/LimiterModuleProcessor.h"
#include "../modules/GateModuleProcessor.h"
#include "../modules/DriveModuleProcessor.h"
#include "../modules/BitCrusherModuleProcessor.h"
#include "../modules/PanVolModuleProcessor.h"
#include "../modules/SnapshotSequencerModuleProcessor.h"
#include "../modules/MIDICVModuleProcessor.h"
#include "../modules/MIDIFadersModuleProcessor.h"
#include "../modules/MIDIKnobsModuleProcessor.h"
#include "../modules/MIDIButtonsModuleProcessor.h"
#include "../modules/MIDIJogWheelModuleProcessor.h"
#include "../modules/MIDIPadModuleProcessor.h"
#include "../modules/MidiLoggerModuleProcessor.h"
#include "../modules/TempoClockModuleProcessor.h"
#include "../modules/PhysicsModuleProcessor.h"
#include "../modules/StrokeSequencerModuleProcessor.h"
#include "../modules/AnimationModuleProcessor.h"
#include "../modules/WebcamLoaderModule.h"
#include "../modules/VideoFileLoaderModule.h"
#include "../modules/VideoFXModule.h"
#include "../modules/MovementDetectorModule.h"
#include "../modules/PoseEstimatorModule.h"
#include "../modules/HandTrackerModule.h"
#include "../modules/FaceTrackerModule.h"
#include "../modules/ObjectDetectorModule.h"
#include "../modules/ColorTrackerModule.h"
#include "../modules/ContourDetectorModule.h"
#include "../modules/CropVideoModule.h"
#include "../modules/InletModuleProcessor.h"
#include "../modules/OutletModuleProcessor.h"
#include "../modules/MetaModuleProcessor.h"

#if JUCE_DEBUG
namespace
{
struct ScopedGraphMutation
{
    std::atomic<int>& depth;
    explicit ScopedGraphMutation(std::atomic<int>& d) : depth(d)
    {
        depth.fetch_add(1, std::memory_order_acq_rel);
    }
    ~ScopedGraphMutation()
    {
        depth.fetch_sub(1, std::memory_order_acq_rel);
    }
};
}
#endif

ModularSynthProcessor::ModularSynthProcessor()
    : juce::AudioProcessor(BusesProperties()
                            .withInput("Input", juce::AudioChannelSet::stereo(), true)
                            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ModularSynthParams", {})
{
    internalGraph = std::make_unique<juce::AudioProcessorGraph>();

    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    audioInputNode = internalGraph->addNode(std::make_unique<IOProcessor>(IOProcessor::audioInputNode));
    audioOutputNode = internalGraph->addNode(std::make_unique<IOProcessor>(IOProcessor::audioOutputNode));
    midiInputNode  = internalGraph->addNode(std::make_unique<IOProcessor>(IOProcessor::midiInputNode));

    internalGraph->addConnection({ { midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                                   { audioOutputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });
    
    probeScopeNode = internalGraph->addNode(std::make_unique<ScopeModuleProcessor>());
    probeScopeNodeId = probeScopeNode->nodeID;
    juce::Logger::writeToLog("[ModularSynth] Initialized probe scope with nodeID: " + juce::String(probeScopeNodeId.uid));
    
    // Create BPM Monitor node (always present, undeletable like output node)
    auto bpmMonitor = std::make_unique<BPMMonitorModuleProcessor>();
    bpmMonitor->setLogicalId(999); // Special ID to make it undeletable
    bpmMonitorNode = internalGraph->addNode(std::move(bpmMonitor));
    if (auto* processor = dynamic_cast<ModuleProcessor*>(bpmMonitorNode->getProcessor()))
        processor->setParent(this);
    // Add to logicalIdToModule so it appears in the UI
    logicalIdToModule[999] = LogicalModule{ bpmMonitorNode->nodeID, "bpm_monitor" };
    juce::Logger::writeToLog("[ModularSynth] Initialized BPM Monitor with logicalID: 999");
    
    activeAudioProcessors.store(std::make_shared<const std::vector<std::shared_ptr<ModuleProcessor>>>());
    connectionSnapshot.store(std::make_shared<const std::vector<ConnectionInfo>>(), std::memory_order_relaxed);
    
    m_voices.resize(8);
    for (auto& voice : m_voices)
    {
        voice.isActive = false;
        voice.noteNumber = -1;
        voice.velocity = 0.0f;
        voice.age = 0;
        voice.targetModuleLogicalId = 0;
    }
}

ModularSynthProcessor::~ModularSynthProcessor()
{
}

void ModularSynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    internalGraph->setPlayConfigDetails(getTotalNumInputChannels(), getTotalNumOutputChannels(), sampleRate, samplesPerBlock);
    internalGraph->prepareToPlay(sampleRate, samplesPerBlock);
}

void ModularSynthProcessor::releaseResources()
{
    internalGraph->releaseResources();
}

//==============================================================================
// Multi-MIDI Device Support
//==============================================================================

void ModularSynthProcessor::processMidiWithDeviceInfo(const std::vector<MidiMessageWithDevice>& messages)
{
    const juce::ScopedLock lock(midiActivityLock);
    currentBlockMidiMessages = messages;
    
    // DEBUG LOGGING
    if (!messages.empty())
    {
        juce::Logger::writeToLog("[ModularSynth] processMidiWithDeviceInfo received " + 
                                juce::String(messages.size()) + " MIDI messages");
    }
    
    // Update activity tracking
    currentActivity.deviceChannelActivity.clear();
    currentActivity.deviceNames.clear();
    
    for (const auto& msg : messages)
    {
        // Skip system realtime messages
        if (msg.message.isMidiClock() || msg.message.isActiveSense())
            continue;
        
        int channel = msg.message.getChannel();
        if (channel >= 1 && channel <= 16)
        {
            int channelIndex = channel - 1;  // 0-15
            currentActivity.deviceChannelActivity[msg.deviceIndex][channelIndex] = true;
            currentActivity.deviceNames[msg.deviceIndex] = msg.deviceName;
        }
    }
}

ModularSynthProcessor::MidiActivityState ModularSynthProcessor::getMidiActivityState() const
{
    const juce::ScopedLock lock(midiActivityLock);
    return currentActivity;
}

//==============================================================================
// Audio Processing
//==============================================================================

void ModularSynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    try {
        // NOTE: Both tempo and division control flags are managed by Tempo Clock modules directly
        // No resets here to avoid flickering in UI
        
        // --- ADD THIS LOGGING BLOCK ---
        if (!midiMessages.isEmpty())
        {
            // If we get this message, it means MIDI is successfully reaching the synth.
            juce::Logger::writeToLog("[SynthCore] Received " + juce::String(midiMessages.getNumEvents()) + " MIDI events this block.");
            m_midiActivityFlag.store(true);
        }
        // --- END OF BLOCK ---
        
        // Only advance transport when NO timeline master is active
        //  - timelineMasterId == 0          → advance normally (no master)
        //  - timelineMasterId == UINT32_MAX → TempoClock is holding transport (no auto advance)
        //  - timelineMasterId > 0           → a module (SampleLoader/Video) is the master
        const juce::uint32 timelineMasterId = timelineMasterLogicalId.load();
        const bool shouldAdvanceTransport = (timelineMasterId == 0);
        if (m_transportState.isPlaying && shouldAdvanceTransport)
        {
            m_samplePosition += buffer.getNumSamples();
            m_transportState.songPositionSeconds = m_samplePosition / getSampleRate();
            m_transportState.songPositionBeats = (m_transportState.songPositionSeconds / 60.0) * m_transportState.bpm;
        }

        // Handle Global Reset Pulse
        // When a Timeline Master (e.g., SampleLoader) loops, it calls triggerGlobalReset()
        // This sets the flag for one block, resetting all time-based modules (LFOs, Sequencers)
        if (m_globalResetRequest.exchange(false)) // Atomic read-and-clear
        {
            m_transportState.forceGlobalReset.store(true);
            m_samplePosition = 0; // Reset internal counter
            m_transportState.songPositionSeconds = 0.0;
            m_transportState.songPositionBeats = 0.0;
        }
        else
        {
            m_transportState.forceGlobalReset.store(false);
        }

        // --- FINAL THREAD-SAFE FIX ---
        auto currentProcessors = activeAudioProcessors.load();
        if (currentProcessors)
        {
            // Iterate over the safe, shared list.
            for (const auto& modulePtr : *currentProcessors)
            {
                // SAFETY NET + GRANULAR LOGGING
                if (modulePtr != nullptr)
                {
                    // Log the memory address before calling the function
                    // juce::Logger::writeToLog("[AudioThread] Ticking module at 0x" + juce::String::toHexString((juce::pointer_sized_int)modulePtr.get()));
                    modulePtr->setTimingInfo(m_transportState);
                }
                else
                {
                    // This should never happen with the shared_ptr fix, but if it does, it's critical info.
                    juce::Logger::writeToLog("[AudioThread] CRITICAL WARNING: Encountered nullptr in active processor list!");
                }
            }
        }
        // --- END OF FIX ---
        
        // === MULTI-MIDI DEVICE SUPPORT: Distribute device-aware MIDI to modules ===
        // This happens BEFORE voice management and graph processing
        // Modules receive device info and can filter by device/channel
        {
            const juce::ScopedLock lock(midiActivityLock);
            
            // --- THREAD-SAFE FIX: Use the same atomic snapshot as the timing info loop ---
            // This prevents race conditions when graph is being rebuilt
            auto currentProcessors = activeAudioProcessors.load();
            if (currentProcessors && !currentBlockMidiMessages.empty())
            {
                int moduleCount = 0;
                for (const auto& modulePtr : *currentProcessors)
                {
                    if (modulePtr != nullptr)
                    {
                        modulePtr->handleDeviceSpecificMidi(currentBlockMidiMessages);
                        moduleCount++;
                    }
                }
                
                // Merge device-aware MIDI into standard MidiBuffer for backward compatibility
                for (const auto& msg : currentBlockMidiMessages)
                {
                    midiMessages.addEvent(msg.message, 0);
                }
                
                // Clear for next block
                currentBlockMidiMessages.clear();
            }
            // --- END OF THREAD-SAFE FIX ---
        }
        // === END MULTI-MIDI DISTRIBUTION ===
        
        if (m_voiceManagerEnabled && !m_voices.empty())
        {
            juce::MidiBuffer processedMidi;
            for (const auto metadata : midiMessages)
            {
                const auto msg = metadata.getMessage();
                if (msg.isNoteOn())
                {
                    int voiceIndex = findFreeVoice();
                    if (voiceIndex < 0) voiceIndex = findOldestVoice();
                    if (voiceIndex >= 0)
                    {
                        assignNoteToVoice(voiceIndex, msg);
                        processedMidi.addEvent(msg, metadata.samplePosition);
                    }
                }
                else if (msg.isNoteOff())
                {
                    releaseVoice(msg);
                    processedMidi.addEvent(msg, metadata.samplePosition);
                }
                else
                {
                    processedMidi.addEvent(msg, metadata.samplePosition);
                }
            }
            midiMessages.swapWith(processedMidi);
        }
        
        internalGraph->processBlock(buffer, midiMessages);
        
        static int silentCtr = 0;
        if (buffer.getMagnitude(0, buffer.getNumSamples()) < 1.0e-6f)
        {
            if ((++silentCtr % 600) == 0)
                juce::Logger::writeToLog("[ModularSynthProcessor] silent block from internal graph");
        }
        else
        {
            silentCtr = 0;
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("[ModSynth][FATAL] Exception in processBlock: ") + e.what());
        buffer.clear();
        return;
    } catch (...) {
        juce::Logger::writeToLog("[ModSynth][FATAL] Unknown exception in processBlock");
        buffer.clear();
        return;
    }
}

void ModularSynthProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock (moduleLock);
    juce::ValueTree root("ModularSynthPreset");
    root.setProperty("version", 1, nullptr);
    root.setProperty("bpm", m_transportState.bpm, nullptr);

    juce::ValueTree modsVT("modules");
    std::map<juce::uint32, juce::uint32> nodeUidToLogical;
    for (const auto& kv : logicalIdToModule)
    {
        const juce::uint32 logicalId = kv.first;
        const auto nodeUID = (juce::uint32) kv.second.nodeID.uid;
        nodeUidToLogical[nodeUID] = logicalId;

        juce::ValueTree mv("module");
        mv.setProperty("logicalId", (int) logicalId, nullptr);
        mv.setProperty("type", kv.second.type, nullptr);
        auto itNode = modules.find(nodeUID);
        if (itNode != modules.end())
        {
            if (auto* modProc = dynamic_cast<ModuleProcessor*>(itNode->second->getProcessor()))
            {
                if (auto* vstHost = dynamic_cast<VstHostModuleProcessor*>(modProc))
                {
                    if (auto extra = vstHost->getExtraStateTree(); extra.isValid())
                    {
                        juce::ValueTree extraWrapper("extra");
                        extraWrapper.addChild(extra, -1, nullptr);
                        mv.addChild(extraWrapper, -1, nullptr);
                    }
                }
                else
                {
                    juce::ValueTree params = modProc->getAPVTS().copyState();
                    juce::ValueTree paramsWrapper("params");
                    paramsWrapper.addChild(params, -1, nullptr);
                    mv.addChild(paramsWrapper, -1, nullptr);

                    if (auto extra = modProc->getExtraStateTree(); extra.isValid())
                    {
                        juce::ValueTree extraWrapper("extra");
                        extraWrapper.addChild(extra, -1, nullptr);
                        mv.addChild(extraWrapper, -1, nullptr);
                    }
                }
            }
        }
        modsVT.addChild(mv, -1, nullptr);
    }
    root.addChild(modsVT, -1, nullptr);

    juce::ValueTree connsVT("connections");
    for (const auto& c : internalGraph->getConnections())
    {
        const juce::uint32 srcUID = (juce::uint32) c.source.nodeID.uid;
        const juce::uint32 dstUID = (juce::uint32) c.destination.nodeID.uid;
        juce::ValueTree cv("connection");
        auto srcIt = nodeUidToLogical.find(srcUID);
        auto dstIt = nodeUidToLogical.find(dstUID);
        if (srcIt != nodeUidToLogical.end() && dstIt != nodeUidToLogical.end())
        {
            cv.setProperty("srcId", (int) srcIt->second, nullptr);
            cv.setProperty("srcChan", (int) c.source.channelIndex, nullptr);
            cv.setProperty("dstId", (int) dstIt->second, nullptr);
            cv.setProperty("dstChan", (int) c.destination.channelIndex, nullptr);
        }
        else if (srcIt != nodeUidToLogical.end() && c.destination.nodeID == audioOutputNode->nodeID)
        {
            cv.setProperty("srcId", (int) srcIt->second, nullptr);
            cv.setProperty("srcChan", (int) c.source.channelIndex, nullptr);
            cv.setProperty("dstId", juce::String("output"), nullptr);
            cv.setProperty("dstChan", (int) c.destination.channelIndex, nullptr);
        }
        else
        {
            continue;
        }
        connsVT.addChild(cv, -1, nullptr);
    }
    root.addChild(connsVT, -1, nullptr);

    if (auto xml = root.createXml())
    {
        juce::MemoryOutputStream mos(destData, false);
        xml->writeTo(mos);
    }
}

void ModularSynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::Logger::writeToLog("--- Restoring Snapshot ---");
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse(juce::String::fromUTF8((const char*)data, (size_t)sizeInBytes)));
    if (!xml || !xml->hasTagName("ModularSynthPreset"))
    {
        juce::Logger::writeToLog("[STATE] ERROR: Invalid XML or wrong root tag. Aborting restore.");
        return;
    }

    clearAll();
    juce::Logger::writeToLog("[STATE] Cleared existing state.");

    juce::ValueTree root = juce::ValueTree::fromXml(*xml);

    // Defensive healing: ensure legacy presets with inconsistent names are normalized
    {
        auto modulesVT = root.getChildWithName("modules");
        if (modulesVT.isValid())
        {
            std::set<juce::String> validNames;
            std::unordered_map<juce::String, juce::String> collapsedToCanonical;
            for (const auto& pair : getModulePinDatabase())
            {
                validNames.insert(pair.first);
                juce::String collapsed;
                for (int i = 0; i < pair.first.length(); ++i)
                {
                    auto ch = pair.first[i];
                    if (ch != '_' && ch != ' ')
                        collapsed += ch;
                }
                collapsedToCanonical[collapsed] = pair.first;
            }

            int fixCount = 0;
            for (auto moduleNode : modulesVT)
            {
                if (!moduleNode.hasType("module")) continue;
                juce::String currentType = moduleNode.getProperty("type").toString();
                if (validNames.count(currentType) > 0) continue;

                juce::String normalized = currentType.toLowerCase().replaceCharacter(' ', '_');

                juce::String caseFixed;
                caseFixed.preallocateBytes((int)currentType.length() * 2);
                for (int i = 0; i < currentType.length(); ++i)
                {
                    juce::juce_wchar c = currentType[i];
                    bool prevIsLower = (i > 0) && juce::CharacterFunctions::isLowerCase(currentType[i - 1]);
                    bool isUpper = juce::CharacterFunctions::isUpperCase(c);
                    if (i > 0 && isUpper && prevIsLower)
                        caseFixed += '_';
                    caseFixed += juce::CharacterFunctions::toLowerCase(c);
                }
                caseFixed = caseFixed.replace(" ", "_");

                if (validNames.count(normalized) > 0)
                {
                    moduleNode.setProperty("type", normalized, nullptr);
                    ++fixCount;
                }
                else if (validNames.count(caseFixed) > 0)
                {
                    moduleNode.setProperty("type", caseFixed, nullptr);
                    ++fixCount;
                }
                else
                {
                    juce::String collapsedCurrent;
                    for (int i = 0; i < currentType.length(); ++i)
                    {
                        auto ch = currentType[i];
                        if (ch != '_' && ch != ' ')
                            collapsedCurrent += juce::CharacterFunctions::toLowerCase(ch);
                    }
                    auto it = collapsedToCanonical.find(collapsedCurrent);
                    if (it != collapsedToCanonical.end())
                    {
                        moduleNode.setProperty("type", it->second, nullptr);
                        ++fixCount;
                    }
                }
            }

            if (fixCount > 0)
                juce::Logger::writeToLog("[STATE] Auto-heal applied: " + juce::String(fixCount) + " fix(es).");
        }
    }
    
    // Restore global transport settings
    m_transportState.bpm = root.getProperty("bpm", 120.0);
    juce::Logger::writeToLog("[STATE] Restored BPM to " + juce::String(m_transportState.bpm));
    
    auto modsVT = root.getChildWithName("modules");
    if (!modsVT.isValid())
    {
        juce::Logger::writeToLog("[STATE] WARNING: No <modules> block found in preset.");
        return;
    }
    
    juce::Logger::writeToLog("[STATE] Found <modules> block with " + juce::String(modsVT.getNumChildren()) + " children.");
    juce::uint32 maxId = 0;
    for (int i = 0; i < modsVT.getNumChildren(); ++i)
    {
        auto mv = modsVT.getChild(i);
        if (mv.hasType("module"))
        {
            maxId = juce::jmax(maxId, (juce::uint32)(int)mv.getProperty("logicalId", 0));
        }
    }
    nextLogicalId = maxId + 1;

    std::map<juce::uint32, NodeID> logicalToNodeId;
    juce::Logger::writeToLog("[STATE] Starting module recreation pass...");
    
    for (int i = 0; i < modsVT.getNumChildren(); ++i)
    {
        auto mv = modsVT.getChild(i);
        if (!mv.hasType("module"))
        {
            juce::Logger::writeToLog("[STATE] Skipping non-module child at index " + juce::String(i));
            continue;
        }

        const juce::uint32 logicalId = (juce::uint32)(int)mv.getProperty("logicalId", 0);
        const juce::String type = mv.getProperty("type").toString();

        juce::Logger::writeToLog("[STATE] Processing module " + juce::String(i) + ": logicalId=" + juce::String(logicalId) + " type='" + type + "'");

        // Skip BPM Monitor (logical ID 999) - it's always present and should not be loaded from preset
        if (logicalId == 999)
        {
            juce::Logger::writeToLog("[STATE] Skipping BPM Monitor (logical ID 999) - always present");
            continue;
        }

        if (logicalId > 0 && type.isNotEmpty())
        {
            NodeID nodeId;
            
            auto extraWrapper = mv.getChildWithName("extra");
            bool isVstModule = false;
            
            if (extraWrapper.isValid() && extraWrapper.getNumChildren() > 0)
            {
                auto extraState = extraWrapper.getChild(0);
                if (extraState.hasType("VstHostState"))
                {
                    isVstModule = true;
                    juce::Logger::writeToLog("[STATE]   Loading VST module...");
                    
                    juce::String identifier = extraState.getProperty("fileOrIdentifier", "").toString();
                    
                    if (identifier.isNotEmpty() && pluginFormatManager != nullptr && knownPluginList != nullptr)
                    {
                        bool found = false;
                        for (const auto& desc : knownPluginList->getTypes())
                        {
                            if (desc.fileOrIdentifier == identifier)
                            {
                                juce::Logger::writeToLog("[STATE]   Found VST to load: " + desc.name);
                                nodeId = addVstModule(*pluginFormatManager, desc, logicalId);
                                found = true;
                                break;
                            }
                        }
                        
                        if (!found)
                        {
                            juce::Logger::writeToLog("[STATE]   ERROR: VST plugin not found: " + identifier);
                        }
                    }
                    else
                    {
                        juce::Logger::writeToLog("[STATE]   ERROR: No plugin identifier or format manager/list not available");
                    }
                    
                    if (nodeId.uid == 0)
                    {
                        juce::Logger::writeToLog("[STATE]   ERROR: Failed to create VST module, skipping...");
                        continue;
                    }
                }
            }
            
            if (!isVstModule)
            {
                juce::Logger::writeToLog("[STATE]   Calling addModule('" + type + "')...");
                nodeId = addModule(type, false);
                juce::Logger::writeToLog("[STATE]   addModule returned nodeId.uid=" + juce::String(nodeId.uid));
            }
            
            auto* node = internalGraph->getNodeForId(nodeId);
            
            if (node)
            {
                juce::Logger::writeToLog("[STATE]   Node created successfully.");
                
                if (!isVstModule)
                {
                    for (auto it = logicalIdToModule.begin(); it != logicalIdToModule.end(); )
                    {
                        if (it->second.nodeID == nodeId)
                            it = logicalIdToModule.erase(it);
                        else
                            ++it;
                    }
                    logicalIdToModule[logicalId] = LogicalModule{ nodeId, type };
                }
                
                logicalToNodeId[logicalId] = nodeId;
                juce::Logger::writeToLog("[STATE]   Mapped logicalId " + juce::String(logicalId) + " to nodeId.uid " + juce::String(nodeId.uid));

                // --- FIX: Restore extra state FIRST ---
                // This will load the clip and reset trim sliders to defaults.
                auto extraWrapper = mv.getChildWithName("extra");
                if (extraWrapper.isValid() && extraWrapper.getNumChildren() > 0)
                {
                    auto extra = extraWrapper.getChild(0);
                    if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
                    {
                        mp->setExtraStateTree(extra);
                        juce::Logger::writeToLog("[STATE]   Restored extra state.");
                    }
                }

                // Now restore parameters SECOND.
                // This will overwrite the temporary default trim values with the correct saved values.
                auto paramsWrapper = mv.getChildWithName("params");
                if (paramsWrapper.isValid() && paramsWrapper.getNumChildren() > 0)
                {
                    auto params = paramsWrapper.getChild(0);
                    if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
                    {
                        mp->getAPVTS().replaceState(params);
                        juce::Logger::writeToLog("[STATE]   Restored parameters.");
                    }
                }
            }
            else
            {
                juce::Logger::writeToLog("[STATE]   ERROR: Node creation failed! nodeId.uid was " + juce::String(nodeId.uid) + " but getNodeForId returned nullptr.");
            }
        }
        else
        {
            juce::Logger::writeToLog("[STATE]   Skipping module: logicalId=" + juce::String(logicalId) + " (valid=" + juce::String(logicalId > 0 ? "yes" : "no") + ") type='" + type + "' (empty=" + juce::String(type.isEmpty() ? "yes" : "no") + ")");
        }
    }
    
    juce::Logger::writeToLog("[STATE] Module recreation complete. Created " + juce::String(logicalToNodeId.size()) + " modules.");

    auto connsVT = root.getChildWithName("connections");
    if (connsVT.isValid())
    {
        juce::Logger::writeToLog("[STATE] Restoring " + juce::String(connsVT.getNumChildren()) + " connections...");
        int connectedCount = 0;
        int skippedCount = 0;
        
        for (int i = 0; i < connsVT.getNumChildren(); ++i)
        {
            auto cv = connsVT.getChild(i);
            if (!cv.hasType("connection")) continue;

            const juce::uint32 srcId = (juce::uint32)(int)cv.getProperty("srcId");
            const int srcChan = (int)cv.getProperty("srcChan", 0);
            const bool dstIsOutput = cv.getProperty("dstId").toString() == "output";
            const juce::uint32 dstId = dstIsOutput ? 0 : (juce::uint32)(int)cv.getProperty("dstId");
            const int dstChan = (int)cv.getProperty("dstChan", 0);

            NodeID srcNodeId = logicalToNodeId[srcId];
            NodeID dstNodeId = dstIsOutput ? audioOutputNode->nodeID : logicalToNodeId[dstId];

            if (srcNodeId.uid != 0 && dstNodeId.uid != 0)
            {
                connect(srcNodeId, srcChan, dstNodeId, dstChan);
                connectedCount++;
            }
            else
            {
                juce::Logger::writeToLog("[STATE]   WARNING: Skipping connection " + juce::String(i) + 
                                        ": srcId=" + juce::String(srcId) + " (uid=" + juce::String(srcNodeId.uid) + 
                                        ") → dstId=" + (dstIsOutput ? "output" : juce::String(dstId)) + 
                                        " (uid=" + juce::String(dstNodeId.uid) + ")");
                skippedCount++;
            }
        }
        
        juce::Logger::writeToLog("[STATE] Connection restore complete: " + juce::String(connectedCount) + 
                                " connected, " + juce::String(skippedCount) + " skipped.");
    }
    else
    {
        juce::Logger::writeToLog("[STATE] WARNING: No <connections> block found in preset.");
    }


    juce::Logger::writeToLog("[STATE] Calling commitChanges()...");
    commitChanges();
    juce::Logger::writeToLog("[STATE] Restore complete.");
}

namespace {
    static juce::String toLowerId (const juce::String& s)
    {
        return s.toLowerCase();
    }

    using Creator = std::function<std::unique_ptr<juce::AudioProcessor>()>;

    static std::map<juce::String, Creator>& getModuleFactory()
    {
        static std::map<juce::String, Creator> factory;
        static bool initialised = false;
        if (!initialised)
        {
            auto reg = [&](const juce::String& key, Creator c) { factory.emplace(toLowerId(key), std::move(c)); };

            reg("vco", []{ return std::make_unique<VCOModuleProcessor>(); });
            reg("audio_input", []{ return std::make_unique<AudioInputModuleProcessor>(); });
            reg("vcf", []{ return std::make_unique<VCFModuleProcessor>(); });
            reg("vca", []{ return std::make_unique<VCAModuleProcessor>(); });
            reg("noise", []{ return std::make_unique<NoiseModuleProcessor>(); });
            reg("lfo", []{ return std::make_unique<LFOModuleProcessor>(); });
            reg("adsr", []{ return std::make_unique<ADSRModuleProcessor>(); });
            reg("mixer", []{ return std::make_unique<MixerModuleProcessor>(); });
            reg("cv_mixer", []{ return std::make_unique<CVMixerModuleProcessor>(); });
            reg("track_mixer", []{ return std::make_unique<TrackMixerModuleProcessor>(); });
            reg("delay", []{ return std::make_unique<DelayModuleProcessor>(); });
            reg("reverb", []{ return std::make_unique<ReverbModuleProcessor>(); });
            reg("attenuverter", []{ return std::make_unique<AttenuverterModuleProcessor>(); });
            reg("scope", []{ return std::make_unique<ScopeModuleProcessor>(); });
            reg("frequency_graph", []{ return std::make_unique<FrequencyGraphModuleProcessor>(); });
            reg("s_and_h", []{ return std::make_unique<SAndHModuleProcessor>(); });
            reg("sequencer", []{ return std::make_unique<StepSequencerModuleProcessor>(); });
            reg("math", []{ return std::make_unique<MathModuleProcessor>(); });
            reg("map_range", []{ return std::make_unique<MapRangeModuleProcessor>(); });
            reg("comparator", []{ return std::make_unique<ComparatorModuleProcessor>(); });
            reg("random", []{ return std::make_unique<RandomModuleProcessor>(); });
            reg("rate", []{ return std::make_unique<RateModuleProcessor>(); });
            reg("quantizer", []{ return std::make_unique<QuantizerModuleProcessor>(); });
            reg("sequential_switch", []{ return std::make_unique<SequentialSwitchModuleProcessor>(); });
            reg("logic", []{ return std::make_unique<LogicModuleProcessor>(); });
            reg("clock_divider", []{ return std::make_unique<ClockDividerModuleProcessor>(); });
            reg("waveshaper", []{ return std::make_unique<WaveshaperModuleProcessor>(); });
            reg("8bandshaper", []{ return std::make_unique<MultiBandShaperModuleProcessor>(); });
            reg("granulator", []{ return std::make_unique<GranulatorModuleProcessor>(); });
            reg("harmonic_shaper", []{ return std::make_unique<HarmonicShaperModuleProcessor>(); });
            reg("debug", []{ return std::make_unique<DebugModuleProcessor>(); });
            reg("input_debug", []{ return std::make_unique<InputDebugModuleProcessor>(); });
            reg("vocal_tract_filter", []{ return std::make_unique<VocalTractFilterModuleProcessor>(); });
            reg("value", []{ return std::make_unique<ValueModuleProcessor>(); });
            reg("tts_performer", []{ return std::make_unique<TTSPerformerModuleProcessor>(); });
            reg("sample_loader", []{ return std::make_unique<SampleLoaderModuleProcessor>(); });
            reg("function_generator", []{ return std::make_unique<FunctionGeneratorModuleProcessor>(); });
            reg("timepitch", []{ return std::make_unique<TimePitchModuleProcessor>(); });
            reg("midi_player", []{ return std::make_unique<MIDIPlayerModuleProcessor>(); });
            reg("polyvco", []{ return std::make_unique<PolyVCOModuleProcessor>(); });
            reg("timeline", []{ return std::make_unique<TimelineModuleProcessor>(); });
            reg("shaping_oscillator", []{ return std::make_unique<ShapingOscillatorModuleProcessor>(); });
            reg("multi_sequencer", []{ return std::make_unique<MultiSequencerModuleProcessor>(); });
            reg("lag_processor", []{ return std::make_unique<LagProcessorModuleProcessor>(); });
            reg("de_crackle", []{ return std::make_unique<DeCrackleModuleProcessor>(); });
            reg("graphic_eq", []{ return std::make_unique<GraphicEQModuleProcessor>(); });
            reg("chorus", []{ return std::make_unique<ChorusModuleProcessor>(); });
            reg("phaser", []{ return std::make_unique<PhaserModuleProcessor>(); });
            reg("compressor", []{ return std::make_unique<CompressorModuleProcessor>(); });
            reg("recorder", []{ return std::make_unique<RecordModuleProcessor>(); });
            reg("limiter", []{ return std::make_unique<LimiterModuleProcessor>(); });
            reg("gate", []{ return std::make_unique<GateModuleProcessor>(); });
            reg("drive", []{ return std::make_unique<DriveModuleProcessor>(); });
            reg("bit_crusher", []{ return std::make_unique<BitCrusherModuleProcessor>(); });
            reg("panvol", []{ return std::make_unique<PanVolModuleProcessor>(); });
            reg("comment", []{ return std::make_unique<CommentModuleProcessor>(); });
            reg("reroute", []{ return std::make_unique<RerouteModuleProcessor>(); });
            reg("snapshot_sequencer", []{ return std::make_unique<SnapshotSequencerModuleProcessor>(); });
            reg("midi_cv", []{ return std::make_unique<MIDICVModuleProcessor>(); });
            reg("midi_faders", []{ return std::make_unique<MIDIFadersModuleProcessor>(); });
            reg("midi_knobs", []{ return std::make_unique<MIDIKnobsModuleProcessor>(); });
            reg("midi_buttons", []{ return std::make_unique<MIDIButtonsModuleProcessor>(); });
            reg("midi_jog_wheel", []{ return std::make_unique<MIDIJogWheelModuleProcessor>(); });
            reg("midi_pads", []{ return std::make_unique<MIDIPadModuleProcessor>(); });
            reg("midi_logger", []{ return std::make_unique<MidiLoggerModuleProcessor>(); });
            reg("tempo_clock", []{ return std::make_unique<TempoClockModuleProcessor>(); });
            reg("physics", []{ return std::make_unique<PhysicsModuleProcessor>(); });
            reg("animation", []{ return std::make_unique<AnimationModuleProcessor>(); });
            reg("bpm_monitor", []{ return std::make_unique<BPMMonitorModuleProcessor>(); });
            reg("webcam_loader", []{ return std::make_unique<WebcamLoaderModule>(); });
            reg("video_file_loader", []{ return std::make_unique<VideoFileLoaderModule>(); });
            reg("video_fx", []{ return std::make_unique<VideoFXModule>(); });
            reg("movement_detector", []{ return std::make_unique<MovementDetectorModule>(); });
            reg("pose_estimator", []{ return std::make_unique<PoseEstimatorModule>(); });
            reg("hand_tracker", []{ return std::make_unique<HandTrackerModule>(); });
            reg("face_tracker", []{ return std::make_unique<FaceTrackerModule>(); });
            reg("object_detector", []{ return std::make_unique<ObjectDetectorModule>(); });
            reg("color_tracker", []{ return std::make_unique<ColorTrackerModule>(); });
            reg("contour_detector", []{ return std::make_unique<ContourDetectorModule>(); });
            reg("crop_video", []{ return std::make_unique<CropVideoModule>(); });
            reg("stroke_sequencer", []{ return std::make_unique<StrokeSequencerModuleProcessor>(); });
            
            // reg("meta_module", []{ return std::make_unique<MetaModuleProcessor>(); });
            // reg("meta module", []{ return std::make_unique<MetaModuleProcessor>(); });
            // reg("metamodule", []{ return std::make_unique<MetaModuleProcessor>(); });
            // reg("meta", []{ return std::make_unique<MetaModuleProcessor>(); });
            // reg("inlet", []{ return std::make_unique<InletModuleProcessor>(); });
            // reg("outlet", []{ return std::make_unique<OutletModuleProcessor>(); });

            initialised = true;
        }
        return factory;
    }
    static juce::String toPrettyModuleName(const juce::String& type)
    {
        juce::String name = type;
        name = name.replaceCharacter('_', ' ').toLowerCase();
        bool capNext = true;
        for (int i = 0; i < name.length(); ++i)
        {
            if (capNext && juce::CharacterFunctions::isLetter(name[i]))
            {
                name = name.substring(0, i) + juce::String::charToString(name[i]).toUpperCase() + name.substring(i + 1);
                capNext = false;
            }
            else if (name[i] == ' ')
            {
                capNext = true;
            }
        }
        return name;
    }
}

ModularSynthProcessor::NodeID ModularSynthProcessor::addModule(const juce::String& moduleType, bool commit)
{
    NodeID createdNodeId;
    bool needsDefaultInputMapping = false;
    {
        const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
        ScopedGraphMutation mutation(graphMutationDepth);
#endif
        auto& factory = getModuleFactory();
        const juce::String key = moduleType.toLowerCase();
        std::unique_ptr<juce::AudioProcessor> processor;

        if (auto it = factory.find(key); it != factory.end())
            processor = it->second();

        if (! processor)
        {
            for (const auto& kv : factory)
                if (moduleType.equalsIgnoreCase(kv.first)) { processor = kv.second(); break; }
        }

        if (! processor)
        {
            juce::Logger::writeToLog("[ModSynth][WARN] Unknown module type: " + moduleType);
            return {};
        }

        auto node = internalGraph->addNode(std::move(processor), {}, juce::AudioProcessorGraph::UpdateKind::none);
        if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
            mp->setParent(this);
        modules[(juce::uint32) node->nodeID.uid] = node;
        const juce::uint32 logicalId = nextLogicalId++;
        logicalIdToModule[logicalId] = LogicalModule{ node->nodeID, moduleType };
        if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
        {
            mp->setLogicalId(logicalId);
            mp->setSecondaryLogicalId(nextLogicalId++); // Assign secondary ID for extra outputs (e.g., cropped video)
        }

        if (moduleType.equalsIgnoreCase("audio_input"))
            needsDefaultInputMapping = true;

        createdNodeId = node->nodeID;
    }

    if (createdNodeId.uid != 0)
    {
        if (needsDefaultInputMapping)
        {
            std::vector<int> defaultMapping = { 0, 1 };
            setAudioInputChannelMapping(createdNodeId, defaultMapping);
        }
        else if (commit)
        {
            commitChanges();
        }
        else
        {
            const juce::ScopedLock lock(moduleLock);
            updateConnectionSnapshot_Locked();
        }
    }

    if (createdNodeId.uid != 0)
    {
        juce::Logger::writeToLog("[Toast] addModule created: " + moduleType + ", invoking notification");
        if (onModuleCreated)
            onModuleCreated(toPrettyModuleName(moduleType));
#if defined(PRESET_CREATOR_UI)
        else
            NotificationManager::post(NotificationManager::Type::Info, "Created " + toPrettyModuleName(moduleType) + " node");
#endif
    }

    return createdNodeId;
}

ModularSynthProcessor::NodeID ModularSynthProcessor::addVstModule(
    juce::AudioPluginFormatManager& formatManager,
    const juce::PluginDescription& vstDesc,
    juce::uint32 logicalIdToAssign)
{
    const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
    ScopedGraphMutation mutation(graphMutationDepth);
#endif
    juce::String errorMessage;
    std::unique_ptr<juce::AudioPluginInstance> instance = 
        formatManager.createPluginInstance(vstDesc, getSampleRate(), getBlockSize(), errorMessage);

    if (instance == nullptr)
    {
        juce::Logger::writeToLog("[ModSynth][ERROR] Could not create VST instance: " + errorMessage);
        return {};
    }

    auto wrapper = std::make_unique<VstHostModuleProcessor>(std::move(instance), vstDesc);
    
    auto node = internalGraph->addNode(std::move(wrapper), {}, juce::AudioProcessorGraph::UpdateKind::none);

    if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
        mp->setParent(this);
    
    modules[(juce::uint32) node->nodeID.uid] = node;
    
    logicalIdToModule[logicalIdToAssign] = LogicalModule{ node->nodeID, vstDesc.name };
    
    if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
        mp->setLogicalId(logicalIdToAssign);
    
    juce::Logger::writeToLog("[ModSynth] Added VST module: " + vstDesc.name + " with logical ID " + juce::String(logicalIdToAssign));
    juce::Logger::writeToLog("[Toast] addVstModule created: " + vstDesc.name + ", invoking notification");
    if (onModuleCreated)
        onModuleCreated(vstDesc.name);
#if defined(PRESET_CREATOR_UI)
    else
        NotificationManager::post(NotificationManager::Type::Info, "Created " + vstDesc.name + " node");
#endif
    return node->nodeID;
}

ModularSynthProcessor::NodeID ModularSynthProcessor::addVstModule(
    juce::AudioPluginFormatManager& formatManager,
    const juce::PluginDescription& vstDesc)
{
    const juce::uint32 logicalId = nextLogicalId++;
    auto nodeId = addVstModule(formatManager, vstDesc, logicalId);
    
    if (nodeId.uid != 0)
        commitChanges();
    
    return nodeId;
}

void ModularSynthProcessor::removeModule(const NodeID& nodeID)
{
    if (nodeID.uid == 0) return;
    const juce::ScopedLock lock(moduleLock); // Ensure thread-safe access
#if JUCE_DEBUG
    ScopedGraphMutation mutation(graphMutationDepth);
#endif

    // --- LOGGING ---
    if (auto* node = internalGraph->getNodeForId(nodeID))
    {
        if (auto* proc = node->getProcessor())
        {
            juce::Logger::writeToLog("[GraphSync] Deleting module L-ID " + juce::String(getLogicalIdForNode(nodeID)) + 
                                   " (ptr: 0x" + juce::String::toHexString((int64_t)proc) + ")");
        }
    }
    // --- END LOGGING ---

    const juce::uint32 logicalId = getLogicalIdForNode(nodeID);

    internalGraph->removeNode(nodeID, juce::AudioProcessorGraph::UpdateKind::none);
    
    modules.erase((juce::uint32) nodeID.uid);
    if (logicalId != 0)
    {
        logicalIdToModule.erase(logicalId);
    }

    updateConnectionSnapshot_Locked();
}

bool ModularSynthProcessor::connect(const NodeID& sourceNodeID, int sourceChannel, const NodeID& destNodeID, int destChannel)
{
    const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
    ScopedGraphMutation mutation(graphMutationDepth);
#endif
    juce::AudioProcessorGraph::Connection connection {
        { sourceNodeID, sourceChannel },
        { destNodeID, destChannel }
    };

    for (const auto& existing : internalGraph->getConnections())
    {
        if (existing.source.nodeID == sourceNodeID &&
            existing.source.channelIndex == sourceChannel &&
            existing.destination.nodeID == destNodeID &&
            existing.destination.channelIndex == destChannel)
        {
            juce::Logger::writeToLog("[ModSynth][INFO] Skipping duplicate connection [" + juce::String(sourceNodeID.uid) + ":" + juce::String(sourceChannel)
                                     + "] -> [" + juce::String(destNodeID.uid) + ":" + juce::String(destChannel) + "]");
            return true;
        }
    }

    const bool ok = internalGraph->addConnection(connection, juce::AudioProcessorGraph::UpdateKind::none);
    if (! ok)
    {
        juce::Logger::writeToLog("[ModSynth][WARN] Failed to connect [" + juce::String(sourceNodeID.uid) + ":" + juce::String(sourceChannel)
                                 + "] -> [" + juce::String(destNodeID.uid) + ":" + juce::String(destChannel) + "]");
    }
    else
    {
        updateConnectionSnapshot_Locked();
    }
    return ok;
}

void ModularSynthProcessor::commitChanges()
{
    const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
    ScopedGraphMutation mutation(graphMutationDepth);
#endif

    internalGraph->rebuild();
    
    if (getSampleRate() > 0 && getBlockSize() > 0)
    {
        internalGraph->prepareToPlay(getSampleRate(), getBlockSize());
    }

    // Set logical IDs
    for (const auto& kv : logicalIdToModule)
    {
        if (ModuleProcessor* mp = getModuleForLogical(kv.first))
        {
            mp->setLogicalId(kv.first);
        }
    }
    
    // --- FINAL THREAD-SAFE FIX: Rebuild the list of active processors for the audio thread ---
    auto newProcessors = std::make_shared<std::vector<std::shared_ptr<ModuleProcessor>>>();
    newProcessors->reserve(logicalIdToModule.size());
    juce::Logger::writeToLog("[GraphSync] Building new processor list...");
    for (const auto& pair : logicalIdToModule)
    {
        auto modIt = modules.find((juce::uint32)pair.second.nodeID.uid);
        if (modIt != modules.end())
        {
            auto nodePtr = modIt->second; // This is the std::shared_ptr<Node>
            if (auto* proc = dynamic_cast<ModuleProcessor*>(nodePtr->getProcessor()))
            {
                // The custom deleter captures nodePtr, keeping it alive
                // as long as this shared_ptr<ModuleProcessor> exists.
                auto processor = std::shared_ptr<ModuleProcessor>(proc, [nodePtr](ModuleProcessor*) {});
                newProcessors->push_back(processor);
                juce::Logger::writeToLog("  [+] Adding module L-ID " + juce::String(pair.first) + 
                                       " (ptr: 0x" + juce::String::toHexString((int64_t)proc) + ")");
            }
        }
    }
    activeAudioProcessors.store(newProcessors);
    juce::Logger::writeToLog("[GraphSync] Updated active processor list for audio thread with " + juce::String(newProcessors->size()) + " modules.");

    updateConnectionSnapshot_Locked();
}

void ModularSynthProcessor::clearAll()
{
    {
        const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
        ScopedGraphMutation mutation(graphMutationDepth);
#endif
        juce::Logger::writeToLog("[GraphSync] clearAll() initiated - removing " + juce::String(logicalIdToModule.size()) + " modules");
        for (const auto& kv : logicalIdToModule)
            internalGraph->removeNode(kv.second.nodeID, juce::AudioProcessorGraph::UpdateKind::none);
        modules.clear();
        logicalIdToModule.clear();
        nextLogicalId = 1;
    }
    commitChanges();
}

void ModularSynthProcessor::clearAllConnections()
{
    {
        const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
        ScopedGraphMutation mutation(graphMutationDepth);
#endif
        auto connections = internalGraph->getConnections();
        for (const auto& conn : connections)
            if (conn.source.channelIndex != juce::AudioProcessorGraph::midiChannelIndex &&
                conn.destination.channelIndex != juce::AudioProcessorGraph::midiChannelIndex)
                internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);
    }
    commitChanges();
}

void ModularSynthProcessor::clearOutputConnections()
{
    if (audioOutputNode == nullptr)
        return;

    {
        const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
        ScopedGraphMutation mutation(graphMutationDepth);
#endif
        auto connections = internalGraph->getConnections();
        for (const auto& conn : connections)
            if (conn.destination.nodeID == audioOutputNode->nodeID)
                internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);
    }
    commitChanges();
}

void ModularSynthProcessor::clearConnectionsForNode(const NodeID& nodeID)
{
    if (nodeID.uid == 0) return;

    {
        const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
        ScopedGraphMutation mutation(graphMutationDepth);
#endif
        auto connections = internalGraph->getConnections();
        for (const auto& conn : connections)
            if (conn.source.nodeID == nodeID || conn.destination.nodeID == nodeID)
                if (conn.source.channelIndex != juce::AudioProcessorGraph::midiChannelIndex)
                    internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);
    }
    commitChanges();
}

void ModularSynthProcessor::setAudioInputChannelMapping(const NodeID& audioInputNodeId, const std::vector<int>& channelMap)
{
    if (audioInputNode == nullptr)
    {
        juce::Logger::writeToLog("[ModSynth][ERROR] setAudioInputChannelMapping called but main audioInputNode is null.");
        return;
    }

    juce::String mapStr;
    for (int i = 0; i < (int)channelMap.size(); ++i)
    {
        if (i > 0) mapStr += ", ";
        mapStr += juce::String(channelMap[i]);
    }
    juce::Logger::writeToLog("[ModSynth] Remapping Audio Input Module " + juce::String(audioInputNodeId.uid) +
                             " to channels: [" + mapStr + "]");

    {
        const juce::ScopedLock lock(moduleLock);
#if JUCE_DEBUG
        ScopedGraphMutation mutation(graphMutationDepth);
#endif
        auto connections = internalGraph->getConnections();
        for (const auto& conn : connections)
            if (conn.source.nodeID == audioInputNode->nodeID && conn.destination.nodeID == audioInputNodeId)
                internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);

        for (int moduleChannel = 0; moduleChannel < (int)channelMap.size(); ++moduleChannel)
        {
            int hardwareChannel = channelMap[moduleChannel];
            internalGraph->addConnection({ { audioInputNode->nodeID, hardwareChannel }, { audioInputNodeId, moduleChannel } },
                                         juce::AudioProcessorGraph::UpdateKind::none);
        }
    }

    commitChanges();
}

std::vector<std::pair<juce::uint32, juce::String>> ModularSynthProcessor::getModulesInfo() const
{
    const juce::ScopedLock lock (moduleLock);
    std::vector<std::pair<juce::uint32, juce::String>> out;
    out.reserve(logicalIdToModule.size());
    for (const auto& kv : logicalIdToModule)
        out.emplace_back(kv.first, kv.second.type);
    return out;
}

juce::AudioProcessorGraph::NodeID ModularSynthProcessor::getNodeIdForLogical (juce::uint32 logicalId) const
{
    const juce::ScopedLock lock (moduleLock);
    auto it = logicalIdToModule.find(logicalId);
    if (it == logicalIdToModule.end()) return {};
    return it->second.nodeID;
}

juce::uint32 ModularSynthProcessor::getLogicalIdForNode (const NodeID& nodeId) const
{
    const juce::ScopedLock lock (moduleLock);
    for (const auto& kv : logicalIdToModule)
        if (kv.second.nodeID == nodeId)
            return kv.first;
    return 0;
}

bool ModularSynthProcessor::disconnect (const NodeID& sourceNodeID, int sourceChannel, const NodeID& destNodeID, int destChannel)
{
    const juce::ScopedLock lock (moduleLock);
#if JUCE_DEBUG
    ScopedGraphMutation mutation(graphMutationDepth);
#endif
    juce::AudioProcessorGraph::Connection connection {
        { sourceNodeID, sourceChannel },
        { destNodeID, destChannel }
    };
    const bool removed = internalGraph->removeConnection(connection, juce::AudioProcessorGraph::UpdateKind::none);
    if (removed)
        updateConnectionSnapshot_Locked();
    return removed;
}

void ModularSynthProcessor::updateConnectionSnapshot_Locked() const
{
    if (! audioOutputNode)
    {
        connectionSnapshot.store(std::make_shared<const std::vector<ConnectionInfo>>(), std::memory_order_release);
        return;
    }

    auto snapshot = std::make_shared<std::vector<ConnectionInfo>>();
    snapshot->reserve(internalGraph->getConnections().size());

    for (const auto& c : internalGraph->getConnections())
    {
        ConnectionInfo info;
        info.srcLogicalId = getLogicalIdForNode(c.source.nodeID);
        info.srcChan = c.source.channelIndex;
        info.dstLogicalId = getLogicalIdForNode(c.destination.nodeID);
        info.dstChan = c.destination.channelIndex;
        info.dstIsOutput = (c.destination.nodeID == audioOutputNode->nodeID);

        if (info.srcLogicalId != 0 && (info.dstLogicalId != 0 || info.dstIsOutput))
            snapshot->push_back(info);
    }

    connectionSnapshot.store(snapshot, std::memory_order_release);
}

std::vector<ModularSynthProcessor::ConnectionInfo> ModularSynthProcessor::getConnectionsInfo() const
{
    auto snapshot = connectionSnapshot.load(std::memory_order_acquire);
    if (! snapshot)
    {
        const juce::ScopedLock lock(moduleLock);
        if (! snapshot)
        {
            const_cast<ModularSynthProcessor*>(this)->updateConnectionSnapshot_Locked();
            snapshot = connectionSnapshot.load(std::memory_order_acquire);
        }
    }

    if (snapshot)
        return *snapshot;
    return {};
}

std::shared_ptr<const std::vector<ModularSynthProcessor::ConnectionInfo>> ModularSynthProcessor::getConnectionSnapshot() const
{
    auto snapshot = connectionSnapshot.load(std::memory_order_acquire);
    if (snapshot)
        return snapshot;

    const juce::ScopedLock lock(moduleLock);
    snapshot = connectionSnapshot.load(std::memory_order_relaxed);
    if (! snapshot)
    {
        const_cast<ModularSynthProcessor*>(this)->updateConnectionSnapshot_Locked();
        snapshot = connectionSnapshot.load(std::memory_order_acquire);
    }
    return snapshot;
}

ModuleProcessor* ModularSynthProcessor::getModuleForLogical (juce::uint32 logicalId) const
{
    const juce::ScopedLock lock (moduleLock);
    auto it = logicalIdToModule.find(logicalId);
    if (it == logicalIdToModule.end()) return nullptr;
    if (auto* node = internalGraph->getNodeForId(it->second.nodeID))
        return dynamic_cast<ModuleProcessor*>(node->getProcessor());
    return nullptr;
}

juce::String ModularSynthProcessor::getModuleTypeForLogical(juce::uint32 logicalId) const
{
    auto it = logicalIdToModule.find(logicalId);
    if (it != logicalIdToModule.end())
    {
        return it->second.type;
    }
    return {};
}

// === COMPREHENSIVE DIAGNOSTICS SYSTEM ===

juce::String ModularSynthProcessor::getSystemDiagnostics() const
{
    juce::String result = "=== MODULAR SYNTH SYSTEM DIAGNOSTICS ===\n\n";
    
    result += "Total Modules: " + juce::String((int)logicalIdToModule.size()) + "\n";
    result += "Next Logical ID: " + juce::String((int)nextLogicalId) + "\n\n";
    
    result += "=== MODULES ===\n";
    for (const auto& pair : logicalIdToModule)
    {
        result += "Logical ID " + juce::String((int)pair.first) + ": " + pair.second.type + 
                 " (Node ID: " + juce::String((int)pair.second.nodeID.uid) + ")\n";
    }
    result += "\n";
    
    result += getConnectionDiagnostics() + "\n";
    
    result += "=== GRAPH STATE ===\n";
    result += "Total Nodes: " + juce::String(internalGraph->getNumNodes()) + "\n";
    result += "Total Connections: (not available)\n";
    
    return result;
}

juce::String ModularSynthProcessor::getModuleDiagnostics(juce::uint32 logicalId) const
{
    auto* module = getModuleForLogical(logicalId);
    if (module)
    {
        return module->getAllDiagnostics();
    }
    else
    {
        return "Module with Logical ID " + juce::String((int)logicalId) + " not found!";
    }
}

juce::String ModularSynthProcessor::getModuleParameterRoutingDiagnostics(juce::uint32 logicalId) const
{
    auto* module = getModuleForLogical(logicalId);
    if (!module)
    {
        return "Module with Logical ID " + juce::String((int)logicalId) + " not found!";
    }
    
    juce::String result = "=== PARAMETER ROUTING DIAGNOSTICS ===\n";
    result += "Module: " + module->getName() + "\n\n";
    
    auto params = module->getParameters();
    
    for (int i = 0; i < params.size(); ++i)
    {
        auto* param = params[i];
        if (auto* paramWithId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
        {
            int busIndex, channelIndex;
            if (module->getParamRouting(paramWithId->paramID, busIndex, channelIndex))
            {
                int absoluteChannel = module->getChannelIndexInProcessBlockBuffer(true, busIndex, channelIndex);
                result += "  \"" + paramWithId->paramID + "\" -> Bus " + juce::String(busIndex) + 
                         ", Channel " + juce::String(channelIndex) + " (Absolute: " + juce::String(absoluteChannel) + ")\n";
            }
            else
            {
                result += "  \"" + paramWithId->paramID + "\" -> NO ROUTING\n";
            }
        }
    }
    
    return result;
}

juce::String ModularSynthProcessor::getConnectionDiagnostics() const
{
    juce::String result = "=== CONNECTIONS ===\n";
    
    auto connections = getConnectionsInfo();
    for (const auto& conn : connections)
    {
        result += "Logical " + juce::String((int)conn.srcLogicalId) + ":" + juce::String(conn.srcChan) + 
                 " -> ";
        
        if (conn.dstIsOutput)
        {
            result += "OUTPUT:" + juce::String(conn.dstChan);
        }
        else
        {
            result += "Logical " + juce::String((int)conn.dstLogicalId) + ":" + juce::String(conn.dstChan);
        }
        result += "\n";
    }
    
    if (connections.empty())
    {
        result += "No connections found.\n";
    }
    
    return result;
}

bool ModularSynthProcessor::isAnyModuleRecording() const
{
    for (const auto& kv : modules)
    {
        if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(kv.second->getProcessor()))
        {
            if (recorder->getIsRecording())
                return true;
        }
    }
    return false;
}

void ModularSynthProcessor::pauseAllRecorders()
{
    for (const auto& kv : modules)
    {
        if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(kv.second->getProcessor()))
        {
            recorder->pauseRecording();
        }
    }
}

void ModularSynthProcessor::resumeAllRecorders()
{
    for (const auto& kv : modules)
    {
        if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(kv.second->getProcessor()))
        {
            recorder->resumeRecording();
        }
    }
}

void ModularSynthProcessor::startAllRecorders()
{
    for (const auto& kv : modules)
    {
        if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(kv.second->getProcessor()))
        {
            recorder->programmaticStartRecording();
        }
    }
}

void ModularSynthProcessor::stopAllRecorders()
{
    for (const auto& kv : modules)
    {
        if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(kv.second->getProcessor()))
        {
            recorder->programmaticStopRecording();
        }
    }
}

// === VOICE MANAGEMENT IMPLEMENTATION ===

int ModularSynthProcessor::findFreeVoice()
{
    for (int i = 0; i < static_cast<int>(m_voices.size()); ++i)
    {
        if (!m_voices[i].isActive)
            return i;
    }
    return -1;
}

int ModularSynthProcessor::findOldestVoice()
{
    if (m_voices.empty())
        return -1;
    
    int oldestIndex = 0;
    juce::uint32 oldestAge = m_voices[0].age;
    
    for (int i = 1; i < static_cast<int>(m_voices.size()); ++i)
    {
        if (m_voices[i].age < oldestAge)
        {
            oldestAge = m_voices[i].age;
            oldestIndex = i;
        }
    }
    
    return oldestIndex;
}

void ModularSynthProcessor::assignNoteToVoice(int voiceIndex, const juce::MidiMessage& noteOn)
{
    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(m_voices.size()))
        return;
    
    Voice& voice = m_voices[voiceIndex];
    voice.isActive = true;
    voice.noteNumber = noteOn.getNoteNumber();
    voice.velocity = noteOn.getFloatVelocity();
    voice.age = m_globalVoiceAge++;
    
    juce::Logger::writeToLog("[VoiceManager] Assigned note " + juce::String(voice.noteNumber) + 
                            " to voice " + juce::String(voiceIndex));
}

void ModularSynthProcessor::releaseVoice(const juce::MidiMessage& noteOff)
{
    int noteNumber = noteOff.getNoteNumber();
    
    for (auto& voice : m_voices)
    {
        if (voice.isActive && voice.noteNumber == noteNumber)
        {
            voice.isActive = false;
            voice.noteNumber = -1;
            juce::Logger::writeToLog("[VoiceManager] Released note " + juce::String(noteNumber));
            return;
        }
    }
}

// === PROBE TOOL IMPLEMENTATION ===

void ModularSynthProcessor::setProbeConnection(const NodeID& sourceNodeID, int sourceChannel)
{
    if (!probeScopeNode || probeScopeNodeId.uid == 0)
    {
        return;
    }
    
    if (internalGraph == nullptr)
        return;
    
    const juce::ScopedLock lock (moduleLock);

    // Clear old connections to probe scope
    auto connections = internalGraph->getConnections();
    for (const auto& conn : connections)
    {
        if (conn.destination.nodeID == probeScopeNodeId)
        {
            internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::sync);
        }
    }
    
    // Connect source to probe scope
    juce::AudioProcessorGraph::Connection newProbeConnection {
        { sourceNodeID, sourceChannel },
        { probeScopeNodeId, 0 }
    };

    if (internalGraph->addConnection(newProbeConnection, juce::AudioProcessorGraph::UpdateKind::sync))
    {
        updateConnectionSnapshot_Locked();
    }
}

void ModularSynthProcessor::clearProbeConnection()
{
    if (!probeScopeNode || probeScopeNodeId.uid == 0)
        return;
    
    bool cleared = false;
    
    if (internalGraph == nullptr)
        return;
    
    const juce::ScopedLock lock (moduleLock);

    auto connections = internalGraph->getConnections();
    for (const auto& conn : connections)
    {
        if (conn.destination.nodeID == probeScopeNodeId)
        {
            internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::sync);
            cleared = true;
        }
    }
    
    if (cleared) {
        updateConnectionSnapshot_Locked();
    }
}

ScopeModuleProcessor* ModularSynthProcessor::getProbeScopeProcessor() const
{
    if (!probeScopeNode)
        return nullptr;
    
    return dynamic_cast<ScopeModuleProcessor*>(probeScopeNode->getProcessor());
}

void ModularSynthProcessor::setTransportPositionSeconds(double positionSeconds)
{
    // Clamp position to valid range (edge case: negative or extreme values)
    positionSeconds = juce::jmax(0.0, positionSeconds);
    
    // Edge case: Prevent extreme position values (sanity check)
    const double maxReasonablePosition = 3600.0 * 24.0; // 24 hours max
    if (positionSeconds > maxReasonablePosition)
    {
        juce::Logger::writeToLog("[ModularSynth] WARNING: Clamping extreme transport position: " + 
            juce::String(positionSeconds, 1) + "s -> " + juce::String(maxReasonablePosition, 1) + "s");
        positionSeconds = maxReasonablePosition;
    }
    
    m_transportState.songPositionSeconds = positionSeconds;
    
    // Derive beats from position if BPM is set
    if (m_transportState.bpm > 0.0)
    {
        m_transportState.songPositionBeats = 
            (positionSeconds * m_transportState.bpm) / 60.0;
    }
    
    // Update sample position for consistency
    double sampleRate = getSampleRate();
    if (sampleRate > 0.0)
    {
        m_samplePosition = (juce::uint64)(positionSeconds * sampleRate);
    }
    
    // Broadcast to all modules (for NEXT block or immediate, depending on call order)
    // Note: If called from TempoClock::processBlock, this update will apply to the next block
    // This is standard and acceptable latency (one-block delay)
    if (auto processors = activeAudioProcessors.load())
    {
        for (const auto& modulePtr : *processors)
            if (modulePtr)
                modulePtr->setTimingInfo(m_transportState);
    }
}
