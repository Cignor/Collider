#include "TestHarnessComponent.h"
#include "MainComponent.h"
#include "../audio/voices/NoiseVoiceProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include "../audio/voices/SynthVoiceProcessor.h"
#include "../audio/voices/SampleVoiceProcessor.h"
#include "../audio/voices/ModularVoice.h"
#include "../audio/utils/VoiceDeletionUtils.h"
#include "../audio/AudioEngine.h"
#include "../ipc/CommandBus.h"

static juce::TextEditor* gLoggerEditor = nullptr;

void OnScreenLogger::attach (juce::TextEditor* editor)
{
    gLoggerEditor = editor;
}

void OnScreenLogger::log (const juce::String& msg)
{
    if (gLoggerEditor != nullptr)
    {
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
            && juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            gLoggerEditor->moveCaretToEnd();
            gLoggerEditor->insertTextAtCaret (msg + "\n");
        }
        else
        {
            juce::MessageManager::callAsync ([s = msg]
            {
                if (gLoggerEditor != nullptr)
                {
                    gLoggerEditor->moveCaretToEnd();
                    gLoggerEditor->insertTextAtCaret (s + "\n");
                }
            });
        }
    }
    juce::Logger::writeToLog (msg);
}

TestHarnessComponent::TestHarnessComponent(juce::AudioDeviceManager& adm)
    : deviceManager(adm)
{
    OnScreenLogger::attach (&logView);
    OnScreenLogger::log ("Harness: Constructor starting...");

    // Set initial harness window size once
    setSize (1600, 900); // More reasonable default size


    addAndMakeVisible (btnAudioSettings);
    addAndMakeVisible (btnCreateSynth);
    addAndMakeVisible (btnCreateSample);
    addAndMakeVisible (btnCreateNoise);
    addAndMakeVisible (btnCreateModular);
    addAndMakeVisible (btnLoadPreset);
    addAndMakeVisible (btnDestroy);
    addAndMakeVisible (btnDestroyRandom);
    addAndMakeVisible (btnDestroySelected);
    addAndMakeVisible (btnRandomPitch);
    addAndMakeVisible (btnRandomTime);
    addAndMakeVisible (btnResetFx);
    addAndMakeVisible (btnChaos);
    addAndMakeVisible (btnManualFx);
    addAndMakeVisible (lEngine);
    addAndMakeVisible (comboEngine);
    comboEngine.addItem ("RubberBand", 1);
    comboEngine.addItem ("Naive", 2);
    comboEngine.setSelectedId (1, juce::dontSendNotification);
    comboEngine.addListener (this);
    addAndMakeVisible (sliderGain);
    addAndMakeVisible (sliderPan);
    addAndMakeVisible (lblGain);
    addAndMakeVisible (lblPan);
    addAndMakeVisible (lblStatus);
    addAndMakeVisible (lblDevice);
    addAndMakeVisible (lblVoices);
    addAndMakeVisible (lblPeak);
    addAndMakeVisible (logView);
    addAndMakeVisible (listDirs);
    addAndMakeVisible (listSamples);
    addAndMakeVisible (listVoices);
    listDirs.setRowHeight (22);
    listSamples.setRowHeight (22);
    listVoices.setRowHeight (22);
    listDirs.setMultipleSelectionEnabled (false);
    listSamples.setMultipleSelectionEnabled (false);
    listVoices.setMultipleSelectionEnabled (false);
    listDirs.setModel (&dirModel);
    listSamples.setModel (&sampleModel);
    listVoices.setModel (&voiceModel);
    listDirs.getVerticalScrollBar().setAutoHide (false);
    listSamples.getVerticalScrollBar().setAutoHide (false);
    listVoices.getVerticalScrollBar().setAutoHide (false);
    auto initSlider = [] (juce::Slider& s, double min, double max, double def, double inc=0.001)
    {
        s.setRange (min, max, inc);
        s.setValue (def);
    };
    auto addL = [this] (juce::Label& l) { addAndMakeVisible (l); };
    auto addS = [this] (juce::Slider& s) { addAndMakeVisible (s); s.addListener (this); };

    // Initialize FX sliders, defaults to "dry" positions
    addL (lFilterCutoff); addS (sFilterCutoff); initSlider (sFilterCutoff, 20.0, 20000.0, 20000.0, 1.0);
    addL (lFilterRes);    addS (sFilterRes);    initSlider (sFilterRes, 1.0, 20.0, 1.0);

    addL (lChRate);  addS (sChRate);  initSlider (sChRate, 0.1, 10.0, 1.0);
    addL (lChDepth); addS (sChDepth); initSlider (sChDepth, 0.0, 1.0, 0.0);
    addL (lChMix);   addS (sChMix);   initSlider (sChMix, 0.0, 1.0, 0.0);

    addL (lPhRate);   addS (sPhRate);   initSlider (sPhRate, 0.01, 10.0, 0.5);
    addL (lPhDepth);  addS (sPhDepth);  initSlider (sPhDepth, 0.0, 1.0, 0.0);
    addL (lPhCentre); addS (sPhCentre); initSlider (sPhCentre, 20.0, 20000.0, 1000.0, 1.0);
    addL (lPhFb);     addS (sPhFb);     initSlider (sPhFb, -0.99, 0.99, 0.0);
    addL (lPhMix);    addS (sPhMix);    initSlider (sPhMix, 0.0, 1.0, 0.0);

    addL (lRvRoom); addS (sRvRoom); initSlider (sRvRoom, 0.0, 1.0, 0.0);
    addL (lRvDamp); addS (sRvDamp); initSlider (sRvDamp, 0.0, 1.0, 0.5);
    addL (lRvWidth);addS (sRvWidth);initSlider (sRvWidth,0.0, 1.0, 1.0);
    addL (lRvMix);  addS (sRvMix);  initSlider (sRvMix,  0.0, 1.0, 0.0);

    addL (lDlTime); addS (sDlTime); initSlider (sDlTime, 1.0, 2000.0, 0.0, 1.0);
    addL (lDlFb);   addS (sDlFb);   initSlider (sDlFb,   0.0, 0.95, 0.0);
    addL (lDlMix);  addS (sDlMix);  initSlider (sDlMix,  0.0, 1.0, 0.0);

    addL (lCpThresh); addS (sCpThresh); initSlider (sCpThresh, -60.0, 0.0, 0.0);
    addL (lCpRatio);  addS (sCpRatio);  initSlider (sCpRatio,  1.0, 20.0, 1.0);
    addL (lCpAtk);    addS (sCpAtk);    initSlider (sCpAtk,    0.1, 200.0, 10.0);
    addL (lCpRel);    addS (sCpRel);    initSlider (sCpRel,    5.0, 1000.0, 100.0);
    addL (lCpMake);   addS (sCpMake);   initSlider (sCpMake,  -12.0, 12.0, 0.0);

    addL (lLmThresh); addS (sLmThresh); initSlider (sLmThresh, -20.0, 0.0, 0.0);
    addL (lLmRel);    addS (sLmRel);    initSlider (sLmRel,     1.0, 200.0, 10.0);

    addL (lDrAmt); addS (sDrAmt); initSlider (sDrAmt, 0.0, 2.0, 0.0);
    addL (lDrMix); addS (sDrMix); initSlider (sDrMix, 0.0, 1.0, 0.0);

    addL (lGtThresh); addS (sGtThresh); initSlider (sGtThresh, -80.0, -20.0, -100.0);
    addL (lGtAtk);    addS (sGtAtk);    initSlider (sGtAtk,      0.1, 50.0, 1.0);
    addL (lGtRel);    addS (sGtRel);    initSlider (sGtRel,        5.0, 500.0, 50.0);

    addL (lTsRatio); addS (sTsRatio); initSlider (sTsRatio, 0.25, 6.0, 1.0);
    addL (lPtSemis); addS (sPtSemis); initSlider (sPtSemis, -24.0, 24.0, 0.0);
    addL (lPtRatio); addS (sPtRatio); initSlider (sPtRatio, 0.5, 2.0, 1.0);

    btnAudioSettings.addListener (this);
    btnCreateSynth.addListener (this);
    btnCreateSample.addListener (this);
    btnCreateNoise.addListener (this);
    btnCreateModular.addListener (this);
    btnLoadPreset.addListener (this);
    btnDestroy.addListener (this);
    btnDestroyRandom.addListener (this);
    btnDestroySelected.addListener (this);
    btnRandomPitch.addListener (this);
    btnRandomTime.addListener (this);
    btnResetFx.addListener (this);

    sliderGain.setRange (0.0, 1.0, 0.001);
    sliderGain.setSkewFactor (0.7);
    sliderGain.setValue (0.7);
    sliderGain.addListener (this);

    sliderPan.setRange (-1.0, 1.0, 0.001);
    sliderPan.setValue (0.0);
    sliderPan.addListener (this);

    lblStatus.setText ("Controlling Voice ID: -", juce::dontSendNotification);
    logView.setMultiLine (true); logView.setReadOnly (true); logView.setScrollbarsShown (true); logView.setCaretVisible (false);
    logView.setText ("[Harness] Ready\n");
    startTimerHz (15);

    // (Removed VCO verification instantiation and slider)

    // Load samples by searching upwards for 'audio/samples' from exe and CWD
    auto findSamplesDir = []() -> juce::File
    {
        juce::Array<juce::File> starts;
        starts.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory());
        starts.add (juce::File::getCurrentWorkingDirectory());
        for (auto s : starts)
        {
            juce::File cur = s;
            for (int i = 0; i < 8; ++i)
            {
                juce::File candidate = cur.getChildFile ("audio").getChildFile ("samples");
                if (candidate.isDirectory())
                    return candidate;
                cur = cur.getParentDirectory();
            }
        }
        return {};
    };

    juce::File root = findSamplesDir();
    if (root.isDirectory())
    {
        OnScreenLogger::log ("[SampleBank] Searching samples in: " + root.getFullPathName());
        sampleBank.loadSamplesFromDirectory (root);
        samplesRoot = root;
        refreshDirectories();
        refreshSamples();
    }
    else
    {
        OnScreenLogger::log ("[SampleBank][WARN] Could not locate 'audio/samples' relative to exe or CWD.");
    }

    OnScreenLogger::log ("Harness: Constructor finished.");
}

// ----------- ListBoxModel (shared for both lists) -----------
int TestHarnessComponent::DirListModel::getNumRows() { return owner.dirNames.size(); }
void TestHarnessComponent::DirListModel::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= owner.dirNames.size()) return;
    g.fillAll (rowIsSelected ? juce::Colours::dimgrey : juce::Colours::transparentBlack);
    g.setColour (juce::Colours::white);
    g.drawText (owner.dirNames[rowNumber], 6, 0, width - 12, height, juce::Justification::centredLeft);
}
void TestHarnessComponent::DirListModel::selectedRowsChanged (int /*lastRowSelected*/)
{
    owner.selectedDirIndex = owner.listDirs.getSelectedRow();
    owner.refreshSamples();
}
void TestHarnessComponent::DirListModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    owner.selectedDirIndex = row;
    owner.listDirs.selectRow (row);
    owner.refreshSamples();
}
int TestHarnessComponent::SampleListModel::getNumRows() { return owner.sampleNames.size(); }
void TestHarnessComponent::SampleListModel::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= owner.sampleNames.size()) return;
    g.fillAll (rowIsSelected ? juce::Colours::dimgrey : juce::Colours::transparentBlack);
    g.setColour (juce::Colours::white);
    g.drawText (owner.sampleNames[rowNumber], 6, 0, width - 12, height, juce::Justification::centredLeft);
}
void TestHarnessComponent::SampleListModel::selectedRowsChanged (int lastRowSelected)
{
    owner.selectedSampleIndex = lastRowSelected;
}

// -------- Voices list model --------
// --- VoiceListModel Methods ---
int TestHarnessComponent::VoiceListModel::getNumRows() { return (int)owner.engineVoices.size(); }

void TestHarnessComponent::VoiceListModel::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    g.fillAll (rowIsSelected ? juce::Colours::dimgrey : juce::Colours::transparentBlack);
    if (rowNumber >= 0 && rowNumber < (int)owner.engineVoices.size())
    {
        const auto& voiceInfo = owner.engineVoices[rowNumber];
        juce::String text = juce::String((juce::int64)voiceInfo.voiceId) + " - " + voiceInfo.voiceType;
        if (voiceInfo.displayName.isNotEmpty())
            text += " (" + voiceInfo.displayName + ")";
        g.setColour (juce::Colours::white);
        g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft);
    }
}

void TestHarnessComponent::VoiceListModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    owner.listVoices.selectRow(row);
}

void TestHarnessComponent::VoiceListModel::selectedRowsChanged(int lastRowSelected)
{
    owner.setControlledVoiceByIndex(lastRowSelected);
}

void TestHarnessComponent::refreshDirectories()
{
    dirNames.clear(); dirPaths.clear();
    if (! samplesRoot.isDirectory()) return;
    auto sub = samplesRoot.findChildFiles (juce::File::findDirectories, false);
    sub.sort();
    for (auto& d : sub)
    {
        dirNames.add (d.getFileName());
        dirPaths.add (d);
    }
    listDirs.updateContent();
    if (selectedDirIndex < 0 && dirNames.size() > 0)
    {
        selectedDirIndex = 0;
        listDirs.selectRow (0);
    }
}

void TestHarnessComponent::refreshSamples()
{
    sampleNames.clear(); samplePaths.clear();
    if (selectedDirIndex >= 0 && selectedDirIndex < dirPaths.size())
    {
        auto dir = dirPaths[(int) selectedDirIndex];
        juce::Array<juce::File> files;
        files.addArray (dir.findChildFiles (juce::File::findFiles, false, "*.wav"));
        files.addArray (dir.findChildFiles (juce::File::findFiles, false, "*.aif"));
        files.addArray (dir.findChildFiles (juce::File::findFiles, false, "*.aiff"));
        files.sort();
        for (auto& f : files)
        {
            sampleNames.add (f.getFileName());
            samplePaths.add (f);
        }
    }
    listSamples.updateContent();
}

TestHarnessComponent::~TestHarnessComponent()
{
}

void TestHarnessComponent::setAudioEngine(AudioEngine* engine)
{
    audioEngine = engine;
    OnScreenLogger::log ("Harness: Connected to AudioEngine.");
}




void TestHarnessComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText ("Collider Audio Test Harness", getLocalBounds(), juce::Justification::centredTop, 1);
}

void TestHarnessComponent::resized()
{
	// Do not setSize() here; window size controlled in constructor and by host
	int x = 10, y = 40, w = juce::jmin (340, getWidth() - 20), h = 24, gap = 6;
    btnAudioSettings.setBounds (x, y, w, h); y += h + gap;
	btnCreateSynth.setBounds (x, y, w, h); y += h + gap;
	btnCreateSample.setBounds (x, y, w, h); y += h + gap;
	btnCreateNoise.setBounds (x, y, w, h); y += h + gap;
	btnCreateModular.setBounds (x, y, w, h); y += h + gap;
	btnLoadPreset.setBounds(x, y, w, h); y += h + gap;
	btnDestroy.setBounds (x, y, w, h); y += h + gap;
	btnDestroyRandom.setBounds (x, y, w, h); y += h + gap;
	btnDestroySelected.setBounds (x, y, w, h); y += h + gap;
	btnRandomPitch.setBounds (x, y, w, h); y += h + gap;
	btnRandomTime.setBounds (x, y, w, h); y += h + gap;
	btnResetFx.setBounds (x, y, w, h); y += h + gap;
	btnManualFx.setBounds (x, y, w, h); btnManualFx.setToggleState (true, juce::dontSendNotification); y += h + gap * 2;
    lEngine.setBounds (x, y, 70, h); comboEngine.setBounds (x + 75, y, w - 80, h); y += h + gap;

	lblGain.setBounds (x, y, 70, h); sliderGain.setBounds (x + 75, y, w - 80, h); y += h + gap;
	lblPan.setBounds (x, y, 70, h); sliderPan.setBounds (x + 75, y, w - 80, h); y += h + gap;
	btnChaos.setBounds (x, y, w, h); y += h + gap;
	lblStatus.setBounds (x, y, w, h); y += h + gap;
	lblDevice.setBounds (x, y, w, h); y += h + gap;
	lblVoices.setBounds (x, y, w, h); y += h + gap;
	lblPeak.setBounds (x, y, w, h); y += h + gap;

	// FX grid to the right with column wrap
	int gx = x + w + 20;
	int gy = 40;
	const int gw = 300;
	auto place = [&] (juce::Label& l, juce::Slider& s)
	{
		if (gy + h > getHeight() - 40)
		{
			gx += gw + 30;
			gy = 40;
		}
		l.setBounds (gx, gy, 100, h); s.setBounds (gx + 105, gy, gw - 110, h); gy += h + gap;
	};
	place (lFilterCutoff, sFilterCutoff);
	place (lFilterRes,    sFilterRes);
	place (lChRate,       sChRate);
	place (lChDepth,      sChDepth);
	place (lChMix,        sChMix);
	place (lPhRate,       sPhRate);
	place (lPhDepth,      sPhDepth);
	place (lPhCentre,     sPhCentre);
	place (lPhFb,         sPhFb);
	place (lPhMix,        sPhMix);
	place (lRvRoom,       sRvRoom);
	place (lRvDamp,       sRvDamp);
	place (lRvWidth,      sRvWidth);
	place (lRvMix,        sRvMix);
	place (lDlTime,       sDlTime);
	place (lDlFb,         sDlFb);
	place (lDlMix,        sDlMix);
	place (lCpThresh,     sCpThresh);
	place (lCpRatio,      sCpRatio);
	place (lCpAtk,        sCpAtk);
	place (lCpRel,        sCpRel);
	place (lCpMake,       sCpMake);
	place (lLmThresh,     sLmThresh);
	place (lLmRel,        sLmRel);
	place (lDrAmt,        sDrAmt);
	place (lDrMix,        sDrMix);
	place (lGtThresh,     sGtThresh);
	place (lGtAtk,        sGtAtk);
	place (lGtRel,        sGtRel);
	place (lTsRatio,      sTsRatio);
	place (lPtSemis,      sPtSemis);
	place (lPtRatio,      sPtRatio);

	int rightX = gx + gw + 30;
	int panelAreaW = getWidth() - (rightX + 40);
	int panelW = juce::jmin (500, panelAreaW / 2);
	int voicesW = juce::jmin (500, panelAreaW - panelW - 20);
	int totalH = getHeight() - 60;
	int dirsH = totalH / 2;
	int samH  = totalH - dirsH - 10;
	listDirs.setBounds (rightX, 40, panelW, dirsH);
	listSamples.setBounds (rightX, 40 + dirsH + 10, panelW, samH);
	listVoices.setBounds (rightX + panelW + 20, 40, voicesW, totalH);
	logView.setBounds (rightX + panelW + 20 + voicesW + 20, 40, getWidth() - (rightX + panelW + 20 + voicesW + 30), getHeight() - 50);
}

void TestHarnessComponent::comboBoxChanged (juce::ComboBox* c)
{
    if (c != &comboEngine) return;
    if (!audioEngine || controlledVoiceId == 0) return;
    // Send engine selection as a dedicated Update param understood by SampleVoiceProcessor via APVTS
    Command cmd; cmd.type = Command::Type::Update; cmd.voiceId = controlledVoiceId;
    cmd.paramName = "engine"; // mirrored by SampleLoader in Preset Creator
    cmd.paramValue = (float) (comboEngine.getSelectedId() == 2 ? 1.0f : 0.0f); // 0=RB, 1=Naive
    audioEngine->getCommandBus().enqueueLatest (cmd);
    OnScreenLogger::log ("[UI] Engine set to: " + comboEngine.getText());
}

void TestHarnessComponent::buttonClicked (juce::Button* b)
{
    // Ensure the engine is connected before sending any commands
    if (audioEngine == nullptr) {
        OnScreenLogger::log("[UI] ERROR: No AudioEngine connected!");
        return;
    }

    // --- Create Logic ---
    if (b == &btnCreateSynth || b == &btnCreateSample || b == &btnCreateNoise || b == &btnCreateModular)
    {
        Command cmd;
        cmd.type = Command::Type::Create;
        cmd.voiceId = juce::Time::getMillisecondCounterHiRes();

        if (b == &btnCreateSynth) {
            cmd.voiceType = "synth";
            OnScreenLogger::log("[UI] Sending CREATE command for Synth voice...");
            audioEngine->getCommandBus().enqueue(cmd);
        }
        else if (b == &btnCreateNoise) {
            cmd.voiceType = "noise";
            OnScreenLogger::log("[UI] Sending CREATE command for Noise voice...");
            audioEngine->getCommandBus().enqueue(cmd);
        }
        else if (b == &btnCreateModular) {
            cmd.voiceType = "modular";
            OnScreenLogger::log("[UI] Sending CREATE command for Modular voice...");
            audioEngine->getCommandBus().enqueue(cmd);
        }
        else if (b == &btnCreateSample) {
            if (selectedSampleIndex >= 0 && selectedSampleIndex < samplePaths.size()) {
                cmd.voiceType = "sample";
                cmd.resourceName = samplePaths[(int)selectedSampleIndex].getFullPathName();
                OnScreenLogger::log("[UI] Sending CREATE command for Sample voice: " + cmd.resourceName);
                audioEngine->getCommandBus().enqueue(cmd);
            } else {
                OnScreenLogger::log("[UI] No sample selected. Cannot create sample voice.");
            }
        }
    }
    // --- Load Preset for Modular ---
    else if (b == &btnLoadPreset)
    {
        // 1. Check if a voice is selected.
        if (controlledVoiceId == 0)
        {
            OnScreenLogger::log("[UI] ERROR: No voice selected to load preset into.");
            return;
        }

        // 2. Verify that the selected voice is a Modular voice.
        bool isModular = false;
        for (const auto& voiceInfo : engineVoices)
        {
            if (voiceInfo.voiceId == controlledVoiceId && voiceInfo.voiceType == "Modular")
            {
                isModular = true;
                break;
            }
        }

        if (!isModular)
        {
            OnScreenLogger::log("[UI] ERROR: The selected voice is not a Modular Synth.");
            return;
        }

        // 3. Launch the file chooser.
        loadChooser = std::make_unique<juce::FileChooser>("Load Modular Preset", juce::File{}, "*.xml");
        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        loadChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile())
            {
                // 4. Read file content and create the command.
                Command cmd;
                cmd.type = Command::Type::LoadPreset;
                cmd.voiceId = controlledVoiceId;
                cmd.presetData = f.loadFileAsString();

                // 5. Send the command to the engine.
                if (audioEngine)
                {
                    audioEngine->getCommandBus().enqueue(cmd);
                    OnScreenLogger::log("[UI] Sent LoadPreset command for voice " + juce::String((juce::int64)controlledVoiceId));
                }
            }
        });
    }

    // --- Destroy Logic ---
    else if (b == &btnDestroySelected || b == &btnDestroy) // Treat "Destroy" and "Destroy Selected" as the same action
    {
        if (controlledVoiceId != 0) {
            Command cmd;
            cmd.type = Command::Type::Destroy;
            cmd.voiceId = controlledVoiceId;
            audioEngine->getCommandBus().enqueue(cmd);
            OnScreenLogger::log("[UI] Sending DESTROY command for selected voice ID: " + juce::String((juce::int64)controlledVoiceId));
            controlledVoiceId = 0; // De-select the voice since it's being deleted
        } else {
            OnScreenLogger::log("[UI] No voice selected to destroy.");
        }
    }
    // --- Audio Settings Dialog ---
    else if (b == &btnAudioSettings)
    {
        if (audioSetupComp == nullptr)
        {
            audioSetupComp = std::make_unique<juce::AudioDeviceSelectorComponent>(
                deviceManager,
                0, 256,   // min/max inputs
                0, 256,   // min/max outputs
                true, false, false, false);
        }
        audioSetupComp->setSize(500, 450);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(audioSetupComp.get());
        o.content.get()->setSize(500, 450);
        o.dialogTitle = "Audio Settings";
        o.dialogBackgroundColour = juce::Colours::darkgrey;
        o.escapeKeyTriggersCloseButton = true;
        o.resizable = false;
        o.launchAsync();
    }
    else if (b == &btnDestroyRandom)
    {
        if (!engineVoices.empty()) {
            auto& rng = juce::Random::getSystemRandom();
            const int index = rng.nextInt((int)engineVoices.size());
            const juce::uint64 idToDestroy = engineVoices[index].voiceId;
            
            Command cmd;
            cmd.type = Command::Type::Destroy;
            cmd.voiceId = idToDestroy;
            audioEngine->getCommandBus().enqueue(cmd);
            OnScreenLogger::log("[UI] Sending DESTROY command for random voice ID: " + juce::String((juce::int64)idToDestroy));
            
            if (controlledVoiceId == idToDestroy) {
                controlledVoiceId = 0; // De-select if it was the one deleted
            }
        } else {
            OnScreenLogger::log("[UI] No voices to destroy at random.");
        }
    }
    else
    {
        OnScreenLogger::log("[UI] Button '" + b->getButtonText() + "' is not yet wired to the AudioEngine.");
    }
}

void TestHarnessComponent::sliderValueChanged (juce::Slider* s)
{
    if (isSyncingSliders) return;
    // Ensure the engine is connected before sending any commands
    if (audioEngine == nullptr) {
        OnScreenLogger::log("[UI] ERROR: No AudioEngine connected!");
        return;
    }

    // Create a lambda for sending update commands
    auto set = [this] (const char* id, double v)
    {
        OnScreenLogger::log("[UI LOG] Slider for '" + juce::String(id) + "' was moved.");
        
        if (audioEngine && controlledVoiceId != 0)
        {
            Command cmd;
            cmd.type = Command::Type::Update;
            cmd.voiceId = controlledVoiceId;
            cmd.paramName = id;
            cmd.paramValue = (float)v;
            audioEngine->getCommandBus().enqueue(cmd);
            OnScreenLogger::log("[UI LOG] Sent UPDATE command for '" + juce::String(id) + "' to AudioEngine.");
        }
        else
        {
            OnScreenLogger::log("[UI LOG] ERROR: No voice selected!");
        }
    };

    if (s == &sliderGain)
    {
        set("gain", sliderGain.getValue());
    }
    else if (s == &sliderPan)
    {
        set("pan", sliderPan.getValue());
    }
    else if (btnManualFx.getToggleState())
    {
        if (s == &sFilterCutoff) set ("filterCutoff", s->getValue());
        else if (s == &sFilterRes) set ("filterResonance", s->getValue());
        else if (s == &sChRate) set ("chorusRate", s->getValue());
        else if (s == &sChDepth) set ("chorusDepth", s->getValue());
        else if (s == &sChMix) set ("chorusMix", s->getValue());
        else if (s == &sPhRate) set ("phaserRate", s->getValue());
        else if (s == &sPhDepth) set ("phaserDepth", s->getValue());
        else if (s == &sPhCentre) set ("phaserCentre", s->getValue());
        else if (s == &sPhFb) set ("phaserFeedback", s->getValue());
        else if (s == &sPhMix) set ("phaserMix", s->getValue());
        else if (s == &sRvRoom) set ("reverbRoom", s->getValue());
        else if (s == &sRvDamp) set ("reverbDamp", s->getValue());
        else if (s == &sRvWidth) set ("reverbWidth", s->getValue());
        else if (s == &sRvMix) set ("reverbMix", s->getValue());
        else if (s == &sDlTime) set ("delayTimeMs", s->getValue());
        else if (s == &sDlFb) set ("delayFeedback", s->getValue());
        else if (s == &sDlMix) set ("delayMix", s->getValue());
        else if (s == &sCpThresh) set ("compThreshold", s->getValue());
        else if (s == &sCpRatio) set ("compRatio", s->getValue());
        else if (s == &sCpAtk) set ("compAttackMs", s->getValue());
        else if (s == &sCpRel) set ("compReleaseMs", s->getValue());
        else if (s == &sCpMake) set ("compMakeup", s->getValue());
        else if (s == &sLmThresh) set ("limitThreshold", s->getValue());
        else if (s == &sLmRel) set ("limitReleaseMs", s->getValue());
        else if (s == &sDrAmt) set ("driveAmount", s->getValue());
        else if (s == &sDrMix) set ("driveMix", s->getValue());
        else if (s == &sGtThresh) set ("gateThreshold", s->getValue());
        else if (s == &sGtAtk) set ("gateAttackMs", s->getValue());
        else if (s == &sGtRel) set ("gateReleaseMs", s->getValue());
        else if (s == &sTsRatio) set ("timeStretchRatio", s->getValue());
        else if (s == &sPtSemis) set ("pitchSemitones", s->getValue());
        else if (s == &sPtRatio) set ("pitchRatio", s->getValue());
    }
}

// --- Sync Timer ---
void TestHarnessComponent::timerCallback()
{
    if (audioEngine)
    {
        auto currentEngineVoices = audioEngine->getActiveVoicesInfo();
        // Check if the list has changed before updating the UI to prevent flickering
        if (currentEngineVoices.size() != engineVoices.size()) // A simple check is enough for now
        {
            engineVoices.clear();
            for (const auto& voice : currentEngineVoices)
            {
                AudioEngine::VoiceInfo info;
                info.voiceId = voice.voiceId;
                info.voiceType = voice.voiceType;
                info.displayName = voice.displayName;
                engineVoices.push_back(info);
            }
            listVoices.updateContent();
            lblVoices.setText("Voices: " + juce::String((int)engineVoices.size()), juce::dontSendNotification);
        }
        
        // Update peak level display
        auto stats = audioEngine->getRuntimeStats();
        lblPeak.setText("Peak: " + juce::String(stats.lastPeak, 3), juce::dontSendNotification);
        
        // Update device info display
        lblDevice.setText("Device: " + juce::String(stats.sampleRate, 0) + "Hz, " + 
                         juce::String(stats.blockSize) + " samples", juce::dontSendNotification);
        
        // Route engine logs to UI
        auto engineLogs = audioEngine->drainLogs();
        for (const auto& log : engineLogs) {
            OnScreenLogger::log(log);
        }
    }
}

void TestHarnessComponent::refreshVoicesList()
{
    listVoices.updateContent();
}

void TestHarnessComponent::syncSlidersWithSelectedVoice()
{
    if (!audioEngine || controlledVoiceId == 0) return;

    isSyncingSliders = true;

    auto syncSlider = [&](juce::Slider& slider, const juce::String& paramId)
    {
        float value = audioEngine->getVoiceParameterValue(controlledVoiceId, paramId);
        slider.setValue(value, juce::dontSendNotification);
    };

    // Core & Time/Pitch
    syncSlider(sliderGain, "gain");
    syncSlider(sliderPan, "pan");
    syncSlider(sTsRatio, "timeStretchRatio");
    syncSlider(sPtSemis, "pitchSemitones");
    syncSlider(sPtRatio, "pitchRatio");

    // Filter
    syncSlider(sFilterCutoff, "filterCutoff");
    syncSlider(sFilterRes,    "filterResonance");

    // Chorus
    syncSlider(sChRate,  "chorusRate");
    syncSlider(sChDepth, "chorusDepth");
    syncSlider(sChMix,   "chorusMix");

    // Phaser
    syncSlider(sPhRate,   "phaserRate");
    syncSlider(sPhDepth,  "phaserDepth");
    syncSlider(sPhCentre, "phaserCentre");
    syncSlider(sPhFb,     "phaserFeedback");
    syncSlider(sPhMix,    "phaserMix");

    // Reverb
    syncSlider(sRvRoom,  "reverbRoom");
    syncSlider(sRvDamp,  "reverbDamp");
    syncSlider(sRvWidth, "reverbWidth");
    syncSlider(sRvMix,   "reverbMix");

    // Delay
    syncSlider(sDlTime, "delayTimeMs");
    syncSlider(sDlFb,   "delayFeedback");
    syncSlider(sDlMix,  "delayMix");

    // Compressor
    syncSlider(sCpThresh, "compThreshold");
    syncSlider(sCpRatio,  "compRatio");
    syncSlider(sCpAtk,    "compAttackMs");
    syncSlider(sCpRel,    "compReleaseMs");
    syncSlider(sCpMake,   "compMakeup");

    // Limiter
    syncSlider(sLmThresh, "limitThreshold");
    syncSlider(sLmRel,    "limitReleaseMs");

    // Drive
    syncSlider(sDrAmt, "driveAmount");
    syncSlider(sDrMix, "driveMix");

    // Gate
    syncSlider(sGtThresh, "gateThreshold");
    syncSlider(sGtAtk,    "gateAttackMs");
    syncSlider(sGtRel,    "gateReleaseMs");

    isSyncingSliders = false;
}

// --- Control Logic ---
void TestHarnessComponent::setControlledVoiceByIndex (int index)
{
    if (index >= 0 && index < (int)engineVoices.size())
    {
        controlledVoiceId = engineVoices[index].voiceId;
        OnScreenLogger::log("[UI] Selected voice ID: " + juce::String((juce::int64)controlledVoiceId));
        syncSlidersWithSelectedVoice(); // Sync UI with voice parameters
    }
    else
    {
        controlledVoiceId = 0;
    }
    lblStatus.setText("Controlling Voice ID: " + (controlledVoiceId != 0 ? juce::String((juce::int64)controlledVoiceId) : "-"), juce::dontSendNotification);
}

