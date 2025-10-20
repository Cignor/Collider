// RtLogger flush integrated via timer in component
#include "PresetCreatorComponent.h"
#include "ImGuiNodeEditorComponent.h"
#include "../utils/RtLogger.h"

PresetCreatorComponent::PresetCreatorComponent(juce::AudioDeviceManager& adm,
                                               juce::AudioPluginFormatManager& fm,
                                               juce::KnownPluginList& kl)
    : deviceManager(adm),
      pluginFormatManager(fm),
      knownPluginList(kl)
{
    juce::Logger::writeToLog("PresetCreatorComponent constructor starting...");
    addAndMakeVisible (log);

    // Replace list/combos UI with ImGui node editor
    juce::Logger::writeToLog("Attempting to create ImGuiNodeEditorComponent...");
    editor.reset (new ImGuiNodeEditorComponent(deviceManager));
    juce::Logger::writeToLog("ImGuiNodeEditorComponent created.");
    editor->onShowAudioSettings = [this]() { this->showAudioSettingsDialog(); };
    addAndMakeVisible (editor.get());
    log.setMultiLine (true); log.setReadOnly (true);

    juce::Logger::writeToLog("Creating ModularSynthProcessor...");
    synth = std::make_unique<ModularSynthProcessor>();
    
    // --- THIS IS THE FIX ---
    // Set the managers immediately so the synth is ready for state restoration.
    synth->setPluginFormatManager(&pluginFormatManager);
    synth->setKnownPluginList(&knownPluginList);
    juce::Logger::writeToLog("Plugin managers set on ModularSynthProcessor.");
    // --- END OF FIX ---
    
    juce::Logger::writeToLog("Setting model on editor...");
    if (editor != nullptr)
        editor->setModel (synth.get());
    synth->prepareToPlay (sampleRate, blockSize);

    // Use the shared AudioDeviceManager settings
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = dev->getCurrentSampleRate();
        blockSize = dev->getCurrentBufferSizeSamples();
        synth->prepareToPlay (sampleRate, blockSize);
        juce::Logger::writeToLog ("Audio device: " + dev->getName() +
                                   ", sr=" + juce::String (sampleRate) +
                                   ", bs=" + juce::String (blockSize));
    }
    // AudioProcessorPlayer lives in juce_audio_utils namespace path include; type is juce::AudioSourcePlayer for routing
    // Use AudioProcessorPlayer via juce_audio_utils module
    processorPlayer.setProcessor (synth.get());
    setWantsKeyboardFocus (true);

    // Setup FileLogger at the same path the user checks: <exe>/juce/logs/preset_creator_*.log
    {
        auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        auto juceLogsDir = exeDir.getChildFile ("juce").getChildFile ("logs");
        juceLogsDir.createDirectory();
        auto logName = juce::String ("preset_creator_") + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S") + ".log";
        auto logFile = juceLogsDir.getChildFile (logName);
        fileLogger = std::make_unique<juce::FileLogger> (logFile, "Preset Creator Session", 10 * 1024 * 1024);
        if (fileLogger != nullptr)
            juce::Logger::setCurrentLogger (fileLogger.get());
        juce::Logger::writeToLog ("PresetCreator log file: " + logFile.getFullPathName());
    }
    // Init RT logger and start periodic flush
    RtLogger::init (2048, 256);
    
    // NOTE: Audio Settings button removed - now using menu integration
    
    juce::Logger::writeToLog ("PresetCreator constructed");
    startTimerHz (30);
    
    setWindowFileName({}); // Set the default title on startup
}

// ADD: Implementation of the audio settings dialog function
void PresetCreatorComponent::showAudioSettingsDialog()
{
    auto* component = new juce::AudioDeviceSelectorComponent(
        deviceManager, 0, 256, 0, 256, false, false, false, false);
    
    component->setSize(500, 450);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(component);
    o.dialogTitle                   = "Audio Settings";
    o.dialogBackgroundColour        = juce::Colours::darkgrey;
    o.escapeKeyTriggersCloseButton  = true;
    o.resizable                     = false;
    o.launchAsync();
}

void PresetCreatorComponent::setWindowFileName(const juce::String& fileName)
{
    // Find the parent window of this component
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        juce::String newTitle = "Preset Creator"; // The default title
        if (fileName.isNotEmpty())
        {
            newTitle += " - " + fileName; // Append the filename if one is provided
        }
        window->setName(newTitle);
    }
}

void PresetCreatorComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void PresetCreatorComponent::resized()
{
    for (auto* c : getChildren())
        if (dynamic_cast<ImGuiNodeEditorComponent*>(c) != nullptr)
            c->setBounds (0, 0, getWidth(), getHeight());
    
    // Audio Settings button removed - now using menu integration
    
    // Keep log overlay minimal for now
    log.setBounds (10, getHeight() - 160, getWidth() - 20, 150);
}

PresetCreatorComponent::~PresetCreatorComponent()
{
    stopAudition();
    processorPlayer.setProcessor (nullptr);
    juce::Logger::writeToLog ("PresetCreator destroyed");
    RtLogger::shutdown();
    juce::Logger::setCurrentLogger (nullptr);
}

void PresetCreatorComponent::buttonClicked (juce::Button* b)
{
    if (b == &btnAddVCO) { synth->addModule ("VCO"); synth->commitChanges(); refreshModulesList(); }
    else if (b == &btnAddVCF) { synth->addModule ("VCF"); synth->commitChanges(); refreshModulesList(); }
    else if (b == &btnAddVCA) { synth->addModule ("VCA"); synth->commitChanges(); refreshModulesList(); }
    else if (b == &btnConnect) { doConnect(); }
    else if (b == &btnSave) { doSave(); }
    else if (b == &btnLoad) { doLoad(); }
}

void PresetCreatorComponent::refreshModulesList()
{
    modulesModel.rows.clear();
    cbSrc.clear(); cbDst.clear();
    int idx = 1;
    for (auto [logicalId, type] : synth->getModulesInfo())
    {
        modulesModel.rows.add (juce::String ((int) logicalId) + " - " + type);
        cbSrc.addItem (juce::String ((int) logicalId) + " - " + type, idx);
        cbDst.addItem (juce::String ((int) logicalId) + " - " + type, idx);
        ++idx;
    }
    cbDst.addItem ("Output", 9999);
    listModules.updateContent();
}

void PresetCreatorComponent::doConnect()
{
    int selSrc = cbSrc.getSelectedId();
    int selDst = cbDst.getSelectedId();
    if (selSrc <= 0 || selDst <= 0) { log.insertTextAtCaret ("Select src/dst first\n"); return; }

    // Extract logical IDs from combo texts
    auto parseId = [] (const juce::String& s) -> juce::uint32 { return (juce::uint32) s.upToFirstOccurrenceOf(" ", false, false).getIntValue(); };
    juce::uint32 srcLogical = parseId (cbSrc.getText());
    juce::uint32 dstLogical = parseId (cbDst.getText());

    auto srcNode = synth->getNodeIdForLogical (srcLogical);
    juce::AudioProcessorGraph::NodeID dstNode;
    if (cbDst.getSelectedId() == 9999)
        dstNode = synth->getOutputNodeID();
    else
        dstNode = synth->getNodeIdForLogical (dstLogical);
    const int srcChan = cbSrcChan.getSelectedId() - 1;
    const int dstChan = cbDstChan.getSelectedId() - 1;
    if (srcNode.uid != 0 && dstNode.uid != 0)
    {
        if (synth->connect (srcNode, srcChan, dstNode, dstChan))
        {
            log.insertTextAtCaret ("Connected\n");
            synth->commitChanges();
        }
        else
        {
            log.insertTextAtCaret ("Connect failed\n");
        }
    }
}

void PresetCreatorComponent::doSave()
{
    // Default to project-root/Synth_presets
    juce::File startDir;
    {
        auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        auto dir = exeDir;
        for (int i = 0; i < 8 && dir.exists(); ++i)
        {
            auto candidate = dir.getSiblingFile("Synth_presets");
            if (candidate.exists() && candidate.isDirectory()) { startDir = candidate; break; }
            dir = dir.getParentDirectory();
        }
    }
    if (! startDir.exists()) startDir = juce::File();
    saveChooser = std::make_unique<juce::FileChooser> ("Save preset", startDir, "*.xml");
    saveChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.exists() || f.getParentDirectory().exists())
        {
            // Ensure directory exists when saving into default folder
            f.getParentDirectory().createDirectory();
            
            // --- FIX: Temporarily unmute nodes to save original connections ---
            // When nodes are muted, their connections are replaced with bypass routing.
            // We must save the ORIGINAL connections, not the bypass connections.
            
            // 1. Get a list of all currently muted nodes from the editor
            std::vector<juce::uint32> currentlyMutedNodes;
            if (editor)
            {
                for (const auto& pair : editor->mutedNodeStates)
                {
                    currentlyMutedNodes.push_back(pair.first);
                }
                
                // 2. Temporarily UNMUTE all of them to restore the original connections
                for (juce::uint32 lid : currentlyMutedNodes)
                {
                    editor->unmuteNode(lid);
                }
            }
            
            // 3. CRITICAL: Force the synth to apply these connection changes immediately
            if (synth)
            {
                synth->commitChanges();
            }
            // At this point, the synth graph is in its "true", unmuted state
            
            // 4. NOW get the state - this will save the correct, original connections
            juce::MemoryBlock mb;
            synth->getStateInformation (mb);
            auto xml = juce::XmlDocument::parse (mb.toString());
            if (! xml) return;
            
            // 5. IMMEDIATELY RE-MUTE the nodes to return the editor to its visible state
            if (editor)
            {
                for (juce::uint32 lid : currentlyMutedNodes)
                {
                    editor->muteNode(lid);
                }
            }
            
            // 6. CRITICAL: Force the synth to apply the re-mute changes immediately
            if (synth)
            {
                synth->commitChanges();
            }
            // The synth graph is now back to its bypassed state for audio processing
            // --- END OF FIX ---
            
            juce::ValueTree presetVT = juce::ValueTree::fromXml (*xml);
            // Attach UI state as child (which correctly contains the "muted" flags)
            if (editor)
            {
                juce::ValueTree ui = editor->getUiValueTree();
                presetVT.addChild (ui, -1, nullptr);
            }
            // Write
            f.replaceWithText (presetVT.createXml()->toString());
            log.insertTextAtCaret ("Saved: " + f.getFullPathName() + "\n");
            
            setWindowFileName(f.getFileName()); // Update title bar with filename
        }
    });
}

void PresetCreatorComponent::doLoad()
{
    // Default to project-root/Synth_presets
    juce::File startDir;
    {
        auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        auto dir = exeDir;
        for (int i = 0; i < 8 && dir.exists(); ++i)
        {
            auto candidate = dir.getSiblingFile("Synth_presets");
            if (candidate.exists() && candidate.isDirectory()) { startDir = candidate; break; }
            dir = dir.getParentDirectory();
        }
    }
    if (! startDir.exists()) startDir = juce::File();
    loadChooser = std::make_unique<juce::FileChooser> ("Load preset", startDir, "*.xml");
    loadChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc) noexcept
    {
        try {
            auto f = fc.getResult();
            if (f.existsAsFile())
            {
                juce::MemoryBlock mb;
                f.loadFileAsData (mb);
                // First set the synth state
                synth->setStateInformation (mb.getData(), (int) mb.getSize());
                // Then parse and queue UI state for next frame (ensures editor exists and nodes are created)
                if (editor)
                {
                    if (auto xml = juce::XmlDocument::parse (mb.toString()))
                    {
                        auto vt = juce::ValueTree::fromXml (*xml);
                        auto ui = vt.getChildWithName ("NodeEditorUI");
                        if (ui.isValid())
                            editor->applyUiValueTreeNow (ui);
                    }
                }
                refreshModulesList();
                log.insertTextAtCaret ("Loaded: " + f.getFullPathName() + "\n");
                
                setWindowFileName(f.getFileName()); // Update title bar with filename
            }
        } catch (...) {
            juce::Logger::writeToLog ("[PresetCreator][FATAL] Exception in doLoad callback");
        }
    });
}

bool PresetCreatorComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (!auditioning)
            startAudition(); // Start audio engine if it's off

        // If any recorder is active, this key press should resume it
        if (synth->isAnyModuleRecording())
            synth->resumeAllRecorders();
        
        return true;
    }
    return false;
}

bool PresetCreatorComponent::keyStateChanged (bool isKeyDown)
{
    juce::ignoreUnused (isKeyDown);

    // This triggers on spacebar RELEASE
    if (! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey) && auditioning)
    {
        if (synth->isAnyModuleRecording())
        {
            // A recording is in progress, so PAUSE it instead of stopping the engine
            synth->pauseAllRecorders();
        }
        else
        {
            // No recording is active, so stop the engine like before
            stopAudition();
        }
        return true;
    }
    return false;
}

void PresetCreatorComponent::visibilityChanged()
{
    juce::Logger::writeToLog (juce::String ("Component visible? ") + (isShowing() ? "yes" : "no"));
}

void PresetCreatorComponent::startAudition()
{
    if (auditioning) return;
    deviceManager.addAudioCallback (&processorPlayer);
    auditioning = true;
    log.insertTextAtCaret ("[Audition] Start (hold space)\n");
}

void PresetCreatorComponent::stopAudition()
{
    if (! auditioning) return;
    deviceManager.removeAudioCallback (&processorPlayer);
    auditioning = false;
    log.insertTextAtCaret ("[Audition] Stop\n");
}

void PresetCreatorComponent::timerCallback()
{
    RtLogger::flushToFileLogger();
    // Keep a heartbeat to ensure log is alive
    static int counter = 0;
    if ((++counter % 60) == 0)
        juce::Logger::writeToLog ("[Heartbeat] UI alive");
}
