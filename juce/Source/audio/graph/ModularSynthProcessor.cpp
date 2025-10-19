#include "ModularSynthProcessor.h"
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
#include "../modules/SampleLoaderModuleProcessor.h"
#include "../modules/FunctionGeneratorModuleProcessor.h"
#include "../modules/TimePitchModuleProcessor.h"
#include "../modules/DebugModuleProcessor.h"
#include "../modules/MIDIPlayerModuleProcessor.h"
#include "../modules/PolyVCOModuleProcessor.h"
#include "../modules/BestPracticeNodeProcessor.h"
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

    // Intentionally do NOT connect audio input -> output.
    // The modular container acts as a source; connections are created programmatically.

    internalGraph->addConnection({ { midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                                   { audioOutputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });
}

ModularSynthProcessor::~ModularSynthProcessor() {}

void ModularSynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Ensure the internal graph has matching I/O channel counts before preparation
    internalGraph->setPlayConfigDetails(getTotalNumInputChannels(), getTotalNumOutputChannels(), sampleRate, samplesPerBlock);
    internalGraph->prepareToPlay(sampleRate, samplesPerBlock);

    internalGraph->rebuild();
}

void ModularSynthProcessor::releaseResources()
{
    internalGraph->releaseResources();
}

void ModularSynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    try {
        internalGraph->processBlock(buffer, midiMessages);
        // Diagnostics: if internal graph produced silence for a while, log it
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
    juce::ValueTree root("ModularSynthPreset");
    root.setProperty("version", 1, nullptr);

    // Modules
    juce::ValueTree modsVT("modules");
    // Build reverse lookup NodeID.uid -> logicalId
    std::map<juce::uint32, juce::uint32> nodeUidToLogical;
    for (const auto& kv : logicalIdToModule)
    {
        const juce::uint32 logicalId = kv.first;
        const auto nodeUID = (juce::uint32) kv.second.nodeID.uid;
        nodeUidToLogical[nodeUID] = logicalId;

        juce::ValueTree mv("module");
        mv.setProperty("logicalId", (int) logicalId, nullptr);
        mv.setProperty("type", kv.second.type, nullptr);
        // Attach params as child using the module's APVTS state
        auto itNode = modules.find(nodeUID);
        if (itNode != modules.end())
        {
            if (auto* modProc = dynamic_cast<ModuleProcessor*>(itNode->second->getProcessor()))
            {
                juce::ValueTree params = modProc->getAPVTS().copyState();
                // Wrap params under a known tag
                juce::ValueTree paramsWrapper("params");
                paramsWrapper.addChild(params, -1, nullptr);
                mv.addChild(paramsWrapper, -1, nullptr);

                // Append optional extra state
                if (auto extra = modProc->getExtraStateTree(); extra.isValid())
                {
                    juce::ValueTree extraWrapper("extra");
                    extraWrapper.addChild(extra, -1, nullptr);
                    mv.addChild(extraWrapper, -1, nullptr);
                }
            }
        }
        modsVT.addChild(mv, -1, nullptr);
    }
    root.addChild(modsVT, -1, nullptr);

    // Connections
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
            continue; // skip non-module connections
        }
        connsVT.addChild(cv, -1, nullptr);
    }
    root.addChild(connsVT, -1, nullptr);


    // Serialize to XML
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

    // --- THIS IS THE NEW, CORRECTED LOGIC ---

    // 1. Clear the entire existing state first.
    clearAll();
    juce::Logger::writeToLog("[STATE] Cleared existing state.");

    juce::ValueTree root = juce::ValueTree::fromXml(*xml);
    auto modsVT = root.getChildWithName("modules");
    if (!modsVT.isValid())
    {
        juce::Logger::writeToLog("[STATE] WARNING: No <modules> block found in preset.");
        return;
    }
    
    juce::Logger::writeToLog("[STATE] Found <modules> block with " + juce::String(modsVT.getNumChildren()) + " children.");

    // 2. First pass: Find the maximum logical ID in the preset to prevent future collisions.
    juce::uint32 maxId = 0;
    for (int i = 0; i < modsVT.getNumChildren(); ++i)
    {
        auto mv = modsVT.getChild(i);
        if (mv.hasType("module"))
        {
            maxId = juce::jmax(maxId, (juce::uint32)(int)mv.getProperty("logicalId", 0));
        }
    }
    nextLogicalId = maxId + 1; // Set the counter to be safely above the highest loaded ID.

    // 3. Second pass: Recreate all modules and map their logical IDs.
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

        if (logicalId > 0 && type.isNotEmpty())
        {
            // Create the module using the existing addModule function
            juce::Logger::writeToLog("[STATE]   Calling addModule('" + type + "')...");
            auto nodeId = addModule(type);
            juce::Logger::writeToLog("[STATE]   addModule returned nodeId.uid=" + juce::String(nodeId.uid));
            
            auto* node = internalGraph->getNodeForId(nodeId);
            
            if (node)
            {
                juce::Logger::writeToLog("[STATE]   Node created successfully.");
                
                // Remove the auto-assigned logical mapping and assign the correct one
                for (auto it = logicalIdToModule.begin(); it != logicalIdToModule.end(); )
                {
                    if (it->second.nodeID == nodeId)
                        it = logicalIdToModule.erase(it);
                    else
                        ++it;
                }
                logicalIdToModule[logicalId] = LogicalModule{ nodeId, type };
                logicalToNodeId[logicalId] = nodeId;
                juce::Logger::writeToLog("[STATE]   Mapped logicalId " + juce::String(logicalId) + " to nodeId.uid " + juce::String(nodeId.uid));

                // Restore parameters for this module
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

                // Restore optional extra state (e.g., file paths)
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

    // 4. Recreate connections using the now-populated logical ID map.
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


    // 6. Finalize all changes.
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
            reg("audio input", []{ return std::make_unique<AudioInputModuleProcessor>(); });
            reg("vcf", []{ return std::make_unique<VCFModuleProcessor>(); });
            reg("vca", []{ return std::make_unique<VCAModuleProcessor>(); });
            reg("noise", []{ return std::make_unique<NoiseModuleProcessor>(); });
            reg("lfo", []{ return std::make_unique<LFOModuleProcessor>(); });
            reg("adsr", []{ return std::make_unique<ADSRModuleProcessor>(); });
            reg("mixer", []{ return std::make_unique<MixerModuleProcessor>(); });
            reg("cvmixer", []{ return std::make_unique<CVMixerModuleProcessor>(); });
            reg("cv mixer", []{ return std::make_unique<CVMixerModuleProcessor>(); });
            reg("trackmixer", []{ return std::make_unique<TrackMixerModuleProcessor>(); });
            reg("delay", []{ return std::make_unique<DelayModuleProcessor>(); });
            reg("reverb", []{ return std::make_unique<ReverbModuleProcessor>(); });
            reg("attenuverter", []{ return std::make_unique<AttenuverterModuleProcessor>(); });
            reg("scope", []{ return std::make_unique<ScopeModuleProcessor>(); });
            reg("frequency graph", []{ return std::make_unique<FrequencyGraphModuleProcessor>(); });
            reg("s&h", []{ return std::make_unique<SAndHModuleProcessor>(); });
            reg("sandh", []{ return std::make_unique<SAndHModuleProcessor>(); });
            reg("sequencer", []{ return std::make_unique<StepSequencerModuleProcessor>(); });
            reg("stepsequencer", []{ return std::make_unique<StepSequencerModuleProcessor>(); });
            reg("math", []{ return std::make_unique<MathModuleProcessor>(); });
            reg("maprange", []{ return std::make_unique<MapRangeModuleProcessor>(); });
            reg("comparator", []{ return std::make_unique<ComparatorModuleProcessor>(); });
            reg("random", []{ return std::make_unique<RandomModuleProcessor>(); });
            reg("rate", []{ return std::make_unique<RateModuleProcessor>(); });
            reg("quantizer", []{ return std::make_unique<QuantizerModuleProcessor>(); });
            reg("sequentialswitch", []{ return std::make_unique<SequentialSwitchModuleProcessor>(); });
            reg("logic", []{ return std::make_unique<LogicModuleProcessor>(); });
            reg("clockdivider", []{ return std::make_unique<ClockDividerModuleProcessor>(); });
            reg("waveshaper", []{ return std::make_unique<WaveshaperModuleProcessor>(); });
            reg("8bandshaper", []{ return std::make_unique<MultiBandShaperModuleProcessor>(); });
            reg("granulator", []{ return std::make_unique<GranulatorModuleProcessor>(); });
            reg("harmonic shaper", []{ return std::make_unique<HarmonicShaperModuleProcessor>(); });
            reg("debug", []{ return std::make_unique<DebugModuleProcessor>(); });
            reg("input debug", []{ return std::make_unique<InputDebugModuleProcessor>(); });
            reg("vocal tract filter", []{ return std::make_unique<VocalTractFilterModuleProcessor>(); });
            reg("value", []{ return std::make_unique<ValueModuleProcessor>(); });
            // obsolete simple TTS removed; keep performer
            reg("tts performer", []{ return std::make_unique<TTSPerformerModuleProcessor>(); });
            reg("sample loader", []{ return std::make_unique<SampleLoaderModuleProcessor>(); });
            // Canonicalize Curve Drawer module key
            reg("function generator", []{ return std::make_unique<FunctionGeneratorModuleProcessor>(); });
            reg("timepitch", []{ return std::make_unique<TimePitchModuleProcessor>(); });
            reg("midi player", []{ return std::make_unique<MIDIPlayerModuleProcessor>(); });
            reg("polyvco", []{ return std::make_unique<PolyVCOModuleProcessor>(); });
            reg("best practice", []{ return std::make_unique<BestPracticeNodeProcessor>(); });
            reg("shaping oscillator", []{ return std::make_unique<ShapingOscillatorModuleProcessor>(); });
            reg("multi sequencer", []{ return std::make_unique<MultiSequencerModuleProcessor>(); });
            reg("lag processor", []{ return std::make_unique<LagProcessorModuleProcessor>(); });
            reg("decrackle", []{ return std::make_unique<DeCrackleModuleProcessor>(); });
            reg("de-crackle", []{ return std::make_unique<DeCrackleModuleProcessor>(); });
            reg("graphic eq", []{ return std::make_unique<GraphicEQModuleProcessor>(); });
            reg("chorus", []{ return std::make_unique<ChorusModuleProcessor>(); });
            reg("phaser", []{ return std::make_unique<PhaserModuleProcessor>(); });
            reg("compressor", []{ return std::make_unique<CompressorModuleProcessor>(); });
            reg("recorder", []{ return std::make_unique<RecordModuleProcessor>(); });
            reg("limiter", []{ return std::make_unique<LimiterModuleProcessor>(); });
            reg("gate", []{ return std::make_unique<GateModuleProcessor>(); });
            reg("drive", []{ return std::make_unique<DriveModuleProcessor>(); });

            initialised = true;
        }
        return factory;
    }
}

ModularSynthProcessor::NodeID ModularSynthProcessor::addModule(const juce::String& moduleType)
{
    auto& factory = getModuleFactory();
    const juce::String key = moduleType.toLowerCase();
    std::unique_ptr<juce::AudioProcessor> processor;

    if (auto it = factory.find(key); it != factory.end())
        processor = it->second();

    if (! processor)
    {
        // Try loose match if aliases weren’t added
        for (const auto& kv : factory)
            if (moduleType.equalsIgnoreCase(kv.first)) { processor = kv.second(); break; }
    }

    if (processor)
    {
        auto node = internalGraph->addNode(std::move(processor), {}, juce::AudioProcessorGraph::UpdateKind::none);
        if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
            mp->setParent(this);
        modules[(juce::uint32) node->nodeID.uid] = node;
        const juce::uint32 logicalId = nextLogicalId++;
        logicalIdToModule[logicalId] = LogicalModule{ node->nodeID, moduleType };
        // Wire stable logical ID into the module to avoid pointer-based lookup races
        if (auto* mp = dynamic_cast<ModuleProcessor*>(node->getProcessor()))
            mp->setLogicalId(logicalId);
        
        // If this is our new Audio Input module, set up initial default channel mapping
        if (moduleType.equalsIgnoreCase("audio input"))
        {
            // Default to 2 channels mapping to hardware channels 0 and 1
            std::vector<int> defaultMapping = {0, 1};
            setAudioInputChannelMapping(node->nodeID, defaultMapping);
        }
        
        // CRITICAL FIX: Rebuild the graph so processBlock is actually called!
        commitChanges();
        
        return node->nodeID;
    }

    juce::Logger::writeToLog("[ModSynth][WARN] Unknown module type: " + moduleType);
    return {};
}

void ModularSynthProcessor::removeModule(const NodeID& nodeID)
{
    if (nodeID.uid == 0) return;

    const juce::uint32 logicalId = getLogicalIdForNode(nodeID);

    // --- 2. Remove the main module node (JUCE will handle audio connections automatically) ---
    internalGraph->removeNode(nodeID, juce::AudioProcessorGraph::UpdateKind::none);
    
    // --- 3. Clean up internal tracking maps ---
    modules.erase((juce::uint32) nodeID.uid);
    if (logicalId != 0)
    {
        logicalIdToModule.erase(logicalId);
    }
}

bool ModularSynthProcessor::connect(const NodeID& sourceNodeID, int sourceChannel, const NodeID& destNodeID, int destChannel)
{
    juce::AudioProcessorGraph::Connection connection {
        { sourceNodeID, sourceChannel },
        { destNodeID, destChannel }
    };

    const bool ok = internalGraph->addConnection(connection, juce::AudioProcessorGraph::UpdateKind::none);
    if (! ok)
    {
        juce::Logger::writeToLog("[ModSynth][WARN] Failed to connect [" + juce::String(sourceNodeID.uid) + ":" + juce::String(sourceChannel)
                                 + "] -> [" + juce::String(destNodeID.uid) + ":" + juce::String(destChannel) + "]");
    }
    return ok;
}

void ModularSynthProcessor::commitChanges()
{
    internalGraph->rebuild();

    // --- BEGIN DIAGNOSTIC INSERTION ---
    juce::Logger::writeToLog("--- Modular Synth Internal Patch State ---");
    juce::Logger::writeToLog("Num Nodes: " + juce::String(internalGraph->getNodes().size()));
    juce::Logger::writeToLog("Num Connections: " + juce::String(internalGraph->getConnections().size()));
    for (const auto& node : internalGraph->getNodes())
    {
        auto* p = node->getProcessor();
        juce::String name = p ? p->getName() : juce::String("<null>");
        const int ins  = p ? p->getTotalNumInputChannels()  : -1;
        const int outs = p ? p->getTotalNumOutputChannels() : -1;
        juce::Logger::writeToLog("  Node: id=" + juce::String(node->nodeID.uid) + " name='" + name + "' ins=" + juce::String(ins) + " outs=" + juce::String(outs));
    }
    for (const auto& conn : internalGraph->getConnections())
    {
        juce::Logger::writeToLog("  Connection: [" + juce::String(conn.source.nodeID.uid) + ":" + juce::String(conn.source.channelIndex)
            + "] -> [" + juce::String(conn.destination.nodeID.uid) + ":" + juce::String(conn.destination.channelIndex) + "]");
    }
    juce::Logger::writeToLog("-----------------------------------------");
    // Dump logicalId -> node mapping for engine/editor identity checks
    juce::Logger::writeToLog("--- Logical ID Map ---");
    for (const auto& kv : logicalIdToModule)
    {
        juce::String type = kv.second.type;
        juce::Logger::writeToLog("  logicalId=" + juce::String((int)kv.first) + " nodeID=" + juce::String(kv.second.nodeID.uid) + " type='" + type + "'");
    }
    juce::Logger::writeToLog("----------------------");
    // Assign missing logical IDs to module processors and log processor pointers
    for (const auto& kv : logicalIdToModule)
    {
        ModuleProcessor* mp = getModuleForLogical(kv.first);
        if (mp != nullptr)
        {
            if (mp->getLogicalId() == 0)
            {
                mp->setLogicalId(kv.first);
                juce::Logger::writeToLog("[ModSynth] Assigned logicalId=" + juce::String((int)kv.first) +
                                         " to procPtr=" + juce::String((juce::uint64)(uintptr_t)mp));
            }
            juce::Logger::writeToLog("[ModSynth] Map logicalId=" + juce::String((int)kv.first) +
                                     " procPtr=" + juce::String((juce::uint64)(uintptr_t)mp));
        }
    }
    // --- END DIAGNOSTIC INSERTION ---
}

void ModularSynthProcessor::clearAll()
{

    // Remove all module nodes
    for (const auto& kv : logicalIdToModule)
    {
        internalGraph->removeNode(kv.second.nodeID, juce::AudioProcessorGraph::UpdateKind::none);
    }

    // Clear tracking maps and reset ID counter
    modules.clear();
    logicalIdToModule.clear();
    nextLogicalId = 1;

    // Rebuild the graph to apply changes
    commitChanges();
}

void ModularSynthProcessor::clearAllConnections()
{

    // Remove all audio connections, except for the internal MIDI connection
    auto connections = internalGraph->getConnections();
    for (const auto& conn : connections)
    {
        // Check if this is a MIDI connection by comparing with MIDI channel index
        if (conn.source.channelIndex != juce::AudioProcessorGraph::midiChannelIndex && 
            conn.destination.channelIndex != juce::AudioProcessorGraph::midiChannelIndex)
        {
            internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);
        }
    }
    commitChanges();
}

void ModularSynthProcessor::clearOutputConnections()
{
    if (audioOutputNode == nullptr)
        return;

    auto connections = internalGraph->getConnections();
    for (const auto& conn : connections)
    {
        if (conn.destination.nodeID == audioOutputNode->nodeID)
        {
            internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);
        }
    }
    commitChanges();
}

void ModularSynthProcessor::clearConnectionsForNode(const NodeID& nodeID)
{
    if (nodeID.uid == 0) return;

    // --- 1. Remove Audio Connections ---
    auto connections = internalGraph->getConnections();
    for (const auto& conn : connections)
    {
        if (conn.source.nodeID == nodeID || conn.destination.nodeID == nodeID)
        {
            // Don't remove the internal MIDI pass-through connection
            if (conn.source.channelIndex != juce::AudioProcessorGraph::midiChannelIndex)
            {
                internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);
            }
        }
    }

    const juce::uint32 logicalId = getLogicalIdForNode(nodeID);

    // --- 3. Apply all changes ---
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

    // 1. Remove all existing connections from hardware input to this module
    auto connections = internalGraph->getConnections();
    for (const auto& conn : connections)
    {
        if (conn.source.nodeID == audioInputNode->nodeID && conn.destination.nodeID == audioInputNodeId)
        {
            internalGraph->removeConnection(conn, juce::AudioProcessorGraph::UpdateKind::none);
        }
    }

    // 2. Add new connections based on the map
    for (int moduleChannel = 0; moduleChannel < (int)channelMap.size(); ++moduleChannel)
    {
        int hardwareChannel = channelMap[moduleChannel];
        internalGraph->addConnection({ { audioInputNode->nodeID, hardwareChannel }, { audioInputNodeId, moduleChannel } }, 
                                     juce::AudioProcessorGraph::UpdateKind::none);
    }

    // 3. Rebuild the graph to apply the changes
    commitChanges();
}

std::vector<std::pair<juce::uint32, juce::String>> ModularSynthProcessor::getModulesInfo() const
{
    std::vector<std::pair<juce::uint32, juce::String>> out;
    out.reserve(logicalIdToModule.size());
    for (const auto& kv : logicalIdToModule)
        out.emplace_back(kv.first, kv.second.type);
    return out;
}

juce::AudioProcessorGraph::NodeID ModularSynthProcessor::getNodeIdForLogical (juce::uint32 logicalId) const
{
    auto it = logicalIdToModule.find(logicalId);
    if (it == logicalIdToModule.end()) return {};
    return it->second.nodeID;
}

juce::uint32 ModularSynthProcessor::getLogicalIdForNode (const NodeID& nodeId) const
{
    for (const auto& kv : logicalIdToModule)
        if (kv.second.nodeID == nodeId)
            return kv.first;
    return 0;
}

bool ModularSynthProcessor::disconnect (const NodeID& sourceNodeID, int sourceChannel, const NodeID& destNodeID, int destChannel)
{
    juce::AudioProcessorGraph::Connection connection {
        { sourceNodeID, sourceChannel },
        { destNodeID, destChannel }
    };
    return internalGraph->removeConnection(connection, juce::AudioProcessorGraph::UpdateKind::none);
}

std::vector<ModularSynthProcessor::ConnectionInfo> ModularSynthProcessor::getConnectionsInfo() const
{
    std::vector<ConnectionInfo> out;
    for (const auto& c : internalGraph->getConnections())
    {
        ConnectionInfo info;
        info.srcLogicalId = getLogicalIdForNode(c.source.nodeID);
        info.srcChan = c.source.channelIndex;
        info.dstLogicalId = getLogicalIdForNode(c.destination.nodeID);
        info.dstChan = c.destination.channelIndex;
        info.dstIsOutput = (c.destination.nodeID == audioOutputNode->nodeID);
        if (info.srcLogicalId != 0 && (info.dstLogicalId != 0 || info.dstIsOutput))
            out.push_back(info);
    }
    return out;
}


ModuleProcessor* ModularSynthProcessor::getModuleForLogical (juce::uint32 logicalId) const
{
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
    
    // Overall system info
    result += "Total Modules: " + juce::String((int)logicalIdToModule.size()) + "\n";
    result += "Next Logical ID: " + juce::String((int)nextLogicalId) + "\n\n";
    
    // Module list
    result += "=== MODULES ===\n";
    for (const auto& pair : logicalIdToModule)
    {
        result += "Logical ID " + juce::String((int)pair.first) + ": " + pair.second.type + 
                 " (Node ID: " + juce::String((int)pair.second.nodeID.uid) + ")\n";
    }
    result += "\n";
    
    // Connection info
    result += getConnectionDiagnostics() + "\n";
    
    // Graph info
    result += "=== GRAPH STATE ===\n";
    result += "Total Nodes: " + juce::String(internalGraph->getNumNodes()) + "\n";
    // Note: AudioProcessorGraph doesn't have getNumConnections() method
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
    
    // Get parameters from the AudioProcessor directly
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


