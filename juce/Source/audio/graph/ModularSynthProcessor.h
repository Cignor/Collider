#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include <memory>
#include "../modules/ModuleProcessor.h"
#include "../modules/InputDebugModuleProcessor.h"

class ModularSynthProcessor : public juce::AudioProcessor
{
public:
    ModularSynthProcessor();
    ~ModularSynthProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    
    const juce::String getName() const override { return "Modular Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Public API (initial)
public:
    using Node = juce::AudioProcessorGraph::Node;
    using NodeID = juce::AudioProcessorGraph::NodeID;

    NodeID addModule(const juce::String& moduleType, bool commit = true);
    NodeID addVstModule(juce::AudioPluginFormatManager& formatManager, const juce::PluginDescription& vstDesc);
    NodeID addVstModule(juce::AudioPluginFormatManager& formatManager, const juce::PluginDescription& vstDesc, juce::uint32 logicalIdToAssign);
    void removeModule(const NodeID& nodeID);
    void clearAll(); // Add this line
    void clearAllConnections(); // Add this line
    void clearOutputConnections(); // <<< ADD THIS LINE
    void clearConnectionsForNode(const NodeID& nodeID); // <<< ADD THIS LINE
    bool connect(const NodeID& sourceNodeID, int sourceChannel, const NodeID& destNodeID, int destChannel);
    
    // Set the hardware input channel mapping for an Audio Input module
    void setAudioInputChannelMapping(const NodeID& audioInputNodeId, const std::vector<int>& channelMap);
    
    void commitChanges();
    NodeID getOutputNodeID() const { return audioOutputNode ? audioOutputNode->nodeID : NodeID{}; }
    // Introspection for editor
    std::vector<std::pair<juce::uint32, juce::String>> getModulesInfo() const;
    juce::AudioProcessorGraph::NodeID getNodeIdForLogical (juce::uint32 logicalId) const;
    juce::uint32 getLogicalIdForNode (const NodeID& nodeId) const;
    juce::String getModuleTypeForLogical(juce::uint32 logicalId) const;
    bool disconnect (const NodeID& sourceNodeID, int sourceChannel, const NodeID& destNodeID, int destChannel);
    struct ConnectionInfo
    {
        juce::uint32 srcLogicalId { 0 };
        int srcChan { 0 };
        juce::uint32 dstLogicalId { 0 }; // 0 means audio output
        int dstChan { 0 };
        bool dstIsOutput { false };
    };
    std::vector<ConnectionInfo> getConnectionsInfo() const;
    // Access a module processor for UI parameter editing
    ModuleProcessor* getModuleForLogical (juce::uint32 logicalId) const;
    
    // === GLOBAL TRANSPORT & TIMING ===
    // (TransportState struct is defined in ModuleProcessor.h)
    
    TransportState getTransportState() const { return m_transportState; }
    void setPlaying(bool playing) { m_transportState.isPlaying = playing; }
    void setBPM(double bpm) { m_transportState.bpm = juce::jlimit(20.0, 999.0, bpm); }
    void setGlobalDivisionIndex(int idx) { m_transportState.globalDivisionIndex = idx; }
    
    // MIDI activity indicator
    bool hasMidiActivity() const { return m_midiActivityFlag.exchange(false); }
    void resetTransportPosition() { m_samplePosition = 0; m_transportState.songPositionBeats = 0.0; m_transportState.songPositionSeconds = 0.0; }
    
    // === VOICE MANAGEMENT FOR POLYPHONY ===
    struct Voice {
        bool isActive = false;
        int noteNumber = -1;
        float velocity = 0.0f;
        juce::uint32 age = 0;  // Used for note stealing (oldest voice)
        juce::uint32 targetModuleLogicalId = 0;  // Which PolyVCO this voice is assigned to
    };
    
    void setVoiceManagerEnabled(bool enabled) { m_voiceManagerEnabled = enabled; }
    bool isVoiceManagerEnabled() const { return m_voiceManagerEnabled; }
    void setMaxVoices(int numVoices) { m_voices.resize(numVoices); }
    int getMaxVoices() const { return static_cast<int>(m_voices.size()); }
    const std::vector<Voice>& getVoices() const { return m_voices; }
    
    // === COMPREHENSIVE DIAGNOSTICS SYSTEM ===
    
    // Get system-wide diagnostics
    juce::String getSystemDiagnostics() const;
    
    // Get diagnostics for a specific module
    juce::String getModuleDiagnostics(juce::uint32 logicalId) const;
    
    // Get parameter routing diagnostics for a specific module
    juce::String getModuleParameterRoutingDiagnostics(juce::uint32 logicalId) const;
    
    // Get all connection diagnostics
    juce::String getConnectionDiagnostics() const;
    
    // Check if any recorder module is currently recording (prevents spacebar from stopping audio)
    bool isAnyModuleRecording() const;
    
    // Pause/Resume all active recorders (used by spacebar during audition)
    void pauseAllRecorders();
    void resumeAllRecorders();
    
    // Global start/stop all recorders (used by menu bar)
    void startAllRecorders();
    void stopAllRecorders();
    
    // Plugin format manager for VST support (optional, set by application)
    void setPluginFormatManager(juce::AudioPluginFormatManager* manager) { pluginFormatManager = manager; }
    void setKnownPluginList(juce::KnownPluginList* list) { knownPluginList = list; }
    
    // === PROBE TOOL API ===
    // Probe system for instant signal debugging without manual patching
    void setProbeConnection(const NodeID& sourceNodeID, int sourceChannel);
    void clearProbeConnection();
    ModuleProcessor* getProbeScopeProcessor() const;

private:
    // The internal graph that represents the modular patch
    std::unique_ptr<juce::AudioProcessorGraph> internalGraph;

    // Special nodes for handling I/O within the internal graph
    Node::Ptr audioInputNode;
    Node::Ptr audioOutputNode;
    Node::Ptr midiInputNode;
    
    // MIDI activity indicator (mutable because hasMidiActivity() is const)
    mutable std::atomic<bool> m_midiActivityFlag{false};

    // The APVTS that will expose proxy parameters to the host/AudioEngine
    juce::AudioProcessorValueTreeState apvts;

    // Thread-safe module access for audio thread
    mutable juce::CriticalSection moduleLock;
    std::atomic<std::shared_ptr<const std::vector<std::shared_ptr<ModuleProcessor>>>> activeAudioProcessors;

    // Manage module nodes (legacy map by NodeID.uid)
    std::map<juce::uint32, Node::Ptr> modules; // keyed by NodeID.uid
    // Logical ID mapping for preset save/load
    struct LogicalModule
    {
        juce::AudioProcessorGraph::NodeID nodeID;
        juce::String type;
    };
    std::map<juce::uint32, LogicalModule> logicalIdToModule; // logicalId -> module
    juce::uint32 nextLogicalId { 1 };
    
    // Optional pointers for VST support
    juce::AudioPluginFormatManager* pluginFormatManager { nullptr };
    juce::KnownPluginList* knownPluginList { nullptr };
    
    // Probe scope for instant signal debugging (hidden from user, not saved in presets)
    Node::Ptr probeScopeNode;
    NodeID probeScopeNodeId;
    
    // Transport state
    TransportState m_transportState;
    juce::uint64 m_samplePosition { 0 };
    
    // Voice management state
    std::vector<Voice> m_voices;
    bool m_voiceManagerEnabled { false };
    juce::uint32 m_globalVoiceAge { 0 };  // Incremented for each note-on
    
    // Voice management helper methods
    int findFreeVoice();
    int findOldestVoice();
    void assignNoteToVoice(int voiceIndex, const juce::MidiMessage& noteOn);
    void releaseVoice(const juce::MidiMessage& noteOff);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModularSynthProcessor)
};