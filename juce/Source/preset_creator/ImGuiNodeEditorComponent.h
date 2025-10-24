#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <unordered_map>
#include <tuple>
#include <deque>
#include <imgui.h>
#include "../audio/modules/ModuleProcessor.h"
#include "../audio/graph/ModularSynthProcessor.h"
#include "PresetManager.h"
#include "SampleManager.h"
#include "MidiManager.h"

// Forward declarations from Dear ImGui / imnodes
struct ImGuiContext; struct ImGuiIO; struct ImNodesContext;
class MIDIPlayerModuleProcessor;
class MultiSequencerModuleProcessor;

// Pin information struct for node editor
struct PinInfo {
    uint32_t id;      // The unique ID of the pin
    juce::String type; // The parsed type ("Pitch", "Gate", "Trig", etc.)
};

class ImGuiNodeEditorComponent : public juce::Component,
                                 private juce::OpenGLRenderer
{
public:
    ImGuiNodeEditorComponent(juce::AudioDeviceManager& deviceManager);
    ~ImGuiNodeEditorComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    // Input is handled by imgui_juce backend; no JUCE overrides needed
    
    void setMidiActivityFrames(int frames) { midiActivityFrames = frames; }

    void setModel (ModularSynthProcessor* model) 
    { 
        synth = model; 
        undoStack.clear(); 
        redoStack.clear(); 
    }
    
    // ADD: Callback for showing audio settings dialog
    std::function<void()> onShowAudioSettings;
    
    // UI state roundtrip
    juce::ValueTree getUiValueTree();
    // Thread-safe: queues UI state to be applied on next render frame
    void applyUiValueTree (const juce::ValueTree& uiState);
    void applyUiValueTreeNow (const juce::ValueTree& uiState);

    // --- Helper structs ---
    struct Range { float min; float max; };
    
    // --- Pin ID System Struct (declare before any usage) ---
    struct PinID
    {
        juce::uint32 logicalId = 0;
        int channel = 0;
        bool isInput = false;
        bool isMod = false;
        juce::String paramId; // used for mod pins
    };

    // OpenGLRenderer
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // Helper functions for pin type checking and UI
    PinDataType getPinDataTypeForPin(const PinID& pin);
    unsigned int getImU32ForType(PinDataType type);
    const char* pinDataTypeToString(PinDataType type);
    
    void handleRandomizePatch();
    void handleRandomizeConnections();
    void handleConnectSelectedToTrackMixer();
    void handleBeautifyLayout();
    void handleMidiPlayerAutoConnect(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid);
    void handleMidiPlayerAutoConnectVCO(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid);
    void handleMidiPlayerAutoConnectHybrid(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid);
    void handleInsertNodeOnSelectedLinks(const juce::String& nodeType);
    void insertNodeBetween(const juce::String& nodeType);
    void insertNodeBetween(const juce::String& nodeType, const PinID& srcPin, const PinID& dstPin);
    void drawInsertNodeOnLinkPopup();

    // New intelligent auto-connection system
    void parsePinName(const juce::String& fullName, juce::String& outType, int& outIndex);
    
    // Helper functions to get pins from modules
    std::vector<AudioPin> getOutputPins(const juce::String& moduleType);
    std::vector<AudioPin> getInputPins(const juce::String& moduleType);
    AudioPin* findInputPin(const juce::String& moduleType, const juce::String& pinName);
    AudioPin* findOutputPin(const juce::String& moduleType, const juce::String& pinName);
    std::vector<juce::uint32> findNodesOfType(const juce::String& moduleType);
    
    // New dynamic pin-fetching helper
    std::vector<PinInfo> getDynamicOutputPins(ModuleProcessor* module);
    
    template<typename TargetProcessorType>
    void connectToMonophonicTargets(ModuleProcessor* sourceNode, const std::map<juce::String, juce::String>& pinNameMapping, const std::vector<juce::uint32>& targetLids);
    template<typename TargetProcessorType>
    void connectToPolyphonicTarget(ModuleProcessor* sourceNode, const std::map<juce::String, juce::String>& pinNameMapping);
    void handleAutoConnectionRequests();
    
    // MultiSequencer specific handlers
    void handleMultiSequencerAutoConnectSamplers(MultiSequencerModuleProcessor* sequencer, juce::uint32 sequencerLid);
    void handleMultiSequencerAutoConnectVCO(MultiSequencerModuleProcessor* sequencer, juce::uint32 sequencerLid);

    void renderImGui();
    void handleDeletion();
    void bypassDeleteSelectedNodes();
    void bypassDeleteNode(juce::uint32 logicalId);
    void startSaveDialog();
    void startLoadDialog();
    juce::String getTypeForLogical (juce::uint32 logicalId) const;

    // --- Collision-Proof Pin ID System ---
    // 32-bit ID with guaranteed separation from node IDs:
    // Bit 31: PIN_ID_FLAG (always 1 for pins, 0 for nodes)
    // Bit 30: IS_INPUT_FLAG (1 for input, 0 for output)
    // Bits 16-29: Channel Index (14 bits, up to 16384 channels)
    // Bits 0-15: Node Logical ID (16 bits, up to 65535 nodes)
    // This ensures pin IDs can never collide with node IDs
    
    static int encodePinId(const PinID& pinId)
    {
        const juce::uint32 PIN_ID_FLAG = (1u << 31);
        const juce::uint32 IS_INPUT_FLAG = (1u << 30);
        
        juce::uint32 encoded = PIN_ID_FLAG |
                               (pinId.isInput ? IS_INPUT_FLAG : 0) |
                               (((juce::uint32)pinId.channel & 0x3FFF) << 16) |
                               (pinId.logicalId & 0xFFFF);
        
        return (int)encoded;
    }

    static PinID decodePinId(int id)
    {
        PinID pinId;
        const juce::uint32 uid = (juce::uint32)id;
        const juce::uint32 PIN_ID_FLAG = (1u << 31);
        const juce::uint32 IS_INPUT_FLAG = (1u << 30);

        // Only decode if this is actually a pin ID (has the flag set)
        if ((uid & PIN_ID_FLAG) == 0)
        {
            // This is not a pin ID! Return invalid pin
            juce::Logger::writeToLog("[ERROR] decodePinId called with non-pin ID: " + juce::String((int)id));
            pinId.logicalId = 0;
            pinId.channel = 0;
            pinId.isInput = false;
            pinId.isMod = false;
            return pinId;
        }

        pinId.logicalId = uid & 0xFFFF;
        pinId.channel   = (int)((uid >> 16) & 0x3FFF); // 14-bit mask
        pinId.isInput   = (uid & IS_INPUT_FLAG) != 0;
        pinId.isMod     = false; // handled contextually, not in the bitmask
        return pinId;
    }


    juce::OpenGLContext glContext;
    ImGuiContext* imguiContext { nullptr };
    ImGuiIO* imguiIO { nullptr };
    ImNodesContext* editorContext { nullptr };
    double lastTime { 0.0 };

    juce::AudioDeviceManager& deviceManager;
    ModularSynthProcessor* synth { nullptr };
    juce::ValueTree uiPending; // applied at next render before drawing nodes
    std::atomic<bool> graphNeedsRebuild { false };
    int midiActivityFrames = 0; // For MIDI activity indicator
    
    // Preset and sample management
    PresetManager m_presetManager;
    SampleManager m_sampleManager;
    juce::String m_presetSearchTerm;
    juce::String m_sampleSearchTerm;
    juce::File m_presetScanPath;
    juce::File m_sampleScanPath;
    std::unique_ptr<juce::FileChooser> presetPathChooser;
    std::unique_ptr<juce::FileChooser> samplePathChooser;
    
    // MIDI file management
    MidiManager m_midiManager;
    juce::File m_midiScanPath;
    juce::String m_midiSearchTerm;
    std::unique_ptr<juce::FileChooser> midiPathChooser;
    
    // Meta module editing state
    juce::uint32 metaModuleToEditLid = 0;
    
    // Cache of last-known valid node positions (used when graphNeedsRebuild prevents rendering)
    std::unordered_map<int, ImVec2> lastKnownNodePositions;

    // Selection state
    int selectedLogicalId { 0 };

    std::unique_ptr<juce::FileChooser> saveChooser, loadChooser;

    // Map of linkId -> (srcAttr, dstAttr) populated each frame
    std::unordered_map<int, std::pair<int,int>> linkIdToAttrs;
    
    // Link ID registry (cleared each frame for stateless rendering)
    struct LinkKey { int srcAttr; int dstAttr; };
    struct LinkKeyHash { size_t operator()(const LinkKey& k) const noexcept { return ((size_t)k.srcAttr << 32) ^ (size_t)k.dstAttr; } };
    struct LinkKeyEq { bool operator()(const LinkKey& a, const LinkKey& b) const noexcept { return a.srcAttr==b.srcAttr && a.dstAttr==b.dstAttr; } };
    std::unordered_map<LinkKey, int, LinkKeyHash, LinkKeyEq> linkToId;
    int nextLinkId { 1000 };
    int getLinkId(int srcAttr, int dstAttr)
    {
        LinkKey k{ srcAttr, dstAttr };
        auto it = linkToId.find(k);
        if (it != linkToId.end()) return it->second;
        const int id = nextLinkId++;
        linkToId.emplace(k, id);
        return id;
    }

    // Cable inspector highlight state (updated once per frame after EndNodeEditor)
    juce::uint32 hoveredLinkSrcId { 0 };
    juce::uint32 hoveredLinkDstId { 0 };
    static constexpr juce::uint32 kOutputHighlightId = 0xFFFFFFFFu; // sentinel for main output node highlight
    int lastHoveredLinkId { -1 }; // cache inside-editor hovered link id for post-editor use

    // Positions to apply for specific node IDs on the next render (grid space)
    std::unordered_map<int, ImVec2> pendingNodePositions;
    // Screen-space positions queued for just-created nodes (converted after draw)
    std::unordered_map<int, ImVec2> pendingNodeScreenPositions;
    // Sizes to apply for specific node IDs on the next render (for Comment nodes)
    std::unordered_map<int, ImVec2> pendingNodeSizes;

    // Cable inspector rolling stats (last N seconds) for quick visual validation
    struct ChannelHistory { std::deque<std::pair<double,float>> samples; };
    std::map<std::pair<juce::uint32,int>, ChannelHistory> inspectorHistory; // key: (logicalId, channel)
    float inspectorWindowSeconds { 5.0f };

    // --- NEW STATE FOR CABLE SPLITTING ---
    // Stores the attribute ID of the pin we are splitting from.
    // -1 means no split operation is active.
    int splittingFromAttrId = -1;

    // A map to cache the screen position of every pin attribute ID each frame.
    // This is a necessary workaround as ImNodes doesn't provide a public API
    // to get a pin's position by its ID.
    std::unordered_map<int, ImVec2> attrPositions;


    // UI state / hover
    int lastHoveredNodeId { -1 };
    bool isDraggingNode { false };
    bool snapshotAfterEditor { false }; // arm when action requires node to exist (add/duplicate)
    // zoom/pan disabled

    // --- Undo/Redo (module ops) ---
    struct Snapshot
    {
        juce::MemoryBlock synthState;
        juce::ValueTree   uiState;
    };
    std::vector<Snapshot> undoStack;
    std::vector<Snapshot> redoStack;
    void pushSnapshot();
    void restoreSnapshot (const Snapshot& s);
    
    // SampleLoader texture management (use JUCE OpenGLTexture to avoid raw GL includes)
    std::unordered_map<int, std::unique_ptr<juce::OpenGLTexture>> sampleLoaderTextureIds;

    // Preset status tracking
    juce::String currentPresetFile;
    bool isPatchDirty { false };

    // Help window
    bool showShortcutsWindow { false };

    // Shortcut debounce
    bool mixerShortcutCooldown { false };
    bool insertNodeShortcutCooldown { false };
    bool showInsertNodePopup { false };
    bool showDebugMenu { false };
    int pendingInsertLinkId { -1 };
    
    // Probe tool state
    bool isProbeModeActive { false };
    bool showProbeScope { true };
    
    // Insert node on link state
    struct LinkInfo
    {
        int linkId = -1;
        bool isMod = false;
        // For Audio links:
        PinID srcPin;
        PinID dstPin;
        // For Mod links:
        juce::uint32 srcLogicalId;
        int srcChan;
        juce::uint32 dstLogicalId;
        juce::String paramId;
    };
    LinkInfo linkToInsertOn;
    
    // Mute/Bypass state management (non-destructive)
    struct MutedNodeState {
        std::vector<ModularSynthProcessor::ConnectionInfo> incomingConnections;
        std::vector<ModularSynthProcessor::ConnectionInfo> outgoingConnections;
    };
    std::map<juce::uint32, MutedNodeState> mutedNodeStates;
    
    void muteNodeSilent(juce::uint32 logicalId);  // Store mute state without modifying graph (for loading presets)
    void muteNode(juce::uint32 logicalId);
    void unmuteNode(juce::uint32 logicalId);
    void handleMuteToggle();

    // Copy/Paste settings clipboard
    juce::ValueTree nodeSettingsClipboard;
    juce::String clipboardModuleType;

    // Helper functions
    void insertNodeOnLink(const juce::String& nodeType, const LinkInfo& linkInfo, const ImVec2& position);
    juce::File findPresetsDirectory();

    // --- NEW: Handler for node chaining shortcut ---
    void handleNodeChaining();

    // --- NEW: Handlers for color-coded chaining ---
    void handleColorCodedChaining(PinDataType targetType);
    std::vector<AudioPin> getPinsOfType(juce::uint32 logicalId, bool isInput, PinDataType targetType);
    
    // --- Recorder Output Shortcut ---
    void handleRecordOutput();
    
    // --- Unified Preset Loading ---
    void loadPresetFromFile(const juce::File& file);
    void mergePresetFromFile(const juce::File& file, ImVec2 dropPosition);
    
    // --- Meta Module (Sub-Patching) Support ---
    void handleCollapseToMetaModule();
    
    // --- Module Category Color Coding ---
    enum class ModuleCategory { Source, Effect, Modulator, Utility, Analysis, Comment, Plugin, MIDI };
    ModuleCategory getModuleCategory(const juce::String& moduleType);
    unsigned int getImU32ForCategory(ModuleCategory category, bool hovered = false);
    
    // --- Quick Add Menu ---
    std::map<juce::String, std::pair<const char*, const char*>> getModuleRegistry();
    std::vector<std::pair<juce::String, const char*>> getModuleDescriptions();
    
    // --- VST Plugin Support ---
    void addPluginModules();
};
