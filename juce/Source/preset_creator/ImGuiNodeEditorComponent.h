#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <unordered_map>
#include <tuple>
#include <deque>
#include <atomic>
#include <imgui.h>
#include <imnodes.h>
#include "../audio/modules/ModuleProcessor.h"
#include "../audio/graph/ModularSynthProcessor.h"
#include "PresetManager.h"
#include "SampleManager.h"
#include "MidiManager.h"
#include "ControllerPresetManager.h"
#include "NotificationManager.h"
#include "ShortcutManager.h"
#include "theme/ThemeEditorComponent.h"
#include "HelpManagerComponent.h"

// Forward declarations from Dear ImGui / imnodes
struct ImGuiContext; struct ImGuiIO; struct ImNodesContext;
class MIDIPlayerModuleProcessor;
class MultiSequencerModuleProcessor;
class StrokeSequencerModuleProcessor;
class AnimationModuleProcessor;
class ColorTrackerModule;
class MetaModuleProcessor;

// Forward declaration (SavePresetJob is now in its own file to avoid circular dependencies)
class SavePresetJob;

// === NODE SIZING SYSTEM ===
// Standardized node width categories for consistent visual layout
enum class NodeWidth 
{ 
    Small,      // 240px - Basic modules (VCO, VCA, simple utilities)
    Medium,     // 360px - Effects with visualizations (Reverb, Chorus, Phaser)
    Big,        // 480px - Complex modules (PolyVCO, advanced effects)
    ExtraWide,  // 840px - Timeline/grid modules (MultiSequencer, MIDI Player)
    Exception   // Custom size - Module defines its own dimensions via getCustomNodeSize()
};

// Helper function to convert NodeWidth category to pixel width
inline float getWidthForCategory(NodeWidth width)
{
    switch (width)
    {
        case NodeWidth::Medium:    return 360.0f;
        case NodeWidth::Big:       return 480.0f;
        case NodeWidth::ExtraWide: return 840.0f;
        case NodeWidth::Exception: return 0.0f;  // Signals that module provides custom size
        case NodeWidth::Small:
        default:                   return 360.0f;
    }
}

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
        if (synth)
        {
            synth->setOnModuleCreated([](const juce::String& pretty){
                NotificationManager::post(NotificationManager::Type::Info, "Created " + pretty + " node");
            });
        }
        undoStack.clear(); 
        redoStack.clear(); 
    }
    
    // ADD: Callback for showing audio settings dialog
    std::function<void()> onShowAudioSettings;
    
    // UI state roundtrip
    juce::ValueTree getUiValueTree() const;
    // Thread-safe: queues UI state to be applied on next render frame
    void applyUiValueTree (const juce::ValueTree& uiState);
    void applyUiValueTreeNow (const juce::ValueTree& uiState);
    void rebuildFontAtlas();
    void requestFontAtlasRebuild()
    {
        fontAtlasNeedsRebuild.store(true, std::memory_order_relaxed);
    }

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
    void insertNodeBetween(const juce::String& nodeType, const PinID& srcPin, const PinID& dstPin, bool createUndoSnapshot = true);
    void drawInsertNodeOnLinkPopup();
    struct LinkInfo;
    void drawLinkInspectorTooltip(const LinkInfo& link);

    // New intelligent auto-connection system
    void parsePinName(const juce::String& fullName, juce::String& outType, int& outIndex);
    void updateRerouteTypeFromConnections(juce::uint32 rerouteLogicalId);
    
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
    
    // MIDI Player Quick Connect handler
    void handleMIDIPlayerConnectionRequest(juce::uint32 midiPlayerLid, MIDIPlayerModuleProcessor* midiPlayer, int requestType);
    
    // StrokeSequencer specific handler
    void handleStrokeSeqBuildDrumKit(StrokeSequencerModuleProcessor* strokeSeq, juce::uint32 strokeSeqLid);
    
    // AnimationModule specific handlers
    void handleAnimationBuildTriggersAudio(AnimationModuleProcessor* animModule, juce::uint32 animModuleLid);
    
    // MultiSequencer specific handlers
    void handleMultiSequencerAutoConnectSamplers(MultiSequencerModuleProcessor* sequencer, juce::uint32 sequencerLid);
    void handleMultiSequencerAutoConnectVCO(MultiSequencerModuleProcessor* sequencer, juce::uint32 sequencerLid);
    
    // Color Tracker auto-connect handlers
    void handleColorTrackerAutoConnectPolyVCO(ColorTrackerModule* colorTracker, juce::uint32 colorTrackerLid);
    void handleColorTrackerAutoConnectSamplers(ColorTrackerModule* colorTracker, juce::uint32 colorTrackerLid);

    void renderImGui();
    void handleDeletion();
    void bypassDeleteSelectedNodes();
    void bypassDeleteNode(juce::uint32 logicalId);
    void startSaveDialog();
    void savePresetToFile(const juce::File& file);
    void startLoadDialog();
    std::vector<juce::uint32> getMutedNodeIds() const;
    juce::String getTypeForLogical (juce::uint32 logicalId) const;

    struct MetaModuleEditorSession
    {
        struct ContextDeleter
        {
            void operator()(ImNodesContext* ctx) const noexcept
            {
                if (ctx != nullptr)
                    ImNodes::DestroyContext(ctx);
            }
        };

        std::unique_ptr<ImNodesContext, ContextDeleter> context;
        juce::uint32 metaLogicalId { 0 };
        MetaModuleProcessor* meta { nullptr };
        ModularSynthProcessor* graph { nullptr };
        std::unordered_map<int, ImVec2> nodePositions;
        std::unordered_map<int, std::pair<int, int>> linkIdToAttrs;
        bool dirty { false };
        juce::String moduleSearchTerm;
    };

    void openMetaModuleEditor(MetaModuleProcessor* metaModule, juce::uint32 metaLogicalId);
    void closeMetaModuleEditor();
    void renderMetaModuleEditor(MetaModuleEditorSession& session);

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
    collider::ShortcutManager& shortcutManager { collider::ShortcutManager::getInstance() };
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
    std::unique_ptr<MetaModuleEditorSession> metaEditorSession;
    
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
    int m_currentlyProbedLinkId { -1 }; // caches the last link sent to the probe to avoid redundant graph rebuilds
    static inline const juce::Identifier nodeEditorContextId { "NodeEditor" };

    void registerShortcuts();
    void unregisterShortcuts();

    static bool consumeShortcutFlag(std::atomic<bool>& flag) noexcept
    {
        return flag.exchange(false, std::memory_order_acq_rel);
    }

    struct ShortcutActionIds
    {
        static inline const juce::Identifier fileSave { "actions.file.save" };
        static inline const juce::Identifier fileSaveAs { "actions.file.saveAs" };
        static inline const juce::Identifier fileOpen { "actions.file.open" };
        static inline const juce::Identifier fileRandomizePatch { "actions.file.randomizePatch" };
        static inline const juce::Identifier fileRandomizeConnections { "actions.file.randomizeConnections" };
        static inline const juce::Identifier fileBeautifyLayout { "actions.file.beautifyLayout" };
        static inline const juce::Identifier editCtrlR { "actions.edit.resetOrRecord" };
        static inline const juce::Identifier editMuteSelection { "actions.edit.muteSelection" };
        static inline const juce::Identifier editSelectAll { "actions.edit.selectAll" };
        static inline const juce::Identifier editConnectOutput { "actions.edit.connectToOutput" };
        static inline const juce::Identifier editDisconnectSelection { "actions.edit.disconnectSelection" };
        static inline const juce::Identifier editDuplicate { "actions.edit.duplicate" };
        static inline const juce::Identifier editDuplicateWithRouting { "actions.edit.duplicateWithRouting" };
        static inline const juce::Identifier editDelete { "actions.edit.delete" };
        static inline const juce::Identifier editBypassDelete { "actions.edit.bypassDelete" };
        static inline const juce::Identifier viewFrameSelection { "actions.view.frameSelection" };
        static inline const juce::Identifier viewFrameAll { "actions.view.frameAll" };
        static inline const juce::Identifier viewResetOrigin { "actions.view.resetOrigin" };
        static inline const juce::Identifier viewToggleMinimap { "actions.view.toggleMinimap" };
        static inline const juce::Identifier viewToggleShortcutsWindow { "actions.view.toggleShortcutsWindow" };
        static inline const juce::Identifier historyUndo { "actions.history.undo" };
        static inline const juce::Identifier historyRedo { "actions.history.redo" };
        static inline const juce::Identifier debugToggleOverlay { "actions.debug.toggleDiagnostics" };
        static inline const juce::Identifier graphInsertMixer { "actions.graph.insertMixer" };
        static inline const juce::Identifier graphConnectSelectedToTrackMixer { "actions.graph.connectSelectedToTrackMixer" };
        static inline const juce::Identifier graphShowInsertPopup { "actions.graph.showInsertPopup" };
        static inline const juce::Identifier graphInsertOnLink { "actions.graph.insertOnLink" };
        static inline const juce::Identifier graphChainSequential { "actions.graph.chainSequential" };
        static inline const juce::Identifier graphChainAudio { "actions.graph.chainAudio" };
        static inline const juce::Identifier graphChainCv { "actions.graph.chainCv" };
        static inline const juce::Identifier graphChainGate { "actions.graph.chainGate" };
        static inline const juce::Identifier graphChainRaw { "actions.graph.chainRaw" };
        static inline const juce::Identifier graphChainVideo { "actions.graph.chainVideo" };
    };

    std::atomic<bool> shortcutFileSaveRequested { false };
    std::atomic<bool> shortcutFileSaveAsRequested { false };
    std::atomic<bool> shortcutFileOpenRequested { false };
    std::atomic<bool> shortcutRandomizePatchRequested { false };
    std::atomic<bool> shortcutRandomizeConnectionsRequested { false };
    std::atomic<bool> shortcutBeautifyLayoutRequested { false };
    std::atomic<bool> shortcutCtrlRRequested { false };
    std::atomic<bool> shortcutSelectAllRequested { false };
    std::atomic<bool> shortcutMuteSelectionRequested { false };
    std::atomic<bool> shortcutConnectOutputRequested { false };
    std::atomic<bool> shortcutDisconnectRequested { false };
    std::atomic<bool> shortcutDuplicateRequested { false };
    std::atomic<bool> shortcutDuplicateWithRoutingRequested { false };
    std::atomic<bool> shortcutDeleteRequested { false };
    std::atomic<bool> shortcutBypassDeleteRequested { false };
    std::atomic<bool> shortcutFrameSelectionRequested { false };
    std::atomic<bool> shortcutFrameAllRequested { false };
    std::atomic<bool> shortcutResetOriginRequested { false };
    std::atomic<bool> shortcutToggleMinimapRequested { false };
    std::atomic<bool> shortcutUndoRequested { false };
    std::atomic<bool> shortcutRedoRequested { false };
    std::atomic<bool> shortcutToggleDebugRequested { false };
    std::atomic<bool> shortcutInsertMixerRequested { false };
    std::atomic<bool> shortcutConnectSelectedToTrackMixerRequested { false };
    std::atomic<bool> shortcutShowInsertPopupRequested { false };
    std::atomic<bool> shortcutInsertOnLinkRequested { false };
    std::atomic<bool> shortcutChainSequentialRequested { false };
    std::atomic<bool> shortcutChainAudioRequested { false };
    std::atomic<bool> shortcutChainCvRequested { false };
    std::atomic<bool> shortcutChainGateRequested { false };
    std::atomic<bool> shortcutChainRawRequested { false };
    std::atomic<bool> shortcutChainVideoRequested { false };

    // Positions to apply for specific node IDs on the next render (grid space)
    std::unordered_map<int, ImVec2> pendingNodePositions;
    // Screen-space positions queued for just-created nodes (converted after draw)
    std::unordered_map<int, ImVec2> pendingNodeScreenPositions;
    // Sizes to apply for specific node IDs on the next render (for Comment nodes)
    std::unordered_map<int, ImVec2> pendingNodeSizes;
    std::atomic<bool> fontAtlasNeedsRebuild { false };
    std::atomic<bool> isMinimapEnlarged { false };
    float modalMinimapScale = 0.2f;

    // Cable inspector rolling stats (last N seconds) for quick visual validation
    struct ChannelHistory 
    { 
        std::deque<std::pair<double, float>> samples; 
        double lastAccessTime = 0.0; // Track when this history was last accessed
    };
    std::map<std::pair<juce::uint32,int>, ChannelHistory> inspectorHistory; // key: (logicalId, channel)
    float inspectorWindowSeconds { 5.0f };

    // --- NEW STATE FOR CABLE SPLITTING ---
    // Stores the attribute ID of the pin we are splitting from.
    // -1 means no split operation is active.
    int splittingFromAttrId = -1;

    // Drag-to-empty detection state
    bool dragInsertActive { false };
    int dragInsertStartAttrId { -1 };
    PinID dragInsertStartPin {};
    ImVec2 dragInsertDropPos { 0.0f, 0.0f };
    bool shouldOpenDragInsertPopup { false };

    // Module suggestion caches (directional)
    std::map<PinDataType, std::vector<juce::String>> dragInsertSuggestionsInputs;
    std::map<PinDataType, std::vector<juce::String>> dragInsertSuggestionsOutputs;

    // A map to cache the screen position of every pin attribute ID each frame.
    // This is a necessary workaround as ImNodes doesn't provide a public API
    // to get a pin's position by its ID.
    std::unordered_map<int, ImVec2> attrPositions;


    // UI state / hover
    int lastHoveredNodeId { -1 };
    bool isDraggingNode { false };
    bool snapshotAfterEditor { false }; // arm when action requires node to exist (add/duplicate)
    // zoom/pan disabled
    
    // --- Modal Minimap Pan State ---

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
    std::unordered_map<int, std::unique_ptr<juce::OpenGLTexture>> visionModuleTextures;
    
    // Accessor for modules that need to render their own preview with interaction
    std::unordered_map<int, std::unique_ptr<juce::OpenGLTexture>>& getVisionModuleTextures() { return visionModuleTextures; }

    // Preset status tracking
    juce::File currentPresetFile;  // Full file path for save operations
    bool isPatchDirty { false };
    
    // Background save/load operations
    std::atomic<bool> isSaveInProgress { false }; // Debouncing flag for save operations
    juce::ThreadPool threadPool { 2 };

    // Help Manager (replaces old shortcut editor window)
    HelpManagerComponent m_helpManager { this };

    // Shortcut debounce
    bool mixerShortcutCooldown { false };
    bool insertNodeShortcutCooldown { false };
    bool showInsertNodePopup { false };
    bool showDebugMenu { false };
    bool showMidiDeviceManager { false };
    ThemeEditorComponent themeEditor { this };
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
        // For inspector/probe tooltip:
        juce::uint32 srcNodeId = 0;
        juce::String pinName;
        juce::String sourceNodeName;
        int srcChannel = -1;
        juce::uint32 srcLogicalNodeId = 0;
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
    void insertNodeOnLinkStereo(const juce::String& nodeType, const LinkInfo& linkLeft, const LinkInfo& linkRight, const ImVec2& position);
    juce::File findPresetsDirectory();
    void populateDragInsertSuggestions();

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
    const std::vector<juce::String>& getDragInsertSuggestionsFor(const PinID& pin) const;
    void insertNodeFromDragSelection(const juce::String& moduleType);
    
    // --- Meta Module (Sub-Patching) Support ---
    void handleCollapseToMetaModule();
    void expandMetaModule(juce::uint32 metaLogicalId);
    
    // --- Module Category Color Coding ---
    enum class ModuleCategory { Source, Effect, Modulator, Utility, Seq, MIDI, Analysis, TTS_Voice, Special_Exp, OpenCV, Sys, Comment, Plugin };
    ModuleCategory getModuleCategory(const juce::String& moduleType);
    unsigned int getImU32ForCategory(ModuleCategory category, bool hovered = false);
    
    // --- Quick Add Menu ---
    std::map<juce::String, std::pair<const char*, const char*>> getModuleRegistry();
    std::vector<std::pair<juce::String, const char*>> getModuleDescriptions();
    
    // --- VST Plugin Support ---
    void addPluginModules();
    
    // --- Global GPU/CPU Settings ---
    static bool getGlobalGpuEnabled() { return s_globalGpuEnabled; }
    static void setGlobalGpuEnabled(bool enabled) { s_globalGpuEnabled = enabled; }
    
    // --- Eyedropper API ---
public:
    void startColorPicking(std::function<void(ImU32)> onPicked)
    {
        m_isPickingColor = true;
        m_onColorPicked = std::move(onPicked);
    }

private:
    static bool s_globalGpuEnabled; // Global preference for GPU acceleration

    // Cached canvas dimensions for modal pan logic
    ImVec2 lastCanvasP0;      // Cached top-left corner of the canvas
    ImVec2 lastCanvasSize;    // Cached size of the canvas
    ImVec2 lastEditorPanning { 0.0f, 0.0f }; // Cached ImNodes panning for manual grid
    bool hasRenderedAtLeastOnce { false }; // Tracks whether the node editor has completed a full frame

    // Eyedropper state
    bool m_isPickingColor { false };
    std::function<void(ImU32)> m_onColorPicked;
};
