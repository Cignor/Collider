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

// Forward declarations from Dear ImGui / imnodes
struct ImGuiContext; struct ImGuiIO; struct ImNodesContext;
class MIDIPlayerModuleProcessor;
class MultiSequencerModuleProcessor;

// Global pin database (defined in ImGuiNodeEditorComponent.cpp)



extern std::map<juce::String, ModulePinInfo> modulePinDatabase;

// Add this simple struct to hold pin information
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

    void setModel (ModularSynthProcessor* model) { synth = model; undoStack.clear(); redoStack.clear(); if (synth != nullptr) pushSnapshot(); }
    
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

    // --- New Bitmask-based Pin ID System ---

    // Define how the 32 bits of an integer are allocated
    constexpr static int LOGICAL_ID_BITS = 16;
    // Use wider channel space so high parameter indices (for mod pins) don't collide
    constexpr static int CHANNEL_BITS = 12; // supports up to 4096 distinct channels/param-indices
    constexpr static int IS_INPUT_BITS = 1;
    constexpr static int IS_MOD_BITS = 1;

    constexpr static int LOGICAL_ID_SHIFT = 0;
    constexpr static int CHANNEL_SHIFT = LOGICAL_ID_SHIFT + LOGICAL_ID_BITS;
    constexpr static int IS_INPUT_SHIFT = CHANNEL_SHIFT + CHANNEL_BITS;
    constexpr static int IS_MOD_SHIFT = IS_INPUT_SHIFT + IS_INPUT_BITS;

    // Stable attribute registry (avoids collisions and survives frame order)
    struct AttrKey { juce::uint32 logicalId; int channel; bool isInput; bool isMod; };
    struct AttrKeyHash { size_t operator()(const AttrKey& k) const noexcept { return ((size_t)k.logicalId << 20) ^ ((size_t)k.channel << 4) ^ ((k.isInput?1:0)<<2) ^ (k.isMod?1:0); } };
    struct AttrKeyEq { bool operator()(const AttrKey& a, const AttrKey& b) const noexcept { return a.logicalId==b.logicalId && a.channel==b.channel && a.isInput==b.isInput && a.isMod==b.isMod; } };

    int getAttrId(juce::uint32 logicalId, int channel, bool isInput, bool isMod)
    {
        AttrKey key{ logicalId, channel, isInput, isMod };
        auto it = pinToAttr.find(key);
        if (it != pinToAttr.end()) return it->second;
        const int id = nextAttrId++;
        pinToAttr.emplace(key, id);
        attrToPin.emplace(id, key);
        return id;
    }
    PinID decodeAttr(int attr) const
    {
        PinID out{};
        if (auto it = attrToPin.find(attr); it != attrToPin.end())
        {
            out.logicalId = it->second.logicalId;
            out.channel   = it->second.channel;
            out.isInput   = it->second.isInput;
            out.isMod     = it->second.isMod;

            // If it's a mod pin, look up its parameter ID string
            if (out.isMod)
            {
                // TODO: Implement parameter ID lookup for new bus-based system
                // if (auto itMod = modAttrToParam.find(attr); itMod != modAttrToParam.end())
                // {
                //     out.paramId = itMod->second.second;
                // }
            }
        }
        return out;
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

    // Selection state
    int selectedLogicalId { 0 };

    std::unique_ptr<juce::FileChooser> saveChooser, loadChooser;

    // Map of linkId -> (srcAttr, dstAttr) populated each frame
    std::unordered_map<int, std::pair<int,int>> linkIdToAttrs;
    // Stable attr registry
    std::unordered_map<AttrKey, int, AttrKeyHash, AttrKeyEq> pinToAttr;
    std::unordered_map<int, AttrKey> attrToPin;
    int nextAttrId { 1 };

    // Stable link registry to avoid hash collisions
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
};



