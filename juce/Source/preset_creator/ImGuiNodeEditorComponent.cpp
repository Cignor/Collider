#include "ImGuiNodeEditorComponent.h"
#include "PinDatabase.h"

#include <imgui.h>
#include <imnodes.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <algorithm>
#include "../audio/graph/ModularSynthProcessor.h"
#include "../audio/modules/ModuleProcessor.h"
#include "../audio/modules/AudioInputModuleProcessor.h"
#include "../audio/modules/AttenuverterModuleProcessor.h"
#include "../audio/modules/MapRangeModuleProcessor.h"
#include "../audio/modules/RandomModuleProcessor.h"
#include "../audio/modules/ValueModuleProcessor.h"
#include "../audio/modules/SampleLoaderModuleProcessor.h"
#include "../audio/modules/MIDIPlayerModuleProcessor.h"
#include "../audio/modules/PolyVCOModuleProcessor.h"
#include "../audio/modules/TrackMixerModuleProcessor.h"
#include "../audio/modules/MathModuleProcessor.h"
#include "../audio/modules/StepSequencerModuleProcessor.h"
#include "../audio/modules/MultiSequencerModuleProcessor.h"
#include "../audio/modules/StrokeSequencerModuleProcessor.h"
#include "../audio/modules/MapRangeModuleProcessor.h"
#include "../audio/modules/LagProcessorModuleProcessor.h"
#include "../audio/modules/DeCrackleModuleProcessor.h"
#include "../audio/modules/GraphicEQModuleProcessor.h"
#include "../audio/modules/FrequencyGraphModuleProcessor.h"
#include "../audio/modules/ChorusModuleProcessor.h"
#include "../audio/modules/PhaserModuleProcessor.h"
#include "../audio/modules/CompressorModuleProcessor.h"
#include "../audio/modules/RecordModuleProcessor.h"
#include "../audio/modules/CommentModuleProcessor.h"
#include "../audio/modules/LimiterModuleProcessor.h"
#include "../audio/modules/GateModuleProcessor.h"
#include "../audio/modules/DriveModuleProcessor.h"
#include "../audio/modules/VstHostModuleProcessor.h"
// #include "../audio/modules/SnapshotSequencerModuleProcessor.h"  // Commented out - causing build errors
#include "../audio/modules/MIDICVModuleProcessor.h"
#include "../audio/modules/ScopeModuleProcessor.h"
#include "../audio/modules/MetaModuleProcessor.h"
#include "../audio/modules/InletModuleProcessor.h"
#include "../audio/modules/OutletModuleProcessor.h"
#include "PresetCreatorApplication.h"
#include "PresetCreatorComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <imgui_impl_juce/imgui_impl_juce.h>
#include <backends/imgui_impl_opengl2.h>
#include <juce_opengl/juce_opengl.h>

#define NODE_DEBUG 1

// --- Module Descriptions for Tooltips ---
static const char* toString(PinDataType t)
{
    switch (t)
    {
        case PinDataType::Audio: return "Audio";
        case PinDataType::CV: return "CV";
        case PinDataType::Gate: return "Gate";
        case PinDataType::Raw: return "Raw";
        default: return "Unknown";
    }
}

#define LOG_LINK(msg) do { if (NODE_DEBUG) juce::Logger::writeToLog("[LINK] " + juce::String(msg)); } while(0)

struct Range { float min; float max; };

// Forward declarations
class ModularSynthProcessor;
class RandomModuleProcessor;
class ValueModuleProcessor;
class StepSequencerModuleProcessor;
class MapRangeModuleProcessor;

// Helper methods for MapRange configuration
ImGuiNodeEditorComponent::Range getSourceRange(const ImGuiNodeEditorComponent::PinID& srcPin, ModularSynthProcessor* synth)
{
    if (synth == nullptr) return {0.0f, 1.0f};
    
    auto* module = synth->getModuleForLogical(srcPin.logicalId);
    if (auto* random = dynamic_cast<RandomModuleProcessor*>(module))
    {
        auto& ap = random->getAPVTS();
        float min = 0.0f, max = 1.0f;
        if (auto* minParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("min")))
            min = minParam->get();
        if (auto* maxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("max")))
            max = maxParam->get();
        return {min, max};
    }
    else if (auto* value = dynamic_cast<ValueModuleProcessor*>(module))
    {
        auto& ap = value->getAPVTS();
        float min = 0.0f, max = 1.0f;
        if (auto* minParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("min")))
            min = minParam->get();
        if (auto* maxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("max")))
            max = maxParam->get();
        return {min, max};
    }
    else if (auto* stepSeq = dynamic_cast<StepSequencerModuleProcessor*>(module))
    {
        // StepSequencer outputs CV range
        return {0.0f, 1.0f};
    }
    // Fallback: estimate from source's lastOutputValues
    // TODO: implement fallback estimation
    return {0.0f, 1.0f};
}

void configureMapRangeFor(PinDataType srcType, PinDataType dstType, MapRangeModuleProcessor& m, ImGuiNodeEditorComponent::Range inRange)
{
    auto& ap = m.getAPVTS();
    
    // Set input range
    if (auto* inMinParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMin")))
        *inMinParam = inRange.min;
    if (auto* inMaxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMax")))
        *inMaxParam = inRange.max;
    
    // Set output range based on destination type
    if (dstType == PinDataType::Audio)
    {
        if (auto* outMinParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMin")))
            *outMinParam = -1.0f;
        if (auto* outMaxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMax")))
            *outMaxParam = 1.0f;
    }
    else // CV or Gate
    {
        if (auto* outMinParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMin")))
            *outMinParam = 0.0f;
        if (auto* outMaxParam = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMax")))
            *outMaxParam = 1.0f;
    }
}




ImGuiNodeEditorComponent::ImGuiNodeEditorComponent(juce::AudioDeviceManager& dm)
    : deviceManager(dm)
{
    juce::Logger::writeToLog("ImGuiNodeEditorComponent constructor starting...");
    
    // --- THIS WILL BE THE SMOKING GUN ---
    juce::Logger::writeToLog("About to populate pin database...");
    populatePinDatabase(); // Initialize the pin database for color coding
    juce::Logger::writeToLog("Pin database populated.");
    
    glContext.setRenderer (this);
    glContext.setContinuousRepainting (true);
    glContext.setComponentPaintingEnabled (false);
    glContext.attachTo (*this);
    setWantsKeyboardFocus (true);
    
    // Initialize browser paths (load from saved settings or use defaults)
    if (auto* props = PresetCreatorApplication::getApp().getProperties())
    {
        // Load the last used paths, providing defaults if they don't exist
        auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
        juce::File defaultPresetPath = appFile.getParentDirectory().getChildFile("Presets");
        juce::File defaultSamplePath = appFile.getParentDirectory().getChildFile("Samples");

        m_presetScanPath = juce::File(props->getValue("presetScanPath", defaultPresetPath.getFullPathName()));
        m_sampleScanPath = juce::File(props->getValue("sampleScanPath", defaultSamplePath.getFullPathName()));
    }
    
    // Create these directories if they don't already exist
    if (!m_presetScanPath.exists())
        m_presetScanPath.createDirectory();
    if (!m_sampleScanPath.exists())
        m_sampleScanPath.createDirectory();
    
    juce::Logger::writeToLog("[UI] Preset path set to: " + m_presetScanPath.getFullPathName());
    juce::Logger::writeToLog("[UI] Sample path set to: " + m_sampleScanPath.getFullPathName());
    
    // --- MIDI BROWSER PATH INITIALIZATION ---
    if (auto* props = PresetCreatorApplication::getApp().getProperties())
    {
        auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
        juce::File defaultMidiPath = appFile.getParentDirectory().getChildFile("audio").getChildFile("MIDI");
        m_midiScanPath = juce::File(props->getValue("midiScanPath", defaultMidiPath.getFullPathName()));
    }
    if (!m_midiScanPath.exists())
        m_midiScanPath.createDirectory();
    juce::Logger::writeToLog("[UI] MIDI path set to: " + m_midiScanPath.getFullPathName());
    // --- END OF MIDI INITIALIZATION ---
}

ImGuiNodeEditorComponent::~ImGuiNodeEditorComponent()
{
    glContext.detach();
}

void ImGuiNodeEditorComponent::paint (juce::Graphics& g)
{
    juce::ignoreUnused (g);
}

void ImGuiNodeEditorComponent::resized()
{
    juce::Logger::writeToLog ("resized: " + juce::String (getWidth()) + "x" + juce::String (getHeight()));
}

// Input handled by imgui_juce backend

void ImGuiNodeEditorComponent::newOpenGLContextCreated()
{
    juce::Logger::writeToLog("ImGuiNodeEditor: newOpenGLContextCreated()");
    // Create ImGui context
    imguiContext = ImGui::CreateContext();
    imguiIO = &ImGui::GetIO();
    ImGui::StyleColorsDark();

    // --- FONT LOADING FOR CHINESE CHARACTERS ---
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault(); // Load default English font

    // Define the path to your new font file
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto fontFile = appFile.getParentDirectory().getChildFile("../../Source/assets/NotoSansSC-VariableFont_wght.ttf");

    if (fontFile.existsAsFile())
    {
        ImFontConfig config;
        config.MergeMode = true; // IMPORTANT: This merges the new font into the default one
        config.PixelSnapH = true;

        // Define the character ranges to load for Chinese
        static const ImWchar ranges[] = { 0x4e00, 0x9fbf, 0, }; // Basic CJK Unified Ideographs

        io.Fonts->AddFontFromFileTTF(fontFile.getFullPathName().toRawUTF8(), 16.0f, &config, ranges);
        juce::Logger::writeToLog("ImGuiNodeEditor: Chinese font loaded successfully");
    }
    else
    {
        juce::Logger::writeToLog("ImGuiNodeEditor: WARNING - Chinese font not found at: " + fontFile.getFullPathName());
    }
    
    // --- END OF FONT LOADING ---

    // imgui_juce backend handles key mapping internally (new IO API)

    // Setup JUCE platform backend and OpenGL2 renderer backend
    ImGui_ImplJuce_Init (*this, glContext);
    ImGui_ImplOpenGL2_Init();
    
    // Build fonts after renderer is initialized
    io.Fonts->Build();

    // Setup imnodes
    ImNodes::SetImGuiContext(ImGui::GetCurrentContext());
    editorContext = ImNodes::CreateContext();
    
    // Enable grid snapping
    ImNodes::GetStyle().GridSpacing = 64.0f;
    
    // Optional ergonomics: Alt = pan, Ctrl = detach link
    {
        auto& ioNodes = ImNodes::GetIO();
        auto& ioImgui = ImGui::GetIO();
        ioNodes.EmulateThreeButtonMouse.Modifier = &ioImgui.KeyAlt;
        ioNodes.LinkDetachWithModifierClick.Modifier = &ioImgui.KeyCtrl;
    }
    juce::Logger::writeToLog("ImGuiNodeEditor: ImNodes context created");
}

void ImGuiNodeEditorComponent::openGLContextClosing()
{
    juce::Logger::writeToLog("ImGuiNodeEditor: openGLContextClosing()");
    ImNodes::DestroyContext(editorContext);
    editorContext = nullptr;
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplJuce_Shutdown();
    ImGui::DestroyContext (imguiContext);
    imguiContext = nullptr; imguiIO = nullptr;
}

void ImGuiNodeEditorComponent::renderOpenGL()
{
    if (imguiContext == nullptr)
        return;

    ImGui::SetCurrentContext (imguiContext);

    // Clear background
    juce::OpenGLHelpers::clear (juce::Colours::darkgrey);

    // Ensure IO is valid and configured each frame (size, delta time, DPI scale, fonts)
    ImGuiIO& io = ImGui::GetIO();
    const float scale = (float) glContext.getRenderingScale();
    io.DisplaySize = ImVec2 ((float) getWidth(), (float) getHeight());
    io.DisplayFramebufferScale = ImVec2 (scale, scale);

    // imgui_juce will queue and apply key/mouse events; avoid manual KeysDown edits that break internal asserts
    io.MouseDrawCursor = false;

    // Mouse input comes via backend listeners; avoid overriding io.MousePos here

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (lastTime <= 0.0)
        lastTime = nowMs;
    const double dtMs = nowMs - lastTime;
    lastTime = nowMs;
    io.DeltaTime = (dtMs > 0.0 ? (float) (dtMs / 1000.0) : 1.0f / 60.0f);

    // Zoom/pan disabled: use default font scale and editor panning

    // Start a new frame for both backends
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplJuce_NewFrame();

    ImGui::NewFrame();
    // Demo is hidden by default; toggle can be added later if needed
    renderImGui();
    ImGui::Render();
    auto* dd = ImGui::GetDrawData();
    // Render via OpenGL2 backend
    ImGui_ImplOpenGL2_RenderDrawData (dd);
}

void ImGuiNodeEditorComponent::renderImGui()
{
    static int frameCounter = 0;
    frameCounter++;

    // ========================= THE DEFINITIVE FIX =========================
    //
    // Rebuild the audio graph at the START of the frame if a change is pending.
    // This ensures that the synth model is in a consistent state BEFORE we try
    // to draw the UI, eliminating the "lost frame" that caused nodes to jump.
    //
    if (graphNeedsRebuild.load())
    {
        juce::Logger::writeToLog("[GraphSync] Rebuild flag is set. Committing changes now...");
        if (synth)
        {
            synth->commitChanges();
        }
        graphNeedsRebuild = false; // Reset the flag immediately after committing.
        
        // CRITICAL: Invalidate hover state to prevent cable inspector from accessing
        // modules that were just deleted/recreated during commitChanges()
        lastHoveredLinkId = -1;
        lastHoveredNodeId = -1;
        hoveredLinkSrcId = 0;
        hoveredLinkDstId = 0;
        
        juce::Logger::writeToLog("[GraphSync] Graph rebuild complete.");
    }
    // ========================== END OF FIX ==========================

    // Frame start
    
    // --- Stateless Frame Rendering ---
    // Clear link registries at start of each frame for fully stateless rendering.
    // Pin IDs are now generated directly via bitmasking, no maps needed.
    linkIdToAttrs.clear();
    linkToId.clear();
    nextLinkId = 1000;

    // Handle F1 key for shortcuts window
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
    {
        showShortcutsWindow = !showShortcutsWindow;
    }

    // Basic docking-like two-panels layout
    ImGui::SetNextWindowPos (ImVec2 (0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize (ImVec2 ((float) getWidth(), (float) getHeight()), ImGuiCond_Always);
    ImGui::Begin ("Preset Creator", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);

    // --- DEFINITIVE STATUS OVERLAY ---
    // This code creates the small, semi-transparent window for the preset status.
    const float sidebarWidth = 260.0f;
    const float menuBarHeight = ImGui::GetFrameHeight();
    const float padding = 10.0f;

    ImGui::SetNextWindowPos(ImVec2(sidebarWidth + padding, menuBarHeight + padding));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("Preset Status Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);

    // Display the preset name or "Unsaved Patch"
    if (currentPresetFile.isNotEmpty()) {
        ImGui::Text("Preset: %s", currentPresetFile.toRawUTF8());
    } else {
        ImGui::Text("Preset: Unsaved Patch");
    }

    // Display the "Saved" or "Edited" status
    if (isPatchDirty) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Status: EDITED");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: SAVED");
    }

    ImGui::End();
    // --- END OF OVERLAY ---
    
    // === PROBE SCOPE OVERLAY ===
    if (synth != nullptr && showProbeScope)
    {
        if (auto* scope = dynamic_cast<ScopeModuleProcessor*>(synth->getProbeScopeProcessor()))
        {
            ImGui::SetNextWindowPos(ImVec2((float)getWidth() - 270.0f, menuBarHeight + padding), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(260, 180), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.85f);
            
            if (ImGui::Begin("🔬 Probe Scope", &showProbeScope, ImGuiWindowFlags_NoFocusOnAppearing))
            {
                ImGui::Text("Signal Probe");
                ImGui::Separator();
                
                // Get scope buffer
                const auto& buffer = scope->getScopeBuffer();
                
                if (buffer.getNumSamples() > 0)
                {
                    // Create a simple waveform display
                    const int numSamples = buffer.getNumSamples();
                    const float* samples = buffer.getReadPointer(0);
                    
                    // Calculate min/max for this buffer
                    float minVal = 0.0f, maxVal = 0.0f;
                    for (int i = 0; i < numSamples; ++i)
                    {
                        minVal = juce::jmin(minVal, samples[i]);
                        maxVal = juce::jmax(maxVal, samples[i]);
                    }
                    
                    // Display stats
                    ImGui::Text("Min: %.3f  Max: %.3f", minVal, maxVal);
                    ImGui::Text("Peak: %.3f", juce::jmax(std::abs(minVal), std::abs(maxVal)));
                    
                    // Draw waveform with explicit width to avoid node expansion feedback
                    ImVec2 plotSize = ImVec2(ImGui::GetContentRegionAvail().x, 100);
                    ImGui::PlotLines("##Waveform", samples, numSamples, 0, nullptr, -1.0f, 1.0f, plotSize);
                    
                    // Button to clear probe connection
                    if (ImGui::Button("Clear Probe"))
                    {
                        synth->clearProbeConnection();
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No signal probed");
                    ImGui::Text("Right-click > Probe Signal");
                    ImGui::Text("Then click any output pin");
                }
            }
            ImGui::End();
        }
    }
    // === END OF PROBE SCOPE OVERLAY ===

    // Clean up textures for deleted sample loaders
    if (synth != nullptr)
    {
        auto infos = synth->getModulesInfo();
        std::unordered_set<int> activeSampleLoaderIds;
        for (const auto& info : infos)
        {
            if (info.second.equalsIgnoreCase("sample loader"))
            {
                activeSampleLoaderIds.insert((int)info.first);
            }
        }

        for (auto it = sampleLoaderTextureIds.begin(); it != sampleLoaderTextureIds.end(); )
        {
            if (activeSampleLoaderIds.find(it->first) == activeSampleLoaderIds.end())
            {
                if (it->second)
                    it->second.reset();
                it = sampleLoaderTextureIds.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // ADD THIS BLOCK:
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Preset", "Ctrl+S")) { startSaveDialog(); }
            if (ImGui::MenuItem("Load Preset", "Ctrl+O")) { startLoadDialog(); }
            
            // ADD: Audio Settings menu item
            if (ImGui::MenuItem("Audio Settings..."))
            {
                if (onShowAudioSettings)
                    onShowAudioSettings();
            }
            
            // MIDI Device Manager menu item
            if (ImGui::MenuItem("MIDI Device Manager..."))
            {
                showMidiDeviceManager = !showMidiDeviceManager;
            }
            
            ImGui::Separator();
            
            // Plugin scanning menu item
            if (ImGui::MenuItem("Scan for Plugins..."))
            {
                // Get the application instance to access plugin management
                auto& app = PresetCreatorApplication::getApp();
                auto& formatManager = app.getPluginFormatManager();
                auto& knownPluginList = app.getKnownPluginList();

                // 1. Find the VST3 format
                juce::VST3PluginFormat* vst3Format = nullptr;
                for (int i = 0; i < formatManager.getNumFormats(); ++i)
                {
                    if (auto* format = formatManager.getFormat(i); format->getName() == "VST3")
                    {
                        vst3Format = dynamic_cast<juce::VST3PluginFormat*>(format);
                        break;
                    }
                }

                if (vst3Format != nullptr)
                {
                    // 2. Define the specific folder to scan
                    juce::File vstDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                            .getParentDirectory().getChildFile("VST");

                    juce::FileSearchPath searchPath;
                    if (vstDir.isDirectory())
                    {
                        searchPath.add(vstDir);
                        juce::Logger::writeToLog("[VST Scan] Starting scan in: " + vstDir.getFullPathName());
                    }
                    else
                    {
                        vstDir.createDirectory();
                        searchPath.add(vstDir);
                        juce::Logger::writeToLog("[VST Scan] Created VST directory at: " + vstDir.getFullPathName());
                    }

                    // 3. Scan for plugins
                    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                        .getChildFile(app.getApplicationName());
                    
                    juce::PluginDirectoryScanner scanner(knownPluginList, *vst3Format, searchPath, true,
                                                         appDataDir.getChildFile("dead_plugins.txt"), true);

                    // 4. Perform the scan
                    juce::String pluginBeingScanned;
                    int numFound = 0;
                    while (scanner.scanNextFile(true, pluginBeingScanned))
                    {
                        juce::Logger::writeToLog("[VST Scan] Scanning: " + pluginBeingScanned);
                        ++numFound;
                    }
                    
                    juce::Logger::writeToLog("[VST Scan] Scan complete. Found " + juce::String(numFound) + " plugin(s).");
                    juce::Logger::writeToLog("[VST Scan] Total plugins in list: " + juce::String(knownPluginList.getNumTypes()));
                    
                    // 5. Save the updated plugin list
                    auto pluginListFile = appDataDir.getChildFile("known_plugins.xml");
                    if (auto pluginListXml = knownPluginList.createXml())
                    {
                        if (pluginListXml->writeTo(pluginListFile))
                        {
                            juce::Logger::writeToLog("[VST Scan] Saved plugin list to: " + pluginListFile.getFullPathName());
                        }
                    }
                }
                else
                {
                    juce::Logger::writeToLog("[VST Scan] ERROR: VST3 format not found in format manager.");
                }
            }
            
            ImGui::EndMenu();
        }
        
        // <<< ADD THIS ENTIRE "Edit" MENU BLOCK >>>
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Clear Output Connections")) 
            {
                if (synth != nullptr)
                {
                    synth->clearOutputConnections();
                    pushSnapshot(); // Make the action undoable
                }
            }

            // <<< ADD THIS ENTIRE BLOCK >>>
            bool isNodeSelected = (ImNodes::NumSelectedNodes() > 0);
            if (ImGui::MenuItem("Clear Selected Node Connections", nullptr, false, isNodeSelected))
            {
                if (synth != nullptr)
                {
                    std::vector<int> selectedNodeIds(ImNodes::NumSelectedNodes());
                    ImNodes::GetSelectedNodes(selectedNodeIds.data());
                    if (!selectedNodeIds.empty())
                    {
                        // Act on the first selected node
                        juce::uint32 logicalId = (juce::uint32)selectedNodeIds[0];
                        auto nodeId = synth->getNodeIdForLogical(logicalId);
                        if (nodeId.uid != 0)
                        {
                            synth->clearConnectionsForNode(nodeId);
                            pushSnapshot(); // Make the action undoable
                        }
                    }
                }
            }
            // <<< END OF BLOCK >>>

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Actions"))
        {
            // This item should only be enabled if at least one node is selected
            bool anyNodesSelected = ImNodes::NumSelectedNodes() > 0;
            bool multipleNodesSelected = ImNodes::NumSelectedNodes() > 1;
            
            if (ImGui::MenuItem("Connect Selected to Track Mixer", nullptr, false, anyNodesSelected))
            {
                handleConnectSelectedToTrackMixer();
            }
            
            // Meta Module: Collapse selected nodes into a reusable sub-patch
            if (ImGui::MenuItem("Collapse to Meta Module", "Ctrl+Shift+M", false, multipleNodesSelected))
            {
                handleCollapseToMetaModule();
            }
            
            if (ImGui::MenuItem("Record Output", "Ctrl+R"))
            {
                handleRecordOutput();
            }
            
            if (ImGui::MenuItem("Beautify Layout", "Ctrl+B"))
            {
                handleBeautifyLayout();
            }
            
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Recording"))
        {
            if (synth != nullptr)
            {
                bool isAnyRecording = synth->isAnyModuleRecording();
                const char* label = isAnyRecording ? "Stop All Recordings" : "Start All Recordings";
                if (ImGui::MenuItem(label))
                {
                    if (isAnyRecording)
                    {
                        synth->stopAllRecorders();
                    }
                    else
                    {
                        synth->startAllRecorders();
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Generate"))
        {
            if (ImGui::MenuItem("Randomize Patch", "Ctrl+P")) { handleRandomizePatch(); }
            if (ImGui::MenuItem("Randomize Connections", "Ctrl+M")) { handleRandomizeConnections(); }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Insert Node"))
        {
            bool isNodeSelected = (selectedLogicalId != 0);
            
            if (ImGui::BeginMenu("Audio Path", isNodeSelected))
            {
                if (ImGui::MenuItem("VCF")) { insertNodeBetween("VCF"); }
                if (ImGui::MenuItem("VCA")) { insertNodeBetween("VCA"); }
                if (ImGui::MenuItem("Delay")) { insertNodeBetween("Delay"); }
                if (ImGui::MenuItem("Reverb")) { insertNodeBetween("Reverb"); }
                if (ImGui::MenuItem("Chorus")) { insertNodeBetween("chorus"); }
                if (ImGui::MenuItem("Phaser")) { insertNodeBetween("phaser"); }
                if (ImGui::MenuItem("Compressor")) { insertNodeBetween("compressor"); }
                if (ImGui::MenuItem("Limiter")) { insertNodeBetween("limiter"); }
                if (ImGui::MenuItem("Gate")) { insertNodeBetween("gate"); }
                if (ImGui::MenuItem("Drive")) { insertNodeBetween("drive"); }
                if (ImGui::MenuItem("Graphic EQ")) { insertNodeBetween("graphic eq"); }
                if (ImGui::MenuItem("Waveshaper")) { insertNodeBetween("Waveshaper"); }
                if (ImGui::MenuItem("Time/Pitch Shifter")) { insertNodeBetween("timepitch"); }
                if (ImGui::MenuItem("De-Crackle")) { insertNodeBetween("De-Crackle"); }
                if (ImGui::MenuItem("Recorder")) { insertNodeBetween("recorder"); }
                if (ImGui::MenuItem("Mixer")) { insertNodeBetween("Mixer"); }
                if (ImGui::MenuItem("Shaping Oscillator")) { insertNodeBetween("shaping oscillator"); }
                if (ImGui::MenuItem("Function Generator")) { insertNodeBetween("Function Generator"); }
                if (ImGui::MenuItem("8-Band Shaper")) { insertNodeBetween("8bandshaper"); }
                if (ImGui::MenuItem("Granulator")) { insertNodeBetween("Granulator"); }
                if (ImGui::MenuItem("Harmonic Shaper")) { insertNodeBetween("harmonic shaper"); }
                if (ImGui::MenuItem("Vocal Tract Filter")) { insertNodeBetween("Vocal Tract Filter"); }
                if (ImGui::MenuItem("Scope")) { insertNodeBetween("Scope"); }
                if (ImGui::MenuItem("Frequency Graph")) { insertNodeBetween("Frequency Graph"); }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Modulation Path", isNodeSelected))
            {
                if (ImGui::MenuItem("Attenuverter")) { insertNodeBetween("Attenuverter"); }
                if (ImGui::MenuItem("Lag Processor")) { insertNodeBetween("Lag Processor"); }
                if (ImGui::MenuItem("Math")) { insertNodeBetween("Math"); }
                if (ImGui::MenuItem("MapRange")) { insertNodeBetween("MapRange"); }
                if (ImGui::MenuItem("Quantizer")) { insertNodeBetween("Quantizer"); }
                if (ImGui::MenuItem("S&H")) { insertNodeBetween("S&H"); }
                if (ImGui::MenuItem("Rate")) { insertNodeBetween("Rate"); }
                if (ImGui::MenuItem("Logic")) { insertNodeBetween("Logic"); }
                if (ImGui::MenuItem("Comparator")) { insertNodeBetween("Comparator"); }
                if (ImGui::MenuItem("CV Mixer")) { insertNodeBetween("CV Mixer"); }
                if (ImGui::MenuItem("Sequential Switch")) { insertNodeBetween("Sequential Switch"); }
                ImGui::EndMenu();
            }
            
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Inspector"))
        {
            ImGui::SliderFloat("Window (s)", &inspectorWindowSeconds, 0.5f, 20.0f, "%.1f s");
            ImGui::EndMenu();
        }
        
        // === DEBUG MENU ===
        if (ImGui::BeginMenu("Debug"))
        {
            if (ImGui::MenuItem("Show System Diagnostics", "Ctrl+Shift+D")) 
            {
                showDebugMenu = !showDebugMenu;
            }
            
            
            if (ImGui::MenuItem("Log System State"))
            {
                if (synth != nullptr)
                {
                    juce::Logger::writeToLog("=== SYSTEM DIAGNOSTICS ===");
                    juce::Logger::writeToLog(synth->getSystemDiagnostics());
                }
            }
            
            if (ImGui::MenuItem("Log Selected Module Diagnostics"))
            {
                if (synth != nullptr && selectedLogicalId != 0)
                {
                    juce::Logger::writeToLog("=== MODULE DIAGNOSTICS ===");
                    juce::Logger::writeToLog(synth->getModuleDiagnostics(selectedLogicalId));
                }
            }
            
            ImGui::EndMenu();
        }
        
        // === TRANSPORT CONTROLS ===
        if (synth != nullptr)
        {
            // Get current transport state
            auto transportState = synth->getTransportState();
            
            // Add some spacing before transport controls
            ImGui::Separator();
            ImGui::Spacing();
            
            // Play/Pause button
            if (transportState.isPlaying)
            {
                if (ImGui::Button("Pause"))
                    synth->setPlaying(false);
            }
            else
            {
                if (ImGui::Button("Play"))
                    synth->setPlaying(true);
            }
            
            ImGui::SameLine();
            
            // Stop button (resets position)
            if (ImGui::Button("Stop"))
            {
                synth->setPlaying(false);
                synth->resetTransportPosition();
            }
            
            ImGui::SameLine();
            
            // BPM control (greyed out if controlled by Tempo Clock module)
            float bpm = static_cast<float>(transportState.bpm);
            ImGui::SetNextItemWidth(80.0f);
            
            bool isControlled = transportState.isTempoControlledByModule.load();
            if (isControlled)
                ImGui::BeginDisabled();
                
            if (ImGui::DragFloat("BPM", &bpm, 0.1f, 20.0f, 999.0f, "%.1f"))
                synth->setBPM(static_cast<double>(bpm));
                
            if (isControlled)
            {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Tempo Clock Module Active");
                    ImGui::TextUnformatted("A Tempo Clock node with 'Sync to Host' disabled is controlling the global BPM.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
            
            ImGui::SameLine();
            
            // Position display
            ImGui::Text("%.2f beats", transportState.songPositionBeats);
        }
        
        // === MULTI-MIDI DEVICE ACTIVITY INDICATOR ===
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        
        if (synth != nullptr)
        {
            auto activityState = synth->getMidiActivityState();
            
            if (activityState.deviceNames.empty())
            {
                // No MIDI devices connected
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 100, 100, 255));
                ImGui::Text("MIDI: No Devices");
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::Text("MIDI:");
                ImGui::SameLine();
                
                // Display each device with active channels
                for (const auto& [deviceIndex, deviceName] : activityState.deviceNames)
                {
                    ImGui::SameLine();
                    
                    bool hasActivity = false;
                    if (activityState.deviceChannelActivity.count(deviceIndex) > 0)
                    {
                        const auto& channels = activityState.deviceChannelActivity.at(deviceIndex);
                        for (bool active : channels)
                        {
                            if (active)
                            {
                                hasActivity = true;
                                break;
                            }
                        }
                    }
                    
                    // Abbreviated device name (max 12 chars)
                    juce::String abbrevName = deviceName;
                    if (abbrevName.length() > 12)
                        abbrevName = abbrevName.substring(0, 12) + "...";
                    
                    // Color: bright green if active, dim gray if inactive
                    if (hasActivity)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255));
                    else
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 100, 100, 255));
                    
                    ImGui::Text("[%s]", abbrevName.toRawUTF8());
                    ImGui::PopStyleColor();
                    
                    // Tooltip with full name and active channels
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s", deviceName.toRawUTF8());
                        ImGui::Separator();
                        
                        if (activityState.deviceChannelActivity.count(deviceIndex) > 0)
                        {
                            const auto& channels = activityState.deviceChannelActivity.at(deviceIndex);
                            ImGui::Text("Active Channels:");
                            juce::String activeChannels;
                            for (int ch = 0; ch < 16; ++ch)
                            {
                                if (channels[ch])
                                {
                                    if (activeChannels.isNotEmpty())
                                        activeChannels += ", ";
                                    activeChannels += juce::String(ch + 1);
                                }
                            }
                            if (activeChannels.isEmpty())
                                activeChannels = "None";
                            ImGui::Text("%s", activeChannels.toRawUTF8());
                        }
                        
                        ImGui::EndTooltip();
                    }
                }
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 100, 100, 255));
            ImGui::Text("MIDI: ---");
            ImGui::PopStyleColor();
        }
        // === END OF MULTI-MIDI INDICATOR ===
        
        ImGui::EndMainMenuBar();
    }

    // --- PRESET STATUS OVERLAY ---
    ImGui::SetNextWindowPos(ImVec2(sidebarWidth + padding, menuBarHeight + padding));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("Preset Status", nullptr, 
                 ImGuiWindowFlags_NoDecoration | 
                 ImGuiWindowFlags_NoMove | 
                 ImGuiWindowFlags_NoFocusOnAppearing | 
                 ImGuiWindowFlags_NoNav | 
                 ImGuiWindowFlags_AlwaysAutoResize);

    if (currentPresetFile.isNotEmpty()) {
        ImGui::Text("Preset: %s", currentPresetFile.toRawUTF8());
    } else {
        ImGui::Text("Preset: Unsaved Patch");
    }

    if (isPatchDirty) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Status: EDITED");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: SAVED");
    }

    ImGui::End();
    // --- END OF PRESET STATUS OVERLAY ---

    ImGui::Columns (2, nullptr, true);
    ImGui::SetColumnWidth (0, 260.0f);

    // Zoom removed

    // ADD THIS BLOCK:
    ImGui::Text("Browser");
    
    // Create a scrolling child window to contain the entire browser
    ImGui::BeginChild("BrowserScrollRegion", ImVec2(0, 0), true);
    
    // Helper lambda to recursively draw the directory tree for presets
    std::function<void(const PresetManager::DirectoryNode*)> drawPresetTree = 
        [&](const PresetManager::DirectoryNode* node)
    {
        if (!node || (node->presets.empty() && node->subdirectories.empty())) return;

        // Draw subdirectories first
        for (const auto& subdir : node->subdirectories)
        {
            if (ImGui::TreeNode(subdir->name.toRawUTF8()))
            {
                drawPresetTree(subdir.get());
                ImGui::TreePop();
            }
        }
        
        // Then draw presets in this directory with drag-and-drop support
        for (const auto& preset : node->presets)
        {
            if (m_presetSearchTerm.isEmpty() || preset.name.containsIgnoreCase(m_presetSearchTerm))
            {
                // Draw the selectable item and capture its return value
                bool clicked = ImGui::Selectable(preset.name.toRawUTF8());

                // --- THIS IS THE FIX ---
                // Check if this item is the source of a drag operation
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    // Set the payload type and data (the preset's file path)
                    const juce::String path = preset.file.getFullPathName();
                    const std::string pathStr = path.toStdString();
                    ImGui::SetDragDropPayload("DND_PRESET_PATH", pathStr.c_str(), pathStr.length() + 1);
                    
                    // Provide visual feedback while dragging
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::Text("Merge Preset: %s", preset.name.toRawUTF8());
                    
                    ImGui::EndDragDropSource();
                }
                // If a drag did NOT occur, and the item was clicked, load the preset
                else if (clicked)
                {
                    loadPresetFromFile(preset.file);
                }
                // --- END OF FIX ---
                
                // Tooltip (only shown when hovering, not dragging)
                if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(0) && preset.description.isNotEmpty())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(preset.description.toRawUTF8());
                    if (!preset.tags.isEmpty())
                        ImGui::Text("Tags: %s", preset.tags.joinIntoString(", ").toRawUTF8());
                    ImGui::EndTooltip();
                }
            }
        }
    };

    // Helper to push category colors (used for all module category headers)
    auto pushCategoryColor = [&](ModuleCategory cat) {
        ImU32 color = getImU32ForCategory(cat);
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
        ImGui::PushStyleColor(ImGuiCol_Header, color);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertFloat4ToU32(ImVec4(c.x*1.2f, c.y*1.2f, c.z*1.2f, 1.0f)));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertFloat4ToU32(ImVec4(c.x*1.4f, c.y*1.4f, c.z*1.4f, 1.0f)));
    };

    // === PRESET BROWSER ===
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(218, 165, 32, 255)); // Gold
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(238, 185, 52, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 205, 72, 255));
    bool presetsExpanded = ImGui::CollapsingHeader("Presets");
    ImGui::PopStyleColor(3);
    
    if (presetsExpanded)
    {
        // 1. Path Display (read-only)
        char pathBuf[1024];
        strncpy(pathBuf, m_presetScanPath.getFullPathName().toRawUTF8(), sizeof(pathBuf) - 1);
        ImGui::InputText("##presetpath", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_ReadOnly);

        // 2. "Change Path" Button
        if (ImGui::Button("Change Path##preset"))
        {
            presetPathChooser = std::make_unique<juce::FileChooser>("Select Preset Directory", m_presetScanPath);
            presetPathChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this](const juce::FileChooser& fc)
                {
                    auto dir = fc.getResult();
                    if (dir.isDirectory())
                    {
                        m_presetScanPath = dir;
                        // Save the new path to the properties file
                        if (auto* props = PresetCreatorApplication::getApp().getProperties())
                        {
                            props->setValue("presetScanPath", m_presetScanPath.getFullPathName());
                        }
                    }
                });
        }
        ImGui::SameLine();

        // 3. "Scan" Button
        if (ImGui::Button("Scan##preset"))
        {
            m_presetManager.clearCache();
            m_presetManager.scanDirectory(m_presetScanPath);
        }

        // 4. Search bar for filtering results
        char searchBuf[256] = {};
        strncpy(searchBuf, m_presetSearchTerm.toRawUTF8(), sizeof(searchBuf) - 1);
        if (ImGui::InputText("Search##preset", searchBuf, sizeof(searchBuf)))
            m_presetSearchTerm = juce::String(searchBuf);

        ImGui::Separator();

        // 5. Display hierarchical preset tree
        drawPresetTree(m_presetManager.getRootNode());
    }
    
    // Helper lambda to recursively draw the directory tree for samples
    std::function<void(const SampleManager::DirectoryNode*)> drawSampleTree = 
        [&](const SampleManager::DirectoryNode* node)
    {
        if (!node || (node->samples.empty() && node->subdirectories.empty())) return;

        // Draw subdirectories first
        for (const auto& subdir : node->subdirectories)
        {
            if (ImGui::TreeNode(subdir->name.toRawUTF8()))
            {
                drawSampleTree(subdir.get());
                ImGui::TreePop();
            }
        }
        
        // Then draw samples in this directory with drag-and-drop support
        for (const auto& sample : node->samples)
        {
            if (m_sampleSearchTerm.isEmpty() || sample.name.containsIgnoreCase(m_sampleSearchTerm))
            {
                // --- THIS IS THE HEROIC FIX ---

                // A. Draw the selectable item and capture its return value (which is true on mouse release).
                bool clicked = ImGui::Selectable(sample.name.toRawUTF8());

                // B. Check if this item is the source of a drag operation. This takes priority.
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    // Set the payload (the data we are transferring is the sample's file path).
                    const juce::String path = sample.file.getFullPathName();
                    const std::string pathStr = path.toStdString();
                    ImGui::SetDragDropPayload("DND_SAMPLE_PATH", pathStr.c_str(), pathStr.length() + 1);
                    
                    // Provide visual feedback during the drag.
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::Text("Dragging: %s", sample.name.toRawUTF8());
                    
                    ImGui::EndDragDropSource();
                }
                // C. If a drag did NOT occur, and the item was clicked (mouse released on it), then create the node.
                else if (clicked)
                {
                    if (synth != nullptr)
                    {
                        auto newNodeId = synth->addModule("sample loader");
                        auto newLogicalId = synth->getLogicalIdForNode(newNodeId);
                        pendingNodeScreenPositions[(int)newLogicalId] = ImGui::GetMousePos();
                        if (auto* sampleLoader = dynamic_cast<SampleLoaderModuleProcessor*>(synth->getModuleForLogical(newLogicalId)))
                        {
                            sampleLoader->loadSample(sample.file);
                        }
                        snapshotAfterEditor = true;
                    }
                }
                
                // --- END OF FIX ---

                // (Existing tooltip for sample info remains the same)
                if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(0))
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Duration: %.2f s", sample.durationSeconds);
                    ImGui::Text("Rate: %d Hz", sample.sampleRate);
                    ImGui::EndTooltip();
                }
            }
        }
    };

    // === SAMPLE BROWSER ===
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 180, 180, 255)); // Cyan
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(20, 200, 200, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(40, 220, 220, 255));
    bool samplesExpanded = ImGui::CollapsingHeader("Samples");
    ImGui::PopStyleColor(3);
    
    if (samplesExpanded)
    {
        // 1. Path Display (read-only)
        char pathBuf[1024];
        strncpy(pathBuf, m_sampleScanPath.getFullPathName().toRawUTF8(), sizeof(pathBuf) - 1);
        ImGui::InputText("##samplepath", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_ReadOnly);

        // 2. "Change Path" Button
        if (ImGui::Button("Change Path##sample"))
        {
            samplePathChooser = std::make_unique<juce::FileChooser>("Select Sample Directory", m_sampleScanPath);
            samplePathChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this](const juce::FileChooser& fc)
                {
                    auto dir = fc.getResult();
                    if (dir.isDirectory())
                    {
                        m_sampleScanPath = dir;
                        // Save the new path to the properties file
                        if (auto* props = PresetCreatorApplication::getApp().getProperties())
                        {
                            props->setValue("sampleScanPath", m_sampleScanPath.getFullPathName());
                        }
                    }
                });
        }
        ImGui::SameLine();

        // 3. "Scan" Button
        if (ImGui::Button("Scan##sample"))
        {
            m_sampleManager.clearCache();
            m_sampleManager.scanDirectory(m_sampleScanPath);
        }

        // 4. Search bar for filtering results
        char searchBuf[256] = {};
        strncpy(searchBuf, m_sampleSearchTerm.toRawUTF8(), sizeof(searchBuf) - 1);
        if (ImGui::InputText("Search##sample", searchBuf, sizeof(searchBuf)))
            m_sampleSearchTerm = juce::String(searchBuf);

        ImGui::Separator();

        // 5. Display hierarchical sample tree
        drawSampleTree(m_sampleManager.getRootNode());
    }
    
    ImGui::Separator();
    
    // === MIDI BROWSER ===
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(180, 120, 255, 255)); // Purple
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(200, 140, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(220, 160, 255, 255));
    bool midiExpanded = ImGui::CollapsingHeader("MIDI Files");
    ImGui::PopStyleColor(3);
    
    if (midiExpanded)
    {
        // 1. Path Display (read-only)
        char pathBuf[1024];
        strncpy(pathBuf, m_midiScanPath.getFullPathName().toRawUTF8(), sizeof(pathBuf) - 1);
        ImGui::InputText("##midipath", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_ReadOnly);

        // 2. "Change Path" Button
        if (ImGui::Button("Change Path##midi"))
        {
            midiPathChooser = std::make_unique<juce::FileChooser>("Select MIDI Directory", m_midiScanPath);
            midiPathChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this](const juce::FileChooser& fc)
                {
                    auto dir = fc.getResult();
                    if (dir.isDirectory())
                    {
                        m_midiScanPath = dir;
                        // Save the new path to the properties file
                        if (auto* props = PresetCreatorApplication::getApp().getProperties())
                        {
                            props->setValue("midiScanPath", m_midiScanPath.getFullPathName());
                        }
                    }
                });
        }
        ImGui::SameLine();

        // 3. "Scan" Button
        if (ImGui::Button("Scan##midi"))
        {
            m_midiManager.clearCache();
            m_midiManager.scanDirectory(m_midiScanPath);
        }

        // 4. Search bar for filtering results
        char searchBuf[256] = {};
        strncpy(searchBuf, m_midiSearchTerm.toRawUTF8(), sizeof(searchBuf) - 1);
        if (ImGui::InputText("Search##midi", searchBuf, sizeof(searchBuf)))
            m_midiSearchTerm = juce::String(searchBuf);

        ImGui::Separator();
        
        // 5. Display hierarchical MIDI tree
        std::function<void(const MidiManager::DirectoryNode*)> drawMidiTree = 
            [&](const MidiManager::DirectoryNode* node)
        {
            if (!node || (node->midiFiles.empty() && node->subdirectories.empty())) return;

            // Draw subdirectories first
            for (const auto& subdir : node->subdirectories)
            {
                if (ImGui::TreeNode(subdir->name.toRawUTF8()))
                {
                    drawMidiTree(subdir.get());
                    ImGui::TreePop();
                }
            }
            
            // Then draw MIDI files in this directory with drag-and-drop support
            for (const auto& midi : node->midiFiles)
            {
                if (m_midiSearchTerm.isEmpty() || midi.name.containsIgnoreCase(m_midiSearchTerm))
                {
                    // Draw the selectable item and capture its return value
                    bool clicked = ImGui::Selectable(midi.name.toRawUTF8());

                    // Check if this item is the source of a drag operation
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        // Set the payload (the MIDI file path)
                        const juce::String path = midi.file.getFullPathName();
                        const std::string pathStr = path.toStdString();
                        ImGui::SetDragDropPayload("DND_MIDI_PATH", pathStr.c_str(), pathStr.length() + 1);
                        
                        // Provide visual feedback during the drag
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImGui::Text("Dragging: %s", midi.name.toRawUTF8());
                        
                        ImGui::EndDragDropSource();
                    }
                    // If a drag did NOT occur, and the item was clicked, create a new MIDI Player node
                    else if (clicked)
                    {
                        if (synth != nullptr)
                        {
                            auto newNodeId = synth->addModule("midi player");
                            auto newLogicalId = synth->getLogicalIdForNode(newNodeId);
                            pendingNodeScreenPositions[(int)newLogicalId] = ImGui::GetMousePos();
                            
                            // Load the MIDI file into the new player
                            if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(synth->getModuleForLogical(newLogicalId)))
                            {
                                midiPlayer->loadMIDIFile(midi.file);
                            }
                            
                            snapshotAfterEditor = true;
                        }
                    }
                    
                    // Tooltip for MIDI info (only shown when hovering, not dragging)
                    if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(0))
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("MIDI File: %s", midi.file.getFileName().toRawUTF8());
                        ImGui::EndTooltip();
                    }
                }
            }
        };
        
        drawMidiTree(m_midiManager.getRootNode());
    }
    
    ImGui::Separator();
    
    // === MODULE BROWSER ===
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(80, 80, 80, 255)); // Neutral Grey
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(100, 100, 100, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(120, 120, 120, 255));
    bool modulesExpanded = ImGui::CollapsingHeader("Modules", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    
    if (modulesExpanded)
    {
    
    auto addModuleButton = [this](const char* label, const char* type)
    {
        if (ImGui::Selectable(label, false))
        {
            if (synth != nullptr)
            {
                auto nodeId = synth->addModule(type);
                const ImVec2 mouse = ImGui::GetMousePos();
                // queue screen-space placement after node is drawn to avoid assertions
                const int logicalId = (int) synth->getLogicalIdForNode (nodeId);
                pendingNodeScreenPositions[logicalId] = mouse;
                // Defer snapshot until after EndNodeEditor so the node exists in this frame
                snapshotAfterEditor = true;
            }
        }
        
        // --- FIX: Show tooltip with module description on hover ---
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            
            // Find the description in our list using the module's internal 'type'
            bool found = false;
            for (const auto& pair : getModuleDescriptions())
            {
                if (pair.first.equalsIgnoreCase(type))
                {
                    // If found, display it
                    ImGui::TextUnformatted(pair.second);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                // Fallback text if a description is missing
                ImGui::TextUnformatted("No description available.");
            }
            
            ImGui::EndTooltip();
        }
    };
    
    // ═══════════════════════════════════════════════════════════════════════════════
    // MODULE NAMING CONVENTION:
    // ─────────────────────────────────────────────────────────────────────────────── 
    // ALL module type names MUST follow this strict naming convention:
    //   • Use ONLY lowercase letters (a-z)
    //   • Use ONLY numbers (0-9) where appropriate
    //   • Replace ALL spaces with underscores (_)
    //   • NO capital letters allowed
    //   • NO hyphens or other special characters
    //
    // Examples:
    //   ✓ CORRECT:   "midi_player", "sample_loader", "graphic_eq", "vco"
    //   ✗ INCORRECT: "MIDI Player", "Sample Loader", "Graphic EQ", "VCO"
    //
    // This ensures consistent module identification across the system.
    // ═══════════════════════════════════════════════════════════════════════════════
    
    pushCategoryColor(ModuleCategory::Source);
    bool sourcesExpanded = ImGui::CollapsingHeader("Sources", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (sourcesExpanded) {
    addModuleButton("Audio Input", "audio_input");
    addModuleButton("VCO", "vco");
    addModuleButton("Polyphonic VCO", "polyvco");
    addModuleButton("Noise", "noise");
        addModuleButton("Sequencer", "sequencer");
        addModuleButton("Multi Sequencer", "multi_sequencer");
        addModuleButton("Stroke Sequencer", "stroke_sequencer");
        addModuleButton("Value", "value");
        addModuleButton("Sample Loader", "sample_loader");
    }
    
    pushCategoryColor(ModuleCategory::MIDI);
    bool midiFamilyExpanded = ImGui::CollapsingHeader("MIDI Family", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (midiFamilyExpanded) {
        addModuleButton("MIDI CV", "midi_cv");
        addModuleButton("MIDI Player", "midi_player");
        ImGui::Separator();
        addModuleButton("MIDI Faders", "midi_faders");
        addModuleButton("MIDI Knobs", "midi_knobs");
        addModuleButton("MIDI Buttons", "midi_buttons");
        addModuleButton("MIDI Jog Wheel", "midi_jog_wheel");
        addModuleButton("MIDI Pads", "midi_pads");
        ImGui::Separator();
        addModuleButton("MIDI Logger", "midi_logger");
        ImGui::Separator();
    }
    
    pushCategoryColor(ModuleCategory::Source);
    bool ttsFamilyExpanded = ImGui::CollapsingHeader("TTS Family", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (ttsFamilyExpanded) {

        addModuleButton("TTS Performer", "tts_performer");
        addModuleButton("Vocal Tract Filter", "vocal_tract_filter");
    }
    
    pushCategoryColor(ModuleCategory::Physics);
    bool physicsFamilyExpanded = ImGui::CollapsingHeader("Physics Family", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (physicsFamilyExpanded) {
        addModuleButton("Physics", "physics");
        addModuleButton("Animation", "animation");
    }
    
    pushCategoryColor(ModuleCategory::Effect);
    bool effectsExpanded = ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (effectsExpanded) {
        addModuleButton("VCF", "vcf");
        // addModuleButton("Vocal Tract Filter", "vocal_tract_filter");
        addModuleButton("Delay", "delay");
        addModuleButton("Reverb", "reverb");
        addModuleButton("Chorus", "chorus");
        addModuleButton("Phaser", "phaser");
        addModuleButton("Compressor", "compressor");
        addModuleButton("Recorder", "recorder");
        addModuleButton("Limiter", "limiter");
        addModuleButton("Noise Gate", "gate");
        addModuleButton("Drive", "drive");
        addModuleButton("Graphic EQ", "graphic_eq");
        addModuleButton("Time/Pitch Shifter", "timepitch");
        addModuleButton("Waveshaper", "waveshaper");
        addModuleButton("8-Band Shaper", "8bandshaper");
        addModuleButton("Granulator", "granulator");
        addModuleButton("Harmonic Shaper", "harmonic_shaper");
    }
    
    pushCategoryColor(ModuleCategory::Modulator);
    bool modulatorsExpanded = ImGui::CollapsingHeader("Modulators", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (modulatorsExpanded) {
        addModuleButton("LFO", "lfo");
        addModuleButton("ADSR", "adsr");
        addModuleButton("Random", "random");
    addModuleButton("S&H", "s_and_h");
            addModuleButton("Function Generator", "function_generator");
        addModuleButton("Shaping Oscillator", "shaping_oscillator");

    }
    
    pushCategoryColor(ModuleCategory::Utility);
    bool utilitiesExpanded = ImGui::CollapsingHeader("Utilities & Logic", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (utilitiesExpanded) {
        addModuleButton("VCA", "vca");
        addModuleButton("Mixer", "mixer");
        addModuleButton("CV Mixer", "cv_mixer");
        addModuleButton("Track Mixer", "track_mixer");
    addModuleButton("Attenuverter", "attenuverter");
        addModuleButton("Lag Processor", "lag_processor");
        addModuleButton("De-Crackle", "de_crackle");
        addModuleButton("Math", "math");
        addModuleButton("Map Range", "map_range");
        addModuleButton("Quantizer", "quantizer");
        addModuleButton("Rate", "rate");
        addModuleButton("Comparator", "comparator");
        addModuleButton("Logic", "logic");
        addModuleButton("Clock Divider", "clock_divider");
        addModuleButton("Sequential Switch", "sequential_switch");
        addModuleButton("Tempo Clock", "tempo_clock");
        addModuleButton("Snapshot Sequencer", "snapshot_sequencer");
        addModuleButton("Best Practice", "best_practice");
    }
    
    pushCategoryColor(ModuleCategory::Analysis);
    bool analysisExpanded = ImGui::CollapsingHeader("Analysis", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (analysisExpanded) {
        addModuleButton("Scope", "scope");
        addModuleButton("Debug", "debug");
        addModuleButton("Input Debug", "input_debug");
        addModuleButton("Frequency Graph", "frequency_graph");
    }
    
    } // End of Modules collapsing header
    
    // VST Plugins section
    pushCategoryColor(ModuleCategory::Plugin);
    bool pluginsExpanded = ImGui::CollapsingHeader("Plugins", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    if (pluginsExpanded) {
        addPluginModules();
    }

    // End the scrolling region
    ImGui::EndChild();

    ImGui::NextColumn();

    // --- DEFINITIVE FIX FOR PRESET DRAG-AND-DROP WITH VISUAL FEEDBACK ---
    // Step 1: Define canvas dimensions first (needed for the drop target)
    const ImU32 GRID_COLOR = IM_COL32(50, 50, 50, 255);
    const ImU32 GRID_ORIGIN_COLOR = IM_COL32(80, 80, 80, 255);
    const float GRID_SIZE = 64.0f;
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    // Step 2: Create a full-canvas invisible button to act as our drop area
    ImGui::SetCursorScreenPos(canvas_p0);
    ImGui::InvisibleButton("##canvas_drop_target", canvas_sz);

    // Step 3: Make this area a drop target with visual feedback
    if (ImGui::BeginDragDropTarget())
    {
        // Check if a preset payload is being hovered over the canvas
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_PRESET_PATH", ImGuiDragDropFlags_AcceptBeforeDelivery))
        {
            // Draw a semi-transparent overlay to show the canvas is a valid drop zone
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            drawList->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(218, 165, 32, 80)); // Preset Gold color
            
            // Check if the mouse button was released to complete the drop
            if (payload->IsDelivery())
            {
                const char* path = (const char*)payload->Data;
                ImVec2 dropPos = ImGui::GetMousePos(); // Get the exact drop position
                mergePresetFromFile(juce::File(path), dropPos);
            }
        }
        ImGui::EndDragDropTarget();
    }
    // --- END OF DEFINITIVE FIX ---

    // Reset cursor position for subsequent drawing
    ImGui::SetCursorScreenPos(canvas_p0);

    // <<< ADD THIS ENTIRE BLOCK TO CACHE CONNECTION STATUS >>>
    std::unordered_set<int> connectedInputAttrs;
    std::unordered_set<int> connectedOutputAttrs;
    if (synth != nullptr)
    {
        for (const auto& c : synth->getConnectionsInfo())
        {
            int srcAttr = encodePinId({c.srcLogicalId, c.srcChan, false});
            connectedOutputAttrs.insert(srcAttr);

            int dstAttr = c.dstIsOutput ? 
                encodePinId({0, c.dstChan, true}) : 
                encodePinId({c.dstLogicalId, c.dstChan, true});
            connectedInputAttrs.insert(dstAttr);
        }
    }
    // <<< END OF BLOCK >>>

    // <<< ADD THIS BLOCK TO DEFINE COLORS >>>
    const ImU32 colPin = IM_COL32(150, 150, 150, 255); // Grey for disconnected
    const ImU32 colPinConnected = IM_COL32(120, 255, 120, 255); // Green for connected
    // <<< END OF BLOCK >>>

    // Pre-register is no longer needed - stateless encoding generates IDs on-the-fly
    // (Removed the old pre-registration loop)

    // --- BACKGROUND GRID AND COORDINATE DISPLAY ---
    // (Canvas dimensions already defined above in the drop target code)
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 panning = ImNodes::EditorContextGetPanning();

    // Draw grid lines
    for (float x = fmodf(panning.x, GRID_SIZE); x < canvas_sz.x; x += GRID_SIZE)
        draw_list->AddLine(ImVec2(canvas_p0.x + x, canvas_p0.y), ImVec2(canvas_p0.x + x, canvas_p0.y + canvas_sz.y), GRID_COLOR);
    for (float y = fmodf(panning.y, GRID_SIZE); y < canvas_sz.y; y += GRID_SIZE)
        draw_list->AddLine(ImVec2(canvas_p0.x, canvas_p0.y + y), ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + y), GRID_COLOR);

    // Draw thicker lines for the origin (0,0)
    ImVec2 origin_on_screen = ImVec2(canvas_p0.x + panning.x, canvas_p0.y + panning.y);
    draw_list->AddLine(ImVec2(origin_on_screen.x, canvas_p0.y), ImVec2(origin_on_screen.x, canvas_p1.y), GRID_ORIGIN_COLOR, 2.0f);
    draw_list->AddLine(ImVec2(canvas_p0.x, origin_on_screen.y), ImVec2(canvas_p1.x, origin_on_screen.y), GRID_ORIGIN_COLOR, 2.0f);

    // Draw scale markers every 400 grid units as a grid (not a cross)
    const float SCALE_INTERVAL = 400.0f;
    const ImU32 SCALE_TEXT_COLOR = IM_COL32(150, 150, 150, 80); // Reduced opacity
    ImDrawList* fg_draw_list = ImGui::GetForegroundDrawList();
    
    // X-axis scale markers - always at the bottom edge
    float gridLeft = -panning.x;
    float gridRight = canvas_sz.x - panning.x;
    int startX = (int)std::floor(gridLeft / SCALE_INTERVAL);
    int endX = (int)std::ceil(gridRight / SCALE_INTERVAL);
    
    for (int i = startX; i <= endX; ++i)
    {
        float gridX = i * SCALE_INTERVAL;
        float screenX = canvas_p0.x + panning.x + gridX;
        
        // Only draw if visible on screen
        if (screenX >= canvas_p0.x && screenX <= canvas_p1.x)
        {
            char label[16];
            snprintf(label, sizeof(label), "%.0f", gridX);
            // Always draw at bottom edge
            fg_draw_list->AddText(ImVec2(screenX + 2, canvas_p1.y - 45), SCALE_TEXT_COLOR, label);
        }
    }
    
    // Y-axis scale markers - always at the left edge
    float gridTop = -panning.y;
    float gridBottom = canvas_sz.y - panning.y;
    int startY = (int)std::floor(gridTop / SCALE_INTERVAL);
    int endY = (int)std::ceil(gridBottom / SCALE_INTERVAL);
    
    for (int i = startY; i <= endY; ++i)
    {
        float gridY = i * SCALE_INTERVAL;
        float screenY = canvas_p0.y + panning.y + gridY;
        
        // Only draw if visible on screen
        if (screenY >= canvas_p0.y && screenY <= canvas_p1.y)
        {
            char label[16];
            snprintf(label, sizeof(label), "%.0f", gridY);
            // Always draw at left edge
            fg_draw_list->AddText(ImVec2(canvas_p0.x + 5, screenY + 2), SCALE_TEXT_COLOR, label);
        }
    }

    // Mouse coordinate display overlay (bottom-left)
    ImVec2 mouseScreenPos = ImGui::GetMousePos();
    ImVec2 mouseGridPos = ImVec2(mouseScreenPos.x - canvas_p0.x - panning.x, mouseScreenPos.y - canvas_p0.y - panning.y);
    char posStr[32];
    snprintf(posStr, sizeof(posStr), "%.0f, %.0f", mouseGridPos.x, mouseGridPos.y);
    // Use the foreground draw list to ensure text is on top of everything
    // Position at bottom-left: canvas_p1.y is bottom edge, subtract text height plus padding
    ImGui::GetForegroundDrawList()->AddText(ImVec2(canvas_p0.x + 10, canvas_p1.y - 25), IM_COL32(200, 200, 200, 150), posStr);
    // --- END OF BACKGROUND GRID AND COORDINATE DISPLAY ---

    // Node canvas bound to the underlying model if available
    ImNodes::BeginNodeEditor();
    // Begin the editor

    // +++ ADD THIS LINE AT THE START OF THE RENDER LOOP +++
    attrPositions.clear(); // Clear the cache at the beginning of each frame.
    // Rebuild mod attribute mapping from currently drawn nodes only
    // modAttrToParam.clear(); // TODO: Remove when fully migrated
    // Track which attribute IDs were actually registered this frame
    std::unordered_set<int> availableAttrs;
    // Track duplicates to diagnose disappearing pins
    std::unordered_set<int> seenAttrs;
    auto linkIdOf = [this] (int srcAttr, int dstAttr) -> int
    {
        return getLinkId(srcAttr, dstAttr);
    };

    if (synth != nullptr)
    {
        // Apply any pending UI state restore (first frame after load)
        if (uiPending.isValid())
        {
            // Cache target positions to ensure they stick even if nodes are created later this frame
            auto nodes = uiPending;
for (int i = 0; i < nodes.getNumChildren(); ++i)
            {
                auto n = nodes.getChild(i);
if (! n.hasType("node")) continue;
                const int nid = (int) n.getProperty("id", 0);
                const float x = (float) n.getProperty("x", 0.0f);
const float y = (float) n.getProperty("y", 0.0f);
                if (!(x == 0.0f && y == 0.0f))
                    pendingNodePositions[nid] = ImVec2(x, y);
}
            uiPending = {};
}

        // Draw module nodes (exactly once per logical module)
        // Graph is now always in consistent state since we rebuild at frame start
        std::unordered_set<int> drawnNodes;
        for (const auto& mod : synth->getModulesInfo())
        {
            const juce::uint32 lid = mod.first;
const juce::String& type = mod.second;

            // Color-code modules by category (base colors)
            const auto moduleCategory = getModuleCategory(type);
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, getImU32ForCategory(moduleCategory));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, getImU32ForCategory(moduleCategory, true));
            ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, getImU32ForCategory(moduleCategory, true));

            // Highlight nodes participating in the hovered link (overrides category color)
            const bool isHoveredSource = (hoveredLinkSrcId != 0 && hoveredLinkSrcId == (juce::uint32) lid);
            const bool isHoveredDest   = (hoveredLinkDstId != 0 && hoveredLinkDstId == (juce::uint32) lid);
            if (isHoveredSource || isHoveredDest)
                ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(255, 220, 0, 255));

            // Visual feedback for muted nodes (overrides category color and hover)
            const bool isMuted = mutedNodeStates.count(lid) > 0;
            if (isMuted) {
                ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, ImVec2(8, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(80, 80, 80, 255));
            }

            ImNodes::BeginNode ((int) lid);
            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted (type.toRawUTF8());
            ImNodes::EndNodeTitleBar();

            // Constrain node content width for compact layout and predictable label placement
            const float nodeContentWidth = 240.0f;

            // Inline parameter controls per module type
            if (synth != nullptr)
            {
if (auto* mp = synth->getModuleForLogical (lid))
{
    ImGui::PushID ((int) lid);

    // This new lambda function checks if a parameter is being modulated
    auto isParamModulated = [&](const juce::String& paramId) -> bool {
        if (!synth) return false;
        if (auto* mp = synth->getModuleForLogical(lid))
        {
            int busIdx = -1, chInBus = -1;
            // Use the new standardized routing API on the module itself
            if (!mp->getParamRouting(paramId, busIdx, chInBus)) 
                return false;

            // Calculate the absolute channel index that the graph uses for this bus/channel pair
            const int absoluteChannelIndex = mp->getChannelIndexInProcessBlockBuffer(true, busIdx, chInBus);
            if (absoluteChannelIndex < 0) return false;
            
            // Scan the simple graph connections for a match
            for (const auto& c : synth->getConnectionsInfo())
            {
                if (c.dstLogicalId == lid && c.dstChan == absoluteChannelIndex)
                    return true;
            }
        }
        return false;
    };

    // Helper to read a live, modulated value if available (respects _mod alias)
    auto getLiveValueOr = [&](const juce::String& paramId, float fallback) -> float
    {
        if (!synth) return fallback;
        if (auto* mp = synth->getModuleForLogical(lid))
            return mp->getLiveParamValueFor(paramId + "_mod", paramId + "_live", fallback);
        return fallback;
    };

    // Create a new function that calls pushSnapshot
    auto onModificationEnded = [&](){ this->pushSnapshot(); };

    // --- SPECIAL RENDERING FOR SAMPLE LOADER ---
    if (auto* sampleLoader = dynamic_cast<SampleLoaderModuleProcessor*>(mp))
    {
        // First, draw the standard parameters (buttons, sliders, etc.)
        // We pass a modified onModificationEnded to avoid creating undo states while dragging.
        sampleLoader->drawParametersInNode(nodeContentWidth, isParamModulated, onModificationEnded);

        // Now, handle the spectrogram texture and drawing
        juce::OpenGLTexture* texturePtr = nullptr;
        if (auto it = sampleLoaderTextureIds.find((int)lid); it != sampleLoaderTextureIds.end())
            texturePtr = it->second.get();

        juce::Image spectrogram = sampleLoader->getSpectrogramImage();
        if (spectrogram.isValid())
        {
            if (texturePtr == nullptr)
            {
                auto tex = std::make_unique<juce::OpenGLTexture>();
                texturePtr = tex.get();
                sampleLoaderTextureIds[(int)lid] = std::move(tex);
            }
            // Upload or update texture from JUCE image (handles format & parameters internally)
            texturePtr->loadImage(spectrogram);

            ImGui::Image((void*)(intptr_t) texturePtr->getTextureID(), ImVec2(nodeContentWidth, 100.0f));

            // Drag state is tracked per Sample Loader node to avoid cross-node interference
            static std::unordered_map<int,int> draggedHandleByNode; // lid -> -1,0,1
            int& draggedHandle = draggedHandleByNode[(int) lid];
            if (draggedHandle != 0 && draggedHandle != 1) draggedHandle = -1;
            ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
            ImGui::InvisibleButton("##spectrogram_interaction", ImVec2(nodeContentWidth, 100.0f));

            auto* drawList = ImGui::GetWindowDrawList();
            const ImVec2 rectMin = ImGui::GetItemRectMin();
            const ImVec2 rectMax = ImGui::GetItemRectMax();

            float startNorm = sampleLoader->getAPVTS().getRawParameterValue("rangeStart")->load();
            float endNorm = sampleLoader->getAPVTS().getRawParameterValue("rangeEnd")->load();

            // Use live telemetry values when modulated
            startNorm = sampleLoader->getLiveParamValueFor("rangeStart_mod", "rangeStart_live", startNorm);
            endNorm = sampleLoader->getLiveParamValueFor("rangeEnd_mod", "rangeEnd_live", endNorm);

            // Visual guard even when modulated
            const float kMinGap = 0.001f;
            startNorm = juce::jlimit(0.0f, 1.0f, startNorm);
            endNorm   = juce::jlimit(0.0f, 1.0f, endNorm);
            if (startNorm >= endNorm)
            {
                if (startNorm <= 1.0f - kMinGap)
                    endNorm = juce::jmin(1.0f, startNorm + kMinGap);
                else
                    startNorm = juce::jmax(0.0f, endNorm - kMinGap);
            }

            // --- FIX FOR BUG 1: Separate modulation checks for each handle ---
            bool startIsModulated = isParamModulated("rangeStart_mod");
            bool endIsModulated = isParamModulated("rangeEnd_mod");

            const bool itemHovered = ImGui::IsItemHovered();
            const bool itemActive  = ImGui::IsItemActive();
            if (itemHovered)
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                float startHandleX = rectMin.x + startNorm * nodeContentWidth;
                float endHandleX = rectMin.x + endNorm * nodeContentWidth;

                bool canDragStart = !startIsModulated && (std::abs(mousePos.x - startHandleX) < 5);
                bool canDragEnd = !endIsModulated && (std::abs(mousePos.x - endHandleX) < 5);

                if (draggedHandle == -1 && (canDragStart || canDragEnd))
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }

                if (ImGui::IsItemClicked())
                {
                    // Only allow dragging to start if the corresponding handle is not modulated
                    if (canDragStart && canDragEnd)
                        draggedHandle = (std::abs(mousePos.x - startHandleX) < std::abs(mousePos.x - endHandleX)) ? 0 : 1;
                    else if (canDragStart)
                        draggedHandle = 0;
                    else if (canDragEnd)
                        draggedHandle = 1;
                }
            }

            if (itemActive && ImGui::IsMouseReleased(0))
            {
                if (draggedHandle != -1) onModificationEnded();
                draggedHandle = -1;
            }

            // Handle the drag update, checking the specific modulation flag for the active handle
            if (itemActive && draggedHandle != -1 && ImGui::IsMouseDragging(0))
            {
                float newNormX = juce::jlimit(0.0f, 1.0f, (ImGui::GetMousePos().x - rectMin.x) / nodeContentWidth);
                if (draggedHandle == 0 && !startIsModulated)
                {
                    // Guard: start cannot be >= end
                    startNorm = juce::jmin(newNormX, endNorm - 0.001f);
                    sampleLoader->getAPVTS().getParameter("rangeStart")->setValueNotifyingHost(startNorm);
                }
                else if (draggedHandle == 1 && !endIsModulated)
                {
                    // Guard: end cannot be <= start
                    endNorm = juce::jmax(newNormX, startNorm + 0.001f);
                    sampleLoader->getAPVTS().getParameter("rangeEnd")->setValueNotifyingHost(endNorm);
                }
            }

            float startX = rectMin.x + startNorm * nodeContentWidth;
            float endX = rectMin.x + endNorm * nodeContentWidth;
            drawList->AddRectFilled(rectMin, ImVec2(startX, rectMax.y), IM_COL32(0, 0, 0, 120));
            drawList->AddRectFilled(ImVec2(endX, rectMin.y), rectMax, IM_COL32(0, 0, 0, 120));
            drawList->AddLine(ImVec2(startX, rectMin.y), ImVec2(startX, rectMax.y), IM_COL32(255, 255, 0, 255), 3.0f);
            drawList->AddLine(ImVec2(endX, rectMin.y), ImVec2(endX, rectMax.y), IM_COL32(255, 255, 0, 255), 3.0f);
        }
    }
    // --- SPECIAL RENDERING FOR AUDIO INPUT (MULTI-CHANNEL) ---
    else if (auto* audioIn = dynamic_cast<AudioInputModuleProcessor*>(mp))
    {
        auto& apvts = audioIn->getAPVTS();

        // --- Device Selectors ---
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);
        
        // Input Device
        juce::StringArray availableInputDevices;
        if (auto* deviceType = deviceManager.getAvailableDeviceTypes().getFirst()) {
            availableInputDevices = deviceType->getDeviceNames(true);
        }
        std::vector<const char*> inputDeviceItems;
        for (const auto& name : availableInputDevices) inputDeviceItems.push_back(name.toRawUTF8());
        int currentInputDeviceIndex = availableInputDevices.indexOf(setup.inputDeviceName);
        if (currentInputDeviceIndex < 0) currentInputDeviceIndex = 0;

        ImGui::PushItemWidth(nodeContentWidth);
        if (ImGui::Combo("Input Device", &currentInputDeviceIndex, inputDeviceItems.data(), (int)inputDeviceItems.size())) {
            if (currentInputDeviceIndex < availableInputDevices.size()) {
                setup.inputDeviceName = availableInputDevices[currentInputDeviceIndex];
                deviceManager.setAudioDeviceSetup(setup, true);
                onModificationEnded();
            }
        }

        // Output Device
        juce::StringArray availableOutputDevices;
        if (auto* deviceType = deviceManager.getAvailableDeviceTypes().getFirst()) {
            availableOutputDevices = deviceType->getDeviceNames(false);
        }
        std::vector<const char*> outputDeviceItems;
        for (const auto& name : availableOutputDevices) outputDeviceItems.push_back(name.toRawUTF8());
        int currentOutputDeviceIndex = availableOutputDevices.indexOf(setup.outputDeviceName);
        if (currentOutputDeviceIndex < 0) currentOutputDeviceIndex = 0;
        
        if (ImGui::Combo("Output Device", &currentOutputDeviceIndex, outputDeviceItems.data(), (int)outputDeviceItems.size())) {
            if (currentOutputDeviceIndex < availableOutputDevices.size()) {
                setup.outputDeviceName = availableOutputDevices[currentOutputDeviceIndex];
                deviceManager.setAudioDeviceSetup(setup, true);
                onModificationEnded();
            }
        }
        
        // --- Channel Count ---
        auto* numChannelsParam = static_cast<juce::AudioParameterInt*>(apvts.getParameter("numChannels"));
        int numChannels = numChannelsParam->get();
        if (ImGui::SliderInt("Channels", &numChannels, 1, AudioInputModuleProcessor::MAX_CHANNELS)) {
            *numChannelsParam = numChannels;
            onModificationEnded();
        }
        
        // --- Threshold Sliders ---
        auto* gateThreshParam = static_cast<juce::AudioParameterFloat*>(apvts.getParameter("gateThreshold"));
        float gateThresh = gateThreshParam->get();
        if (ImGui::SliderFloat("Gate Threshold", &gateThresh, 0.0f, 1.0f, "%.3f")) {
            *gateThreshParam = gateThresh;
            onModificationEnded();
        }
        
        auto* trigThreshParam = static_cast<juce::AudioParameterFloat*>(apvts.getParameter("triggerThreshold"));
        float trigThresh = trigThreshParam->get();
        if (ImGui::SliderFloat("Trigger Threshold", &trigThresh, 0.0f, 1.0f, "%.3f")) {
            *trigThreshParam = trigThresh;
            onModificationEnded();
        }
        
        ImGui::PopItemWidth();

        // --- Dynamic Channel Selectors & VU Meters ---
        auto hardwareChannels = deviceManager.getCurrentAudioDevice() ? deviceManager.getCurrentAudioDevice()->getInputChannelNames() : juce::StringArray{};
        if (!hardwareChannels.isEmpty())
        {
            std::vector<const char*> hwChannelItems;
            for (const auto& name : hardwareChannels) hwChannelItems.push_back(name.toRawUTF8());
            
            for (int i = 0; i < numChannels; ++i) {
                auto* mappingParam = static_cast<juce::AudioParameterInt*>(apvts.getParameter("channelMap" + juce::String(i)));
                int selectedHwChannel = mappingParam->get();
                selectedHwChannel = juce::jlimit(0, (int)hwChannelItems.size() - 1, selectedHwChannel);

                ImGui::PushID(i);
                ImGui::PushItemWidth(nodeContentWidth * 0.6f);
                if (ImGui::Combo(("Input for Out " + juce::String(i + 1)).toRawUTF8(), &selectedHwChannel, hwChannelItems.data(), (int)hwChannelItems.size())) {
                    *mappingParam = selectedHwChannel;
                    std::vector<int> newMapping(numChannels);
                    for (int j = 0; j < numChannels; ++j) {
                        auto* p = static_cast<juce::AudioParameterInt*>(apvts.getParameter("channelMap" + juce::String(j)));
                        newMapping[j] = p->get();
                    }
                    synth->setAudioInputChannelMapping(synth->getNodeIdForLogical(lid), newMapping);
                    onModificationEnded();
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                
                // --- VU Meter with Threshold Lines ---
                float level = (i < (int)audioIn->channelLevels.size() && audioIn->channelLevels[i]) ? audioIn->channelLevels[i]->load() : 0.0f;
                ImVec2 meterSize(nodeContentWidth * 0.38f, ImGui::GetTextLineHeightWithSpacing() * 0.8f);
                ImGui::ProgressBar(level, meterSize, "");

                // Draw threshold lines on top of the progress bar
                ImVec2 p_min = ImGui::GetItemRectMin();
                ImVec2 p_max = ImGui::GetItemRectMax();
                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                // Gate Threshold (Yellow)
                float gateLineX = p_min.x + gateThresh * (p_max.x - p_min.x);
                draw_list->AddLine(ImVec2(gateLineX, p_min.y), ImVec2(gateLineX, p_max.y), IM_COL32(255, 255, 0, 200), 2.0f);

                // Trigger Threshold (Orange)
                float trigLineX = p_min.x + trigThresh * (p_max.x - p_min.x);
                draw_list->AddLine(ImVec2(trigLineX, p_min.y), ImVec2(trigLineX, p_max.y), IM_COL32(255, 165, 0, 200), 2.0f);
                
                ImGui::PopID();
            }
        }
    }
    // --- SPECIAL RENDERING FOR SNAPSHOT SEQUENCER ---
    // Commented out - SnapshotSequencerModuleProcessor causing build errors
    /*else if (auto* snapshotSeq = dynamic_cast<SnapshotSequencerModuleProcessor*>(mp))
    {
        // First, draw the standard parameters (number of steps, etc.)
        snapshotSeq->drawParametersInNode(nodeContentWidth, isParamModulated, onModificationEnded);
        
        ImGui::Separator();
        ImGui::Text("Snapshot Management:");
        
        const int numSteps = 8; // Default, could read from parameter
        const int currentStepIndex = 0; // TODO: Get from module if exposed
        
        // Draw capture/clear buttons for each step
        for (int i = 0; i < numSteps; ++i)
        {
            ImGui::PushID(i);
            
            bool stored = snapshotSeq->isSnapshotStored(i);
            
            // Capture button
            if (ImGui::Button("Capture"))
            {
                // Get the current state of the whole synth
                juce::MemoryBlock currentState;
                synth->getStateInformation(currentState);
                
                // Store it in the snapshot sequencer
                snapshotSeq->setSnapshotForStep(i, currentState);
                
                // Create undo state
                pushSnapshot();
                
                juce::Logger::writeToLog("[SnapshotSeq UI] Captured snapshot for step " + juce::String(i));
            }
            
            ImGui::SameLine();
            
            // Clear button (only enabled if snapshot exists)
            if (!stored)
            {
                ImGui::BeginDisabled();
            }
            
            if (ImGui::Button("Clear"))
            {
                snapshotSeq->clearSnapshotForStep(i);
                pushSnapshot();
                juce::Logger::writeToLog("[SnapshotSeq UI] Cleared snapshot for step " + juce::String(i));
            }
            
            if (!stored)
            {
                ImGui::EndDisabled();
            }
            
            ImGui::PopID();
        }
    }*/
    else
    {
        mp->drawParametersInNode (nodeContentWidth, isParamModulated, onModificationEnded);
    }
    ImGui::Spacing();
    ImGui::PopID();
}
            }

            // IO per module type via helpers
            NodePinHelpers helpers;
            
            // Helper to draw right-aligned text within a node's content width
            // From imnodes examples (color_node_editor.cpp:353, save_load.cpp:77, multi_editor.cpp:73):
            // Use ImGui::Indent() for right-alignment - this is the CORRECT ImNodes pattern!
            auto rightLabelWithinWidth = [&](const char* txt, float nodeContentWidth)
            {
                const ImVec2 textSize = ImGui::CalcTextSize(txt);
                
                // Indent by (nodeWidth - textWidth) to right-align the text
                // CRITICAL: Must call Unindent() to prevent indent from persisting!
                const float indentAmount = juce::jmax(0.0f, nodeContentWidth - textSize.x);
                ImGui::Indent(indentAmount);
                ImGui::TextUnformatted(txt);
                ImGui::Unindent(indentAmount);  // Reset indent!
            };
            helpers.drawAudioInputPin = [&](const char* label, int channel)
            {
                int attr = encodePinId({lid, channel, true});
                seenAttrs.insert(attr);
                availableAttrs.insert(attr);

                // Get pin data type for color coding
                PinID pinId = { lid, channel, true, false, "" };
                PinDataType pinType = this->getPinDataTypeForPin(pinId);
                unsigned int pinColor = this->getImU32ForType(pinType);

                bool isConnected = connectedInputAttrs.count(attr) > 0;
                ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);

                ImNodes::BeginInputAttribute(attr); ImGui::TextUnformatted(label); ImNodes::EndInputAttribute();

                // --- THIS IS THE DEFINITIVE FIX ---
                // Get the bounding box of the pin circle that was just drawn.
                ImVec2 pinMin = ImGui::GetItemRectMin();
                ImVec2 pinMax = ImGui::GetItemRectMax();
                // Calculate the exact center and cache it.
                float centerX = (pinMin.x + pinMax.x) * 0.5f;
                float centerY = (pinMin.y + pinMax.y) * 0.5f;
                attrPositions[attr] = ImVec2(centerX, centerY);
                // --- END OF FIX ---

                ImNodes::PopColorStyle(); // Restore default color

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    if (isConnected) {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Connected");
                        // Find which output this input is connected to and show source info
                        for (const auto& c : synth->getConnectionsInfo())
                        {
                            bool isConnectedToThisPin = (!c.dstIsOutput && c.dstLogicalId == lid && c.dstChan == channel) || (c.dstIsOutput && lid == 0 && c.dstChan == channel);
                            if (isConnectedToThisPin)
                            {
                                if (auto* srcMod = synth->getModuleForLogical(c.srcLogicalId))
                                {
                                    float value = srcMod->getOutputChannelValue(c.srcChan);
                                    ImGui::Text("From %u:%d", c.srcLogicalId, c.srcChan);
                                    ImGui::Text("Value: %.3f", value);
                                }
                                break; 
                            }
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not Connected");
                    }
                    // Show pin data type
                    ImGui::Text("Type: %s", this->pinDataTypeToString(pinType));
                    ImGui::EndTooltip();
                }
            };

                // NEW CLEAN OUTPUT PIN TEXT FUNCTION - FIXED SPACING
                helpers.drawAudioOutputPin = [&](const char* label, int channel)
                {
                    const int attr = encodePinId({(juce::uint32)lid, channel, false});
                    seenAttrs.insert(attr);
                    availableAttrs.insert(attr);

                    PinID pinId = {(juce::uint32)lid, channel, false, false, ""};
                    PinDataType pinType = this->getPinDataTypeForPin(pinId);
                    unsigned int pinColor = this->getImU32ForType(pinType);
                    bool isConnected = connectedOutputAttrs.count(attr) > 0;

                    ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);

                    // EXACT OFFICIAL PATTERN: Text right-aligned, pin touches text edge
                    ImNodes::BeginOutputAttribute(attr);
                    const float label_width = ImGui::CalcTextSize(label).x;
                    ImGui::Indent(nodeContentWidth - label_width);  // Right-align to content width
                    ImGui::TextUnformatted(label);
                    ImGui::Unindent(nodeContentWidth - label_width);
                    ImNodes::EndOutputAttribute();

                    // Cache pin center
                    {
                        ImVec2 pinMin = ImGui::GetItemRectMin();
                        ImVec2 pinMax = ImGui::GetItemRectMax();
                        float centerY = (pinMin.y + pinMax.y) * 0.5f;
                        float x_pos   = pinMax.x;
                        attrPositions[attr] = ImVec2(x_pos, centerY);
                    }

                    ImNodes::PopColorStyle();

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        if (isConnected) {
                            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Connected");
                        } else {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not Connected");
                        }
                        ImGui::Text("Type: %s", this->pinDataTypeToString(pinType));
                        if (auto* mp = synth->getModuleForLogical(lid))
                        {
                            float value = mp->getOutputChannelValue(channel);
                            ImGui::Text("Value: %.3f", value);
                        }
                        ImGui::EndTooltip();
                    }
                };

            // ADD THE NEW drawParallelPins HELPER
            helpers.drawParallelPins = [&](const char* inLabel, int inChannel, const char* outLabel, int outChannel)
            {
                // 3-column layout: [InputPin] [Right-aligned Output Label] [Output Pin]
                ImGui::PushID((inChannel << 16) ^ outChannel ^ lid);
                ImGui::Columns(3, "parallel_io_layout", false);

                const float pinW = 18.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                // CRITICAL FIX: Use stable nodeContentWidth to set ALL column widths explicitly
                // This prevents the feedback loop that causes position-dependent node scaling
                
                // Calculate column widths to fill exactly nodeContentWidth
                float inTextW = inLabel ? ImGui::CalcTextSize(inLabel).x : 0.0f;
                float inColW = inLabel ? (inTextW + pinW + spacing) : 0.0f;  // Input label + pin + spacing
                float outPinColW = 20.0f;  // Output pin column (fixed narrow width)
                float labelColW = nodeContentWidth - inColW - outPinColW - spacing;  // Middle fills remaining space

                ImGui::SetColumnWidth(0, inColW);
                ImGui::SetColumnWidth(1, labelColW);
                ImGui::SetColumnWidth(2, outPinColW);

                // Column 0: Input pin with label
                if (inLabel != nullptr)
                {
                    int inAttr = encodePinId({lid, inChannel, true});
                    seenAttrs.insert(inAttr);
                    availableAttrs.insert(inAttr);
                    PinID pinId = { lid, inChannel, true, false, "" };
                    PinDataType pinType = this->getPinDataTypeForPin(pinId);
                    unsigned int pinColor = this->getImU32ForType(pinType);
                    bool isConnected = connectedInputAttrs.count(inAttr) > 0;
                    ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);
                    ImNodes::BeginInputAttribute(inAttr);
                    ImGui::TextUnformatted(inLabel);
                    ImNodes::EndInputAttribute();
                    ImNodes::PopColorStyle();
                }

                // Column 1: Output label (right-aligned within this column)
                ImGui::NextColumn();
                if (outLabel != nullptr)
                {
                    // EXACT OFFICIAL PATTERN: Same as regular output pins
                    const float textW = ImGui::CalcTextSize(outLabel).x;
                    ImGui::Indent(labelColW - textW);  // Right-align to column width
                    ImGui::TextUnformatted(outLabel);
                    ImGui::Unindent(labelColW - textW);
                }

                // Column 2: Output pin
                ImGui::NextColumn();
                if (outLabel != nullptr)
                {
                    int outAttr = encodePinId({lid, outChannel, false});
                    seenAttrs.insert(outAttr);
                    availableAttrs.insert(outAttr);
                    PinID pinId = { lid, outChannel, false, false, "" };
                    PinDataType pinType = this->getPinDataTypeForPin(pinId);
                    unsigned int pinColor = this->getImU32ForType(pinType);
                    bool isConnected = connectedOutputAttrs.count(outAttr) > 0;
                    ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);
                    ImNodes::BeginOutputAttribute(outAttr);
                    ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
                    ImNodes::EndOutputAttribute();

                    // Cache pin center
                    ImVec2 pinMin = ImGui::GetItemRectMin();
                    ImVec2 pinMax = ImGui::GetItemRectMax();
                    float yCenter = pinMin.y + (pinMax.y - pinMin.y) * 0.5f;
                    float xPos = pinMax.x;
                    attrPositions[outAttr] = ImVec2(xPos, yCenter);
                    ImNodes::PopColorStyle();
                }

                // Restore to single column for the next row
                ImGui::Columns(1);
                ImGui::PopID();
            };

            // --- DYNAMIC PIN FIX ---
            // Add a new helper that uses dynamic pin information from modules
            helpers.drawIoPins = [&](ModuleProcessor* module) {
                if (!module) return;
                const auto logicalId = module->getLogicalId();
                const auto moduleType = synth->getModuleTypeForLogical(logicalId);

                // 1. Get dynamic pins from the module itself.
                auto dynamicInputs = module->getDynamicInputPins();
                auto dynamicOutputs = module->getDynamicOutputPins();

                // 2. Get static pins from the database as a fallback.
                const auto& pinDb = getModulePinDatabase();
                auto pinInfoIt = pinDb.find(moduleType.toLowerCase());
                const bool hasStaticPinInfo = (pinInfoIt != pinDb.end());
                const auto& staticPinInfo = hasStaticPinInfo ? pinInfoIt->second : ModulePinInfo{};

                // 3. If the module has dynamic pins, use the new system
                const bool hasDynamicPins = !dynamicInputs.empty() || !dynamicOutputs.empty();
                
                if (hasDynamicPins)
                {
                    // Draw inputs (dynamic if available, otherwise static)
                    if (!dynamicInputs.empty())
                    {
                        for (const auto& pin : dynamicInputs)
                        {
                            helpers.drawAudioInputPin(pin.name.toRawUTF8(), pin.channel);
                        }
                    }
                    else
                    {
                        for (const auto& pin : staticPinInfo.audioIns)
                        {
                            helpers.drawAudioInputPin(pin.name.toRawUTF8(), pin.channel);
                        }
                    }
                    
                    // Draw outputs (dynamic if available, otherwise static)
                    if (!dynamicOutputs.empty())
                    {
                        for (const auto& pin : dynamicOutputs)
                        {
                            helpers.drawAudioOutputPin(pin.name.toRawUTF8(), pin.channel);
                        }
                    }
                    else
                    {
                        for (const auto& pin : staticPinInfo.audioOuts)
                        {
                            helpers.drawAudioOutputPin(pin.name.toRawUTF8(), pin.channel);
                        }
                    }
                }
                else
                {
                    // 4. Otherwise, fall back to the module's custom drawIoPins implementation
                    module->drawIoPins(helpers);
                }
            };
            // --- END OF DYNAMIC PIN FIX ---

            // Delegate per-module IO pin drawing
            if (synth != nullptr)
                if (auto* mp = synth->getModuleForLogical (lid))
                    helpers.drawIoPins(mp);

            // Optional per-node right-click popup
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                selectedLogicalId = (int) lid;
                ImGui::OpenPopup("NodeActionPopup");
            }

            // Legacy per-type IO drawing removed; delegated to module implementations via helpers

            ImNodes::EndNode();
            
            // Cache position for snapshot safety
            // Graph is always in consistent state since we rebuild at frame start
            lastKnownNodePositions[(int)lid] = ImNodes::GetNodeGridSpacePos((int)lid);
            
            // Pop muted node styles (in reverse order of push)
            if (isMuted) {
                ImNodes::PopColorStyle();      // Mute TitleBar
                ImGui::PopStyleVar();          // Alpha
                ImNodes::PopStyleVar();        // NodePadding
            }
            
            // Pop hover highlight color
            if (isHoveredSource || isHoveredDest)
                ImNodes::PopColorStyle();      // Hover TitleBar
            
            // Pop category colors (in reverse order of push)
            ImNodes::PopColorStyle();          // TitleBarSelected
            ImNodes::PopColorStyle();          // TitleBarHovered
            ImNodes::PopColorStyle();          // TitleBar
            
            // Apply pending placement if queued
            if (auto itS = pendingNodeScreenPositions.find((int) lid); itS != pendingNodeScreenPositions.end())
            {
                ImNodes::SetNodeScreenSpacePos((int) lid, itS->second);
                pendingNodeScreenPositions.erase(itS);
            }
        if (auto it = pendingNodePositions.find((int) lid); it != pendingNodePositions.end())
        {
            // Apply saved position once; do not write (0,0) defaults
            const ImVec2 p = it->second;
            if (!(p.x == 0.0f && p.y == 0.0f))
            {
                ImNodes::SetNodeGridSpacePos((int) lid, p);
                juce::Logger::writeToLog("[PositionRestore] Applied pending position for node " + juce::String((int)lid) + ": (" + juce::String(p.x) + ", " + juce::String(p.y) + ")");
            }
            pendingNodePositions.erase(it);
        }
            // Apply pending size if queued (for Comment nodes to prevent feedback loop)
            if (auto itSize = pendingNodeSizes.find((int) lid); itSize != pendingNodeSizes.end())
            {
                // Store the desired size in the Comment module itself
                if (auto* comment = dynamic_cast<CommentModuleProcessor*>(synth->getModuleForLogical((juce::uint32)lid)))
                {
                    comment->nodeWidth = itSize->second.x;
                    comment->nodeHeight = itSize->second.y;
                }
                pendingNodeSizes.erase(itSize);
            }
            drawnNodes.insert((int) lid);
        }

        // Node action popup (Delete / Duplicate)
        bool triggerInsertMixer = false;
        if (ImGui::BeginPopup("NodeActionPopup"))
        {
            if (ImGui::MenuItem("Delete") && selectedLogicalId != 0)
            {
                mutedNodeStates.erase((juce::uint32)selectedLogicalId); // Clean up muted state if exists
                synth->removeModule (synth->getNodeIdForLogical ((juce::uint32) selectedLogicalId));
                graphNeedsRebuild = true;
                // Post-state snapshot
                pushSnapshot();
                selectedLogicalId = 0;
            }
            if (ImGui::MenuItem("Duplicate") && selectedLogicalId != 0)
            {
                const juce::String type = getTypeForLogical ((juce::uint32) selectedLogicalId);
                if (! type.isEmpty())
                {
                    auto newNodeId = synth->addModule (type);
                    graphNeedsRebuild = true;
                    if (auto* src = synth->getModuleForLogical ((juce::uint32) selectedLogicalId))
                        if (auto* dst = synth->getModuleForLogical (synth->getLogicalIdForNode(newNodeId)))
                            dst->getAPVTS().replaceState (src->getAPVTS().copyState());
                    ImVec2 pos = ImNodes::GetNodeGridSpacePos (selectedLogicalId);
                    ImNodes::SetNodeGridSpacePos ((int) synth->getLogicalIdForNode(newNodeId), ImVec2 (pos.x + 40.0f, pos.y + 40.0f));
                    // Post-state snapshot after duplication and position
                    pushSnapshot();
                }
            }
            if (ImGui::MenuItem("Insert Mixer", "Ctrl+T") && selectedLogicalId != 0) { triggerInsertMixer = true; }
            ImGui::EndPopup();
        }

        // Shortcut: Ctrl+T to insert a Mixer after selected node and reroute
        // Debounced Ctrl+T
        const bool ctrlDown = ImGui::GetIO().KeyCtrl;
        if (!ctrlDown) {
            mixerShortcutCooldown = false;
            insertNodeShortcutCooldown = false;
        }
        // Ctrl+R: Record Output
        if (ctrlDown && ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            handleRecordOutput();
        }
        
        if ((triggerInsertMixer || (selectedLogicalId != 0 && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_T))) && !mixerShortcutCooldown)
        {
            mixerShortcutCooldown = true; // Prevent re-triggering in the same frame
            const juce::uint32 srcLid = (juce::uint32) selectedLogicalId;

            juce::Logger::writeToLog("--- [InsertMixer] Start ---");
            juce::Logger::writeToLog("[InsertMixer] Selected Node Logical ID: " + juce::String(srcLid));

            auto srcNodeId = synth->getNodeIdForLogical(srcLid);
            if (srcNodeId.uid == 0) 
            {
                juce::Logger::writeToLog("[InsertMixer] ABORT: Source node with logical ID " + juce::String(srcLid) + " is invalid or could not be found.");
            } 
            else 
            {
                // 1. Collect all outgoing connections from the selected node
                std::vector<ModularSynthProcessor::ConnectionInfo> outgoingConnections;
                for (const auto& c : synth->getConnectionsInfo()) {
                    if (c.srcLogicalId == srcLid) {
                        outgoingConnections.push_back(c);
                    }
                }
                juce::Logger::writeToLog("[InsertMixer] Found " + juce::String(outgoingConnections.size()) + " outgoing connections to reroute.");
                for (const auto& c : outgoingConnections) {
                    juce::String destStr = c.dstIsOutput ? "Main Output" : "Node " + juce::String(c.dstLogicalId);
                    juce::Logger::writeToLog("  - Stored connection: [Src: " + juce::String(c.srcLogicalId) + ":" + juce::String(c.srcChan) + "] -> [Dst: " + destStr + ":" + juce::String(c.dstChan) + "]");
                }

                // 2. Create and position the new mixer node intelligently
                auto mixNodeIdGraph = synth->addModule("Mixer");
                const juce::uint32 mixLid = synth->getLogicalIdForNode(mixNodeIdGraph);
                
                ImVec2 srcPos = ImNodes::GetNodeGridSpacePos(selectedLogicalId);
                ImVec2 avgDestPos = srcPos; // Default to source pos if no outputs
                
                if (!outgoingConnections.empty())
                {
                    float totalX = 0.0f, totalY = 0.0f;
                    for (const auto& c : outgoingConnections)
                    {
                        int destId = c.dstIsOutput ? 0 : (int)c.dstLogicalId;
                        ImVec2 pos = ImNodes::GetNodeGridSpacePos(destId);
                        totalX += pos.x;
                        totalY += pos.y;
                    }
                    avgDestPos = ImVec2(totalX / outgoingConnections.size(), totalY / outgoingConnections.size());
                }
                else
                {
                    // If there are no outgoing connections, place it to the right
                    avgDestPos.x += 600.0f;
                }
                
                // Place the new mixer halfway between the source and the average destination
                pendingNodePositions[(int)mixLid] = ImVec2((srcPos.x + avgDestPos.x) * 0.5f, (srcPos.y + avgDestPos.y) * 0.5f);
                juce::Logger::writeToLog("[InsertMixer] Added new Mixer. Logical ID: " + juce::String(mixLid) + ", Node ID: " + juce::String(mixNodeIdGraph.uid));

                // 3. Disconnect all original outgoing links
                juce::Logger::writeToLog("[InsertMixer] Disconnecting original links...");
                for (const auto& c : outgoingConnections) {
                    auto currentSrcNodeId = synth->getNodeIdForLogical(c.srcLogicalId);
                    auto dstNodeId = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);

                    if (currentSrcNodeId.uid != 0 && dstNodeId.uid != 0) {
                        bool success = synth->disconnect(currentSrcNodeId, c.srcChan, dstNodeId, c.dstChan);
                        juce::Logger::writeToLog("  - Disconnecting [" + juce::String(currentSrcNodeId.uid) + ":" + juce::String(c.srcChan) + "] -> [" + juce::String(dstNodeId.uid) + ":" + juce::String(c.dstChan) + "]... " + (success ? "SUCCESS" : "FAILED"));
                    } else {
                        juce::Logger::writeToLog("  - SKIPPING Disconnect due to invalid node ID.");
                    }
                }

                // 4. Connect the source node to the new mixer's first input
                juce::Logger::writeToLog("[InsertMixer] Connecting source node to new mixer...");
                bool c1 = synth->connect(srcNodeId, 0, mixNodeIdGraph, 0); // L to In A L
                juce::Logger::writeToLog("  - Connecting [" + juce::String(srcNodeId.uid) + ":0] -> [" + juce::String(mixNodeIdGraph.uid) + ":0]... " + (c1 ? "SUCCESS" : "FAILED"));
                bool c2 = synth->connect(srcNodeId, 1, mixNodeIdGraph, 1); // R to In A R
                juce::Logger::writeToLog("  - Connecting [" + juce::String(srcNodeId.uid) + ":1] -> [" + juce::String(mixNodeIdGraph.uid) + ":1]... " + (c2 ? "SUCCESS" : "FAILED"));


                // 5. Connect the mixer's output to all the original destinations (maintaining the chain)
                juce::Logger::writeToLog("[InsertMixer] Connecting mixer to original destinations to maintain chain...");
                if (outgoingConnections.empty()) {
                    juce::Logger::writeToLog("  - No original outgoing connections. Connecting mixer to Main Output by default.");
                    auto outNode = synth->getOutputNodeID();
                    if (outNode.uid != 0) {
                        bool o1 = synth->connect(mixNodeIdGraph, 0, outNode, 0);
                        juce::Logger::writeToLog("  - Connecting [" + juce::String(mixNodeIdGraph.uid) + ":0] -> [Output:0]... " + (o1 ? "SUCCESS" : "FAILED"));
                        bool o2 = synth->connect(mixNodeIdGraph, 1, outNode, 1);
                        juce::Logger::writeToLog("  - Connecting [" + juce::String(mixNodeIdGraph.uid) + ":1] -> [Output:1]... " + (o2 ? "SUCCESS" : "FAILED"));
                    }
                } else {
                    for (const auto& c : outgoingConnections) {
                        auto dstNodeId = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);
                        if (dstNodeId.uid != 0) {
                            // Connect mixer output to the same destination the original node was connected to
                            // This maintains the chain: original -> mixer -> destination
                            bool success = synth->connect(mixNodeIdGraph, c.srcChan, dstNodeId, c.dstChan);
                            juce::String destStr = c.dstIsOutput ? "Main Output" : "Node " + juce::String(c.dstLogicalId);
                            juce::Logger::writeToLog("  - Maintaining chain: Mixer [" + juce::String(mixNodeIdGraph.uid) + ":" + juce::String(c.srcChan) + "] -> " + destStr + "[" + juce::String(dstNodeId.uid) + ":" + juce::String(c.dstChan) + "]... " + (success ? "SUCCESS" : "FAILED"));
                        } else {
                            juce::Logger::writeToLog("  - SKIPPING Reconnect due to invalid destination node ID for original logical ID " + juce::String(c.dstLogicalId));
                        }
                    }
                }

                graphNeedsRebuild = true;
                pushSnapshot(); // Make the entire operation undoable
                juce::Logger::writeToLog("[InsertMixer] Rerouting complete. Flagging for graph rebuild.");
            }
            juce::Logger::writeToLog("--- [InsertMixer] End ---");
        }

        // Shortcut: Ctrl+I to show Insert Node popup menu
        if (selectedLogicalId != 0 && ctrlDown && ImGui::IsKeyPressed(ImGuiKey_I) && !insertNodeShortcutCooldown)
        {
            insertNodeShortcutCooldown = true;
            showInsertNodePopup = true;
        }

        // Insert Node popup menu
        if (showInsertNodePopup)
        {
            ImGui::OpenPopup("InsertNodePopup");
            showInsertNodePopup = false;
        }

        if (ImGui::BeginPopup("InsertNodePopup"))
        {
            ImGui::Text("Insert Node Between Connections");
            
            // Audio Path
            if (ImGui::MenuItem("VCF")) { insertNodeBetween("VCF"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("VCA")) { insertNodeBetween("VCA"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Delay")) { insertNodeBetween("Delay"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Reverb")) { insertNodeBetween("Reverb"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Mixer")) { insertNodeBetween("Mixer"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Recorder")) { insertNodeBetween("recorder"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Shaping Oscillator")) { insertNodeBetween("shaping oscillator"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("8-Band Shaper")) { insertNodeBetween("8bandshaper"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Granulator")) { insertNodeBetween("Granulator"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Harmonic Shaper")) { insertNodeBetween("harmonic shaper"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Vocal Tract Filter")) { insertNodeBetween("Vocal Tract Filter"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Scope")) { insertNodeBetween("Scope"); ImGui::CloseCurrentPopup(); }
            
            ImGui::Separator();
            
            // Modulation Path
            if (ImGui::MenuItem("Attenuverter")) { insertNodeBetween("Attenuverter"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Math")) { insertNodeBetween("Math"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Comparator")) { insertNodeBetween("Comparator"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("CV Mixer")) { insertNodeBetween("CV Mixer"); ImGui::CloseCurrentPopup(); }
            if (ImGui::MenuItem("Sequential Switch")) { insertNodeBetween("Sequential Switch"); ImGui::CloseCurrentPopup(); }
            
            ImGui::EndPopup();
        }

        // Output sink node with stereo inputs (single, fixed ID 0)
        const bool isOutputHovered = (hoveredLinkDstId == kOutputHighlightId);
        if (isOutputHovered)
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(255, 220, 0, 255));
        ImNodes::BeginNode (0);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted ("Output");
        ImNodes::EndNodeTitleBar();
        if (isOutputHovered)
            ImNodes::PopColorStyle();
        
        // In L pin with proper Audio type coloring (green)
        { 
            int a = encodePinId({0, 0, true}); 
            seenAttrs.insert(a); 
            availableAttrs.insert(a); 
            bool isConnected = connectedInputAttrs.count(a) > 0;
            PinID pinId = {0, 0, true, false, ""};
            PinDataType pinType = getPinDataTypeForPin(pinId);
            unsigned int pinColor = getImU32ForType(pinType);
            ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);
            ImNodes::BeginInputAttribute (a);
            ImGui::Text ("In L");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        
        // In R pin with proper Audio type coloring (green)
        { 
            int a = encodePinId({0, 1, true}); 
            seenAttrs.insert(a); 
            availableAttrs.insert(a); 
            bool isConnected = connectedInputAttrs.count(a) > 0;
            PinID pinId = {0, 1, true, false, ""};
            PinDataType pinType = getPinDataTypeForPin(pinId);
            unsigned int pinColor = getImU32ForType(pinType);
            ImNodes::PushColorStyle(ImNodesCol_Pin, isConnected ? colPinConnected : pinColor);
            ImNodes::BeginInputAttribute (a);
            ImGui::Text ("In R");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        
        ImNodes::EndNode();
        
        // Cache output node position for snapshot safety
        // Graph is always in consistent state since we rebuild at frame start
        lastKnownNodePositions[0] = ImNodes::GetNodeGridSpacePos(0);
        
        if (auto it = pendingNodePositions.find(0); it != pendingNodePositions.end())
        {
            ImNodes::SetNodeGridSpacePos(0, it->second);
            juce::Logger::writeToLog("[PositionRestore] Applied pending position for output node 0: (" + juce::String(it->second.x) + ", " + juce::String(it->second.y) + ")");
            pendingNodePositions.erase(it);
        }
        drawnNodes.insert(0);

        // Use last frame's hovered node id for highlighting (queried after EndNodeEditor)
        int hoveredNodeId = lastHoveredNodeId;

        // Draw existing audio connections (IDs stable via bitmasking)
        int cableIdx = 0;
        for (const auto& c : synth->getConnectionsInfo())
        {
            
            // Skip links whose nodes weren't drawn this frame (e.g., just deleted)
            if (c.srcLogicalId != 0 && ! drawnNodes.count((int) c.srcLogicalId)) {
                continue;
            }
            if (! c.dstIsOutput && c.dstLogicalId != 0 && ! drawnNodes.count((int) c.dstLogicalId)) {
                continue;
            }
            
            const int srcAttr = encodePinId({c.srcLogicalId, c.srcChan, false});
            const int dstAttr = c.dstIsOutput ? encodePinId({0, c.dstChan, true}) : encodePinId({c.dstLogicalId, c.dstChan, true});
            
            if (! availableAttrs.count(srcAttr) || ! availableAttrs.count(dstAttr))
            {
                static std::unordered_set<std::string> skipOnce;
                const std::string key = std::to_string((int)c.srcLogicalId) + ":" + std::to_string(c.srcChan) + "->" +
                                         (c.dstIsOutput? std::string("0") : std::to_string((int)c.dstLogicalId)) + ":" + std::to_string(c.dstChan);
                if (skipOnce.insert(key).second)
                {
                    juce::Logger::writeToLog(
                        juce::String("[ImNodes][SKIP] missing attr: srcPresent=") + (availableAttrs.count(srcAttr)?"1":"0") +
                        " dstPresent=" + (availableAttrs.count(dstAttr)?"1":"0") +
                        " srcKey=(lid=" + juce::String((int)c.srcLogicalId) + ",ch=" + juce::String(c.srcChan) + ")" +
                        " dstKey=(lid=" + juce::String(c.dstIsOutput?0:(int)c.dstLogicalId) + ",ch=" + juce::String(c.dstChan) + ",in=1) id(s)=" +
                        juce::String(srcAttr) + "," + juce::String(dstAttr));
                }
                continue;
            }
            
            const int linkId = linkIdOf(srcAttr, dstAttr);
            linkIdToAttrs[linkId] = { srcAttr, dstAttr };
            
            // --- THIS IS THE DEFINITIVE FIX ---
            // 1. Determine the base color and check for signal activity.
            auto srcPin = decodePinId(srcAttr);
            PinDataType linkDataType = getPinDataTypeForPin(srcPin);
            ImU32 linkColor = getImU32ForType(linkDataType);
            float magnitude = 0.0f;
            bool hasThicknessModification = false;

            if (auto* srcModule = synth->getModuleForLogical(srcPin.logicalId))
            {
                magnitude = srcModule->getOutputChannelValue(srcPin.channel);
            }

            // 2. If the signal is active, calculate a glowing/blinking color.
            if (magnitude > 0.01f)
            {
                const float blinkSpeed = 8.0f;
                float blinkFactor = (std::sin((float)ImGui::GetTime() * blinkSpeed) + 1.0f) * 0.5f;
                float glowIntensity = juce::jlimit(0.0f, 1.0f, blinkFactor * magnitude * 2.0f);

                // Brighten the base color and modulate alpha for glow effect
                ImVec4 colorVec = ImGui::ColorConvertU32ToFloat4(linkColor);
                colorVec.x = juce::jmin(1.0f, colorVec.x + glowIntensity * 0.4f);
                colorVec.y = juce::jmin(1.0f, colorVec.y + glowIntensity * 0.4f);
                colorVec.z = juce::jmin(1.0f, colorVec.z + glowIntensity * 0.4f);
                colorVec.w = juce::jlimit(0.5f, 1.0f, 0.5f + glowIntensity * 0.5f);
                linkColor = ImGui::ColorConvertFloat4ToU32(colorVec);

                // Make active cables slightly thicker
                ImNodes::PushStyleVar(ImNodesStyleVar_LinkThickness, 3.0f);
                hasThicknessModification = true;
            }

            // 3. Push the chosen color (either normal or glowing) to the style stack.
            ImNodes::PushColorStyle(ImNodesCol_Link, linkColor);
            ImNodes::PushColorStyle(ImNodesCol_LinkHovered, IM_COL32(255, 255, 0, 255));
            ImNodes::PushColorStyle(ImNodesCol_LinkSelected, IM_COL32(255, 255, 0, 255));

            // 4. Check for node hover highlight (this should override the glow).
            const bool hl = (hoveredNodeId != -1) && ((int) c.srcLogicalId == hoveredNodeId || (! c.dstIsOutput && (int) c.dstLogicalId == hoveredNodeId) || (c.dstIsOutput && hoveredNodeId == 0));
            if (hl)
            {
                ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(255, 255, 0, 255));
            }
            
            // 5. Tell imnodes to draw the link. It will use the color we just pushed.
            ImNodes::Link(linkId, srcAttr, dstAttr);
            
            // 6. Pop ALL style modifications to restore the defaults for the next link.
            if (hl) ImNodes::PopColorStyle();
            ImNodes::PopColorStyle(); // LinkSelected
            ImNodes::PopColorStyle(); // LinkHovered
            ImNodes::PopColorStyle(); // Link
            if (hasThicknessModification) ImNodes::PopStyleVar(); // LinkThickness
            
            // --- END OF FIX ---
        }

        // Drag detection for node movement: snapshot once on mouse release (post-state)
        const bool hoveringNode = (lastHoveredNodeId != -1);
        if (hoveringNode && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            isDraggingNode = true;
        }
        if (isDraggingNode && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            isDraggingNode = false;
            // Capture positions after a move so subsequent operations (e.g. delete) undo to the moved location
            pushSnapshot();
        }
    }

    // --- Handle Auto-Connect Requests from MIDI Players ---
    for (const auto& modInfo : synth->getModulesInfo())
    {
        if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(synth->getModuleForLogical(modInfo.first)))
        {
            // Check for initial button presses
            if (midiPlayer->autoConnectTriggered.exchange(false))
            {
                midiPlayer->lastAutoConnectState = MIDIPlayerModuleProcessor::AutoConnectState::Samplers;
                handleMidiPlayerAutoConnect(midiPlayer, modInfo.first);
                pushSnapshot();
            }
            else if (midiPlayer->autoConnectVCOTriggered.exchange(false))
            {
                midiPlayer->lastAutoConnectState = MIDIPlayerModuleProcessor::AutoConnectState::PolyVCO;
                handleMidiPlayerAutoConnectVCO(midiPlayer, modInfo.first);
                pushSnapshot();
            }
            else if (midiPlayer->autoConnectHybridTriggered.exchange(false))
            {
                midiPlayer->lastAutoConnectState = MIDIPlayerModuleProcessor::AutoConnectState::Hybrid;
                handleMidiPlayerAutoConnectHybrid(midiPlayer, modInfo.first);
                pushSnapshot();
            }
            // --- THIS IS THE NEW LOGIC ---
            // Check if an update was requested after a new file was loaded
            else if (midiPlayer->connectionUpdateRequested.exchange(false))
            {
                // Re-run the correct handler based on the saved state
                switch (midiPlayer->lastAutoConnectState.load())
                {
                    case MIDIPlayerModuleProcessor::AutoConnectState::Samplers:
                        handleMidiPlayerAutoConnect(midiPlayer, modInfo.first);
                        pushSnapshot();
                        break;
                    case MIDIPlayerModuleProcessor::AutoConnectState::PolyVCO:
                        handleMidiPlayerAutoConnectVCO(midiPlayer, modInfo.first);
                        pushSnapshot();
                        break;
                    case MIDIPlayerModuleProcessor::AutoConnectState::Hybrid:
                        handleMidiPlayerAutoConnectHybrid(midiPlayer, modInfo.first);
                        pushSnapshot();
                        break;
                    case MIDIPlayerModuleProcessor::AutoConnectState::None:
                    default:
                        // Do nothing if it wasn't auto-connected before
                        break;
                }
            }
            // --- END OF NEW LOGIC ---
        }
    }

    // --- Handle Auto-Connect Requests using new intelligent system ---
    handleAutoConnectionRequests();

    ImNodes::MiniMap (0.2f, ImNodesMiniMapLocation_BottomRight);

    ImNodes::EndNodeEditor();
    
    // ================== MIDI PLAYER QUICK CONNECT LOGIC ==================
    // Poll all MIDI Player modules for connection requests
    if (synth != nullptr)
    {
        for (const auto& modInfo : synth->getModulesInfo())
        {
            if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(synth->getModuleForLogical(modInfo.first)))
            {
                int requestType = midiPlayer->getAndClearConnectionRequest();
                if (requestType > 0)
                {
                    handleMIDIPlayerConnectionRequest(modInfo.first, midiPlayer, requestType);
                    break; // Only handle one request per frame
                }
            }
        }
    }
    // ================== END MIDI PLAYER QUICK CONNECT ==================
    
    // ================== META MODULE EDITING LOGIC ==================
    // Check if any Meta Module has requested to be edited
    if (synth != nullptr && metaModuleToEditLid == 0) // Only check if not already editing one
    {
        for (const auto& modInfo : synth->getModulesInfo())
        {
            if (auto* metaModule = dynamic_cast<MetaModuleProcessor*>(synth->getModuleForLogical(modInfo.first)))
            {
                // Atomically check and reset the flag
                if (metaModule->editRequested.exchange(false))
                {
                    metaModuleToEditLid = modInfo.first;
                    juce::Logger::writeToLog("[MetaEdit] Opening editor for Meta Module L-ID " + juce::String((int)metaModuleToEditLid));
                    ImGui::OpenPopup("Edit Meta Module");
                    break; // Only handle one request per frame
                }
            }
        }
    }
    
    // Draw the modal popup for the internal editor if one is selected
    if (metaModuleToEditLid != 0)
    {
        ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal("Edit Meta Module", nullptr, ImGuiWindowFlags_MenuBar))
        {
            // Get the internal synth processor from the meta module
            auto* metaModule = dynamic_cast<MetaModuleProcessor*>(synth->getModuleForLogical(metaModuleToEditLid));
            if (metaModule && metaModule->getInternalGraph())
            {
                // Display a placeholder for now
                // TODO: Full recursive editor implementation would go here
                ImGui::Text("Editing internal graph of Meta Module %d", (int)metaModuleToEditLid);
                ImGui::Separator();
                
                auto* internalGraph = metaModule->getInternalGraph();
                auto modules = internalGraph->getModulesInfo();
                
                ImGui::Text("Internal modules: %d", (int)modules.size());
                if (ImGui::BeginChild("ModuleList", ImVec2(0, -30), true))
                {
                    for (const auto& [lid, type] : modules)
                    {
                        ImGui::Text("  [%d] %s", (int)lid, type.toRawUTF8());
                    }
                }
                ImGui::EndChild();
                
                ImGui::Text("NOTE: Full nested editor UI is a TODO");
                ImGui::Text("For now, you can inspect the internal graph structure above.");
            }
            
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
                metaModuleToEditLid = 0;
                // When closing, the meta module might have new/removed inlets/outlets,
                // so we need to rebuild the main graph to update its pins
                graphNeedsRebuild = true;
            }
            ImGui::EndPopup();
        }
        else
        {
            // If the popup was closed by the user (e.g., pressing ESC)
            metaModuleToEditLid = 0;
            graphNeedsRebuild = true;
        }
    }
    // ======================= END OF META MODULE LOGIC =======================

    // --- CONSOLIDATED HOVERED LINK DETECTION ---
    // Declare these variables ONCE, immediately after the editor has ended.
    // All subsequent features that need to know about hovered links can now
    // safely reuse these results without causing redefinition or scope errors.
    // Graph is always in consistent state since we rebuild at frame start
    int hoveredLinkId = -1;
    bool isLinkHovered = ImNodes::IsLinkHovered(&hoveredLinkId);
    // --- END OF CONSOLIDATED DECLARATION ---
    
    // Smart cable visualization is now integrated directly into the link drawing loop above.
    // No separate overlay needed - cables glow by modifying their own color.
    
    // === PROBE TOOL MODE HANDLING ===
    if (isProbeModeActive)
    {
        // Change cursor to indicate probe mode is active
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        
        // Draw "PROBE ACTIVE" indicator at mouse position
        auto* drawList = ImGui::GetForegroundDrawList();
        ImVec2 mousePos = ImGui::GetMousePos();
        const char* text = "PROBE MODE: Click output pin";
        auto textSize = ImGui::CalcTextSize(text);
        ImVec2 textPos = ImVec2(mousePos.x + 20, mousePos.y - 20);
        drawList->AddRectFilled(
            ImVec2(textPos.x - 5, textPos.y - 2),
            ImVec2(textPos.x + textSize.x + 5, textPos.y + textSize.y + 2),
            IM_COL32(50, 50, 50, 200)
        );
        drawList->AddText(textPos, IM_COL32(255, 255, 100, 255), text);
        
        // Check for pin clicks
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            int hoveredPinId = -1;
            if (ImNodes::IsPinHovered(&hoveredPinId) && hoveredPinId != -1)
            {
                auto pinId = decodePinId(hoveredPinId);
                // Check if it's an output pin (not input, not mod)
                if (!pinId.isInput && !pinId.isMod && pinId.logicalId != 0)
                {
                    juce::Logger::writeToLog("[PROBE_UI] Probe clicked on valid output pin. LogicalID: " + juce::String(pinId.logicalId) + ", Channel: " + juce::String(pinId.channel));
                    auto nodeId = synth->getNodeIdForLogical(pinId.logicalId);
                    synth->setProbeConnection(nodeId, pinId.channel);
                    isProbeModeActive = false; // Deactivate after probing
                }
                else
                {
                    juce::Logger::writeToLog("[PROBE_UI] Probe clicked on an invalid pin (input or output node). Cancelling.");
                    isProbeModeActive = false;
                }
            }
            else
            {
                // Clicked on empty space, cancel probe mode
                juce::Logger::writeToLog("[PROBE_UI] Probe clicked on empty space. Cancelling.");
                isProbeModeActive = false;
            }
        }
        
        // Allow ESC to cancel probe mode
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            isProbeModeActive = false;
            juce::Logger::writeToLog("[PROBE_UI] Cancelled with ESC");
        }
    }

    // --- CONTEXTUAL RIGHT-CLICK HANDLER ---
    // A cable was right-clicked. Store its info and open the insert popup.
    if (isLinkHovered && hoveredLinkId != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        juce::Logger::writeToLog("[InsertNode][RC] Hovered link id=" + juce::String(hoveredLinkId));
        // A cable was right-clicked. Store its info and open the insert popup.
        linkToInsertOn = {}; // Reset previous info
        linkToInsertOn.linkId = hoveredLinkId;

        bool captured = false;
        // TODO: Implement modulation link detection for new bus-based system
        // if (modLinkIdToRoute.count(hoveredLinkId))
        // {
        //     linkToInsertOn.isMod = true;
        //     auto& route = modLinkIdToRoute[hoveredLinkId];
        //     linkToInsertOn.srcLogicalId = std::get<0>(route);
        //     linkToInsertOn.srcChan = std::get<1>(route);
        //     linkToInsertOn.dstLogicalId = std::get<2>(route);
        //     linkToInsertOn.paramId = std::get<3>(route);
        //     juce::Logger::writeToLog("[InsertNode][RC] Mod link captured: srcLID=" + juce::String((int)linkToInsertOn.srcLogicalId) +
        //                               " srcChan=" + juce::String(linkToInsertOn.srcChan) +
        //                               " dstLID=" + juce::String((int)linkToInsertOn.dstLogicalId) +
        //                               " param='" + linkToInsertOn.paramId + "'");
        //     captured = true;
        // }
        if (linkIdToAttrs.count(hoveredLinkId))
        {
            linkToInsertOn.isMod = false;
            auto& attrs = linkIdToAttrs[hoveredLinkId];
            juce::Logger::writeToLog("[InsertNode][RC] Audio link attrs: srcAttr=" + juce::String(attrs.first) +
                                      " dstAttr=" + juce::String(attrs.second));
            linkToInsertOn.srcPin = decodePinId(attrs.first);
            linkToInsertOn.dstPin = decodePinId(attrs.second);
            juce::Logger::writeToLog("[InsertNode][RC] Audio pins: src(lid=" + juce::String((int)linkToInsertOn.srcPin.logicalId) +
                                      ",ch=" + juce::String(linkToInsertOn.srcPin.channel) +
                                      ",in=" + juce::String((int)linkToInsertOn.srcPin.isInput) + ") -> dst(lid=" +
                                      juce::String((int)linkToInsertOn.dstPin.logicalId) + ",ch=" +
                                      juce::String(linkToInsertOn.dstPin.channel) + ",in=" +
                                      juce::String((int)linkToInsertOn.dstPin.isInput) + ")");
            captured = true;
        }
        else
        {
            juce::Logger::writeToLog("[InsertNode][RC] Link id not found in maps");
        }

        if (captured)
        {
            showInsertNodePopup = true; // defer opening until after EndNodeEditor
            pendingInsertLinkId = hoveredLinkId;
            juce::Logger::writeToLog("[InsertNode][RC] Will open popup after EndNodeEditor");
        }
        else
        {
            linkToInsertOn.linkId = -1; // nothing recognized; do not open
        }
    }

    // --- Keyboard Shortcuts for Node Chaining ---
    // Check if multiple nodes are selected and no modifiers are held
    if (ImNodes::NumSelectedNodes() > 1 && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt)
    {
        // C: Standard stereo chaining (channels 0->0, 1->1)
        if (ImGui::IsKeyPressed(ImGuiKey_C))
        {
            handleNodeChaining();
        }
        // G: Audio type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_G))
        {
            handleColorCodedChaining(PinDataType::Audio);
        }
        // B: CV type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_B))
        {
            handleColorCodedChaining(PinDataType::CV);
        }
        // R: Raw type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_R))
        {
            handleColorCodedChaining(PinDataType::Raw);
        }
        // Y: Gate type chaining
        else if (ImGui::IsKeyPressed(ImGuiKey_Y))
        {
            handleColorCodedChaining(PinDataType::Gate);
        }
    }
    // --- END OF KEYBOARD SHORTCUTS ---

    // --- Cable Splitting (Ctrl+Middle-Click) ---
    if (isLinkHovered && hoveredLinkId != -1)
    {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            // User initiated a split. Find the source pin of the hovered link.
            if (auto it = linkIdToAttrs.find(hoveredLinkId); it != linkIdToAttrs.end())
            {
                splittingFromAttrId = it->second.first; // The source attribute ID
                juce::Logger::writeToLog("[CableSplit] Starting split from attr ID: " + juce::String(splittingFromAttrId));
            }
        }
    }
    // --- END OF CABLE SPLITTING ---

    // 2. If a split-drag is active, handle drawing and completion.
    if (splittingFromAttrId != -1)
    {
        // Draw a line from the source pin to the mouse cursor for visual feedback.
        if (auto it = attrPositions.find(splittingFromAttrId); it != attrPositions.end())
        {
            ImVec2 sourcePos = it->second;
            ImVec2 mousePos = ImGui::GetMousePos();
            ImGui::GetForegroundDrawList()->AddLine(sourcePos, mousePos, IM_COL32(255, 255, 0, 200), 3.0f);
        }

        // 3. Handle completion or cancellation of the drag.
        // We use Left-click to complete the link, matching ImNodes' default behavior.
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            int hoveredPinId = -1;
            if (ImNodes::IsPinHovered(&hoveredPinId) && hoveredPinId != -1)
            {
                // User dropped the link on a pin.
                auto srcPin = decodePinId(splittingFromAttrId);
                auto dstPin = decodePinId(hoveredPinId);

                // Ensure the connection is valid (Output -> Input).
                if (!srcPin.isInput && dstPin.isInput)
                {
                    auto srcNode = synth->getNodeIdForLogical(srcPin.logicalId);
                    auto dstNode = (dstPin.logicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical(dstPin.logicalId);

                    synth->connect(srcNode, srcPin.channel, dstNode, dstPin.channel);
                    graphNeedsRebuild = true;
                    pushSnapshot(); // Make it undoable
                }
            }

            // ALWAYS reset the state, whether the connection was successful or not.
            splittingFromAttrId = -1;
        }
        // Also allow cancellation with a right-click.
        else if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            splittingFromAttrId = -1; // Cancel the operation.
        }
    }
    // --- END OF NEW LOGIC ---

    // Open popup now (outside editor) if requested this frame
    if (showInsertNodePopup)
    {
        showInsertNodePopup = false;
        // Validate the link still exists
        if (pendingInsertLinkId != -1)
        {
            bool stillValid = (/* modLinkIdToRoute.count(pendingInsertLinkId) || */ linkIdToAttrs.count(pendingInsertLinkId));
            if (!stillValid)
            {
                juce::Logger::writeToLog("[InsertNode] Skipping popup: link disappeared this frame");
                pendingInsertLinkId = -1;
            }
        }
        if (pendingInsertLinkId != -1)
        {
            ImGui::OpenPopup("InsertNodeOnLinkPopup");
            // Consume the mouse release/click so the popup stays open
            ImGui::GetIO().WantCaptureMouse = true;
            juce::Logger::writeToLog("[InsertNode] Opened popup (post-editor)");
        }
        else
        {
            linkToInsertOn = {}; // safety
        }
        pendingInsertLinkId = -1;
    }

    // Fallback: If user right-clicked and a link was hovered this frame, open popup using cached hover
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)
        && lastHoveredLinkId != -1
        && !ImGui::IsPopupOpen("InsertNodeOnLinkPopup"))
    {
        int id = lastHoveredLinkId;
        linkToInsertOn = {}; linkToInsertOn.linkId = id;
        bool captured = false;
        // TODO: Handle modulation link deletion for new bus-based system
        // if (auto itM = modLinkIdToRoute.find(id); itM != modLinkIdToRoute.end())
        // {
        //     linkToInsertOn.isMod = true;
        //     int sL, sC, dL; juce::String paramId; std::tie(sL, sC, dL, paramId) = itM->second;
        //     linkToInsertOn.srcLogicalId = (juce::uint32) sL;
        //     linkToInsertOn.srcChan = sC;
        //     linkToInsertOn.dstLogicalId = (juce::uint32) dL;
        //     linkToInsertOn.paramId = paramId;
        //     captured = true;
        //     juce::Logger::writeToLog("[InsertNode][RC-Fallback] Mod link captured id=" + juce::String(id));
        // }
        // else 
        if (auto it = linkIdToAttrs.find(id); it != linkIdToAttrs.end())
        {
            linkToInsertOn.isMod = false;
            linkToInsertOn.srcPin = decodePinId(it->second.first);
            linkToInsertOn.dstPin = decodePinId(it->second.second);
            captured = true;
            juce::Logger::writeToLog("[InsertNode][RC-Fallback] Audio link captured id=" + juce::String(id));
        }
        if (captured)
        {
            ImGui::OpenPopup("InsertNodeOnLinkPopup");
            ImGui::GetIO().WantCaptureMouse = true;
            juce::Logger::writeToLog("[InsertNode][RC-Fallback] Opened popup");
        }
        else
        {
            linkToInsertOn.linkId = -1;
        }
    }
    // This function draws the popup if the popup is open.
    drawInsertNodeOnLinkPopup();

    // --- Cable Inspector: Stateless, rebuild-safe implementation ---
    hoveredLinkSrcId = 0;
    hoveredLinkDstId = 0;

    // Skip inspector if popups are open (graph is always in consistent state now)
    const bool anyPopupOpen = ImGui::IsPopupOpen("InsertNodeOnLinkPopup") || ImGui::IsPopupOpen("AddModulePopup");
    // Do not early-return here; we still need to finish the frame and close any ImGui scopes.

    if (!anyPopupOpen && isLinkHovered && hoveredLinkId != -1 && synth != nullptr)
    {
        // Safety: Re-verify link still exists in our mapping
        auto it = linkIdToAttrs.find(hoveredLinkId);
        if (it != linkIdToAttrs.end())
        {
            auto srcPin = decodePinId(it->second.first);
            auto dstPin = decodePinId(it->second.second);

            // Set highlight IDs for this frame only
            hoveredLinkSrcId = srcPin.logicalId;
            hoveredLinkDstId = (dstPin.logicalId == 0) ? kOutputHighlightId : dstPin.logicalId;

            // Query source module (no caching - stateless)
            if (auto* srcModule = synth->getModuleForLogical(srcPin.logicalId))
            {
                // Validate channel index
                const int numOutputs = srcModule->getTotalNumOutputChannels();
                if (srcPin.channel >= 0 && srcPin.channel < numOutputs)
                {
                    // Optional: Throttle value sampling to 60 Hz (every 16.67ms)
                    // For now, query every frame for responsive UI
                    const float liveValue = srcModule->getOutputChannelValue(srcPin.channel);
                    const juce::String srcName = srcModule->getName();
                    const juce::String srcLabel = srcModule->getAudioOutputLabel(srcPin.channel);

                    // Render tooltip (stateless - no caching)
                    ImGui::BeginTooltip();
                    ImGui::Text("Value: %.3f", liveValue);
                    ImGui::Text("From: %s (ID %u)", srcName.toRawUTF8(), (unsigned)srcPin.logicalId);
                    if (srcLabel.isNotEmpty())
                        ImGui::Text("Pin: %s", srcLabel.toRawUTF8());
                    ImGui::EndTooltip();
                }
            }
        }
    }
    

    // Update hovered node/link id for next frame (must be called outside editor scope)
    // Graph is always in consistent state since we rebuild at frame start
    int hv = -1;
    if (ImNodes::IsNodeHovered(&hv)) lastHoveredNodeId = hv; else lastHoveredNodeId = -1;
    
    int hl = -1;
    if (ImNodes::IsLinkHovered(&hl)) lastHoveredLinkId = hl; else lastHoveredLinkId = -1;
    

    // Shortcut: press 'I' while hovering a link to open Insert-on-Link popup (bypasses mouse handling)
    if (ImGui::IsKeyPressed(ImGuiKey_I) && lastHoveredLinkId != -1 && !ImGui::IsPopupOpen("InsertNodeOnLinkPopup"))
    {
        linkToInsertOn = {}; // reset
        linkToInsertOn.linkId = lastHoveredLinkId;
        bool captured = false;
        // TODO: Handle modulation link hover end for new bus-based system
        // if (auto itM = modLinkIdToRoute.find(lastHoveredLinkId); itM != modLinkIdToRoute.end())
        // {
        //     linkToInsertOn.isMod = true;
        //     int sL, sC, dL; juce::String paramId; std::tie(sL, sC, dL, paramId) = itM->second;
        //     linkToInsertOn.srcLogicalId = (juce::uint32) sL;
        //     linkToInsertOn.srcChan = sC;
        //     linkToInsertOn.dstLogicalId = (juce::uint32) dL;
        //     linkToInsertOn.paramId = paramId;
        //     captured = true;
        //     juce::Logger::writeToLog("[InsertNode][KeyI] Mod link captured id=" + juce::String(lastHoveredLinkId));
        // }
        // else 
        if (auto it = linkIdToAttrs.find(lastHoveredLinkId); it != linkIdToAttrs.end())
        {
            linkToInsertOn.isMod = false;
            linkToInsertOn.srcPin = decodePinId(it->second.first);
            linkToInsertOn.dstPin = decodePinId(it->second.second);
            captured = true;
            juce::Logger::writeToLog("[InsertNode][KeyI] Audio link captured id=" + juce::String(lastHoveredLinkId));
        }
        if (captured)
        {
            pendingInsertLinkId = lastHoveredLinkId;
            showInsertNodePopup = true; // will open next lines
        }
        else
        {
            linkToInsertOn.linkId = -1;
            juce::Logger::writeToLog("[InsertNode][KeyI] No link data found for id=" + juce::String(lastHoveredLinkId));
        }
    }

    // After editor pass, if we added/duplicated a node, take snapshot now that nodes exist
    if (snapshotAfterEditor)
    {
        snapshotAfterEditor = false;
        pushSnapshot();
    }

    if (synth != nullptr)
    {
        // No persistent panning state when zoom is disabled

        // Right-click on empty canvas -> Add module popup
        // Avoid passing nullptr to ImNodes::IsLinkHovered; some builds may write to the pointer
        int dummyHoveredLinkId = -1;
        const bool anyLinkHovered = ImNodes::IsLinkHovered(&dummyHoveredLinkId);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)
            && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
            && ! ImGui::IsAnyItemHovered()
            && !anyLinkHovered
            && !ImGui::IsPopupOpen("InsertNodeOnLinkPopup")
            && linkToInsertOn.linkId == -1) // avoid conflict with insert-on-link popup
        {
                ImGui::OpenPopup("AddModulePopup");
        }

        // --- REVISED AND IMPROVED "QUICK ADD" POPUP ---
        if (ImGui::BeginPopup("AddModulePopup"))
        {
            static char searchQuery[128] = "";

            // Auto-focus the search bar when the popup opens and clear any previous search
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere(0);
                searchQuery[0] = '\0';
            }
            
            ImGui::Text("Add Module");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::InputText("Search##addmodule", searchQuery, sizeof(searchQuery))) {
                // Text was changed
            }
            ImGui::PopItemWidth();
            ImGui::Separator();
            
            // --- PROBE TOOL ---
            if (ImGui::MenuItem("🔬 Probe Signal (Click any output pin)"))
            {
                isProbeModeActive = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Activate probe mode to instantly visualize any signal without manual patching.\nClick on any output pin to route it to the probe scope.");
            }
            ImGui::Separator();

            auto addAtMouse = [this](const char* type) {
                auto nodeId = synth->addModule(type);
                const int logicalId = (int) synth->getLogicalIdForNode (nodeId);
                // This places the new node exactly where the user right-clicked
                pendingNodeScreenPositions[logicalId] = ImGui::GetMousePosOnOpeningCurrentPopup();
                
                // Special handling for recorder module
                if (juce::String(type).equalsIgnoreCase("recorder"))
                {
                    if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(synth->getModuleForLogical((juce::uint32)logicalId)))
                    {
                        recorder->setPropertiesFile(PresetCreatorApplication::getApp().getProperties());
                    }
                }
                
                // Give comment nodes a default size to prevent feedback loop
                if (juce::String(type).equalsIgnoreCase("comment"))
                {
                    pendingNodeSizes[logicalId] = ImVec2(250.f, 150.f);
                }
                
                snapshotAfterEditor = true;
                ImGui::CloseCurrentPopup();
            };
            
            juce::String filter(searchQuery);

            ImGui::BeginChild("ModuleList", ImVec2(280, 350), true);

            if (filter.isEmpty())
            {
                // --- BROWSE MODE (No text in search bar) ---
                if (ImGui::BeginMenu("Sources")) {
                    if (ImGui::MenuItem("Audio Input")) addAtMouse("audio_input");
                    if (ImGui::MenuItem("VCO")) addAtMouse("vco");
                    if (ImGui::MenuItem("Polyphonic VCO")) addAtMouse("polyvco");
                    if (ImGui::MenuItem("Noise")) addAtMouse("noise");
                    if (ImGui::MenuItem("Sequencer")) addAtMouse("sequencer");
                    if (ImGui::MenuItem("Multi Sequencer")) addAtMouse("multi_sequencer");
                    if (ImGui::MenuItem("Snapshot Sequencer")) addAtMouse("snapshot_sequencer");
                    if (ImGui::MenuItem("Stroke Sequencer")) addAtMouse("stroke_sequencer");
                    if (ImGui::MenuItem("Value")) addAtMouse("value");
                    if (ImGui::MenuItem("Sample Loader")) addAtMouse("sample_loader");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("MIDI Family")) {
                    if (ImGui::MenuItem("MIDI CV")) addAtMouse("midi_cv");
                    if (ImGui::MenuItem("MIDI Player")) addAtMouse("midi_player");
                    ImGui::Separator();
                    if (ImGui::MenuItem("MIDI Faders")) addAtMouse("midi_faders");
                    if (ImGui::MenuItem("MIDI Knobs")) addAtMouse("midi_knobs");
                    if (ImGui::MenuItem("MIDI Buttons")) addAtMouse("midi_buttons");
                    if (ImGui::MenuItem("MIDI Jog Wheel")) addAtMouse("midi_jog_wheel");
                    if (ImGui::MenuItem("MIDI Pads")) addAtMouse("midi_pads");
                    ImGui::Separator();
                    if (ImGui::MenuItem("MIDI Logger")) addAtMouse("midi_logger");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("TTS")) {
                    if (ImGui::MenuItem("TTS Performer")) addAtMouse("tts_performer");
                    if (ImGui::MenuItem("Vocal Tract Filter")) addAtMouse("vocal_tract_filter");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Physics Family")) {
                    if (ImGui::MenuItem("Physics")) addAtMouse("physics");
                    if (ImGui::MenuItem("Animation")) addAtMouse("animation");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Effects")) {
                    if (ImGui::MenuItem("VCF")) addAtMouse("vcf");
                    if (ImGui::MenuItem("Delay")) addAtMouse("delay");
                    if (ImGui::MenuItem("Reverb")) addAtMouse("reverb");
                    if (ImGui::MenuItem("Chorus")) addAtMouse("chorus");
                    if (ImGui::MenuItem("Phaser")) addAtMouse("phaser");
                    if (ImGui::MenuItem("Compressor")) addAtMouse("compressor");
                    if (ImGui::MenuItem("Recorder")) addAtMouse("recorder");
                    if (ImGui::MenuItem("Limiter")) addAtMouse("limiter");
                    if (ImGui::MenuItem("Noise Gate")) addAtMouse("gate");
                    if (ImGui::MenuItem("Drive")) addAtMouse("drive");
                    if (ImGui::MenuItem("Graphic EQ")) addAtMouse("graphic_eq");
                    if (ImGui::MenuItem("Waveshaper")) addAtMouse("waveshaper");
                    if (ImGui::MenuItem("8-Band Shaper")) addAtMouse("8bandshaper");
                    if (ImGui::MenuItem("Granulator")) addAtMouse("granulator");
                    if (ImGui::MenuItem("Harmonic Shaper")) addAtMouse("harmonic_shaper");
                    if (ImGui::MenuItem("Time/Pitch Shifter")) addAtMouse("timepitch");
                    if (ImGui::MenuItem("De-Crackle")) addAtMouse("de_crackle");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Modulators")) {
                    if (ImGui::MenuItem("LFO")) addAtMouse("lfo");
                    if (ImGui::MenuItem("ADSR")) addAtMouse("adsr");
                    if (ImGui::MenuItem("Random")) addAtMouse("random");
                    if (ImGui::MenuItem("S&H")) addAtMouse("s_and_h");
                    if (ImGui::MenuItem("Tempo Clock")) addAtMouse("tempo_clock");
                    if (ImGui::MenuItem("Function Generator")) addAtMouse("function_generator");
                    if (ImGui::MenuItem("Shaping Oscillator")) addAtMouse("shaping_oscillator");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Utilities & Logic")) {
                    if (ImGui::MenuItem("VCA")) addAtMouse("vca");
                    if (ImGui::MenuItem("Mixer")) addAtMouse("mixer");
                    if (ImGui::MenuItem("CV Mixer")) addAtMouse("cv_mixer");
                    if (ImGui::MenuItem("Track Mixer")) addAtMouse("track_mixer");
                    if (ImGui::MenuItem("Attenuverter")) addAtMouse("attenuverter");
                    if (ImGui::MenuItem("Lag Processor")) addAtMouse("lag_processor");
                    if (ImGui::MenuItem("Math")) addAtMouse("math");
                    if (ImGui::MenuItem("Map Range")) addAtMouse("map_range");
                    if (ImGui::MenuItem("Quantizer")) addAtMouse("quantizer");
                    if (ImGui::MenuItem("Rate")) addAtMouse("rate");
                    if (ImGui::MenuItem("Comparator")) addAtMouse("comparator");
                    if (ImGui::MenuItem("Logic")) addAtMouse("logic");
                    if (ImGui::MenuItem("Clock Divider")) addAtMouse("clock_divider");
                    if (ImGui::MenuItem("Sequential Switch")) addAtMouse("sequential_switch");
                    if (ImGui::MenuItem("Comment")) addAtMouse("comment");
                    if (ImGui::MenuItem("Best Practice")) addAtMouse("best_practice");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Analysis")) {
                    if (ImGui::MenuItem("Scope")) addAtMouse("scope");
                    if (ImGui::MenuItem("Debug")) addAtMouse("debug");
                    if (ImGui::MenuItem("Input Debug")) addAtMouse("input_debug");
                    if (ImGui::MenuItem("Frequency Graph")) addAtMouse("frequency_graph");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("VST Plugins")) {
                    addPluginModules(); // Re-use your existing plugin menu logic
                    ImGui::EndMenu();
                }
            }
            else
            {
                // --- SEARCH MODE (Text has been entered) ---
                // Use the new registry to get display names and internal types
                for (const auto& entry : getModuleRegistry())
                {
                    const juce::String& displayName = entry.first;
                    const char* internalType = entry.second.first;
                    const char* description = entry.second.second;

                    // Search against the display name, not the internal type
                    if (displayName.containsIgnoreCase(filter))
                    {
                        if (ImGui::Selectable(displayName.toRawUTF8()))
                        {
                            // Use the correct internal type name!
                            addAtMouse(internalType);
                        }
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(description);
                            ImGui::EndTooltip();
                        }
                    }
                }
            }

            ImGui::EndChild();
            ImGui::EndPopup();
        }

        // Helper functions are now class methods

        // Handle user-created links (must be called after EndNodeEditor)
        int startAttr = 0, endAttr = 0;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr))
        {
            auto startPin = decodePinId(startAttr);
            auto endPin = decodePinId(endAttr);
            auto srcPin = startPin.isInput ? endPin : startPin;
            auto dstPin = startPin.isInput ? startPin : endPin;

            // Ensure connection is always Output -> Input
            if (!srcPin.isInput && dstPin.isInput)
            {
                PinDataType srcType = getPinDataTypeForPin(srcPin);
                PinDataType dstType = getPinDataTypeForPin(dstPin);

                bool conversionHandled = false;

                // Determine if a converter is needed based on pin types
                if (srcType == PinDataType::Audio && dstType == PinDataType::CV)
                {
                    insertNodeBetween("Attenuverter", srcPin, dstPin);
                    conversionHandled = true;
                }
                else if (srcType == PinDataType::CV && dstType == PinDataType::Gate)
                {
                    insertNodeBetween("Comparator", srcPin, dstPin);
                    conversionHandled = true;
                }
                else if (srcType == PinDataType::Audio && dstType == PinDataType::Gate)
                {
                    insertNodeBetween("Comparator", srcPin, dstPin);
                    conversionHandled = true;
                }
                else if (srcType == PinDataType::Raw && dstType != PinDataType::Raw)
                {
                    insertNodeBetween("MapRange", srcPin, dstPin);
                    conversionHandled = true;
                }

                if (conversionHandled)
                {
                    graphNeedsRebuild = true;
                    pushSnapshot();
                }
                else
                {
                    // All other combinations are considered directly compatible.
                    auto srcNode = synth->getNodeIdForLogical(srcPin.logicalId);
                    auto dstNode = (dstPin.logicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical(dstPin.logicalId);

                    synth->connect(srcNode, srcPin.channel, dstNode, dstPin.channel);
                    // Immediate commit for RecordModuleProcessor filename update
                    synth->commitChanges();

                    if (auto* dstModule = synth->getModuleForLogical(dstPin.logicalId)) {
                        if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(dstModule)) {
                            juce::String sourceName;
                            if (auto* srcModule = synth->getModuleForLogical(srcPin.logicalId)) {
                                sourceName = srcModule->getName();
                            }
                            recorder->updateSuggestedFilename(sourceName);
                        }
                    }

                    pushSnapshot();
                }
            }
        }

        // Handle link deletion (single)
        int linkId = 0;
        if (ImNodes::IsLinkDestroyed(&linkId))
        {
            if (auto it = linkIdToAttrs.find(linkId); it != linkIdToAttrs.end())
            {
                auto srcPin = decodePinId(it->second.first);
                auto dstPin = decodePinId(it->second.second);
                
                auto srcNode = synth->getNodeIdForLogical(srcPin.logicalId);
                auto dstNode = (dstPin.logicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical(dstPin.logicalId);

                // Debug log disconnect intent
                juce::Logger::writeToLog(
                    juce::String("[LinkDelete] src(lid=") + juce::String((int)srcPin.logicalId) + ",ch=" + juce::String(srcPin.channel) +
                    ") -> dst(lid=" + juce::String((int)dstPin.logicalId) + ",ch=" + juce::String(dstPin.channel) + ")");

                synth->disconnect(srcNode, srcPin.channel, dstNode, dstPin.channel);
                
                // Immediate commit for RecordModuleProcessor filename update
                synth->commitChanges();
                
                // After disconnecting, tell the recorder to update (pass empty string for unconnected)
                if (auto* dstModule = synth->getModuleForLogical(dstPin.logicalId))
                {
                    if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(dstModule))
                    {
                        recorder->updateSuggestedFilename(""); // Empty = unconnected
                    }
                }
                
                pushSnapshot();
                linkIdToAttrs.erase (it);
            }
        }
        // Handle link deletion (multi-select via Delete)

        // Keyboard shortcuts
        // Only process global keyboard shortcuts if no ImGui widget wants the keyboard
        if (!ImGui::GetIO().WantCaptureKeyboard)
        {
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            const bool shift = ImGui::GetIO().KeyShift;
            const bool alt = ImGui::GetIO().KeyAlt;
            
            if (ctrl && ImGui::IsKeyPressed (ImGuiKey_S)) { startSaveDialog(); }
            if (ctrl && ImGui::IsKeyPressed (ImGuiKey_O)) { startLoadDialog(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P)) { handleRandomizePatch(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_M)) { handleRandomizeConnections(); }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_B)) { handleBeautifyLayout(); }
            if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_R, false)) { handleRecordOutput(); }
        
        // M: Mute/Bypass selected nodes (without Ctrl modifier)
        if (!ctrl && !alt && !shift && ImGui::IsKeyPressed(ImGuiKey_M, false) && ImNodes::NumSelectedNodes() > 0)
        {
            handleMuteToggle();
        }
        
        // Ctrl + A: Select All
        if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            if (synth != nullptr)
            {
                const auto& modules = synth->getModulesInfo();
                std::vector<int> allNodeIds;
                allNodeIds.push_back(0); // Include output node
                for (const auto& mod : modules)
                {
                    allNodeIds.push_back((int)mod.first);
                }
                ImNodes::ClearNodeSelection();
                for (int id : allNodeIds)
                {
                    ImNodes::SelectNode(id);
                }
            }
        }
        
        // Ctrl + R: Reset selected node(s) to default parameters
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            const int numSelected = ImNodes::NumSelectedNodes();
            if (numSelected > 0 && synth != nullptr)
            {
                // Create a single undo state for the entire operation
                pushSnapshot();
                
                std::vector<int> selectedNodeIds(numSelected);
                ImNodes::GetSelectedNodes(selectedNodeIds.data());

                for (int lid : selectedNodeIds)
                {
                    if (auto* module = synth->getModuleForLogical((juce::uint32)lid))
                    {
                        // Get all parameters for this module
                        auto& params = module->getParameters();
                        for (auto* paramBase : params)
                        {
                            // Cast to a ranged parameter to access default values
                            if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(paramBase))
                            {
                                // Get the default value (normalized 0-1) and apply it
                                param->setValueNotifyingHost(param->getDefaultValue());
                            }
                        }
                        juce::Logger::writeToLog("[Reset] Reset parameters for node " + juce::String(lid));
                    }
                }
            }
        }
        
        // O: Connect selected to Output
        if (!ctrl && !alt && !shift && ImGui::IsKeyPressed(ImGuiKey_O, false) && ImNodes::NumSelectedNodes() == 1)
        {
            if (synth != nullptr)
            {
                int selectedId;
                ImNodes::GetSelectedNodes(&selectedId);
                if (selectedId != 0)
                {
                    synth->connect(synth->getNodeIdForLogical(selectedId), 0, synth->getOutputNodeID(), 0);
                    synth->connect(synth->getNodeIdForLogical(selectedId), 1, synth->getOutputNodeID(), 1);
                    graphNeedsRebuild = true;
                    pushSnapshot();
                }
            }
        }
        
        // Alt + D: Disconnect selected nodes
        if (alt && ImGui::IsKeyPressed(ImGuiKey_D, false) && ImNodes::NumSelectedNodes() > 0)
        {
            if (synth != nullptr)
            {
                std::vector<int> selectedNodeIds(ImNodes::NumSelectedNodes());
                ImNodes::GetSelectedNodes(selectedNodeIds.data());
                for (int id : selectedNodeIds)
                {
                    synth->clearConnectionsForNode(synth->getNodeIdForLogical(id));
                }
                graphNeedsRebuild = true;
                pushSnapshot();
            }
        }
        
        // --- REVISED 'F' and 'Home' KEY LOGIC ---
        auto frameNodes = [&](const std::vector<int>& nodeIds) {
            if (nodeIds.empty() || synth == nullptr) return;

            juce::Rectangle<float> bounds;
            bool foundAny = false;
            
            // Build a set of valid node IDs for checking
            std::unordered_set<int> validNodes;
            validNodes.insert(0); // Output node
            for (const auto& mod : synth->getModulesInfo())
                validNodes.insert((int)mod.first);
            
            for (size_t i = 0; i < nodeIds.size(); ++i)
            {
                // Ensure the node exists before getting its position
                if (validNodes.find(nodeIds[i]) != validNodes.end())
                {
                    ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodeIds[i]);
                    if (!foundAny)
                    {
                        bounds = juce::Rectangle<float>(pos.x, pos.y, 1, 1);
                        foundAny = true;
                    }
                    else
                    {
                        bounds = bounds.getUnion(juce::Rectangle<float>(pos.x, pos.y, 1, 1));
                    }
                }
            }

            if (!foundAny) return;

            // Add some padding to the bounds
            if (!nodeIds.empty() && validNodes.find(nodeIds[0]) != validNodes.end())
                bounds = bounds.expanded(ImNodes::GetNodeDimensions(nodeIds[0]).x, ImNodes::GetNodeDimensions(nodeIds[0]).y);
            
            ImVec2 center((bounds.getX() + bounds.getRight()) * 0.5f, (bounds.getY() + bounds.getBottom()) * 0.5f);
            ImNodes::EditorContextResetPanning(center);
        };

        // F: Frame Selected
        if (!ctrl && !alt && !shift && ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            const int numSelected = ImNodes::NumSelectedNodes();
            if (numSelected > 0)
            {
                std::vector<int> selectedNodeIds(numSelected);
                ImNodes::GetSelectedNodes(selectedNodeIds.data());
                frameNodes(selectedNodeIds);
            }
        }

        // Home and Ctrl+Home: Frame All / Reset to Origin
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
        {
            if (ctrl) // Ctrl+Home: Reset to origin
            {
                ImNodes::EditorContextResetPanning(ImVec2(0, 0));
            }
            else // Home: Frame all
            {
                if (synth != nullptr)
                {
                    auto modules = synth->getModulesInfo();
                    std::vector<int> allNodeIds;
                    allNodeIds.push_back(0); // Include output node
                    for (const auto& mod : modules)
                    {
                        allNodeIds.push_back((int)mod.first);
                    }
                    frameNodes(allNodeIds);
                }
            }
        }
        
        // Debug menu (Ctrl+Shift+D)
        if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_D)) { showDebugMenu = !showDebugMenu; }

        // Undo / Redo (Ctrl+Z / Ctrl+Y)
    if (ctrl && ImGui::IsKeyPressed (ImGuiKey_Z))
        {
            if (undoStack.size() > 1)
            {
                Snapshot current = undoStack.back();
                redoStack.push_back (current);
                undoStack.pop_back();
                restoreSnapshot (undoStack.back());
                // After a restore, clear transient link maps only; keep pending positions so they apply next frame
                linkIdToAttrs.clear();
                // modLinkIdToRoute.clear(); // TODO: Remove when fully migrated
            }
        }
        if (ctrl && ImGui::IsKeyPressed (ImGuiKey_Y))
        {
            if (! redoStack.empty())
            {
                Snapshot s = redoStack.back(); redoStack.pop_back();
                restoreSnapshot (s);
                undoStack.push_back (s);
                linkIdToAttrs.clear();
                // modLinkIdToRoute.clear(); // TODO: Remove when fully migrated
            }
        }

        // Duplicate selected nodes (Ctrl+D) and Duplicate with connections (Shift+D)
        if ((ctrl || ImGui::GetIO().KeyShift) && ImGui::IsKeyPressed (ImGuiKey_D))
        {
            const int n = ImNodes::NumSelectedNodes();
            if (n > 0)
            {
                std::vector<int> sel((size_t) n);
                ImNodes::GetSelectedNodes(sel.data());
                for (int oldId : sel)
                {
                    if (oldId == 0) continue;
                    const juce::String type = getTypeForLogical ((juce::uint32) oldId);
                    if (type.isEmpty()) continue;
                    auto newNodeId = synth->addModule (type);
                    graphNeedsRebuild = true;
                    const juce::uint32 newLogical = synth->getLogicalIdForNode (newNodeId);
                    if (newLogical != 0)
                    {
                        if (auto* src = synth->getModuleForLogical ((juce::uint32) oldId))
                            if (auto* dst = synth->getModuleForLogical (newLogical))
                                dst->getAPVTS().replaceState (src->getAPVTS().copyState());
                        // Position offset
                        ImVec2 pos = ImNodes::GetNodeGridSpacePos (oldId);
                        pendingNodePositions[(int) newLogical] = ImVec2 (pos.x + 40.0f, pos.y + 40.0f);

                        // If Shift is held: duplicate connections into and out of this node
                        if (!ctrl && ImGui::GetIO().KeyShift)
                        {
                            const auto oldNode = synth->getNodeIdForLogical ((juce::uint32) oldId);
                            const auto newNode = newNodeId;
                            // Duplicate audio/CV connections
                            for (const auto& c : synth->getConnectionsInfo())
                            {
                                // Outgoing from old -> someone
                                if ((int) c.srcLogicalId == oldId)
                                {
                                    auto dstNode = (c.dstLogicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical (c.dstLogicalId);
                                    synth->connect (newNode, c.srcChan, dstNode, c.dstChan);
                                }
                                // Incoming from someone -> old
                                if ((int) c.dstLogicalId == oldId)
                                {
                                    auto srcNode = synth->getNodeIdForLogical (c.srcLogicalId);
                                    synth->connect (srcNode, c.srcChan, newNode, c.dstChan);
                                }
                            }
                            // TODO: Implement modulation route duplication for new bus-based system
                        }
                    }
                }
                pushSnapshot();
            }
        }
        
        } // End of keyboard shortcuts (WantCaptureKeyboard check)

        // Update selection for parameter panel
        {
            int selCount = ImNodes::NumSelectedNodes();
            if (selCount > 0)
            {
                std::vector<int> ids((size_t) selCount);
                ImNodes::GetSelectedNodes(ids.data());
                selectedLogicalId = ids.back();
            }
            else
            {
                selectedLogicalId = 0;
            }
        }

        handleDeletion();
    }

    // === MIDI DEVICE MANAGER WINDOW ===
    if (showMidiDeviceManager)
    {
        if (ImGui::Begin("MIDI Device Manager", &showMidiDeviceManager, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "MIDI Input Devices");
            ImGui::Separator();
            
            // Access MidiDeviceManager from PresetCreatorComponent
            auto* presetCreator = dynamic_cast<PresetCreatorComponent*>(getParentComponent());
            if (presetCreator && presetCreator->midiDeviceManager)
            {
                auto& midiMgr = *presetCreator->midiDeviceManager;
                const auto& devices = midiMgr.getDevices();
                
                if (devices.empty())
                {
                    ImGui::TextDisabled("No MIDI devices found");
                }
                else
                {
                    ImGui::Text("Found %d device(s):", (int)devices.size());
                    ImGui::Spacing();
                    
                    // Display each device
                    for (const auto& device : devices)
                    {
                        ImGui::PushID(device.identifier.toRawUTF8());
                        
                        // Checkbox to enable/disable device
                        bool enabled = device.enabled;
                        if (ImGui::Checkbox("##enabled", &enabled))
                        {
                            if (enabled)
                                midiMgr.enableDevice(device.identifier);
                            else
                                midiMgr.disableDevice(device.identifier);
                        }
                        
                        ImGui::SameLine();
                        
                        // Device name
                        ImGui::Text("%s", device.name.toRawUTF8());
                        
                        // Activity indicator
                        auto activity = midiMgr.getDeviceActivity(device.identifier);
                        if (activity.lastMessageTime > 0)
                        {
                            ImGui::SameLine();
                            float timeSinceMessage = (juce::Time::getMillisecondCounter() - activity.lastMessageTime) / 1000.0f;
                            if (timeSinceMessage < 1.0f)
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255));
                                ImGui::Text("ACTIVE");
                                ImGui::PopStyleColor();
                            }
                            else
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 100, 100, 255));
                                ImGui::Text("idle");
                                ImGui::PopStyleColor();
                            }
                        }
                        
                        ImGui::PopID();
                    }
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                // Rescan button
                if (ImGui::Button("Rescan Devices"))
                {
                    midiMgr.scanDevices();
                }
                
                ImGui::SameLine();
                
                // Enable/Disable all buttons
                if (ImGui::Button("Enable All"))
                {
                    midiMgr.enableAllDevices();
                }
                
                ImGui::SameLine();
                
                if (ImGui::Button("Disable All"))
                {
                    midiMgr.disableAllDevices();
                }
            }
            else
            {
                ImGui::TextDisabled("MIDI Manager not available");
            }
        }
        ImGui::End();
    }

    // === DEBUG WINDOW ===
    if (showDebugMenu)
    {
        if (ImGui::Begin("System Diagnostics", &showDebugMenu))
        {
            if (synth != nullptr)
            {
                ImGui::Text("=== SYSTEM OVERVIEW ===");
                if (ImGui::Button("Refresh"))
                {
                    // Force refresh of diagnostics
                }
                
                
                // System diagnostics
                ImGui::Text("System State:");
                juce::String systemDiag = synth->getSystemDiagnostics();
                ImGui::TextWrapped("%s", systemDiag.toUTF8());
                
                
                // Module selector
                ImGui::Text("Module Diagnostics:");
                auto modules = synth->getModulesInfo();
                if (!modules.empty())
                {
                    static int selectedModuleIndex = 0;
                    if (selectedModuleIndex >= (int)modules.size()) selectedModuleIndex = 0;
                    
                    juce::String moduleList = "";
                    for (size_t i = 0; i < modules.size(); ++i)
                    {
                        if (i > 0) moduleList += "\0";
                        moduleList += "Logical " + juce::String((int)modules[i].first) + ": " + modules[i].second;
                    }
                    moduleList += "\0";
                    
                    if (ImGui::Combo("Select Module", &selectedModuleIndex, moduleList.toUTF8()))
                    {
                        if (selectedModuleIndex < (int)modules.size())
                        {
                            juce::String moduleDiag = synth->getModuleDiagnostics(modules[selectedModuleIndex].first);
                            ImGui::TextWrapped("%s", moduleDiag.toUTF8());
                        }
                    }
                }
                else
                {
                    ImGui::Text("No modules found.");
                }
            }
            else
            {
                ImGui::Text("No synth processor available.");
            }
        }
        ImGui::End();
    }

    // Keyboard Shortcuts Help Window (F1)
    if (showShortcutsWindow)
    {
        ImGui::Begin("Keyboard Shortcuts", &showShortcutsWindow, ImGuiWindowFlags_AlwaysAutoResize);
        
        // --- NEW, COMPREHENSIVE SHORTCUT LIST ---

        ImGui::Text("Patch & File Management");
        ImGui::Separator();
        ImGui::BulletText("Ctrl + S: Save Preset.");
        ImGui::BulletText("Ctrl + O: Load Preset.");
        ImGui::BulletText("Ctrl + Z: Undo last action.");
        ImGui::BulletText("Ctrl + Y: Redo last action.");
        ImGui::BulletText("Ctrl + P: Generate a new random patch.");

        ImGui::Spacing();
        ImGui::Text("Node Creation & Deletion");
        ImGui::Separator();
        ImGui::BulletText("Right-click canvas: Open Quick Add menu to create a node.");
        ImGui::BulletText("Delete: Delete selected nodes and links.");
        ImGui::BulletText("Shift + Delete: Bypass-delete selected node(s), preserving signal chain.");
        ImGui::BulletText("Ctrl + D: Duplicate selected node(s).");
        ImGui::BulletText("Shift + D: Duplicate selected node(s) with their connections.");
        ImGui::BulletText("Ctrl + Shift + M: Collapse selected nodes into a new 'Meta Module'.");

        ImGui::Spacing();
        ImGui::Text("Connections & Signal Flow");
        ImGui::Separator();
        ImGui::BulletText("Right-click canvas -> Probe Signal: Enter Probe Mode.");
        ImGui::BulletText("  (In Probe Mode) Left-click output pin: Instantly view signal in the Probe Scope.");
        ImGui::BulletText("Right-click link: Open menu to insert a node on that cable.");
        ImGui::BulletText("I key (while hovering link): Open 'Insert Node' menu for that cable.");
        ImGui::BulletText("Ctrl + Middle-click link: Split a new cable from a connected output pin.");
        ImGui::BulletText("O key (with one node selected): Connect node's output to the Main Output.");
        ImGui::BulletText("Alt + D: Disconnect all cables from selected node(s).");
        ImGui::BulletText("Ctrl + M: Randomize connections between existing nodes.");
        ImGui::BulletText("C key (multi-select): Chain selected nodes (L->L, R->R).");
        ImGui::BulletText("G, B, Y, R keys (multi-select): Chain pins by type (Audio, CV, Gate, Raw).");
        
        ImGui::Spacing();
        ImGui::Text("Navigation & View");
        ImGui::Separator();
        ImGui::BulletText("F: Frame (zoom to fit) selected nodes.");
        ImGui::BulletText("Home: Frame all nodes in the patch.");
        ImGui::BulletText("Ctrl + Home: Reset view panning to the origin (0,0).");
        ImGui::BulletText("Ctrl + B: Automatically arrange nodes for a clean layout ('Beautify').");
        ImGui::BulletText("Ctrl + A: Select all nodes.");

        ImGui::Spacing();
        ImGui::Text("Parameter & Settings");
        ImGui::Separator();
        ImGui::BulletText("M key (with node(s) selected): Mute or Bypass the selected node(s).");
        ImGui::BulletText("Ctrl + R (with node(s) selected): Reset parameters of selected node(s) to default.");
        ImGui::BulletText("Ctrl + Shift + C: Copy selected node's settings to clipboard.");
        ImGui::BulletText("Ctrl + Shift + V: Paste settings to selected node (must be same type).");
        ImGui::BulletText("Mouse Wheel (on slider): Fine-tune parameter value.");

        ImGui::Spacing();
        ImGui::Text("General & Debugging");
        ImGui::Separator();
        ImGui::BulletText("Ctrl + R (no node selected): Insert a Recorder tapped into the Main Output.");
        ImGui::BulletText("Ctrl + Shift + D: Show System Diagnostics window.");
        ImGui::BulletText("F1: Toggle this help window.");
        
        ImGui::End();
    }

    ImGui::End();
    // drawPendingModPopup(); // TODO: Remove when fully migrated

    // No deferred snapshots; unified pre-state strategy
}

void ImGuiNodeEditorComponent::pushSnapshot()
{
    // Ensure any newly scheduled positions are flushed into the current UI state
    // by applying them immediately before capturing.
    if (! pendingNodePositions.empty())
    {
        // Temporarily mask rebuild flag to avoid ImNodes queries during capture
        const bool rebuilding = graphNeedsRebuild.load();
        if (rebuilding) {
            // getUiValueTree will still avoid ImNodes now, but assert safety
        }
        juce::ValueTree applied = getUiValueTree();
        for (const auto& kv : pendingNodePositions)
        {
            // Overwrite the entry for this node if present
            for (int i = 0; i < applied.getNumChildren(); ++i)
            {
                auto n = applied.getChild(i);
                if (n.hasType("node") && (int) n.getProperty("id", -1) == kv.first)
                { n.setProperty("x", kv.second.x, nullptr); n.setProperty("y", kv.second.y, nullptr); break; }
            }
        }
        // Do not commit pending positions of (0,0) which are placeholders
        for (int i = 0; i < applied.getNumChildren(); ++i)
        {
            auto n = applied.getChild(i);
            if (! n.hasType("node")) continue;
            const float x = (float) n.getProperty("x", 0.0f);
            const float y = (float) n.getProperty("y", 0.0f);
            if (x == 0.0f && y == 0.0f) {
                // Try to recover from last-known or pending
                const int nid = (int) n.getProperty("id", -1);
                auto itL = lastKnownNodePositions.find(nid);
                if (itL != lastKnownNodePositions.end()) { n.setProperty("x", itL->second.x, nullptr); n.setProperty("y", itL->second.y, nullptr); }
                else if (auto itP = pendingNodePositions.find(nid); itP != pendingNodePositions.end()) { n.setProperty("x", itP->second.x, nullptr); n.setProperty("y", itP->second.y, nullptr); }
            }
        }
        Snapshot s; s.uiState = applied; if (synth != nullptr) synth->getStateInformation (s.synthState);
        undoStack.push_back (std::move (s)); redoStack.clear();
        isPatchDirty = true; // Mark patch as dirty
        return;
    }
    Snapshot s; s.uiState = getUiValueTree();
    if (synth != nullptr) synth->getStateInformation (s.synthState);
    undoStack.push_back (std::move (s));
    redoStack.clear();
    
    // Mark patch as dirty whenever a change is made
    isPatchDirty = true;
}

void ImGuiNodeEditorComponent::restoreSnapshot (const Snapshot& s)
{
    if (synth != nullptr && s.synthState.getSize() > 0)
        synth->setStateInformation (s.synthState.getData(), (int) s.synthState.getSize());
    // Restore UI positions exactly as saved
    applyUiValueTreeNow (s.uiState);
}

juce::String ImGuiNodeEditorComponent::getTypeForLogical (juce::uint32 logicalId) const
{
    if (synth == nullptr) return {};
    for (const auto& p : synth->getModulesInfo())
        if (p.first == logicalId) return p.second;
    return {};
}

// Parameters are now drawn inline within each node; side panel removed


juce::ValueTree ImGuiNodeEditorComponent::getUiValueTree()
{
    juce::ValueTree ui ("NodeEditorUI");
    if (synth == nullptr) return ui;
    // Save node positions
    for (const auto& mod : synth->getModulesInfo())
    {
        const int nid = (int) mod.first;
        
        // Prefer cached position if available; never query ImNodes while rebuilding
        ImVec2 pos;
        if (lastKnownNodePositions.count(nid) > 0)
        {
            pos = lastKnownNodePositions[nid];
        }
        else if (graphNeedsRebuild.load())
        {
            // Fallback to any pending position queued for this node
            auto it = pendingNodePositions.find(nid);
            pos = (it != pendingNodePositions.end()) ? it->second : ImVec2(0.0f, 0.0f);
        }
        else
        {
            pos = ImNodes::GetNodeGridSpacePos(nid);
        }
        
        juce::ValueTree n ("node");
        n.setProperty ("id", nid, nullptr);
        n.setProperty ("x", pos.x, nullptr);
        n.setProperty ("y", pos.y, nullptr);
        
        // --- FIX: Save muted/bypassed state ---
        // If this node's ID is in our map of muted nodes, add the property to the XML
        if (mutedNodeStates.count(nid) > 0)
        {
            n.setProperty("muted", true, nullptr);
        }
        
        ui.addChild (n, -1, nullptr);
    }
    
    // --- FIX: Explicitly save the output node position (ID 0) ---
    // The main output node is not part of getModulesInfo(), so we need to save it separately
    
    // Prefer cached output position; avoid ImNodes when rebuilding
    ImVec2 outputPos;
    if (lastKnownNodePositions.count(0) > 0)
        outputPos = lastKnownNodePositions[0];
    else if (graphNeedsRebuild.load())
    {
        auto it0 = pendingNodePositions.find(0);
        outputPos = (it0 != pendingNodePositions.end()) ? it0->second : ImVec2(0.0f, 0.0f);
    }
    else
        outputPos = ImNodes::GetNodeGridSpacePos(0);
    
    juce::ValueTree outputNode("node");
    outputNode.setProperty("id", 0, nullptr);
    outputNode.setProperty("x", outputPos.x, nullptr);
    outputNode.setProperty("y", outputPos.y, nullptr);
    ui.addChild(outputNode, -1, nullptr);
    // --- END OF FIX ---
    
    return ui;
}

void ImGuiNodeEditorComponent::applyUiValueTreeNow (const juce::ValueTree& uiState)
{
    if (! uiState.isValid() || synth == nullptr) return;
    
    juce::Logger::writeToLog("[UI_RESTORE] Applying UI ValueTree now...");

    // This is the core of the crash: the synth graph has already been rebuilt by setStateInformation.
    // We must clear our stale UI data (like muted nodes) before applying the new state from the preset.
    mutedNodeStates.clear();
    
    auto nodes = uiState; // expect tag NodeEditorUI
    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        auto n = nodes.getChild(i);
        
        if (! n.hasType ("node")) continue;
        const int nid = (int) n.getProperty ("id", 0);

        // ========================= THE FIX STARTS HERE =========================
        //
        // Before applying any property, VERIFY that this node ID actually exists
        // in the synth. This prevents crashes when loading presets that contain
        // modules which are not available in the current build.
        //
        bool nodeExistsInSynth = (nid == 0); // Node 0 is always the output node.
        if (!nodeExistsInSynth) {
            for (const auto& modInfo : synth->getModulesInfo()) {
                if ((int)modInfo.first == nid) {
                    nodeExistsInSynth = true;
                    break;
                }
            }
        }

        if (!nodeExistsInSynth)
        {
            juce::Logger::writeToLog("[UI_RESTORE] WARNING: Skipping UI properties for non-existent node ID " + juce::String(nid) + ". The module may be missing or failed to load.");
            continue; // Skip to the next node in the preset.
        }
        // ========================== END OF FIX ==========================

        const float x = (float) n.getProperty ("x", 0.0f);
        const float y = (float) n.getProperty ("y", 0.0f);
        if (!(x == 0.0f && y == 0.0f))
        {
            pendingNodePositions[nid] = ImVec2(x, y);
            juce::Logger::writeToLog("[UI_RESTORE] Queued position for node " + juce::String(nid) + ": (" + juce::String(x) + ", " + juce::String(y) + ")");
        }
        
        // Read and apply muted state from preset for existing nodes.
        if ((bool) n.getProperty("muted", false))
        {
            // Use muteNodeSilent to store the original connections first,
            // then apply the mute (which creates bypass connections)
            muteNodeSilent(nid);
            muteNode(nid);
        }
    }
    
    // Muting/unmuting modifies graph connections, so we must tell the
    // synth to rebuild its processing order.
    graphNeedsRebuild = true;
    juce::Logger::writeToLog("[UI_RESTORE] UI state applied. Flagging for graph rebuild.");
}

void ImGuiNodeEditorComponent::applyUiValueTree (const juce::ValueTree& uiState)
{
    // Queue for next frame to avoid calling imnodes setters before editor is begun
    uiPending = uiState;
}

void ImGuiNodeEditorComponent::handleDeletion()
{
    if (synth == nullptr)
        return;

    // Shift+Delete => bypass delete (keep chain intact)
    if ((ImGui::GetIO().KeyShift) && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        bypassDeleteSelectedNodes();
        return;
    }

    // Use new key query API (1.90+) for normal delete
    if (! ImGui::IsKeyPressed(ImGuiKey_Delete))
        return;

    // If a drag was in progress, capture positions before we mutate the graph
    if (isDraggingNode || ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        isDraggingNode = false;
        pushSnapshot();
    }

    // Early out if nothing selected
    const int numSelLinks = ImNodes::NumSelectedLinks();
    const int numSelNodes = ImNodes::NumSelectedNodes();

    if (numSelLinks <= 0 && numSelNodes <= 0)
        return;

    // Perform batch delete; snapshot after commit

    // Disconnect selected links
        if (numSelLinks > 0)
        {
        std::vector<int> ids((size_t) numSelLinks);
        ImNodes::GetSelectedLinks(ids.data());
        for (int id : ids)
        {
            // TODO: Handle modulation link deletion for new bus-based system
            // if (auto itM = modLinkIdToRoute.find (id); itM != modLinkIdToRoute.end())
            // {
            //     int sL, sC, dL; juce::String paramId; std::tie(sL, sC, dL, paramId) = itM->second;
            //     // TODO: Handle modulation route removal
            //     // if (paramId.isNotEmpty())
            //     //     synth->removeModulationRoute (synth->getNodeIdForLogical ((juce::uint32) sL), sC, (juce::uint32) dL, paramId);
            //     // else
            //     //     synth->removeModulationRoute (synth->getNodeIdForLogical ((juce::uint32) sL), sC, (juce::uint32) dL);
            // }
            // else 
            if (auto it = linkIdToAttrs.find(id); it != linkIdToAttrs.end())
            {
                auto srcPin = decodePinId(it->second.first);
                auto dstPin = decodePinId(it->second.second);

                auto srcNode = synth->getNodeIdForLogical(srcPin.logicalId);
                auto dstNode = (dstPin.logicalId == 0) ? synth->getOutputNodeID() : synth->getNodeIdForLogical(dstPin.logicalId);
                synth->disconnect(srcNode, srcPin.channel, dstNode, dstPin.channel);
            }
            }
        }

        if (numSelNodes > 0)
        {
        std::vector<int> nodeIds((size_t) numSelNodes);
        ImNodes::GetSelectedNodes(nodeIds.data());
        // Build a set for quick lookup when removing connections
        std::unordered_map<int, bool> toDelete;
        for (int nid : nodeIds) toDelete[nid] = true;
        // Disconnect all connections touching any selected node
        for (const auto& c : synth->getConnectionsInfo())
        {
            if (toDelete.count((int) c.srcLogicalId) || (! c.dstIsOutput && toDelete.count((int) c.dstLogicalId)))
            {
                auto srcNode = synth->getNodeIdForLogical(c.srcLogicalId);
                auto dstNode = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);
                synth->disconnect(srcNode, c.srcChan, dstNode, c.dstChan);
            }
        }
        // Remove nodes
        for (int nid : nodeIds)
        {
            if (nid == 0) continue; // don't delete output sink
            mutedNodeStates.erase((juce::uint32)nid); // Clean up muted state if exists
            lastKnownNodePositions.erase(nid); // Clean up position cache
            synth->removeModule(synth->getNodeIdForLogical((juce::uint32) nid));
        }
    }
    graphNeedsRebuild = true;
    pushSnapshot();
}

void ImGuiNodeEditorComponent::bypassDeleteSelectedNodes()
{
    const int numSelNodes = ImNodes::NumSelectedNodes();
    if (numSelNodes <= 0 || synth == nullptr) return;

    // Snapshot positions first if dragging
    if (isDraggingNode || ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        isDraggingNode = false;
        pushSnapshot();
    }

    std::vector<int> nodeIds((size_t) numSelNodes);
    ImNodes::GetSelectedNodes(nodeIds.data());

    for (int nid : nodeIds)
    {
        if (nid == 0) continue; // don't bypass-delete the output sink
        bypassDeleteNode((juce::uint32) nid);
    }
    graphNeedsRebuild = true;
    pushSnapshot();
}

void ImGuiNodeEditorComponent::bypassDeleteNode(juce::uint32 logicalId)
{
    // Collect all incoming/outgoing audio links for this node
    std::vector<decltype(synth->getConnectionsInfo())::value_type> inputs, outputs;
    for (const auto& c : synth->getConnectionsInfo())
    {
        if (!c.dstIsOutput && c.dstLogicalId == logicalId) inputs.push_back(c);
        if (c.srcLogicalId == logicalId) outputs.push_back(c);
    }

    // For each output channel, find matching input channel to splice
    for (const auto& out : outputs)
    {
        // Try to find input with same channel index, else fallback to first input
        const auto* inPtr = (const decltype(inputs)::value_type*) nullptr;
        for (const auto& in : inputs) { if (in.dstChan == out.srcChan) { inPtr = &in; break; } }
        if (inPtr == nullptr && !inputs.empty()) inPtr = &inputs.front();

        // Disconnect out link first
        auto srcNode = synth->getNodeIdForLogical(out.srcLogicalId);
        auto dstNode = out.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(out.dstLogicalId);
        synth->disconnect(srcNode, out.srcChan, dstNode, out.dstChan);

        if (inPtr != nullptr)
        {
            // Disconnect incoming link from the node
            auto inSrcNode = synth->getNodeIdForLogical(inPtr->srcLogicalId);
            auto inDstNode = synth->getNodeIdForLogical(inPtr->dstLogicalId);
            synth->disconnect(inSrcNode, inPtr->srcChan, inDstNode, inPtr->dstChan);

            // Connect source of incoming directly to destination of outgoing
            auto finalDstNode = out.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(out.dstLogicalId);
            synth->connect(inSrcNode, inPtr->srcChan, finalDstNode, out.dstChan);
        }
    }

    // TODO: Remove modulation routes targeting or originating this node using new bus-based system

    // Finally remove the node itself
    mutedNodeStates.erase(logicalId); // Clean up muted state if exists
    synth->removeModule(synth->getNodeIdForLogical(logicalId));
}

// === Non-Destructive Mute/Bypass Implementation ===

void ImGuiNodeEditorComponent::muteNodeSilent(juce::uint32 logicalId)
{
    // This function is used when loading presets. It records the connections that were
    // loaded from the XML without modifying the graph or creating bypass connections.
    // This preserves the original "unmuted" connections for later use.
    
    if (!synth) return;

    MutedNodeState state;
    auto allConnections = synth->getConnectionsInfo();

    // Store all connections attached to this node
    for (const auto& c : allConnections) {
        if (!c.dstIsOutput && c.dstLogicalId == logicalId) {
            state.incomingConnections.push_back(c);
        }
        if (c.srcLogicalId == logicalId) {
            state.outgoingConnections.push_back(c);
        }
    }

    // Store the state, but DON'T modify the graph or create bypass connections
    mutedNodeStates[logicalId] = state;
    juce::Logger::writeToLog("[MuteSilent] Node " + juce::String(logicalId) + 
                            " marked as muted, stored " + juce::String(state.incomingConnections.size()) + 
                            " incoming and " + juce::String(state.outgoingConnections.size()) + 
                            " outgoing connections.");
}

void ImGuiNodeEditorComponent::muteNode(juce::uint32 logicalId)
{
    if (!synth) return;

    MutedNodeState state;
    auto allConnections = synth->getConnectionsInfo();

    // 1. Find and store all connections attached to this node.
    for (const auto& c : allConnections) {
        if (!c.dstIsOutput && c.dstLogicalId == logicalId) {
            state.incomingConnections.push_back(c);
        }
        if (c.srcLogicalId == logicalId) {
            state.outgoingConnections.push_back(c);
        }
    }

    // 2. Disconnect all of them.
    for (const auto& c : state.incomingConnections) {
        synth->disconnect(synth->getNodeIdForLogical(c.srcLogicalId), c.srcChan, synth->getNodeIdForLogical(c.dstLogicalId), c.dstChan);
    }
    for (const auto& c : state.outgoingConnections) {
        auto dstNodeId = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);
        synth->disconnect(synth->getNodeIdForLogical(c.srcLogicalId), c.srcChan, dstNodeId, c.dstChan);
    }
    
    // --- FIX: More robust bypass splicing logic ---
    // 3. Splice the connections to bypass the node.
    // Connect the FIRST input source to ALL output destinations.
    // This correctly handles cases where input channel != output channel (e.g., Mixer input 3 → output 0).
    if (!state.incomingConnections.empty() && !state.outgoingConnections.empty())
    {
        const auto& primary_input = state.incomingConnections[0];
        auto srcNodeId = synth->getNodeIdForLogical(primary_input.srcLogicalId);

        for (const auto& out_conn : state.outgoingConnections)
        {
            auto dstNodeId = out_conn.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(out_conn.dstLogicalId);
            // Connect the primary input's source directly to the original output's destination
            synth->connect(srcNodeId, primary_input.srcChan, dstNodeId, out_conn.dstChan);
            juce::Logger::writeToLog("[Mute] Splicing bypass: [" + juce::String(primary_input.srcLogicalId) + 
                                    ":" + juce::String(primary_input.srcChan) + "] -> [" + 
                                    (out_conn.dstIsOutput ? "Output" : juce::String(out_conn.dstLogicalId)) + 
                                    ":" + juce::String(out_conn.dstChan) + "]");
        }
    }

    // 4. Store the original state.
    mutedNodeStates[logicalId] = state;
    juce::Logger::writeToLog("[Mute] Node " + juce::String(logicalId) + " muted and bypassed.");
}

void ImGuiNodeEditorComponent::unmuteNode(juce::uint32 logicalId)
{
    if (!synth || mutedNodeStates.find(logicalId) == mutedNodeStates.end()) return;

    MutedNodeState state = mutedNodeStates[logicalId];

    // --- FIX: Remove bypass connections matching the new mute logic ---
    // 1. Find and remove the bypass connections.
    // The bypass connected the first input source to all output destinations.
    if (!state.incomingConnections.empty() && !state.outgoingConnections.empty())
    {
        const auto& primary_input = state.incomingConnections[0];
        auto srcNodeId = synth->getNodeIdForLogical(primary_input.srcLogicalId);

        for (const auto& out_conn : state.outgoingConnections)
        {
            auto dstNodeId = out_conn.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(out_conn.dstLogicalId);
            // Disconnect the bypass connection
            synth->disconnect(srcNodeId, primary_input.srcChan, dstNodeId, out_conn.dstChan);
            juce::Logger::writeToLog("[Unmute] Removing bypass: [" + juce::String(primary_input.srcLogicalId) + 
                                    ":" + juce::String(primary_input.srcChan) + "] -> [" + 
                                    (out_conn.dstIsOutput ? "Output" : juce::String(out_conn.dstLogicalId)) + 
                                    ":" + juce::String(out_conn.dstChan) + "]");
        }
    }

    // 2. Restore the original connections.
    for (const auto& c : state.incomingConnections) {
        synth->connect(synth->getNodeIdForLogical(c.srcLogicalId), c.srcChan, synth->getNodeIdForLogical(c.dstLogicalId), c.dstChan);
    }
    for (const auto& c : state.outgoingConnections) {
        auto dstNodeId = c.dstIsOutput ? synth->getOutputNodeID() : synth->getNodeIdForLogical(c.dstLogicalId);
        synth->connect(synth->getNodeIdForLogical(c.srcLogicalId), c.srcChan, dstNodeId, c.dstChan);
    }

    // 3. Remove from muted state.
    mutedNodeStates.erase(logicalId);
    juce::Logger::writeToLog("[Mute] Node " + juce::String(logicalId) + " unmuted.");
}

void ImGuiNodeEditorComponent::handleMuteToggle()
{
    const int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected == 0) return;

    pushSnapshot(); // Create a single undo state for the whole operation.

    std::vector<int> selectedNodeIds(numSelected);
    ImNodes::GetSelectedNodes(selectedNodeIds.data());

    for (int lid : selectedNodeIds) {
        if (mutedNodeStates.count(lid)) {
            unmuteNode(lid);
        } else {
            muteNode(lid);
        }
    }

    graphNeedsRebuild = true;
}

void ImGuiNodeEditorComponent::startSaveDialog()
{
    saveChooser = std::make_unique<juce::FileChooser> ("Save preset", findPresetsDirectory(), "*.xml");
    saveChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.exists() && ! f.getParentDirectory().exists()) return;
        if (synth == nullptr) return;
        
        // --- FIX: Temporarily unmute nodes to save original connections ---
        // Collect all currently muted nodes
        std::vector<juce::uint32> currentlyMutedNodes;
        for (const auto& pair : mutedNodeStates)
        {
            currentlyMutedNodes.push_back(pair.first);
        }
        
        // Temporarily UNMUTE all of them to restore the original connections
        for (juce::uint32 lid : currentlyMutedNodes)
        {
            unmuteNode(lid);
        }
        
        // Force the synth to apply these connection changes immediately
        if (synth)
        {
            synth->commitChanges();
        }
        // At this point, the synth graph is in its "true", unmuted state
        
        // NOW get the state - this will save the correct, original connections
        juce::MemoryBlock mb; synth->getStateInformation (mb);
        auto xml = juce::XmlDocument::parse (mb.toString());
        
        // IMMEDIATELY RE-MUTE the nodes to return the editor to its visible state
        for (juce::uint32 lid : currentlyMutedNodes)
        {
            muteNode(lid);
        }
        
        // Force the synth to apply the re-mute changes immediately
        if (synth)
        {
            synth->commitChanges();
        }
        // The synth graph is now back to its bypassed state for audio processing
        // --- END OF FIX ---
        
        if (! xml) return;
        juce::ValueTree presetVT = juce::ValueTree::fromXml (*xml);
        presetVT.addChild (getUiValueTree(), -1, nullptr);
        f.replaceWithText (presetVT.createXml()->toString());
        
        // Update preset status tracking
        isPatchDirty = false;
        currentPresetFile = f.getFileName();
    });
}

void ImGuiNodeEditorComponent::startLoadDialog()
{
    loadChooser = std::make_unique<juce::FileChooser> ("Load preset", findPresetsDirectory(), "*.xml");
    loadChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile())
        {
            loadPresetFromFile(f); // Use the unified loading function
        }
    });
}

void ImGuiNodeEditorComponent::handleRandomizePatch()
{
    if (synth == nullptr) return;
    
    populatePinDatabase();

    // 1. --- SETUP ---
    synth->clearAll();
    juce::Random rng(juce::Time::getMillisecondCounterHiRes());
    
    // 2. --- ADD A "CLOUD" OF RANDOM MODULES ---
    std::vector<juce::String> modulePool = {
        "VCO", "Noise", "Sequencer", "VCF", "Delay", "Reverb", "Waveshaper",
        "LFO", "ADSR", "Random", "S&H", "Math", "MapRange", "Quantizer", "ClockDivider"
    };
    int numModules = 6 + rng.nextInt(7); // 6 to 12 modules
    std::vector<std::pair<juce::uint32, juce::String>> addedModules;

    for (int i = 0; i < numModules; ++i) {
        auto type = modulePool[rng.nextInt(modulePool.size())];
        auto newId = synth->getLogicalIdForNode(synth->addModule(type));
        addedModules.push_back({newId, type});
    }

    // 3. --- ESTABLISH AN OBSERVATION POINT ---
    // Always add a Mixer and Scope. This is our window into the chaos.
    auto mixerId = synth->getLogicalIdForNode(synth->addModule("Mixer"));
    addedModules.push_back({mixerId, "Mixer"});
    auto scopeId = synth->getLogicalIdForNode(synth->addModule("Scope"));
    addedModules.push_back({scopeId, "Scope"});
    
    // Connect the observation path: Mixer -> Scope -> Output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(synth->getNodeIdForLogical(mixerId), 0, synth->getNodeIdForLogical(scopeId), 0);
    synth->connect(synth->getNodeIdForLogical(scopeId), 0, outputNodeId, 0);
    synth->connect(synth->getNodeIdForLogical(scopeId), 1, outputNodeId, 1);

    // 4. --- CREATE CHAOTIC CONNECTIONS ---
    std::vector<std::pair<juce::uint32, AudioPin>> allAudioOuts;
    std::vector<std::pair<juce::uint32, AudioPin>> allAudioIns;
    std::vector<std::pair<juce::uint32, ModPin>> allModIns;

    for (const auto& mod : addedModules) {
        auto it = getModulePinDatabase().find(mod.second);
        if (it != getModulePinDatabase().end()) {
            for(const auto& pin : it->second.audioOuts) allAudioOuts.push_back({mod.first, pin});
            for(const auto& pin : it->second.audioIns) allAudioIns.push_back({mod.first, pin});
            for(const auto& pin : it->second.modIns) allModIns.push_back({mod.first, pin});
        }
    }
    
    // Connect a few random audio sources to the Mixer to make sound likely
    int numMixerInputs = 2 + rng.nextInt(3); // 2 to 4 mixer inputs
    if (!allAudioOuts.empty()) {
        for (int i = 0; i < numMixerInputs; ++i) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            // Connect to mixer inputs 0, 1, 2, 3
            synth->connect(synth->getNodeIdForLogical(source.first), source.second.channel, synth->getNodeIdForLogical(mixerId), i);
        }
    }

    // Make a large number of fully random connections
    int numRandomConnections = numModules + rng.nextInt(numModules);
    for (int i = 0; i < numRandomConnections; ++i)
    {
        float choice = rng.nextFloat();
        // 70% chance of making a CV modulation connection
        if (choice < 0.7f && !allAudioOuts.empty() && !allModIns.empty()) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            auto& target = allModIns[rng.nextInt(allModIns.size())];
            // TODO: synth->addModulationRouteByLogical(source.first, source.second.channel, target.first, target.second.paramId);
        }
        // 30% chance of making an audio-path or gate connection
        else if (!allAudioOuts.empty() && !allAudioIns.empty()) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            auto& target = allAudioIns[rng.nextInt(allAudioIns.size())];
            // Allow self-connection for feedback
            if (source.first != target.first || rng.nextFloat() < 0.2f) {
                synth->connect(synth->getNodeIdForLogical(source.first), source.second.channel, synth->getNodeIdForLogical(target.first), target.second.channel);
            }
        }
    }

    // 5. --- LAYOUT AND FINALIZE ---
    // Arrange nodes in a neat grid to prevent overlap.
    const float startX = 50.0f;
    const float startY = 50.0f;
    const float cellWidth = 300.0f;
    const float cellHeight = 400.0f;
    const int numColumns = 4;
    int col = 0;
    int row = 0;

    juce::uint32 finalMixerId = 0, finalScopeId = 0;
    for (const auto& mod : addedModules) {
        if (mod.second == "Mixer") finalMixerId = mod.first;
        if (mod.second == "Scope") finalScopeId = mod.first;
    }

    for (const auto& mod : addedModules)
    {
        // Skip the special output-chain nodes; we will place them manually.
        if (mod.first == finalMixerId || mod.first == finalScopeId) continue;

        float x = startX + col * cellWidth;
        float y = startY + row * cellHeight;
        pendingNodePositions[(int)mod.first] = ImVec2(x, y);

        col++;
        if (col >= numColumns) {
            col = 0;
            row++;
        }
    }

    // Manually place the Mixer and Scope on the far right for a clean, readable signal flow.
    float finalX = startX + numColumns * cellWidth;
    if (finalMixerId != 0) pendingNodePositions[(int)finalMixerId] = ImVec2(finalX, startY);
    if (finalScopeId != 0) pendingNodePositions[(int)finalScopeId] = ImVec2(finalX, startY + cellHeight);
    
    synth->commitChanges();
    pushSnapshot();
}

void ImGuiNodeEditorComponent::handleRandomizeConnections()
{
    if (synth == nullptr) return;
    auto currentModules = synth->getModulesInfo();
    if (currentModules.empty()) return;

    // 1. --- SETUP AND CLEAR ---
    synth->clearAllConnections();
    juce::Random rng(juce::Time::getMillisecondCounterHiRes());

    // 2. --- ESTABLISH AN OBSERVATION POINT ---
    juce::uint32 mixerId = 0, scopeId = 0;
    for (const auto& mod : currentModules) {
        if (mod.second == "Mixer") mixerId = mod.first;
        if (mod.second == "Scope") scopeId = mod.first;
    }
    // Add Mixer/Scope if they don't exist, as they are crucial for listening
    if (mixerId == 0) mixerId = synth->getLogicalIdForNode(synth->addModule("Mixer"));
    if (scopeId == 0) scopeId = synth->getLogicalIdForNode(synth->addModule("Scope"));

    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(synth->getNodeIdForLogical(mixerId), 0, synth->getNodeIdForLogical(scopeId), 0);
    synth->connect(synth->getNodeIdForLogical(scopeId), 0, outputNodeId, 0);

    // 3. --- CREATE CHAOTIC CONNECTIONS ---
    std::vector<std::pair<juce::uint32, AudioPin>> allAudioOuts;
    std::vector<std::pair<juce::uint32, AudioPin>> allAudioIns;
    std::vector<std::pair<juce::uint32, ModPin>> allModIns;
    
    // Refresh module list in case we added a Mixer/Scope
    auto updatedModules = synth->getModulesInfo();
    for (const auto& mod : updatedModules) {
        auto it = getModulePinDatabase().find(mod.second);
        if (it != getModulePinDatabase().end()) {
            for(const auto& pin : it->second.audioOuts) allAudioOuts.push_back({mod.first, pin});
            for(const auto& pin : it->second.audioIns) allAudioIns.push_back({mod.first, pin});
            for(const auto& pin : it->second.modIns) allModIns.push_back({mod.first, pin});
        }
    }
    
    // Connect random sources to the Mixer
    int numMixerInputs = 2 + rng.nextInt(3);
    if (!allAudioOuts.empty()) {
        for (int i = 0; i < numMixerInputs; ++i) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            if (source.first != mixerId) // Don't connect mixer to itself here
                synth->connect(synth->getNodeIdForLogical(source.first), source.second.channel, synth->getNodeIdForLogical(mixerId), i);
        }
    }
    
    // Make a large number of fully random connections
    int numRandomConnections = (int)updatedModules.size() + rng.nextInt((int)updatedModules.size());
    for (int i = 0; i < numRandomConnections; ++i)
    {
        float choice = rng.nextFloat();
        if (choice < 0.7f && !allAudioOuts.empty() && !allModIns.empty()) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            auto& target = allModIns[rng.nextInt(allModIns.size())];
            // TODO: synth->addModulationRouteByLogical(source.first, source.second.channel, target.first, target.second.paramId);
        }
        else if (!allAudioOuts.empty() && !allAudioIns.empty()) {
            auto& source = allAudioOuts[rng.nextInt(allAudioOuts.size())];
            auto& target = allAudioIns[rng.nextInt(allAudioIns.size())];
            if (source.first != target.first || rng.nextFloat() < 0.2f) { // Allow feedback
                synth->connect(synth->getNodeIdForLogical(source.first), source.second.channel, synth->getNodeIdForLogical(target.first), target.second.channel);
            }
        }
    }

    // 4. --- FINALIZE ---
    synth->commitChanges();
    pushSnapshot();
}

void ImGuiNodeEditorComponent::handleBeautifyLayout()
{
    if (synth == nullptr) return;

    // Graph is always in consistent state since we rebuild at frame start
    // Create an undo state so the action can be reversed
    pushSnapshot();
    juce::Logger::writeToLog("--- [Beautify Layout] Starting ---");

    // --- STEP 1: Build Graph Representation ---
    // Adjacency list: map<source_lid, vector<destination_lid>>
    std::map<juce::uint32, std::vector<juce::uint32>> adjacencyList;
    std::map<juce::uint32, int> inDegree; // Counts incoming connections for each node
    std::vector<juce::uint32> sourceNodes;

    auto modules = synth->getModulesInfo();
    for (const auto& mod : modules)
    {
        inDegree[mod.first] = 0;
        adjacencyList[mod.first] = {};
    }
    // Include the output node in the graph
    inDegree[0] = 0; // Output node ID is 0
    adjacencyList[0] = {}; // Output node has no outgoing connections

    for (const auto& conn : synth->getConnectionsInfo())
    {
        if (conn.dstIsOutput)
        {
            adjacencyList[conn.srcLogicalId].push_back(0); // Connect to output node
            inDegree[0]++;
        }
        else
        {
            adjacencyList[conn.srcLogicalId].push_back(conn.dstLogicalId);
            inDegree[conn.dstLogicalId]++;
        }
    }

    for (const auto& mod : modules)
    {
        if (inDegree[mod.first] == 0)
        {
            sourceNodes.push_back(mod.first);
        }
    }

    juce::Logger::writeToLog("[Beautify] Found " + juce::String(sourceNodes.size()) + " source nodes");

    // --- STEP 2: Assign Nodes to Columns (Topological Sort) ---
    std::map<juce::uint32, int> nodeColumn;
    std::vector<std::vector<juce::uint32>> columns;
    int maxColumn = 0;

    // Initialize source nodes in column 0
    for (juce::uint32 nodeId : sourceNodes)
    {
        nodeColumn[nodeId] = 0;
    }
    columns.push_back(sourceNodes);

    // Process each column and assign children to appropriate columns
    std::queue<juce::uint32> processQueue;
    for (juce::uint32 srcNode : sourceNodes)
        processQueue.push(srcNode);

    while (!processQueue.empty())
    {
        juce::uint32 u = processQueue.front();
        processQueue.pop();

        for (juce::uint32 v : adjacencyList[u])
        {
            // The column for node 'v' is the maximum of its predecessors' columns + 1
            int newColumn = nodeColumn[u] + 1;
            if (nodeColumn.count(v) == 0 || newColumn > nodeColumn[v])
            {
                nodeColumn[v] = newColumn;
                maxColumn = std::max(maxColumn, newColumn);
                processQueue.push(v);
            }
        }
    }

    // Re-populate columns based on assignments
    columns.assign(maxColumn + 1, {});
    for (const auto& pair : nodeColumn)
    {
        columns[pair.second].push_back(pair.first);
    }

    juce::Logger::writeToLog("[Beautify] Arranged nodes into " + juce::String(maxColumn + 1) + " columns");

    // --- STEP 3: Optimize Node Ordering Within Columns ---
    // Sort nodes in each column based on median position of their parents
    for (int c = 1; c <= maxColumn; ++c)
    {
        std::map<juce::uint32, float> medianPositions;
        
        for (juce::uint32 nodeId : columns[c])
        {
            std::vector<float> parentPositions;
            
            // Find all parents in previous columns
            for (const auto& pair : adjacencyList)
            {
                for (juce::uint32 dest : pair.second)
                {
                    if (dest == nodeId)
                    {
                        // Find the vertical index of the parent node
                        int parentColumn = nodeColumn[pair.first];
                        auto& parentColVec = columns[parentColumn];
                        auto it = std::find(parentColVec.begin(), parentColVec.end(), pair.first);
                        if (it != parentColVec.end())
                        {
                            parentPositions.push_back((float)std::distance(parentColVec.begin(), it));
                        }
                    }
                }
            }
            
            if (!parentPositions.empty())
            {
                std::sort(parentPositions.begin(), parentPositions.end());
                medianPositions[nodeId] = parentPositions[parentPositions.size() / 2];
            }
            else
            {
                medianPositions[nodeId] = 0.0f;
            }
        }
        
        // Sort the column based on median positions
        std::sort(columns[c].begin(), columns[c].end(), [&](juce::uint32 a, juce::uint32 b) {
            return medianPositions[a] < medianPositions[b];
        });
    }

    // --- STEP 4: Calculate Final Coordinates ---
    const float COLUMN_WIDTH = 400.0f;
    const float NODE_VERTICAL_PADDING = 50.0f;

    // Find the tallest column to center shorter ones
    float tallestColumnHeight = 0.0f;
    for (const auto& col : columns)
    {
        float height = 0.0f;
        for (juce::uint32 lid : col)
        {
            ImVec2 nodeSize = ImNodes::GetNodeDimensions((int)lid);
            height += nodeSize.y + NODE_VERTICAL_PADDING;
        }
        tallestColumnHeight = std::max(tallestColumnHeight, height);
    }

    // --- STEP 5: Apply Positions ---
    for (int c = 0; c <= maxColumn; ++c)
    {
        // Calculate column height for centering
        float columnHeight = 0.0f;
        for (juce::uint32 lid : columns[c])
        {
            columnHeight += ImNodes::GetNodeDimensions((int)lid).y + NODE_VERTICAL_PADDING;
        }
        
        // Start Y position (centered vertically)
        float currentY = (tallestColumnHeight - columnHeight) / 2.0f;

        for (juce::uint32 lid : columns[c])
        {
            float x = c * COLUMN_WIDTH;
            pendingNodePositions[(int)lid] = ImVec2(x, currentY);
            
            ImVec2 nodeSize = ImNodes::GetNodeDimensions((int)lid);
            currentY += nodeSize.y + NODE_VERTICAL_PADDING;
        }
    }

    // Position the output node to the right of all other modules
    float finalX = (maxColumn + 1) * COLUMN_WIDTH;
    float outputNodeY = (tallestColumnHeight - ImNodes::GetNodeDimensions(0).y) / 2.0f;
    pendingNodePositions[0] = ImVec2(finalX, outputNodeY);
    juce::Logger::writeToLog("[Beautify] Applied position to Output Node");
    
    juce::Logger::writeToLog("[Beautify] Applied positions to " + juce::String(modules.size()) + " nodes");
    juce::Logger::writeToLog("--- [Beautify Layout] Complete ---");
}

void ImGuiNodeEditorComponent::handleConnectSelectedToTrackMixer()
{
    if (synth == nullptr || ImNodes::NumSelectedNodes() <= 0)
    {
        juce::Logger::writeToLog("[AutoConnect] Aborted: No synth or no nodes selected.");
        return;
    }

    // This is a significant action, so create an undo state first.
    pushSnapshot();
    juce::Logger::writeToLog("--- [Connect to Mixer] Starting routine ---");

    // 1. Get all selected node IDs.
    const int numSelectedNodes = ImNodes::NumSelectedNodes();
    std::vector<int> selectedNodeLids(numSelectedNodes);
    ImNodes::GetSelectedNodes(selectedNodeLids.data());

    // 2. Find the geometric center of the selected nodes to position our new modules.
    float totalX = 0.0f, maxX = 0.0f, totalY = 0.0f;
    for (int lid : selectedNodeLids)
    {
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(lid);
        totalX += pos.x;
        totalY += pos.y;
        if (pos.x > maxX) {
            maxX = pos.x;
        }
    }
    ImVec2 centerPos = ImVec2(totalX / numSelectedNodes, totalY / numSelectedNodes);
    
    // 3. Create the Value node and set its value to the number of selected nodes.
    auto valueNodeId = synth->addModule("Value");
    auto valueLid = synth->getLogicalIdForNode(valueNodeId);
    if (auto* valueProc = dynamic_cast<ValueModuleProcessor*>(synth->getModuleForLogical(valueLid)))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(valueProc->getAPVTS().getParameter("value")))
        {
            *p = (float)numSelectedNodes;
            juce::Logger::writeToLog("[AutoConnect] Created Value node " + juce::String(valueLid) + " and set its value to " + juce::String(numSelectedNodes));
        }
    }
    // Position it slightly to the right of the center of the selection.
    pendingNodePositions[(int)valueLid] = ImVec2(centerPos.x + 400.0f, centerPos.y);

    // 4. Create the Track Mixer node.
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    // Position it to the right of the right-most selected node for a clean signal flow.
    pendingNodePositions[(int)mixerLid] = ImVec2(maxX + 800.0f, centerPos.y);
    juce::Logger::writeToLog("[AutoConnect] Created Track Mixer with logical ID " + juce::String(mixerLid));

    // 5. Connect the Value node to the Track Mixer's "Num Tracks Mod" input.
    // The Value module's "Raw" output is channel 0 (provides the exact value entered by the user).
    // The Track Mixer's "Num Tracks Mod" is on Bus 1, Channel 0, which is absolute channel 64.
    synth->connect(valueNodeId, 0, mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS);
    juce::Logger::writeToLog("[AutoConnect] Connected Value node 'Raw' output to Track Mixer's Num Tracks Mod input.");

    // 6. Connect the primary audio output of each selected node to a unique input on the Track Mixer.
    int mixerInputChannel = 0;
    for (int lid : selectedNodeLids)
    {
        if (mixerInputChannel >= TrackMixerModuleProcessor::MAX_TRACKS) break;

        auto sourceNodeId = synth->getNodeIdForLogical((juce::uint32)lid);
        
        // We will connect the first audio output (channel 0) of the source to the next available mixer input.
        synth->connect(sourceNodeId, 0, mixerNodeId, mixerInputChannel);
        juce::Logger::writeToLog("[AutoConnect] Connected node " + juce::String(lid) + " (Out 0) to Track Mixer (In " + juce::String(mixerInputChannel + 1) + ")");
        
        mixerInputChannel++;
    }

    // 7. Flag the graph for a rebuild to apply all changes.
    graphNeedsRebuild = true;
    juce::Logger::writeToLog("--- [Connect to Mixer] Routine complete. ---");
}

void ImGuiNodeEditorComponent::handleMidiPlayerAutoConnect(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid)
{
    if (!synth || !midiPlayer || midiPlayerLid == 0 || !midiPlayer->hasMIDIFileLoaded())
    {
        juce::Logger::writeToLog("[AutoConnect] Aborted: MIDI Player not ready.");
        return;
    }

    juce::Logger::writeToLog("--- [AutoConnect to Samplers] Starting routine for MIDI Player " + juce::String(midiPlayerLid) + " ---");

    // 1. Get initial positions and clear existing connections from the MIDI Player.
    auto midiPlayerNodeId = synth->getNodeIdForLogical(midiPlayerLid);
    ImVec2 midiPlayerPos = ImNodes::GetNodeGridSpacePos((int)midiPlayerLid);
    synth->clearConnectionsForNode(midiPlayerNodeId);

    // --- FIX: Create and position the Track Mixer first ---
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(midiPlayerPos.x + 1200.0f, midiPlayerPos.y);
    juce::Logger::writeToLog("[AutoConnect] Created Track Mixer with logical ID " + juce::String(mixerLid));

    // --- FIX: Connect MIDI Player "Num Tracks" output to Track Mixer "Num Tracks Mod" input ---
    // This ensures the Track Mixer automatically adjusts its track count based on the MIDI file content
    synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kNumTracksChannelIndex, mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS);
    juce::Logger::writeToLog("[AutoConnect] Connected MIDI Player Num Tracks to Track Mixer Num Tracks Mod");

    // 2. Create and connect a Sample Loader for each active MIDI track.
    const auto& activeTrackIndices = midiPlayer->getActiveTrackIndices();
    juce::Logger::writeToLog("[AutoConnect] MIDI file has " + juce::String(activeTrackIndices.size()) + " active tracks.");

    for (int i = 0; i < (int)activeTrackIndices.size(); ++i)
    {
        if (i >= MIDIPlayerModuleProcessor::kMaxTracks) break;

        // A. Create and position the new modules.
        auto samplerNodeId = synth->addModule("sample loader");
        auto samplerLid = synth->getLogicalIdForNode(samplerNodeId);
        pendingNodePositions[(int)samplerLid] = ImVec2(midiPlayerPos.x + 800.0f, midiPlayerPos.y + (i * 350.0f));

        auto mapRangeNodeId = synth->addModule("MapRange");
        auto mapRangeLid = synth->getLogicalIdForNode(mapRangeNodeId);
        pendingNodePositions[(int)mapRangeLid] = ImVec2(midiPlayerPos.x + 400.0f, midiPlayerPos.y + (i * 350.0f));
        
        // B. Configure the MapRange module for Pitch CV conversion.
        if (auto* mapRangeProc = dynamic_cast<MapRangeModuleProcessor*>(synth->getModuleForLogical(mapRangeLid)))
        {
            auto& ap = mapRangeProc->getAPVTS();
            // MIDI Player Pitch Out (0..1) -> Sample Loader Pitch Mod (-24..+24 semitones)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMin"))) *p = 0.0f;
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMax"))) *p = 1.0f;
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMin"))) *p = -24.0f;
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMax"))) *p = 24.0f;
        }

        // C. Connect the outputs for this track.
        const int pitchChan = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 0;
        const int gateChan  = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 1;
        const int trigChan  = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 3;

        // Pitch: MIDI Player -> MapRange -> Sample Loader
        synth->connect(midiPlayerNodeId, pitchChan, mapRangeNodeId, 0); // Pitch Out -> MapRange In
        synth->connect(mapRangeNodeId, 1, samplerNodeId, 0);             // MapRange Raw Out -> SampleLoader Pitch Mod In

        // Gate: MIDI Player -> Sample Loader
        synth->connect(midiPlayerNodeId, gateChan, samplerNodeId, 2);    // Gate Out -> SampleLoader Gate Mod In

        // Trigger: MIDI Player -> Sample Loader
        synth->connect(midiPlayerNodeId, trigChan, samplerNodeId, 3);    // Trigger Out -> SampleLoader Trigger Mod In

        // --- FIX: Connect the Sample Loader's audio output to the Track Mixer ---
        // The Sample Loader's main audio output is channel 0.
        // The Track Mixer's inputs are mono channels 0, 1, 2...
        synth->connect(samplerNodeId, 0, mixerNodeId, i);
    }

    // --- FIX: Connect the mixer to the main output so you can hear it! ---
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Mixer Out L -> Main Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Mixer Out R -> Main Out R

    // 3. Flag the graph for a rebuild to apply all changes.
    graphNeedsRebuild = true;
    juce::Logger::writeToLog("--- [AutoConnect to Samplers] Routine complete. ---");
}

void ImGuiNodeEditorComponent::handleMidiPlayerAutoConnectVCO(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid)
{
    if (!synth || !midiPlayer || midiPlayerLid == 0 || !midiPlayer->hasMIDIFileLoaded())
    {
        juce::Logger::writeToLog("[AutoConnectVCO] Aborted: MIDI Player not ready.");
        return;
    }
    
    juce::Logger::writeToLog("--- [AutoConnectVCO] Starting routine for MIDI Player " + juce::String(midiPlayerLid) + " ---");

    // 1. Get initial positions and clear all existing connections from the MIDI Player.
    auto midiPlayerNodeId = synth->getNodeIdForLogical(midiPlayerLid);
    ImVec2 midiPlayerPos = ImNodes::GetNodeGridSpacePos((int)midiPlayerLid);
    synth->clearConnectionsForNode(midiPlayerNodeId);
    
    // 2. Create and position the PolyVCO and Track Mixer.
    auto polyVcoNodeId = synth->addModule("polyvco");
    auto polyVcoLid = synth->getLogicalIdForNode(polyVcoNodeId);
    pendingNodePositions[(int)polyVcoLid] = ImVec2(midiPlayerPos.x + 400.0f, midiPlayerPos.y);
    juce::Logger::writeToLog("[AutoConnectVCO] Created PolyVCO with logical ID " + juce::String(polyVcoLid));

    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(midiPlayerPos.x + 800.0f, midiPlayerPos.y);
    juce::Logger::writeToLog("[AutoConnectVCO] Created Track Mixer with logical ID " + juce::String(mixerLid));

    // 3. Connect the track count outputs to control both new modules.
    synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, polyVcoNodeId, 0); // Raw Num Tracks -> PolyVCO Num Voices Mod
    synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS); // Raw Num Tracks -> Mixer Num Tracks Mod
    juce::Logger::writeToLog("[AutoConnectVCO] Connected MIDI Player raw track counts to PolyVCO and Track Mixer modulation inputs.");
    
    // 4. Loop through active MIDI tracks to connect CV routes and audio.
    const auto& activeTrackIndices = midiPlayer->getActiveTrackIndices();
    juce::Logger::writeToLog("[AutoConnectVCO] MIDI file has " + juce::String(activeTrackIndices.size()) + " active tracks. Patching voices...");

    for (int i = 0; i < (int)activeTrackIndices.size(); ++i)
    {
        if (i >= PolyVCOModuleProcessor::MAX_VOICES) break; // Don't try to connect more voices than the PolyVCO has

        int sourceTrackIndex = activeTrackIndices[i];

        // A. Connect CV modulation routes from MIDI Player to the corresponding PolyVCO voice.
        int pitchChan = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 0;
        int velChan   = i * MIDIPlayerModuleProcessor::kOutputsPerTrack + 2;
        
        // Connect MIDI CV to the corresponding PolyVCO voice inputs
        synth->connect(midiPlayerNodeId, pitchChan, polyVcoNodeId, 1 + i); // Pitch -> Freq Mod
        synth->connect(midiPlayerNodeId, velChan,   polyVcoNodeId, 1 + PolyVCOModuleProcessor::MAX_VOICES * 2 + i); // Velocity -> Gate Mod

        // B. Connect the PolyVCO voice's audio output to the Track Mixer's input.
        synth->connect(polyVcoNodeId, i, mixerNodeId, i);
    }
    
    // 5. Connect the Track Mixer to the main audio output.
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Mixer Out L -> Main Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Mixer Out R -> Main Out R
    
    // 6. Flag the graph for a rebuild.
    graphNeedsRebuild = true;
    juce::Logger::writeToLog("--- [AutoConnectVCO] Routine complete. ---");
}

void ImGuiNodeEditorComponent::handleMidiPlayerAutoConnectHybrid(MIDIPlayerModuleProcessor* midiPlayer, juce::uint32 midiPlayerLid)
{
    if (!synth || !midiPlayer) return;

    pushSnapshot();

    const int numTracks = midiPlayer->getNumTracks();
    if (numTracks == 0) return;

    auto midiPlayerNodeId = synth->getNodeIdForLogical(midiPlayerLid);
    ImVec2 midiPos = ImNodes::GetNodeGridSpacePos((int)midiPlayerLid);

    // --- THIS IS THE NEW "FIND-BY-TRACING" LOGIC ---

    juce::uint32 polyVcoLid = 0;
    juce::uint32 trackMixerLid = 0;

    // 1. Scan existing connections to find modules to reuse by tracing backwards.
    // First, find a TrackMixer connected to the output.
    for (const auto& conn : synth->getConnectionsInfo())
    {
        if (conn.dstIsOutput && synth->getModuleTypeForLogical(conn.srcLogicalId).equalsIgnoreCase("trackmixer"))
        {
            trackMixerLid = conn.srcLogicalId; // Found a TrackMixer to reuse!
            break;
        }
    }
    // If we found a TrackMixer, now find a PolyVCO connected to it.
    if (trackMixerLid != 0)
    {
        for (const auto& conn : synth->getConnectionsInfo())
        {
            if (conn.dstLogicalId == trackMixerLid && synth->getModuleTypeForLogical(conn.srcLogicalId).equalsIgnoreCase("polyvco"))
            {
                polyVcoLid = conn.srcLogicalId; // Found a PolyVCO to reuse!
                break;
            }
        }
    }

    // 2. Clear all old Pitch/Gate/Velocity connections from the MIDI Player.
    std::vector<ModularSynthProcessor::ConnectionInfo> oldConnections;
    for (const auto& conn : synth->getConnectionsInfo())
    {
        if (conn.srcLogicalId == midiPlayerLid && conn.srcChan < 16 * 3)
            oldConnections.push_back(conn);
    }
    for (const auto& conn : oldConnections)
    {
        synth->disconnect(synth->getNodeIdForLogical(conn.srcLogicalId), conn.srcChan,
                          synth->getNodeIdForLogical(conn.dstLogicalId), conn.dstChan);
    }

    // 3. If we didn't find a PolyVCO to reuse after tracing, create a new one.
    if (polyVcoLid == 0)
    {
        auto polyVcoNodeId = synth->addModule("polyvco", false);
        polyVcoLid = synth->getLogicalIdForNode(polyVcoNodeId);
        pendingNodePositions[(int)polyVcoLid] = ImVec2(midiPos.x + 400.0f, midiPos.y);
    }

    // 4. If we didn't find a TrackMixer to reuse after tracing, create a new one.
    if (trackMixerLid == 0)
    {
        auto trackMixerNodeId = synth->addModule("trackmixer", false);
        trackMixerLid = synth->getLogicalIdForNode(trackMixerNodeId);
        pendingNodePositions[(int)trackMixerLid] = ImVec2(midiPos.x + 800.0f, midiPos.y);
    }
    // --- END OF NEW LOGIC ---

    auto polyVcoNodeId = synth->getNodeIdForLogical(polyVcoLid);
    auto trackMixerNodeId = synth->getNodeIdForLogical(trackMixerLid);

    if (auto* vco = dynamic_cast<PolyVCOModuleProcessor*>(synth->getModuleForLogical(polyVcoLid)))
        if (auto* p = dynamic_cast<juce::AudioParameterInt*>(vco->getAPVTS().getParameter("numVoices"))) *p = numTracks;
    if (auto* mixer = dynamic_cast<TrackMixerModuleProcessor*>(synth->getModuleForLogical(trackMixerLid)))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(mixer->getAPVTS().getParameter("numTracks"))) *p = (float)numTracks;

    int voicesToConnect = std::min({numTracks, PolyVCOModuleProcessor::MAX_VOICES, 64});
    for (int i = 0; i < voicesToConnect; ++i)
    {
        synth->connect(midiPlayerNodeId, i, polyVcoNodeId, 1 + i);
        synth->connect(midiPlayerNodeId, i + 16, polyVcoNodeId, 1 + PolyVCOModuleProcessor::MAX_VOICES * 2 + i);
        synth->connect(polyVcoNodeId, i, trackMixerNodeId, i * 2);
        synth->connect(polyVcoNodeId, i, trackMixerNodeId, i * 2 + 1);
    }
    
    synth->connect(trackMixerNodeId, 0, synth->getOutputNodeID(), 0);
    synth->connect(trackMixerNodeId, 1, synth->getOutputNodeID(), 1);

    synth->commitChanges();
}

void ImGuiNodeEditorComponent::handleStrokeSeqBuildDrumKit(StrokeSequencerModuleProcessor* strokeSeq, juce::uint32 strokeSeqLid)
{
    if (!synth || !strokeSeq) return;

    juce::Logger::writeToLog("🥁 BUILD DRUM KIT handler called! Creating modules...");

    // 1. Get Stroke Sequencer position
    auto seqNodeId = synth->getNodeIdForLogical(strokeSeqLid);
    ImVec2 seqPos = ImNodes::GetNodeGridSpacePos((int)strokeSeqLid);

    // 2. Create 3 Sample Loaders (for Floor, Mid, Ceiling triggers)
    auto sampler1NodeId = synth->addModule("sample loader");
    auto sampler2NodeId = synth->addModule("sample loader");
    auto sampler3NodeId = synth->addModule("sample loader");
    
    auto sampler1Lid = synth->getLogicalIdForNode(sampler1NodeId);
    auto sampler2Lid = synth->getLogicalIdForNode(sampler2NodeId);
    auto sampler3Lid = synth->getLogicalIdForNode(sampler3NodeId);
    
    // Position samplers in a vertical stack to the right
    pendingNodePositions[(int)sampler1Lid] = ImVec2(seqPos.x + 400.0f, seqPos.y);
    pendingNodePositions[(int)sampler2Lid] = ImVec2(seqPos.x + 400.0f, seqPos.y + 220.0f);
    pendingNodePositions[(int)sampler3Lid] = ImVec2(seqPos.x + 400.0f, seqPos.y + 440.0f);

    // 3. Create Track Mixer (will be set to 6 tracks by Value node)
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(seqPos.x + 800.0f, seqPos.y + 200.0f);

    // 4. Create Value node set to 6.0 (for 3 stereo tracks = 6 channels)
    auto valueNodeId = synth->addModule("value");
    auto valueLid = synth->getLogicalIdForNode(valueNodeId);
    pendingNodePositions[(int)valueLid] = ImVec2(seqPos.x + 600.0f, seqPos.y + 550.0f);
    
    if (auto* valueNode = dynamic_cast<ValueModuleProcessor*>(synth->getModuleForLogical(valueLid)))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(valueNode->getAPVTS().getParameter("value")) = 6.0f;
    }

    // 5. Connect Stroke Sequencer TRIGGERS to Sample Loader TRIGGER MOD inputs (channel 3)
    synth->connect(seqNodeId, 0, sampler1NodeId, 3); // Floor Trig   -> Sampler 1 Trigger Mod
    synth->connect(seqNodeId, 1, sampler2NodeId, 3); // Mid Trig     -> Sampler 2 Trigger Mod
    synth->connect(seqNodeId, 2, sampler3NodeId, 3); // Ceiling Trig -> Sampler 3 Trigger Mod

    // 6. Connect Sample Loader AUDIO OUTPUTS to Track Mixer AUDIO INPUTS (stereo pairs)
    // Sampler 1 (L+R) -> Mixer Audio 1+2
    synth->connect(sampler1NodeId, 0, mixerNodeId, 0); // Sampler 1 L -> Mixer Audio 1
    synth->connect(sampler1NodeId, 1, mixerNodeId, 1); // Sampler 1 R -> Mixer Audio 2
    
    // Sampler 2 (L+R) -> Mixer Audio 3+4
    synth->connect(sampler2NodeId, 0, mixerNodeId, 2); // Sampler 2 L -> Mixer Audio 3
    synth->connect(sampler2NodeId, 1, mixerNodeId, 3); // Sampler 2 R -> Mixer Audio 4
    
    // Sampler 3 (L+R) -> Mixer Audio 5+6
    synth->connect(sampler3NodeId, 0, mixerNodeId, 4); // Sampler 3 L -> Mixer Audio 5
    synth->connect(sampler3NodeId, 1, mixerNodeId, 5); // Sampler 3 R -> Mixer Audio 6

    // 7. Connect Value node (6.0) to Track Mixer's "Num Tracks" input
    synth->connect(valueNodeId, 0, mixerNodeId, 64); // Value (6) -> Num Tracks Mod

    // 8. Connect Track Mixer output to global output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Mixer Out L -> Global Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Mixer Out R -> Global Out R

    synth->commitChanges();
    graphNeedsRebuild = true;
}

void ImGuiNodeEditorComponent::handleMultiSequencerAutoConnectSamplers(MultiSequencerModuleProcessor* sequencer, juce::uint32 sequencerLid)
{
    if (!synth || !sequencer) return;

    // 1. Get Sequencer info and clear its old connections
    auto seqNodeId = synth->getNodeIdForLogical(sequencerLid);
    ImVec2 seqPos = ImNodes::GetNodeGridSpacePos((int)sequencerLid);
    const int numSteps = static_cast<int>(sequencer->getAPVTS().getRawParameterValue("numSteps")->load());
    synth->clearConnectionsForNode(seqNodeId);

    // 2. Create the necessary Mixer
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(seqPos.x + 800.0f, seqPos.y + 100.0f);
    if (auto* mixer = dynamic_cast<TrackMixerModuleProcessor*>(synth->getModuleForLogical(mixerLid))) {
        *dynamic_cast<juce::AudioParameterInt*>(mixer->getAPVTS().getParameter("numTracks")) = numSteps;
    }

    // 3. CREATE a Sample Loader for each step and connect its audio to the mixer
    for (int i = 0; i < numSteps; ++i)
    {
        auto samplerNodeId = synth->addModule("sample loader");
        auto samplerLid = synth->getLogicalIdForNode(samplerNodeId);
        pendingNodePositions[(int)samplerLid] = ImVec2(seqPos.x + 400.0f, seqPos.y + (i * 220.0f));

        // Connect this sampler's audio output to the mixer's input
        synth->connect(samplerNodeId, 0 /*Audio Output*/, mixerNodeId, i);
        
        // Connect the Sequencer's CV/Trig for this step directly to the new sampler
        synth->connect(seqNodeId, 7 + i * 3 + 0, samplerNodeId, 0); // Pitch N -> Pitch Mod
        synth->connect(seqNodeId, 1, samplerNodeId, 2); // Main Gate -> Gate Mod
        synth->connect(seqNodeId, 7 + i * 3 + 2, samplerNodeId, 3); // Trig N  -> Trigger Mod
    }
    
    // Connect Num Steps output (channel 6) to Track Mixer's Num Tracks Mod input (channel 64)
    synth->connect(seqNodeId, 6, mixerNodeId, 64); // Num Steps -> Num Tracks Mod

    // 4. Connect the mixer to the main output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Out R

    graphNeedsRebuild = true;
}

void ImGuiNodeEditorComponent::handleMultiSequencerAutoConnectVCO(MultiSequencerModuleProcessor* sequencer, juce::uint32 sequencerLid)
{
    if (!synth || !sequencer) return;

    // 1. Get Sequencer info and clear its old connections
    auto seqNodeId = synth->getNodeIdForLogical(sequencerLid);
    ImVec2 seqPos = ImNodes::GetNodeGridSpacePos((int)sequencerLid);
    const int numSteps = static_cast<int>(sequencer->getAPVTS().getRawParameterValue("numSteps")->load());
    synth->clearConnectionsForNode(seqNodeId);

    // 2. CREATE the PolyVCO and Track Mixer
    auto polyVcoNodeId = synth->addModule("polyvco");
    auto polyVcoLid = synth->getLogicalIdForNode(polyVcoNodeId);
    pendingNodePositions[(int)polyVcoLid] = ImVec2(seqPos.x + 400.0f, seqPos.y);
    if (auto* vco = dynamic_cast<PolyVCOModuleProcessor*>(synth->getModuleForLogical(polyVcoLid))) {
        *dynamic_cast<juce::AudioParameterInt*>(vco->getAPVTS().getParameter("numVoices")) = numSteps;
    }
    
    auto mixerNodeId = synth->addModule("trackmixer");
    auto mixerLid = synth->getLogicalIdForNode(mixerNodeId);
    pendingNodePositions[(int)mixerLid] = ImVec2(seqPos.x + 800.0f, seqPos.y);
    if (auto* mixer = dynamic_cast<TrackMixerModuleProcessor*>(synth->getModuleForLogical(mixerLid))) {
        *dynamic_cast<juce::AudioParameterInt*>(mixer->getAPVTS().getParameter("numTracks")) = numSteps;
    }

    // 3. Connect CV, Audio, and Main Output
    for (int i = 0; i < numSteps; ++i)
    {
        // Connect CV: Sequencer -> PolyVCO
        synth->connect(seqNodeId, 7 + i * 3 + 0, polyVcoNodeId, 1 + i);                                  // Pitch N -> Freq N Mod
        synth->connect(seqNodeId, 1, polyVcoNodeId, 1 + PolyVCOModuleProcessor::MAX_VOICES * 2 + i); // Main Gate -> Gate N Mod

        // Connect Audio: PolyVCO -> Mixer
        synth->connect(polyVcoNodeId, i, mixerNodeId, i);
    }
    
    // Connect Num Steps output (channel 6) to PolyVCO's Num Voices Mod input (channel 0)
    synth->connect(seqNodeId, 6, polyVcoNodeId, 0); // Num Steps -> Num Voices Mod
    
    // Connect Num Steps output (channel 6) to Track Mixer's Num Tracks Mod input (channel 64)
    synth->connect(seqNodeId, 6, mixerNodeId, 64); // Num Steps -> Num Tracks Mod
    
    // Connect Mixer -> Main Output
    auto outputNodeId = synth->getOutputNodeID();
    synth->connect(mixerNodeId, 0, outputNodeId, 0); // Out L
    synth->connect(mixerNodeId, 1, outputNodeId, 1); // Out R

    graphNeedsRebuild = true;
}

// Add this exact helper function to the class
void ImGuiNodeEditorComponent::parsePinName(const juce::String& fullName, juce::String& outType, int& outIndex)
{
    outIndex = -1; // Default to no index
    outType = fullName;

    if (fullName.contains(" "))
    {
        const juce::String lastWord = fullName.substring(fullName.lastIndexOfChar(' ') + 1);
        if (lastWord.containsOnly("0123456789"))
        {
            outIndex = lastWord.getIntValue();
            outType = fullName.substring(0, fullName.lastIndexOfChar(' '));
        }
    }
}

// Helper functions to get pins from modules
std::vector<AudioPin> ImGuiNodeEditorComponent::getOutputPins(const juce::String& moduleType)
{
    auto it = getModulePinDatabase().find(moduleType);
    if (it != getModulePinDatabase().end())
        return it->second.audioOuts;
    return {};
}

std::vector<AudioPin> ImGuiNodeEditorComponent::getInputPins(const juce::String& moduleType)
{
    auto it = getModulePinDatabase().find(moduleType);
    if (it != getModulePinDatabase().end())
        return it->second.audioIns;
    return {};
}

AudioPin* ImGuiNodeEditorComponent::findInputPin(const juce::String& moduleType, const juce::String& pinName)
{
    auto pins = getInputPins(moduleType);
    for (auto& pin : pins)
    {
        if (pin.name == pinName)
            return &pin;
    }
    return nullptr;
}

AudioPin* ImGuiNodeEditorComponent::findOutputPin(const juce::String& moduleType, const juce::String& pinName)
{
    auto pins = getOutputPins(moduleType);
    for (auto& pin : pins)
    {
        if (pin.name == pinName)
            return &pin;
    }
    return nullptr;
}

std::vector<juce::uint32> ImGuiNodeEditorComponent::findNodesOfType(const juce::String& moduleType)
{
    std::vector<juce::uint32> result;
    if (!synth) return result;
    
    for (const auto& modInfo : synth->getModulesInfo())
    {
        if (synth->getModuleTypeForLogical(modInfo.first) == moduleType)
        {
            result.push_back(modInfo.first);
        }
    }
    return result;
}

// New dynamic pin-fetching helper
std::vector<PinInfo> ImGuiNodeEditorComponent::getDynamicOutputPins(ModuleProcessor* module)
{
    std::vector<PinInfo> pins;
    if (!module) return pins;

    const int numOutputChannels = module->getBus(false, 0)->getNumberOfChannels();
    for (int i = 0; i < numOutputChannels; ++i)
    {
        juce::String pinName = module->getAudioOutputLabel(i);
        if (pinName.isNotEmpty())
        {
            pins.push_back({(uint32_t)i, pinName}); // Store the full pin name in the type field
        }
    }
    return pins;
}

// Template function implementations
template<typename TargetProcessorType>
void ImGuiNodeEditorComponent::connectToMonophonicTargets(
    ModuleProcessor* sourceNode,
    const std::map<juce::String, juce::String>& pinNameMapping,
    const std::vector<juce::uint32>& targetLids)
{
    if (!synth || !sourceNode || targetLids.empty()) return;
    
    juce::Logger::writeToLog("[AutoConnect] connectToMonophonicTargets called for " + sourceNode->getName());
    
    // Get the source module type
    juce::String sourceModuleType;
    for (const auto& modInfo : synth->getModulesInfo())
    {
        if (synth->getModuleForLogical(modInfo.first) == sourceNode)
        {
            sourceModuleType = synth->getModuleTypeForLogical(modInfo.first);
            break;
        }
    }
    
    if (sourceModuleType.isEmpty()) return;
    
    // Use provided target logical IDs explicitly
    auto targetNodes = targetLids;

    int currentTargetIndex = 0;

    // First, group all of the source node's output pins by their index number.
    // For example, "Pitch 1" and "Trig 1" will both be in the group for index 1.
    std::map<int, std::vector<PinInfo>> pinsByIndex;
    
    // THE FIX: Get pins directly from the module instance.
    auto outputPins = getDynamicOutputPins(sourceNode);
    
    for (const auto& pin : outputPins)
    {
        juce::String type;
        int index = -1;
        parsePinName(pin.type, type, index); // Use pin.type instead of pin.name
        if (index != -1) {
            // Store channel ID as the pin's ID
            pinsByIndex[index].push_back({(uint32_t)pin.id, type}); 
        }
    }

    // Now, loop through each group of pins (each voice).
    for (auto const& [index, pinsInGroup] : pinsByIndex)
    {
        if (currentTargetIndex >= (int)targetNodes.size()) break; // Stop if we run out of targets
        auto targetNodeId = targetNodes[currentTargetIndex];

        // For each pin in the group (e.g., for "Pitch 1" and "Trig 1")...
        for (const auto& pinInfo : pinsInGroup)
        {
            // Check if we have a connection rule for this pin type (e.g., "Pitch").
            if (pinNameMapping.count(pinInfo.type))
            {
                juce::String targetPinName = pinNameMapping.at(pinInfo.type);
                auto* targetPin = findInputPin("sample loader", targetPinName);

                // If the target pin exists, create the connection.
                if (targetPin)
                {
                    juce::uint32 sourceLogicalId = 0;
                    for (const auto& modInfo : synth->getModulesInfo())
                    {
                        if (synth->getModuleForLogical(modInfo.first) == sourceNode)
                        {
                            sourceLogicalId = modInfo.first;
                            break;
                        }
                    }
                    auto sourceNodeId = synth->getNodeIdForLogical(sourceLogicalId);
                    synth->connect(sourceNodeId, pinInfo.id, synth->getNodeIdForLogical(targetNodeId), targetPin->channel);
                }
            }
        }
        // IMPORTANT: Move to the next target module for the next voice.
        currentTargetIndex++;
    }
}

template<typename TargetProcessorType>
void ImGuiNodeEditorComponent::connectToPolyphonicTarget(
    ModuleProcessor* sourceNode,
    const std::map<juce::String, juce::String>& pinNameMapping)
{
    if (!synth || !sourceNode) return;
    
    juce::Logger::writeToLog("[AutoConnect] connectToPolyphonicTarget called for " + sourceNode->getName());
    
    // Get the source module type
    juce::String sourceModuleType;
    juce::uint32 sourceLogicalId = 0;
    for (const auto& modInfo : synth->getModulesInfo())
    {
        if (synth->getModuleForLogical(modInfo.first) == sourceNode)
        {
            sourceModuleType = synth->getModuleTypeForLogical(modInfo.first);
            sourceLogicalId = modInfo.first;
            break;
        }
    }
    
    if (sourceModuleType.isEmpty()) return;
    
    auto targetNodes = findNodesOfType("polyvco");
    if (targetNodes.empty()) return;
    auto targetNodeId = targetNodes[0]; // Use the first available PolyVCO

    auto sourceNodeId = synth->getNodeIdForLogical(sourceLogicalId);

    // THE FIX: Get pins directly from the module instance, not the database.
    auto outputPins = getDynamicOutputPins(sourceNode);

    // Loop through every output pin on the source module.
    for (const auto& sourcePin : outputPins)
    {
        // Parse the source pin's name to get its type and index.
        juce::String sourceType;
        int sourceIndex = -1;
        parsePinName(sourcePin.type, sourceType, sourceIndex); // Use pin.type instead of pin.name

        if (sourceIndex == -1) continue; // Skip pins that aren't numbered.

        // Check if we have a rule for this pin type (e.g., "Pitch" maps to "Freq").
        if (pinNameMapping.count(sourceType))
        {
            juce::String targetType = pinNameMapping.at(sourceType);
            // PolyVCO inputs use the format "Freq 1 Mod", "Gate 1 Mod", etc.
            juce::String targetPinName = targetType + " " + juce::String(sourceIndex) + " Mod";

            // Find that pin on the target and connect it if available.
            auto* targetPin = findInputPin("polyvco", targetPinName);
            if (targetPin)
            {
                synth->connect(sourceNodeId, sourcePin.id, synth->getNodeIdForLogical(targetNodeId), targetPin->channel);
            }
        }
    }
}

void ImGuiNodeEditorComponent::handleAutoConnectionRequests()
{
    if (!synth) return;
    
    for (const auto& modInfo : synth->getModulesInfo())
    {
        auto* module = synth->getModuleForLogical(modInfo.first);
        if (!module) continue;

        // --- Check MultiSequencer Flags ---
        if (auto* multiSeq = dynamic_cast<MultiSequencerModuleProcessor*>(module))
        {
            if (multiSeq->autoConnectSamplersTriggered.exchange(false))
            {
                handleMultiSequencerAutoConnectSamplers(multiSeq, modInfo.first); // Call the new specific handler
                pushSnapshot();
                return;
            }
            if (multiSeq->autoConnectVCOTriggered.exchange(false))
            {
                handleMultiSequencerAutoConnectVCO(multiSeq, modInfo.first); // Call the new specific handler
                pushSnapshot();
                return;
            }
        }
        
        // --- Check StrokeSequencer Flags ---
        if (auto* strokeSeq = dynamic_cast<StrokeSequencerModuleProcessor*>(module))
        {
            if (strokeSeq->autoBuildDrumKitTriggered.exchange(false))
            {
                handleStrokeSeqBuildDrumKit(strokeSeq, modInfo.first);
                pushSnapshot();
                return;
            }
        }
        
        // --- Check MIDIPlayer Flags ---
        if (auto* midiPlayer = dynamic_cast<MIDIPlayerModuleProcessor*>(module))
        {
            if (midiPlayer->autoConnectTriggered.exchange(false)) // Samplers
            {
                handleMidiPlayerAutoConnect(midiPlayer, modInfo.first); // Reuse old detailed handler
                pushSnapshot();
                return;
            }
            if (midiPlayer->autoConnectVCOTriggered.exchange(false))
            {
                handleMidiPlayerAutoConnectVCO(midiPlayer, modInfo.first); // Reuse old detailed handler
                pushSnapshot();
                return;
            }
            if (midiPlayer->autoConnectHybridTriggered.exchange(false))
            {
                handleMidiPlayerAutoConnectHybrid(midiPlayer, modInfo.first); // Reuse old detailed handler
                pushSnapshot();
                return;
            }
        }
    }
}

void ImGuiNodeEditorComponent::handleMIDIPlayerConnectionRequest(juce::uint32 midiPlayerLid, MIDIPlayerModuleProcessor* midiPlayer, int requestType)
{
    if (!synth || !midiPlayer) return;
    
    juce::Logger::writeToLog("[MIDI Player Quick Connect] Request type: " + juce::String(requestType));
    
    // Get ALL tracks (don't filter by whether they have notes)
    const auto& notesByTrack = midiPlayer->getNotesByTrack();
    int numTracks = (int)notesByTrack.size();
    
    if (numTracks == 0)
    {
        juce::Logger::writeToLog("[MIDI Player Quick Connect] No tracks in MIDI file");
        return;
    }
    
    // Get MIDI Player position for positioning new nodes
    ImVec2 playerPos = ImNodes::GetNodeEditorSpacePos(static_cast<int>(midiPlayerLid));
    auto midiPlayerNodeId = synth->getNodeIdForLogical(midiPlayerLid);
    
    // Request Type: 1=PolyVCO, 2=Samplers, 3=Both
    juce::uint32 polyVCOLid = 0;
    juce::uint32 mixerLid = 0;
    
    if (requestType == 1 || requestType == 3) // PolyVCO or Both
    {
        // 1. Create PolyVCO
        auto polyVCONodeId = synth->addModule("polyvco");
        polyVCOLid = synth->getLogicalIdForNode(polyVCONodeId);
        pendingNodeScreenPositions[(int)polyVCOLid] = ImVec2(playerPos.x + 400.0f, playerPos.y);
        juce::Logger::writeToLog("[MIDI Player Quick Connect] Created PolyVCO at LID " + juce::String((int)polyVCOLid));
        
        // 2. Create Track Mixer
        auto mixerNodeId = synth->addModule("trackmixer");
        mixerLid = synth->getLogicalIdForNode(mixerNodeId);
        pendingNodeScreenPositions[(int)mixerLid] = ImVec2(playerPos.x + 700.0f, playerPos.y);
        juce::Logger::writeToLog("[MIDI Player Quick Connect] Created Track Mixer at LID " + juce::String((int)mixerLid));
        
        // 3. Connect MIDI Player tracks to PolyVCO
        // Connect ALL tracks, regardless of whether they have notes
        int trackIdx = 0;
        for (size_t i = 0; i < notesByTrack.size() && trackIdx < 32; ++i)
        {
            const int midiPitchPin = trackIdx * 4 + 1;
            const int midiGatePin = trackIdx * 4 + 0;
            const int midiVeloPin = trackIdx * 4 + 2;
            
            const int vcoFreqPin = trackIdx + 1;
            const int vcoWavePin = 32 + trackIdx + 1;
            const int vcoGatePin = 64 + trackIdx + 1;
            
            synth->connect(midiPlayerNodeId, midiPitchPin, polyVCONodeId, vcoFreqPin);
            synth->connect(midiPlayerNodeId, midiGatePin, polyVCONodeId, vcoGatePin);
            synth->connect(midiPlayerNodeId, midiVeloPin, polyVCONodeId, vcoWavePin);
            trackIdx++;
        }
        
        // 4. Connect Num Tracks to PolyVCO (Num Voices Mod on channel 0)
        synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, 
                       polyVCONodeId, 0);
        
        // 5. Connect PolyVCO outputs to Track Mixer inputs
        for (int i = 0; i < trackIdx; ++i)
        {
            synth->connect(polyVCONodeId, i, mixerNodeId, i);
        }
        
        // 6. Connect Num Tracks output to mixer's Num Tracks Mod input
        synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, 
                       mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS);
        
        // 7. Connect Track Mixer to main output
        auto outputNodeId = synth->getOutputNodeID();
        synth->connect(mixerNodeId, 0, outputNodeId, 0); // L
        synth->connect(mixerNodeId, 1, outputNodeId, 1); // R
        
        juce::Logger::writeToLog("[MIDI Player Quick Connect] Connected " + juce::String(trackIdx) + 
                                " tracks: MIDI Player → PolyVCO → Track Mixer → Output");
    }
    
    if (requestType == 2 || requestType == 3) // Samplers or Both
    {
        float samplerX = playerPos.x + 400.0f;
        float mixerX = playerPos.x + 700.0f;
        
        // If PolyVCO mode (Both), offset samplers and use same mixer
        if (requestType == 3)
        {
            samplerX += 300.0f; // Offset samplers if PolyVCO exists
            // Reuse existing mixer created in PolyVCO section
        }
        else
        {
            // 1. Create Track Mixer for Samplers-only mode
            auto mixerNodeId = synth->addModule("trackmixer");
            mixerLid = synth->getLogicalIdForNode(mixerNodeId);
            pendingNodeScreenPositions[(int)mixerLid] = ImVec2(mixerX, playerPos.y);
            juce::Logger::writeToLog("[MIDI Player Quick Connect] Created Track Mixer at LID " + juce::String((int)mixerLid));
        }
        
        // 2. Create samplers and connect
        // Connect ALL tracks, regardless of whether they have notes
        auto mixerNodeId = synth->getNodeIdForLogical(mixerLid);
        int trackIdx = 0;
        int totalTracks = (int)notesByTrack.size();
        int mixerStartChannel = (requestType == 3) ? totalTracks : 0; // Offset for "Both" mode
        
        for (size_t i = 0; i < notesByTrack.size(); ++i)
        {
            // Create SampleLoader
            float samplerY = playerPos.y + (trackIdx * 150.0f);
            auto samplerNodeId = synth->addModule("Sample Loader");
            juce::uint32 samplerLid = synth->getLogicalIdForNode(samplerNodeId);
            pendingNodeScreenPositions[(int)samplerLid] = ImVec2(samplerX, samplerY);
            
            const int midiPitchPin = trackIdx * 4 + 1;
            const int midiGatePin = trackIdx * 4 + 0;
            const int midiTrigPin = trackIdx * 4 + 3;
            
            // Connect MIDI Player to Sampler
            synth->connect(midiPlayerNodeId, midiPitchPin, samplerNodeId, 0);
            synth->connect(midiPlayerNodeId, midiGatePin, samplerNodeId, 2);
            synth->connect(midiPlayerNodeId, midiTrigPin, samplerNodeId, 3);
            
            // Connect Sampler output to Track Mixer input
            synth->connect(samplerNodeId, 0, mixerNodeId, mixerStartChannel + trackIdx);
            
            trackIdx++;
        }
        
        // 3. Connect Num Tracks to mixer and route to output (only if not already done in PolyVCO mode)
        if (requestType != 3)
        {
            synth->connect(midiPlayerNodeId, MIDIPlayerModuleProcessor::kRawNumTracksChannelIndex, 
                           mixerNodeId, TrackMixerModuleProcessor::MAX_TRACKS);
            
            // 4. Connect Track Mixer to output
            auto outputNodeId = synth->getOutputNodeID();
            synth->connect(mixerNodeId, 0, outputNodeId, 0);
            synth->connect(mixerNodeId, 1, outputNodeId, 1);
            
            juce::Logger::writeToLog("[MIDI Player Quick Connect] Complete chain: " + juce::String(trackIdx) + 
                                    " SampleLoaders → Track Mixer (with Num Tracks) → Stereo Output");
        }
        else
        {
            juce::Logger::writeToLog("[MIDI Player Quick Connect] Connected " + juce::String(trackIdx) + 
                                    " SampleLoaders → Track Mixer (channels " + juce::String(mixerStartChannel) + 
                                    "-" + juce::String(mixerStartChannel + trackIdx - 1) + 
                                    ") [Mixer already connected in PolyVCO section]");
        }
    }
    
    // Commit changes
    if (synth)
    {
        synth->commitChanges();
        graphNeedsRebuild = true;
    }
    
    pushSnapshot();
}

void ImGuiNodeEditorComponent::drawInsertNodeOnLinkPopup()
{
    if (ImGui::BeginPopup("InsertNodeOnLinkPopup"))
    {
        const int numSelected = ImNodes::NumSelectedLinks();
        const bool isMultiInsert = numSelected > 1;

        // --- FIX: Use map to separate display names from internal type names ---
        // Map format: {Display Name, Internal Type}
        // Internal types use lowercase with underscores for spaces
        const std::map<const char*, const char*> audioInsertable = {
            {"VCF", "vcf"}, {"VCA", "vca"}, {"Delay", "delay"}, {"Reverb", "reverb"},
            {"Chorus", "chorus"}, {"Phaser", "phaser"}, {"Compressor", "compressor"},
            {"Recorder", "recorder"}, {"Limiter", "limiter"}, {"Gate", "gate"}, {"Drive", "drive"},
            {"Graphic EQ", "graphic_eq"}, {"Waveshaper", "waveshaper"}, {"Time/Pitch Shifter", "timepitch"},
            {"Attenuverter", "attenuverter"}, {"De-Crackle", "de_crackle"}, {"Mixer", "mixer"},
            {"Shaping Oscillator", "shaping_oscillator"}, {"Function Generator", "function_generator"},
            {"8-Band Shaper", "8bandshaper"},
            {"Granulator", "granulator"}, {"Harmonic Shaper", "harmonic_shaper"},
            {"Vocal Tract Filter", "vocal_tract_filter"}, {"Scope", "scope"}
        };
        const std::map<const char*, const char*> modInsertable = {
            {"Attenuverter", "attenuverter"}, {"Lag Processor", "lag_processor"}, {"Math", "math"},
            {"MapRange", "map_range"}, {"Quantizer", "quantizer"}, {"S&H", "s_and_h"},
            {"Rate", "rate"}, {"Logic", "logic"}, {"Comparator", "comparator"},
            {"CV Mixer", "cv_mixer"}, {"Sequential Switch", "sequential_switch"}
        };
        const auto& listToShow = linkToInsertOn.isMod ? modInsertable : audioInsertable;

        if (isMultiInsert)
            ImGui::Text("Insert Node on %d Cables", numSelected);
        else
            ImGui::Text("Insert Node on Cable");

        // --- FIX: Iterate over map pairs instead of simple strings ---
        for (const auto& pair : listToShow)
        {
            // pair.first = display label, pair.second = internal type
            if (ImGui::MenuItem(pair.first))
            {
                if (isMultiInsert)
                {
                    handleInsertNodeOnSelectedLinks(pair.second);
                }
                else
                {
                    insertNodeBetween(pair.second);
                }
                ImGui::CloseCurrentPopup();
            }
        }
        
        // VST Plugins submenu (only for audio cables)
        if (!linkToInsertOn.isMod)
        {
            ImGui::Separator();
            if (ImGui::BeginMenu("VST"))
            {
                auto& app = PresetCreatorApplication::getApp();
                auto& knownPluginList = app.getKnownPluginList();
                
                for (const auto& desc : knownPluginList.getTypes())
                {
                    if (ImGui::MenuItem(desc.name.toRawUTF8()))
                    {
                        if (isMultiInsert)
                        {
                            handleInsertNodeOnSelectedLinks(desc.name);
                        }
                        else
                        {
                            insertNodeBetween(desc.name);
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    
                    // Show tooltip with plugin info
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Manufacturer: %s", desc.manufacturerName.toRawUTF8());
                        ImGui::Text("Version: %s", desc.version.toRawUTF8());
                        ImGui::EndTooltip();
                    }
                }
                ImGui::EndMenu();
            }
        }
        
        ImGui::EndPopup();
    }
    else
    {
        // --- FIX: Reset state when popup is closed ---
        // If the popup is not open (i.e., it was closed or the user clicked away),
        // we must reset the state variable. This ensures that the application
        // is no longer "stuck" in the insert-on-link mode and right-click on
        // empty canvas will work again.
        linkToInsertOn.linkId = -1;
    }
}

// --- NEW HELPER FUNCTION ---
void ImGuiNodeEditorComponent::insertNodeOnLink(const juce::String& nodeType, const LinkInfo& linkInfo, const ImVec2& position)
{
    if (synth == nullptr) return;

    PinDataType srcType = getPinDataTypeForPin(linkInfo.srcPin);
    PinDataType dstType = getPinDataTypeForPin(linkInfo.dstPin);

    // 1. Create and Position the New Node
    // Check if this is a VST plugin by checking against known plugins
    juce::AudioProcessorGraph::NodeID newNodeId;
    auto& app = PresetCreatorApplication::getApp();
    auto& knownPluginList = app.getKnownPluginList();
    bool isVst = false;
    
    for (const auto& desc : knownPluginList.getTypes())
    {
        if (desc.name == nodeType)
        {
            // This is a VST plugin - use addVstModule
            newNodeId = synth->addVstModule(app.getPluginFormatManager(), desc);
            isVst = true;
            break;
        }
    }
    
    if (!isVst)
    {
        // Regular module - use addModule
        newNodeId = synth->addModule(nodeType);
    }
    
    auto newNodeLid = synth->getLogicalIdForNode(newNodeId);
    pendingNodeScreenPositions[(int)newNodeLid] = position;

    // 2. Get Original Connection Points
    auto originalSrcNodeId = synth->getNodeIdForLogical(linkInfo.srcPin.logicalId);
    auto originalDstNodeId = (linkInfo.dstPin.logicalId == 0) 
        ? synth->getOutputNodeID() 
        : synth->getNodeIdForLogical(linkInfo.dstPin.logicalId);

    // 3. Disconnect the Original Link
    synth->disconnect(originalSrcNodeId, linkInfo.srcPin.channel, originalDstNodeId, linkInfo.dstPin.channel);

    // 4. Configure newly inserted node if necessary (e.g., MapRange)
    int newNodeOutputChannel = 0;
    if (nodeType == "MapRange")
    {
        if (auto* mapRange = dynamic_cast<MapRangeModuleProcessor*>(synth->getModuleForLogical(newNodeLid)))
        {
            Range inRange = getSourceRange(linkInfo.srcPin, synth);
            configureMapRangeFor(srcType, dstType, *mapRange, inRange);
            newNodeOutputChannel = (dstType == PinDataType::Audio) ? 1 : 0;
        }
    }

    // 5. Reconnect Through the New Node
    synth->connect(originalSrcNodeId, linkInfo.srcPin.channel, newNodeId, 0);
    synth->connect(newNodeId, newNodeOutputChannel, originalDstNodeId, linkInfo.dstPin.channel);
}

void ImGuiNodeEditorComponent::insertNodeOnLinkStereo(const juce::String& nodeType, 
                                                       const LinkInfo& linkLeft, 
                                                       const LinkInfo& linkRight, 
                                                       const ImVec2& position)
{
    if (synth == nullptr) return;

    juce::Logger::writeToLog("[InsertStereo] Inserting stereo node: " + nodeType);
    juce::Logger::writeToLog("[InsertStereo] Left cable: " + juce::String(linkLeft.srcPin.logicalId) + 
                            " ch" + juce::String(linkLeft.srcPin.channel) + " -> " + 
                            juce::String(linkLeft.dstPin.logicalId) + " ch" + juce::String(linkLeft.dstPin.channel));
    juce::Logger::writeToLog("[InsertStereo] Right cable: " + juce::String(linkRight.srcPin.logicalId) + 
                            " ch" + juce::String(linkRight.srcPin.channel) + " -> " + 
                            juce::String(linkRight.dstPin.logicalId) + " ch" + juce::String(linkRight.dstPin.channel));

    // 1. Create ONE node for both channels
    juce::AudioProcessorGraph::NodeID newNodeId;
    auto& app = PresetCreatorApplication::getApp();
    auto& knownPluginList = app.getKnownPluginList();
    bool isVst = false;
    
    for (const auto& desc : knownPluginList.getTypes())
    {
        if (desc.name == nodeType)
        {
            newNodeId = synth->addVstModule(app.getPluginFormatManager(), desc);
            isVst = true;
            break;
        }
    }
    
    if (!isVst)
    {
        newNodeId = synth->addModule(nodeType);
    }
    
    auto newNodeLid = synth->getLogicalIdForNode(newNodeId);
    pendingNodeScreenPositions[(int)newNodeLid] = position;

    // 2. Get Original Connection Points for LEFT cable (first cable)
    auto leftSrcNodeId = synth->getNodeIdForLogical(linkLeft.srcPin.logicalId);
    auto leftDstNodeId = (linkLeft.dstPin.logicalId == 0) 
        ? synth->getOutputNodeID() 
        : synth->getNodeIdForLogical(linkLeft.dstPin.logicalId);

    // 3. Get Original Connection Points for RIGHT cable (second cable)
    auto rightSrcNodeId = synth->getNodeIdForLogical(linkRight.srcPin.logicalId);
    auto rightDstNodeId = (linkRight.dstPin.logicalId == 0) 
        ? synth->getOutputNodeID() 
        : synth->getNodeIdForLogical(linkRight.dstPin.logicalId);

    // 4. Disconnect BOTH Original Links (using their actual source/dest channels)
    synth->disconnect(leftSrcNodeId, linkLeft.srcPin.channel, leftDstNodeId, linkLeft.dstPin.channel);
    synth->disconnect(rightSrcNodeId, linkRight.srcPin.channel, rightDstNodeId, linkRight.dstPin.channel);

    // 5. Reconnect Through the New Node
    // Left cable -> new node's LEFT input (ch0)
    synth->connect(leftSrcNodeId, linkLeft.srcPin.channel, newNodeId, 0);
    
    // Right cable -> new node's RIGHT input (ch1)
    synth->connect(rightSrcNodeId, linkRight.srcPin.channel, newNodeId, 1);
    
    // New node's outputs -> original destinations
    // Note: We'll connect both outputs to their respective destinations
    synth->connect(newNodeId, 0, leftDstNodeId, linkLeft.dstPin.channel);
    synth->connect(newNodeId, 1, rightDstNodeId, linkRight.dstPin.channel);

    juce::Logger::writeToLog("[InsertStereo] Successfully inserted stereo node with separate sources/destinations");
}

// --- REFACTORED OLD FUNCTION ---
void ImGuiNodeEditorComponent::insertNodeBetween(const juce::String& nodeType, const PinID& srcPin, const PinID& dstPin)
{
    if (synth == nullptr) return;

    // 1. Get positions to place the new node between the source and destination
    ImVec2 srcPos = ImNodes::GetNodeGridSpacePos(srcPin.logicalId);
    ImVec2 dstPos = ImNodes::GetNodeGridSpacePos(dstPin.logicalId == 0 ? 0 : dstPin.logicalId);
    ImVec2 newNodePos = ImVec2((srcPos.x + dstPos.x) * 0.5f, (srcPos.y + dstPos.y) * 0.5f);

    // 2. Create and position the new converter node
    // Check if this is a VST plugin
    juce::AudioProcessorGraph::NodeID newNodeId;
    auto& app = PresetCreatorApplication::getApp();
    auto& knownPluginList = app.getKnownPluginList();
    bool isVst = false;
    
    for (const auto& desc : knownPluginList.getTypes())
    {
        if (desc.name == nodeType)
        {
            newNodeId = synth->addVstModule(app.getPluginFormatManager(), desc);
            isVst = true;
            break;
        }
    }
    
    if (!isVst)
    {
        newNodeId = synth->addModule(nodeType);
    }
    
    auto newNodeLid = synth->getLogicalIdForNode(newNodeId);
    pendingNodePositions[(int)newNodeLid] = newNodePos;

    // 3. Get original node IDs
    auto originalSrcNodeId = synth->getNodeIdForLogical(srcPin.logicalId);
    auto originalDstNodeId = (dstPin.logicalId == 0)
        ? synth->getOutputNodeID()
        : synth->getNodeIdForLogical(dstPin.logicalId);

    // 4. Configure the new node if it's a MapRange or Attenuverter
    int newNodeOutputChannel = 0;
    if (nodeType == "MapRange") {
        if (auto* mapRange = dynamic_cast<MapRangeModuleProcessor*>(synth->getModuleForLogical(newNodeLid))) {
            PinDataType srcType = getPinDataTypeForPin(srcPin);
            PinDataType dstType = getPinDataTypeForPin(dstPin);
            Range inRange = getSourceRange(srcPin, synth);
            configureMapRangeFor(srcType, dstType, *mapRange, inRange);
            newNodeOutputChannel = (dstType == PinDataType::Audio) ? 1 : 0; // Use Raw Out for Audio, Norm Out for CV
        }
    } else if (nodeType == "Attenuverter") {
        // You might want to pre-configure the Attenuverter here if needed
    }

    // 5. Connect the signal chain: Original Source -> New Node -> Original Destination
    synth->connect(originalSrcNodeId, srcPin.channel, newNodeId, 0); // Source -> New Node's first input
    synth->connect(newNodeId, newNodeOutputChannel, originalDstNodeId, dstPin.channel); // New Node -> Destination

    juce::Logger::writeToLog("[AutoConvert] Inserted '" + nodeType + "' between " + juce::String(srcPin.logicalId) + " and " + juce::String(dstPin.logicalId));
}

void ImGuiNodeEditorComponent::insertNodeBetween(const juce::String& nodeType)
{
    // This function is now just a wrapper that calls the helper
    // with the stored link info and the current mouse position.
    if (linkToInsertOn.linkId != -1)
    {
        insertNodeOnLink(nodeType, linkToInsertOn, ImGui::GetMousePos());
        graphNeedsRebuild = true;
        pushSnapshot();
        linkToInsertOn.linkId = -1; // Reset state
    }
}

void ImGuiNodeEditorComponent::handleInsertNodeOnSelectedLinks(const juce::String& nodeType)
{
    if (synth == nullptr || ImNodes::NumSelectedLinks() == 0) return;

    pushSnapshot(); // Create one undo state for the entire batch operation.

    const int numSelectedLinks = ImNodes::NumSelectedLinks();
    std::vector<int> selectedLinkIds(numSelectedLinks);
    ImNodes::GetSelectedLinks(selectedLinkIds.data());

    ImVec2 basePosition = ImGui::GetMousePos();
    float x_offset = 0.0f;

    // === OPTION A: If exactly 2 audio cables are selected, insert ONE stereo node ===
    if (numSelectedLinks == 2)
    {
        // Get info for both cables
        auto it0 = linkIdToAttrs.find(selectedLinkIds[0]);
        auto it1 = linkIdToAttrs.find(selectedLinkIds[1]);
        
        if (it0 != linkIdToAttrs.end() && it1 != linkIdToAttrs.end())
        {
            LinkInfo link0, link1;
            link0.linkId = selectedLinkIds[0];
            link0.srcPin = decodePinId(it0->second.first);
            link0.dstPin = decodePinId(it0->second.second);
            link0.isMod = link0.srcPin.isMod || link0.dstPin.isMod;
            
            link1.linkId = selectedLinkIds[1];
            link1.srcPin = decodePinId(it1->second.first);
            link1.dstPin = decodePinId(it1->second.second);
            link1.isMod = link1.srcPin.isMod || link1.dstPin.isMod;
            
            // Check if BOTH are audio cables (not mod cables)
            if (!link0.isMod && !link1.isMod)
            {
                // Create ONE stereo node with link0 -> Left (ch0), link1 -> Right (ch1)
                insertNodeOnLinkStereo(nodeType, link0, link1, basePosition);
                juce::Logger::writeToLog("[InsertNode] Inserted STEREO node for 2 selected audio cables");
                graphNeedsRebuild = true;
                return; // Done - we've handled both cables with one node
            }
        }
    }

    // === FALLBACK: Multiple cables or mixed mod/audio - insert separate nodes ===
    std::set<int> processedLinks; // Track which links we've already handled
    
    for (size_t i = 0; i < selectedLinkIds.size(); ++i)
    {
        int linkId = selectedLinkIds[i];
        if (processedLinks.count(linkId)) continue;

        auto it = linkIdToAttrs.find(linkId);
        if (it == linkIdToAttrs.end()) continue;

        LinkInfo currentLink;
        currentLink.linkId = linkId;
        currentLink.srcPin = decodePinId(it->second.first);
        currentLink.dstPin = decodePinId(it->second.second);
        currentLink.isMod = currentLink.srcPin.isMod || currentLink.dstPin.isMod;

        ImVec2 newPosition = ImVec2(basePosition.x + x_offset, basePosition.y);
        
        // === MONO INSERT: Create separate node for each cable ===
        insertNodeOnLink(nodeType, currentLink, newPosition);
        processedLinks.insert(linkId);
        juce::Logger::writeToLog("[InsertNode] Inserted MONO node for link " + juce::String(linkId));

        x_offset += 40.0f;
    }

    graphNeedsRebuild = true;
    // The single pushSnapshot at the beginning handles the undo state.
}

juce::File ImGuiNodeEditorComponent::findPresetsDirectory()
{
    // Search upwards from the executable's location for a sibling directory
    // named "Synth_presets". This is robust to different build configurations.
    juce::File dir = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    for (int i = 0; i < 8; ++i) // Limit search depth to 8 levels
    {
        dir = dir.getParentDirectory();
        if (!dir.exists()) break;

        juce::File candidate = dir.getSiblingFile("Synth_presets");
        if (candidate.isDirectory())
        {
            return candidate;
        }
    }
    
    // Fallback to an empty file (system default) if not found
    return {};
}

// Helper function implementations
PinDataType ImGuiNodeEditorComponent::getPinDataTypeForPin(const PinID& pin)
{
    if (synth == nullptr) return PinDataType::Raw;

    // Handle the main output node as a special case
    if (pin.logicalId == 0)
    {
        return PinDataType::Audio;
    }

    juce::String moduleType = getTypeForLogical(pin.logicalId);
    if (moduleType.isEmpty()) return PinDataType::Raw;

    // *** NEW: Check dynamic pins FIRST ***
    if (auto* module = synth->getModuleForLogical(pin.logicalId))
    {
        // Check dynamic input pins
        if (pin.isInput && !pin.isMod)
        {
            auto dynamicInputs = module->getDynamicInputPins();
            for (const auto& dynPin : dynamicInputs)
            {
                if (dynPin.channel == pin.channel)
                {
                    return dynPin.type;
                }
            }
        }
        // Check dynamic output pins
        else if (!pin.isInput && !pin.isMod)
        {
            auto dynamicOutputs = module->getDynamicOutputPins();
            for (const auto& dynPin : dynamicOutputs)
            {
                if (dynPin.channel == pin.channel)
                {
                    return dynPin.type;
                }
            }
        }
    }
    // *** END NEW CODE ***

    auto it = getModulePinDatabase().find(moduleType);
    if (it == getModulePinDatabase().end())
    {
        // Fallback: case-insensitive lookup (module registry may use different casing)
        juce::String moduleTypeLower = moduleType.toLowerCase();
        for (const auto& kv : getModulePinDatabase())
        {
            if (kv.first.compareIgnoreCase(moduleType) == 0 || kv.first.toLowerCase() == moduleTypeLower)
            {
                it = getModulePinDatabase().find(kv.first);
                break;
            }
        }
        if (it == getModulePinDatabase().end())
        {
            // If the module type is not in our static database, it's likely a VST plugin.
            // A safe assumption is that its pins are for audio.
            if (auto* module = synth->getModuleForLogical(pin.logicalId))
            {
                if (dynamic_cast<VstHostModuleProcessor*>(module))
                {
                    return PinDataType::Audio; // Green for VST pins
                }
            }
            return PinDataType::Raw;
        }
    }

    const auto& pinInfo = it->second;

    if (pin.isMod)
    {
        for (const auto& modPin : pinInfo.modIns)
        {
            if (modPin.paramId == pin.paramId)
            {
                return modPin.type;
            }
        }
    }
    else // It's an audio pin
    {
        const auto& pins = pin.isInput ? pinInfo.audioIns : pinInfo.audioOuts;
        for (const auto& audioPin : pins)
        {
            if (audioPin.channel == pin.channel)
            {
                return audioPin.type;
            }
        }
    }
    return PinDataType::Raw; // Fallback
}

unsigned int ImGuiNodeEditorComponent::getImU32ForType(PinDataType type)
{
    switch (type)
    {
        case PinDataType::CV:    return IM_COL32(100, 150, 255, 255); // Blue
        case PinDataType::Audio: return IM_COL32(100, 255, 150, 255); // Green
        case PinDataType::Gate:  return IM_COL32(255, 220, 100, 255); // Yellow
        case PinDataType::Raw:   return IM_COL32(255, 100, 100, 255); // Red
        default:                 return IM_COL32(150, 150, 150, 255); // Grey
    }
}

const char* ImGuiNodeEditorComponent::pinDataTypeToString(PinDataType type)
{
    switch (type)
    {
        case PinDataType::CV:    return "CV (0 to 1)";
        case PinDataType::Audio: return "Audio (-1 to 1)";
        case PinDataType::Gate:  return "Gate/Trigger";
        case PinDataType::Raw:   return "Raw";
        default:                 return "Unknown";
    }
}

// Add this new function implementation to the .cpp file.

void ImGuiNodeEditorComponent::handleNodeChaining()
{
    if (synth == nullptr) return;

    const int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected <= 1) return;

    juce::Logger::writeToLog("[Node Chaining] Initiated for " + juce::String(numSelected) + " nodes.");

    // 1. Get all selected nodes and their horizontal positions.
    std::vector<int> selectedNodeIds(numSelected);
    ImNodes::GetSelectedNodes(selectedNodeIds.data());

    std::vector<std::pair<float, int>> sortedNodes;
    for (int nodeId : selectedNodeIds)
    {
        // Don't include the main output node in the chaining logic.
        if (nodeId == 0) continue;
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodeId);
        sortedNodes.push_back({pos.x, nodeId});
    }

    // 2. Sort the nodes from left to right based on their X position.
    std::sort(sortedNodes.begin(), sortedNodes.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    // Create a single undo action for the entire operation.
    pushSnapshot();

    // 3. Connect the nodes in sequence.
    for (size_t i = 0; i < sortedNodes.size() - 1; ++i)
    {
        juce::uint32 sourceLid = sortedNodes[i].second;
        juce::uint32 destLid   = sortedNodes[i + 1].second;

        auto sourceNodeId = synth->getNodeIdForLogical(sourceLid);
        auto destNodeId   = synth->getNodeIdForLogical(destLid);

        if (sourceNodeId.uid != 0 && destNodeId.uid != 0)
        {
            // Standard stereo connection: Out L -> In L, Out R -> In R
            synth->connect(sourceNodeId, 0, destNodeId, 0); // Connect channel 0
            synth->connect(sourceNodeId, 1, destNodeId, 1); // Connect channel 1

            juce::Logger::writeToLog("[Node Chaining] Connected " + getTypeForLogical(sourceLid) + " (" + juce::String(sourceLid) + ") to " + getTypeForLogical(destLid) + " (" + juce::String(destLid) + ")");
            
            // Check if the destination is a recorder and update its filename
            if (auto* destModule = synth->getModuleForLogical(destLid))
            {
                if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(destModule))
                {
                    if (auto* sourceModule = synth->getModuleForLogical(sourceLid))
                    {
                        recorder->updateSuggestedFilename(sourceModule->getName());
                    }
                }
            }
        }
    }

    // 4. Apply all the new connections to the audio graph.
    graphNeedsRebuild = true;
}

// Add this new helper function implementation.

std::vector<AudioPin> ImGuiNodeEditorComponent::getPinsOfType(juce::uint32 logicalId, bool isInput, PinDataType targetType)
{
    std::vector<AudioPin> matchingPins;
    juce::String moduleType = getTypeForLogical(logicalId);

    if (moduleType.isEmpty())
    {
        return matchingPins;
    }

    auto it = getModulePinDatabase().find(moduleType);

    // --- CASE-INSENSITIVE LOOKUP ---
    if (it == getModulePinDatabase().end())
    {
        for (const auto& kv : getModulePinDatabase())
        {
            if (kv.first.compareIgnoreCase(moduleType) == 0)
            {
                it = getModulePinDatabase().find(kv.first);
                break;
            }
        }
    }

    if (it != getModulePinDatabase().end())
    {
        // --- Standard path for built-in modules ---
        const auto& pins = isInput ? it->second.audioIns : it->second.audioOuts;
        for (const auto& pin : pins)
        {
            if (pin.type == targetType)
            {
                matchingPins.push_back(pin);
            }
        }
    }
    else if (auto* module = synth->getModuleForLogical(logicalId))
    {
        // --- DYNAMIC PATH FOR MODULES WITH getDynamicInputPins/getDynamicOutputPins ---
        auto dynamicPins = isInput ? module->getDynamicInputPins() : module->getDynamicOutputPins();
        
        if (!dynamicPins.empty())
        {
            // Module provides dynamic pins - filter by type
            for (const auto& pin : dynamicPins)
            {
                if (pin.type == targetType)
                {
                    matchingPins.emplace_back(pin.name, pin.channel, pin.type);
                }
            }
        }
        else if (dynamic_cast<VstHostModuleProcessor*>(module))
        {
            // For VSTs without dynamic pins, assume all pins are 'Audio' type for chaining.
            if (targetType == PinDataType::Audio)
            {
                const int numChannels = isInput ? module->getTotalNumInputChannels() : module->getTotalNumOutputChannels();
                for (int i = 0; i < numChannels; ++i)
                {
                    juce::String pinName = isInput ? module->getAudioInputLabel(i) : module->getAudioOutputLabel(i);
                    if (pinName.isNotEmpty())
                    {
                        matchingPins.emplace_back(pinName, i, PinDataType::Audio);
                    }
                }
            }
        }
    }

    return matchingPins;
}

// Add this new function implementation to the .cpp file.

void ImGuiNodeEditorComponent::handleRecordOutput()
{
    if (!synth) return;

    pushSnapshot();
    juce::Logger::writeToLog("[Record Output] Initiated.");

    // 1. Find connections going to the main output node.
    std::vector<ModularSynthProcessor::ConnectionInfo> outputFeeds;
    for (const auto& c : synth->getConnectionsInfo())
    {
        if (c.dstIsOutput)
        {
            outputFeeds.push_back(c);
        }
    }

    if (outputFeeds.empty())
    {
        juce::Logger::writeToLog("[Record Output] No connections to main output found.");
        return;
    }

    // 2. Create and position the recorder.
    auto recorderNodeId = synth->addModule("recorder");
    auto recorderLid = synth->getLogicalIdForNode(recorderNodeId);
    ImVec2 outPos = ImNodes::GetNodeGridSpacePos(0);
    pendingNodePositions[(int)recorderLid] = ImVec2(outPos.x - 400.0f, outPos.y);
    
    auto* recorder = dynamic_cast<RecordModuleProcessor*>(synth->getModuleForLogical(recorderLid));
    if (recorder)
    {
        recorder->setPropertiesFile(PresetCreatorApplication::getApp().getProperties());
    }

    // 3. "Tap" the signals by connecting the original sources to the recorder.
    juce::String sourceName;
    for (const auto& feed : outputFeeds)
    {
        auto srcNodeId = synth->getNodeIdForLogical(feed.srcLogicalId);
        synth->connect(srcNodeId, feed.srcChan, recorderNodeId, feed.dstChan); // dstChan will be 0 or 1
        
        // Get the name of the first source for the filename prefix
        if (sourceName.isEmpty())
        {
            if (auto* srcModule = synth->getModuleForLogical(feed.srcLogicalId))
            {
                sourceName = srcModule->getName();
            }
        }
    }
    
    if (recorder)
    {
        recorder->updateSuggestedFilename(sourceName);
    }

    graphNeedsRebuild = true;
    juce::Logger::writeToLog("[Record Output] Recorder added and connected.");
}

void ImGuiNodeEditorComponent::handleColorCodedChaining(PinDataType targetType)
{
    if (synth == nullptr)
    {
        juce::Logger::writeToLog("[Color Chaining] ERROR: synth is nullptr");
        return;
    }

    const int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected <= 1)
    {
        juce::Logger::writeToLog("[Color Chaining] ERROR: numSelected <= 1 (" + juce::String(numSelected) + ")");
        return;
    }

    juce::Logger::writeToLog("[Color Chaining] Started for " + juce::String(toString(targetType)) + " with " + juce::String(numSelected) + " nodes");

    // 1. Get and sort selected nodes by their horizontal position.
    std::vector<int> selectedNodeIds(numSelected);
    ImNodes::GetSelectedNodes(selectedNodeIds.data());

    std::vector<std::pair<float, int>> sortedNodes;
    for (int nodeId : selectedNodeIds)
    {
        if (nodeId == 0) continue; // Exclude the output node.
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodeId);
        sortedNodes.push_back({pos.x, nodeId});
    }

    if (sortedNodes.empty())
    {
        juce::Logger::writeToLog("[Color Chaining] ERROR: No valid nodes after filtering");
        return;
    }

    std::sort(sortedNodes.begin(), sortedNodes.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    // Create a single undo action for the entire operation.
    pushSnapshot();

    int totalConnectionsMade = 0;
    int totalConnectionAttempts = 0;

    // 2. Iterate through sorted nodes and connect matching pins.
    for (size_t i = 0; i < sortedNodes.size() - 1; ++i)
    {
        juce::uint32 sourceLid = sortedNodes[i].second;
        juce::uint32 destLid   = sortedNodes[i + 1].second;

        auto sourceNodeId = synth->getNodeIdForLogical(sourceLid);
        auto destNodeId   = synth->getNodeIdForLogical(destLid);

        if (sourceNodeId.uid == 0 || destNodeId.uid == 0)
        {
            juce::Logger::writeToLog("[Color Chaining] Skipping invalid node pair: " + juce::String(sourceLid) + " -> " + juce::String(destLid));
            continue;
        }

        // Find all matching output pins on the source and input pins on the destination.
        auto sourcePins = getPinsOfType(sourceLid, false, targetType);
        auto destPins   = getPinsOfType(destLid, true, targetType);

        if (sourcePins.empty() || destPins.empty())
        {
            juce::Logger::writeToLog("[Color Chaining] No matching pins: " + juce::String(sourcePins.size()) + " src, " + juce::String(destPins.size()) + " dst");
            continue;
        }

        // Connect them one-to-one until we run out of available pins on either side.
        int connectionsToMake = std::min((int)sourcePins.size(), (int)destPins.size());

        for (int j = 0; j < connectionsToMake; ++j)
        {
            totalConnectionAttempts++;
            bool connectResult = synth->connect(sourceNodeId, sourcePins[j].channel, destNodeId, destPins[j].channel);
            if (connectResult)
            {
                totalConnectionsMade++;
                juce::Logger::writeToLog("[Color Chaining] Connected " + getTypeForLogical(sourceLid) + " -> " + getTypeForLogical(destLid));

                // Check if the destination is a recorder and update its filename
                if (auto* destModule = synth->getModuleForLogical(destLid))
                {
                    if (auto* recorder = dynamic_cast<RecordModuleProcessor*>(destModule))
                    {
                        if (auto* sourceModule = synth->getModuleForLogical(sourceLid))
                        {
                            recorder->updateSuggestedFilename(sourceModule->getName());
                        }
                    }
                }
            }
        }
    }

    juce::Logger::writeToLog("[Color Chaining] Completed: " + juce::String(totalConnectionsMade) + "/" + juce::String(totalConnectionAttempts) + " connections made");

    // 3. Apply all new connections to the audio graph.
    graphNeedsRebuild = true;
}

// Module Category Color Coding
ImGuiNodeEditorComponent::ModuleCategory ImGuiNodeEditorComponent::getModuleCategory(const juce::String& moduleType)
{
    juce::String lower = moduleType.toLowerCase();
    
    // --- MIDI Family (Vibrant Purple) ---
    if (lower.contains("midi"))
        return ModuleCategory::MIDI;
    
    // --- Physics Family (Cyan) ---
    if (lower.contains("physics"))
        return ModuleCategory::Physics;
    
    // --- Sources (Green) ---
    // Check specific matches first to avoid substring conflicts
    if (lower == "tts performer")  // Explicit TTS categorization
        return ModuleCategory::Source;
    
    if (lower.contains("vco") || lower.contains("noise") || 
        lower.contains("sequencer") || lower.contains("sample") || 
        lower.contains("input") ||
        lower.contains("polyvco") || lower.contains("value")) 
        return ModuleCategory::Source;
    
    // --- Effects (Red) ---
    // Check "Vocal Tract Filter" before general "filter" check
    if (lower == "vocal tract filter")
        return ModuleCategory::Effect;
    
    if (lower.contains("vcf") || lower.contains("delay") || 
        lower.contains("reverb") || lower.contains("chorus") || 
        lower.contains("phaser") || lower.contains("compressor") || 
        lower.contains("drive") || lower.contains("shaper") ||  // Note: "shaping oscillator" handled above
        lower.contains("filter") || lower.contains("waveshaper") ||
        lower.contains("limiter") || lower.contains("gate") ||
        lower.contains("granulator") || lower.contains("eq") ||
        lower.contains("crackle") || lower.contains("timepitch") ||
        lower.contains("recorder"))  // Moved from Analysis
        return ModuleCategory::Effect;
    
    // --- Modulators (Blue) ---
    if (lower.contains("lfo") || lower.contains("adsr") || 
        lower.contains("random") || lower.contains("s&h") || 
        lower.contains("function")) 
        return ModuleCategory::Modulator;
    
    // --- Analysis (Purple) ---
    if (lower.contains("scope") || lower.contains("debug") || 
        lower.contains("graph")) 
        return ModuleCategory::Analysis;
    
    // --- Comment (Grey) ---
    if (lower.contains("comment")) 
        return ModuleCategory::Comment;
    
    // --- Plugins (Teal) ---
    if (lower.contains("vst") || lower.contains("plugin"))
        return ModuleCategory::Plugin;
    
    // --- Utilities & Logic (Orange) - Default ---
    return ModuleCategory::Utility;
}

unsigned int ImGuiNodeEditorComponent::getImU32ForCategory(ModuleCategory category, bool hovered)
{
    ImU32 color;
    switch (category)
    {
        case ModuleCategory::Source:     color = IM_COL32(50, 120, 50, 255); break;   // Green
        case ModuleCategory::Effect:     color = IM_COL32(130, 60, 60, 255); break;   // Red
        case ModuleCategory::Modulator:  color = IM_COL32(50, 50, 130, 255); break;   // Blue
        case ModuleCategory::Utility:    color = IM_COL32(110, 80, 50, 255); break;   // Orange
        case ModuleCategory::Analysis:   color = IM_COL32(100, 50, 110, 255); break;  // Purple
        case ModuleCategory::Comment:    color = IM_COL32(80, 80, 80, 255); break;    // Grey
        case ModuleCategory::Plugin:     color = IM_COL32(50, 110, 110, 255); break;  // Teal
        case ModuleCategory::MIDI:       color = IM_COL32(180, 120, 255, 255); break; // Vibrant Purple
        case ModuleCategory::Physics:    color = IM_COL32(50, 200, 200, 255); break;  // Cyan
        default:                         color = IM_COL32(70, 70, 70, 255); break;
    }
    
    if (hovered) 
    { 
        // Brighten on hover
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
        c.x *= 1.3f; c.y *= 1.3f; c.z *= 1.3f;
        return ImGui::ColorConvertFloat4ToU32(c);
    }
    return color;
}

// Quick Add Menu - Module Registry - Dictionary
// Maps Display Name -> { Internal Type, Description }
std::map<juce::String, std::pair<const char*, const char*>> ImGuiNodeEditorComponent::getModuleRegistry()
{
    return {
        // Sources
        {"Audio Input", {"audio_input", "Records audio from your audio interface"}},
        {"VCO", {"vco", "Voltage Controlled Oscillator - generates waveforms"}},
        {"Polyphonic VCO", {"polyvco", "Polyphonic VCO with multiple voices"}},
        {"Noise", {"noise", "White, pink, or brown noise generator"}},
        {"Sequencer", {"sequencer", "Step sequencer for creating patterns"}},
        {"Multi Sequencer", {"multi_sequencer", "Multi-track step sequencer"}},
        {"Stroke Sequencer", {"stroke_sequencer", "Freeform visual rhythmic and CV generator"}},
        {"MIDI Player", {"midi_player", "Plays MIDI files"}},
        {"MIDI CV", {"midi_cv", "Converts MIDI Note/CC messages to CV signals. (Monophonic)"}},
        {"MIDI Faders", {"midi_faders", "Up to 16 MIDI faders with CC learning"}},
        {"MIDI Knobs", {"midi_knobs", "Up to 16 MIDI knobs/rotary encoders with CC learning"}},
        {"MIDI Buttons", {"midi_buttons", "Up to 32 MIDI buttons with Gate/Toggle/Trigger modes"}},
        {"MIDI Jog Wheel", {"midi_jog_wheel", "Single MIDI jog wheel/rotary encoder"}},
        {"MIDI Pads", {"midi_pads", "16-pad MIDI controller with polyphonic triggers and velocity outputs"}},
        {"MIDI Logger", {"midi_logger", "Records CV/Gate to MIDI events with piano roll editor and .mid export"}},
        {"Value", {"value", "Constant CV value output"}},
        {"Sample Loader", {"sample_loader", "Loads and plays audio samples"}},
        
        // TTS
        {"TTS Performer", {"tts_performer", "Text-to-speech synthesizer"}},
        {"Vocal Tract Filter", {"vocal_tract_filter", "Physical model vocal tract filter"}},
        
        // Physics & Animation
        {"Physics", {"physics", "2D physics simulation for audio modulation"}},
        {"Animation", {"animation", "Skeletal animation system with glTF file support"}},
        
        // Effects
        {"VCF", {"vcf", "Voltage Controlled Filter"}},
        {"Delay", {"delay", "Echo/delay effect"}},
        {"Reverb", {"reverb", "Reverb effect"}},
        {"Chorus", {"chorus", "Chorus effect for thickening sound"}},
        {"Phaser", {"phaser", "Phaser modulation effect"}},
        {"Compressor", {"compressor", "Dynamic range compressor"}},
        {"Recorder", {"recorder", "Records audio to disk"}},
        {"Limiter", {"limiter", "Peak limiter"}},
        {"Noise Gate", {"gate", "Noise gate"}},
        {"Drive", {"drive", "Distortion/overdrive"}},
        {"Graphic EQ", {"graphic_eq", "Graphic equalizer"}},
        {"Waveshaper", {"waveshaper", "Waveshaping distortion"}},
        {"8-Band Shaper", {"8bandshaper", "8-band spectral shaper"}},
        {"Granulator", {"granulator", "Granular synthesis effect"}},
        {"Harmonic Shaper", {"harmonic_shaper", "Harmonic content shaper"}},
        {"Time/Pitch Shifter", {"timepitch", "Time stretching and pitch shifting"}},
        {"De-Crackle", {"de_crackle", "Removes clicks and pops"}},
        
        // Modulators
        {"LFO", {"lfo", "Low Frequency Oscillator for modulation"}},
        {"ADSR", {"adsr", "Attack Decay Sustain Release envelope"}},
        {"Random", {"random", "Random value generator"}},
        {"S&H", {"s_and_h", "Sample and Hold"}},
        {"Tempo Clock", {"tempo_clock", "Global clock with BPM control, transport (play/stop/reset), division, swing, and clock/gate outputs. Use External Takeover to drive the master transport."}},
        {"Function Generator", {"function_generator", "Custom function curves"}},
        {"Shaping Oscillator", {"shaping_oscillator", "Oscillator with waveshaping"}},
        
        // Utilities
        {"VCA", {"vca", "Voltage Controlled Amplifier"}},
        {"Mixer", {"mixer", "Audio/CV mixer"}},
        {"CV Mixer", {"cv_mixer", "CV signal mixer"}},
        {"Track Mixer", {"track_mixer", "Multi-track mixer with panning"}},
        {"Attenuverter", {"attenuverter", "Attenuate and invert signals"}},
        {"Lag Processor", {"lag_processor", "Slew rate limiter/smoother"}},
        {"Math", {"math", "Mathematical operations"}},
        {"Map Range", {"map_range", "Map values from one range to another"}},
        {"Quantizer", {"quantizer", "Quantize CV to scales"}},
        {"Rate", {"rate", "Rate/frequency divider"}},
        {"Comparator", {"comparator", "Compare and threshold signals"}},
        {"Logic", {"logic", "Boolean logic operations"}},
        {"Clock Divider", {"clock_divider", "Clock division and multiplication"}},
        {"Sequential Switch", {"sequential_switch", "Sequential signal router"}},
        {"Comment", {"comment", "Text comment box"}},
        {"Best Practice", {"best_practice", "Best practice node template"}},
        {"Snapshot Sequencer", {"snapshot_sequencer", "Snapshot sequencer for parameter automation"}},
        
        // Analysis
        {"Scope", {"scope", "Oscilloscope display"}},
        {"Debug", {"debug", "Debug value display"}},
        {"Input Debug", {"input_debug", "Input signal debugger"}},
        {"Frequency Graph", {"frequency_graph", "Spectrum analyzer display"}}
    };
}

// Legacy function for backwards compatibility with tooltip display
std::vector<std::pair<juce::String, const char*>> ImGuiNodeEditorComponent::getModuleDescriptions()
{
    std::vector<std::pair<juce::String, const char*>> result;
    for (const auto& entry : getModuleRegistry())
    {
        // Return {internal type, description} for compatibility
        result.push_back({entry.second.first, entry.second.second});
    }
    return result;
}

// VST Plugin Support
void ImGuiNodeEditorComponent::addPluginModules()
{
    if (synth == nullptr)
        return;
    
    auto& app = PresetCreatorApplication::getApp();
    auto& knownPluginList = app.getKnownPluginList();
    auto& formatManager = app.getPluginFormatManager();
    
    // Set the plugin format manager and known plugin list on the synth if not already set
    synth->setPluginFormatManager(&formatManager);
    synth->setKnownPluginList(&knownPluginList);
    
    // Display each known plugin as a button
    const auto& plugins = knownPluginList.getTypes();
    
    if (plugins.isEmpty())
    {
        ImGui::TextDisabled("No plugins found.");
        ImGui::TextDisabled("Use 'Scan for Plugins...' in the File menu.");
        return;
    }
    
    for (const auto& desc : plugins)
    {
        juce::String buttonLabel = desc.name;
        if (desc.manufacturerName.isNotEmpty())
        {
            buttonLabel += " (" + desc.manufacturerName + ")";
        }
        
        if (ImGui::Selectable(buttonLabel.toRawUTF8()))
        {
            auto nodeId = synth->addVstModule(formatManager, desc);
            if (nodeId.uid != 0)
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const auto logicalId = synth->getLogicalIdForNode(nodeId);
                pendingNodeScreenPositions[(int)logicalId] = mouse;
                snapshotAfterEditor = true;
                juce::Logger::writeToLog("[VST] Added plugin: " + desc.name);
            }
            else
            {
                juce::Logger::writeToLog("[VST] ERROR: Failed to add plugin: " + desc.name);
            }
        }
        
        // Show tooltip with plugin info on hover
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Name: %s", desc.name.toRawUTF8());
            ImGui::Text("Manufacturer: %s", desc.manufacturerName.toRawUTF8());
            ImGui::Text("Version: %s", desc.version.toRawUTF8());
            ImGui::Text("Format: %s", desc.pluginFormatName.toRawUTF8());
            ImGui::Text("Type: %s", desc.isInstrument ? "Instrument" : "Effect");
            ImGui::Text("Inputs: %d", desc.numInputChannels);
            ImGui::Text("Outputs: %d", desc.numOutputChannels);
            ImGui::EndTooltip();
        }
    }
}

void ImGuiNodeEditorComponent::handleCollapseToMetaModule()
{
    if (!synth)
        return;
    
    juce::Logger::writeToLog("[Meta Module] Starting collapse operation...");
    
    // 1. Get selected nodes
    const int numSelected = ImNodes::NumSelectedNodes();
    if (numSelected < 2)
    {
        juce::Logger::writeToLog("[Meta Module] ERROR: Need at least 2 nodes selected");
        return;
    }
    
    std::vector<int> selectedNodeIds(numSelected);
    ImNodes::GetSelectedNodes(selectedNodeIds.data());
    
    // Convert to logical IDs
    std::set<juce::uint32> selectedLogicalIds;
    for (int nodeId : selectedNodeIds)
    {
        selectedLogicalIds.insert((juce::uint32)nodeId);
    }
    
    juce::Logger::writeToLog("[Meta Module] Selected " + juce::String(numSelected) + " nodes");
    
    // 2. Analyze boundary connections
    struct BoundaryConnection
    {
        juce::uint32 externalLogicalId;
        int externalChannel;
        juce::uint32 internalLogicalId;
        int internalChannel;
        bool isInput; // true = external -> internal, false = internal -> external
    };
    
    std::vector<BoundaryConnection> boundaries;
    auto allConnections = synth->getConnectionsInfo();
    
    for (const auto& conn : allConnections)
    {
        bool srcIsSelected = selectedLogicalIds.count(conn.srcLogicalId) > 0;
        bool dstIsSelected = selectedLogicalIds.count(conn.dstLogicalId) > 0 && !conn.dstIsOutput;
        bool dstIsOutput = conn.dstIsOutput;
        
        // Inlet: external -> selected
        if (!srcIsSelected && dstIsSelected)
        {
            BoundaryConnection bc;
            bc.externalLogicalId = conn.srcLogicalId;
            bc.externalChannel = conn.srcChan;
            bc.internalLogicalId = conn.dstLogicalId;
            bc.internalChannel = conn.dstChan;
            bc.isInput = true;
            boundaries.push_back(bc);
            juce::Logger::writeToLog("[Meta Module] Found inlet: " + juce::String(bc.externalLogicalId) + 
                                    " -> " + juce::String(bc.internalLogicalId));
        }
        // Outlet: selected -> external or output
        else if (srcIsSelected && (!dstIsSelected || dstIsOutput))
        {
            BoundaryConnection bc;
            bc.externalLogicalId = dstIsOutput ? 0 : conn.dstLogicalId;
            bc.externalChannel = conn.dstChan;
            bc.internalLogicalId = conn.srcLogicalId;
            bc.internalChannel = conn.srcChan;
            bc.isInput = false;
            boundaries.push_back(bc);
            juce::Logger::writeToLog("[Meta Module] Found outlet: " + juce::String(bc.internalLogicalId) + 
                                    " -> " + (dstIsOutput ? "OUTPUT" : juce::String(bc.externalLogicalId)));
        }
    }
    
    // Count inlets and outlets
    int numInlets = 0;
    int numOutlets = 0;
    for (const auto& bc : boundaries)
    {
        if (bc.isInput)
            numInlets++;
        else
            numOutlets++;
    }
    
    juce::Logger::writeToLog("[META] Boundary Detection: Found " + juce::String(numInlets) + " inlets and " + juce::String(numOutlets) + " outlets.");
    juce::Logger::writeToLog("[META] Found " + juce::String(boundaries.size()) + " boundary connections");
    
    if (boundaries.empty())
    {
        juce::Logger::writeToLog("[META] WARNING: No boundary connections - creating isolated meta module");
    }
    
    // 3. Create the internal graph state
    pushSnapshot(); // Make undoable
    
    // Save the state of selected nodes
    juce::MemoryBlock internalState;
    {
        // Create a temporary state containing only selected nodes
        juce::ValueTree internalRoot("ModularSynthPreset");
        internalRoot.setProperty("version", 1, nullptr);
        
        juce::ValueTree modsVT("modules");
        juce::ValueTree connsVT("connections");
        
        // Add selected modules
        std::map<juce::uint32, juce::uint32> oldToNewLogicalId;
        juce::uint32 newLogicalId = 1;
        
        for (juce::uint32 oldId : selectedLogicalIds)
        {
            oldToNewLogicalId[oldId] = newLogicalId++;
            
            auto* module = synth->getModuleForLogical(oldId);
            if (!module)
                continue;
            
            juce::String moduleType = synth->getModuleTypeForLogical(oldId);
            
            juce::ValueTree mv("module");
            mv.setProperty("logicalId", (int)oldToNewLogicalId[oldId], nullptr);
            mv.setProperty("type", moduleType, nullptr);
            
            // Save parameters
            juce::ValueTree params = module->getAPVTS().copyState();
            juce::ValueTree paramsWrapper("params");
            paramsWrapper.addChild(params, -1, nullptr);
            mv.addChild(paramsWrapper, -1, nullptr);
            
            // Save extra state
            if (auto extra = module->getExtraStateTree(); extra.isValid())
            {
                juce::ValueTree extraWrapper("extra");
                extraWrapper.addChild(extra, -1, nullptr);
                mv.addChild(extraWrapper, -1, nullptr);
            }
            
            modsVT.addChild(mv, -1, nullptr);
        }
        
        // Add inlet modules for each unique input
        std::map<std::pair<juce::uint32, int>, juce::uint32> inletMap; // (extId, extCh) -> inletLogicalId
        for (const auto& bc : boundaries)
        {
            if (bc.isInput)
            {
                auto key = std::make_pair(bc.externalLogicalId, bc.externalChannel);
                if (inletMap.find(key) == inletMap.end())
                {
                    juce::uint32 inletId = newLogicalId++;
                    inletMap[key] = inletId;
                    
                    juce::ValueTree mv("module");
                    mv.setProperty("logicalId", (int)inletId, nullptr);
                    mv.setProperty("type", "inlet", nullptr);
                    modsVT.addChild(mv, -1, nullptr);
                    
                    juce::Logger::writeToLog("[Meta Module] Created inlet node ID=" + juce::String(inletId));
                }
            }
        }
        
        // Add outlet modules for each unique output
        std::map<std::pair<juce::uint32, int>, juce::uint32> outletMap; // (intId, intCh) -> outletLogicalId
        for (const auto& bc : boundaries)
        {
            if (!bc.isInput)
            {
                auto key = std::make_pair(bc.internalLogicalId, bc.internalChannel);
                if (outletMap.find(key) == outletMap.end())
                {
                    juce::uint32 outletId = newLogicalId++;
                    outletMap[key] = outletId;
                    
                    juce::ValueTree mv("module");
                    mv.setProperty("logicalId", (int)outletId, nullptr);
                    mv.setProperty("type", "outlet", nullptr);
                    modsVT.addChild(mv, -1, nullptr);
                    
                    juce::Logger::writeToLog("[Meta Module] Created outlet node ID=" + juce::String(outletId));
                }
            }
        }
        
        // Add internal connections (between selected nodes)
        for (const auto& conn : allConnections)
        {
            bool srcIsSelected = selectedLogicalIds.count(conn.srcLogicalId) > 0;
            bool dstIsSelected = selectedLogicalIds.count(conn.dstLogicalId) > 0;
            
            if (srcIsSelected && dstIsSelected)
            {
                juce::ValueTree cv("connection");
                cv.setProperty("srcId", (int)oldToNewLogicalId[conn.srcLogicalId], nullptr);
                cv.setProperty("srcChan", conn.srcChan, nullptr);
                cv.setProperty("dstId", (int)oldToNewLogicalId[conn.dstLogicalId], nullptr);
                cv.setProperty("dstChan", conn.dstChan, nullptr);
                connsVT.addChild(cv, -1, nullptr);
            }
        }
        
        // Add connections from inlets to internal nodes
        for (const auto& bc : boundaries)
        {
            if (bc.isInput)
            {
                auto key = std::make_pair(bc.externalLogicalId, bc.externalChannel);
                juce::uint32 inletId = inletMap[key];
                
                juce::ValueTree cv("connection");
                cv.setProperty("srcId", (int)inletId, nullptr);
                cv.setProperty("srcChan", 0, nullptr); // Inlets output on channel 0
                cv.setProperty("dstId", (int)oldToNewLogicalId[bc.internalLogicalId], nullptr);
                cv.setProperty("dstChan", bc.internalChannel, nullptr);
                connsVT.addChild(cv, -1, nullptr);
            }
        }
        
        // Add connections from internal nodes to outlets
        for (const auto& bc : boundaries)
        {
            if (!bc.isInput)
            {
                auto key = std::make_pair(bc.internalLogicalId, bc.internalChannel);
                juce::uint32 outletId = outletMap[key];
                
                juce::ValueTree cv("connection");
                cv.setProperty("srcId", (int)oldToNewLogicalId[bc.internalLogicalId], nullptr);
                cv.setProperty("srcChan", bc.internalChannel, nullptr);
                cv.setProperty("dstId", (int)outletId, nullptr);
                cv.setProperty("dstChan", 0, nullptr); // Outlets input on channel 0
                connsVT.addChild(cv, -1, nullptr);
            }
        }
        
        internalRoot.addChild(modsVT, -1, nullptr);
        internalRoot.addChild(connsVT, -1, nullptr);
        
        // Serialize to memory block
        if (auto xml = internalRoot.createXml())
        {
            juce::MemoryOutputStream mos(internalState, false);
            xml->writeTo(mos);
            juce::Logger::writeToLog("[META] Generated state for sub-patch.");
        }
    }
    
    // 4. Calculate average position for the meta module
    ImVec2 avgPos(0.0f, 0.0f);
    int posCount = 0;
    for (juce::uint32 logicalId : selectedLogicalIds)
    {
        ImVec2 pos = ImNodes::GetNodeGridSpacePos((int)logicalId);
        avgPos.x += pos.x;
        avgPos.y += pos.y;
        posCount++;
    }
    if (posCount > 0)
    {
        avgPos.x /= posCount;
        avgPos.y /= posCount;
    }
    
    // 5. Delete selected nodes
    for (juce::uint32 logicalId : selectedLogicalIds)
    {
        auto nodeId = synth->getNodeIdForLogical(logicalId);
        synth->removeModule(nodeId);
    }
    
    // 6. Create meta module
    auto metaNodeId = synth->addModule("meta module");
    auto metaLogicalId = synth->getLogicalIdForNode(metaNodeId);
    pendingNodePositions[(int)metaLogicalId] = avgPos;
    
    juce::Logger::writeToLog("[META] Created new MetaModule with logical ID: " + juce::String((int)metaLogicalId));
    
    auto* metaModule = dynamic_cast<MetaModuleProcessor*>(synth->getModuleForLogical(metaLogicalId));
    if (metaModule)
    {
        // Load the internal state
        metaModule->setStateInformation(internalState.getData(), (int)internalState.getSize());
        juce::Logger::writeToLog("[META] Loaded internal state into meta module");
    }
    else
    {
        juce::Logger::writeToLog("[META] ERROR: Failed to create meta module");
        return;
    }
    
    // 7. Reconnect external connections
    // Note: This is a simplified implementation - in production, you'd need to map
    // inlet/outlet indices to meta module input/output channels properly
    for (const auto& bc : boundaries)
    {
        if (bc.isInput)
        {
            // Connect external source to meta module input
            auto extNodeId = synth->getNodeIdForLogical(bc.externalLogicalId);
            synth->connect(extNodeId, bc.externalChannel, metaNodeId, 0);
        }
        else if (bc.externalLogicalId != 0)
        {
            // Connect meta module output to external destination
            auto extNodeId = synth->getNodeIdForLogical(bc.externalLogicalId);
            synth->connect(metaNodeId, 0, extNodeId, bc.externalChannel);
        }
        else
        {
            // Connect meta module output to main output
            auto outputNodeId = synth->getOutputNodeID();
            synth->connect(metaNodeId, 0, outputNodeId, bc.externalChannel);
        }
    }
    
    graphNeedsRebuild = true;
    synth->commitChanges();
    
    juce::Logger::writeToLog("[META] Reconnected external cables. Collapse complete!");
}

void ImGuiNodeEditorComponent::loadPresetFromFile(const juce::File& file)
{
    if (!file.existsAsFile() || synth == nullptr)
        return;

    // 1. Load the file content.
    juce::MemoryBlock mb;
    file.loadFileAsData(mb);

    // 2. Set the synthesizer's state. This rebuilds the audio graph.
    synth->setStateInformation(mb.getData(), (int)mb.getSize());

    // 3. Parse the XML to find the UI state.
    juce::ValueTree uiState;
    if (auto xml = juce::XmlDocument::parse(mb.toString()))
    {
        auto vt = juce::ValueTree::fromXml(*xml);
        uiState = vt.getChildWithName("NodeEditorUI");
        if (uiState.isValid())
        {
            // 4. Apply the UI state (node positions, muted status, etc.).
            // This queues the changes to be applied on the next frame.
            applyUiValueTree(uiState);
        }
    }

    // 5. Create an undo snapshot for this action.
    Snapshot s;
    synth->getStateInformation(s.synthState);
    s.uiState = uiState.isValid() ? uiState : getUiValueTree();
    undoStack.push_back(std::move(s));
    redoStack.clear();

    // 6. Update the UI status trackers.
    isPatchDirty = false;
    currentPresetFile = file.getFileName();
    
    juce::Logger::writeToLog("[Preset] Successfully loaded preset: " + file.getFullPathName());
}

void ImGuiNodeEditorComponent::mergePresetFromFile(const juce::File& file, ImVec2 dropPosition)
{
    if (!file.existsAsFile() || synth == nullptr)
        return;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) return;

    juce::ValueTree preset = juce::ValueTree::fromXml(*xml);
    auto modulesVT = preset.getChildWithName("modules");
    auto connectionsVT = preset.getChildWithName("connections");
    auto uiVT = preset.getChildWithName("NodeEditorUI");

    if (!modulesVT.isValid()) return;

    pushSnapshot(); // Create an undo state before we start merging.

    // --- THIS IS THE NEW LOGIC ---
    // 1. Find the top-most Y coordinate of all existing nodes on the canvas.
    float topMostY = FLT_MAX;
    auto currentUiState = getUiValueTree();
    bool canvasHasNodes = false;
    for (int i = 0; i < currentUiState.getNumChildren(); ++i)
    {
        auto nodePosVT = currentUiState.getChild(i);
        if (nodePosVT.hasType("node"))
        {
            canvasHasNodes = true;
            float y = (float)nodePosVT.getProperty("y");
            if (y < topMostY)
            {
                topMostY = y;
            }
        }
    }
    // If the canvas is empty, use the drop position as the reference.
    if (!canvasHasNodes)
    {
        topMostY = dropPosition.y;
    }

    // 2. Find the bounding box of the nodes within the preset we are dropping.
    float presetMinX = FLT_MAX;
    float presetMaxY = -FLT_MAX;
    if (uiVT.isValid())
    {
        for (int i = 0; i < uiVT.getNumChildren(); ++i)
        {
            auto nodePosVT = uiVT.getChild(i);
            if (nodePosVT.hasType("node"))
            {
                float x = (float)nodePosVT.getProperty("x");
                float y = (float)nodePosVT.getProperty("y");
                if (x < presetMinX) presetMinX = x;
                if (y > presetMaxY) presetMaxY = y; // We need the lowest point (max Y) of the preset group.
            }
        }
    }
    
    // 3. Calculate the necessary offsets.
    const float verticalPadding = 100.0f;
    const float yOffset = topMostY - presetMaxY - verticalPadding;
    const float xOffset = dropPosition.x - presetMinX;
    // --- END OF NEW LOGIC ---

    // This map will track how we remap old IDs from the file to new, unique IDs on the canvas.
    std::map<juce::uint32, juce::uint32> oldIdToNewId;

    // First pass: create all the new modules from the preset.
    for (int i = 0; i < modulesVT.getNumChildren(); ++i)
    {
        auto moduleNode = modulesVT.getChild(i);
        if (moduleNode.hasType("module"))
        {
            juce::uint32 oldLogicalId = (juce::uint32)(int)moduleNode.getProperty("logicalId");
            juce::String type = moduleNode.getProperty("type").toString();
            
            // Add the module without committing the graph changes yet.
            auto newNodeId = synth->addModule(type, false);
            juce::uint32 newLogicalId = synth->getLogicalIdForNode(newNodeId);

            oldIdToNewId[oldLogicalId] = newLogicalId; // Store the mapping

            // Restore the new module's parameters and extra state.
            if (auto* proc = synth->getModuleForLogical(newLogicalId))
            {
                auto paramsWrapper = moduleNode.getChildWithName("params");
                if (paramsWrapper.isValid()) proc->getAPVTS().replaceState(paramsWrapper.getChild(0));
                
                auto extraWrapper = moduleNode.getChildWithName("extra");
                if (extraWrapper.isValid()) proc->setExtraStateTree(extraWrapper.getChild(0));
            }
        }
    }

    // Second pass: recreate the internal connections between the new modules.
    if (connectionsVT.isValid())
    {
        for (int i = 0; i < connectionsVT.getNumChildren(); ++i)
        {
            auto connNode = connectionsVT.getChild(i);
            if (connNode.hasType("connection"))
            {
                juce::uint32 oldSrcId = (juce::uint32)(int)connNode.getProperty("srcId");
                int srcChan = (int)connNode.getProperty("srcChan");
                juce::uint32 oldDstId = (juce::uint32)(int)connNode.getProperty("dstId");
                int dstChan = (int)connNode.getProperty("dstChan");

                // Only connect if both source and destination are part of the preset we're merging.
                if (oldIdToNewId.count(oldSrcId) && oldIdToNewId.count(oldDstId))
                {
                    auto newSrcNodeId = synth->getNodeIdForLogical(oldIdToNewId[oldSrcId]);
                    auto newDstNodeId = synth->getNodeIdForLogical(oldIdToNewId[oldDstId]);
                    synth->connect(newSrcNodeId, srcChan, newDstNodeId, dstChan);
                }
            }
        }
    }
    
    // Third pass: Apply UI positions using our new calculated offsets.
    if (uiVT.isValid())
    {
        for (int i = 0; i < uiVT.getNumChildren(); ++i)
        {
            auto nodePosVT = uiVT.getChild(i);
            if (nodePosVT.hasType("node"))
            {
                juce::uint32 oldId = (juce::uint32)(int)nodePosVT.getProperty("id");
                if (oldIdToNewId.count(oldId)) // Check if it's one of our new nodes
                {
                    ImVec2 pos = ImVec2((float)nodePosVT.getProperty("x"), (float)nodePosVT.getProperty("y"));
                    
                    // Apply the smart offsets
                    ImVec2 newPos = ImVec2(pos.x + xOffset, pos.y + yOffset);
                    
                    pendingNodeScreenPositions[(int)oldIdToNewId[oldId]] = newPos;
                }
            }
        }
    }

    // Finally, commit all the changes to the audio graph at once.
    synth->commitChanges();
    isPatchDirty = true; // Mark the patch as edited.
    
    juce::Logger::writeToLog("[Preset] Successfully merged preset: " + file.getFullPathName() + 
                             " above existing nodes with offsets (" + juce::String(xOffset) + ", " + juce::String(yOffset) + ")");
}


