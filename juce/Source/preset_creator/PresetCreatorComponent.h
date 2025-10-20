#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../audio/graph/ModularSynthProcessor.h"

class PresetCreatorComponent : public juce::Component,
                               private juce::Button::Listener,
                               private juce::Timer
{
public:
    PresetCreatorComponent(juce::AudioDeviceManager& deviceManager,
                           juce::AudioPluginFormatManager& formatManager,
                           juce::KnownPluginList& knownPluginList);
    ~PresetCreatorComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;
    void visibilityChanged() override;

    // ADD: Public function to show audio settings dialog
    void showAudioSettingsDialog();

private:
    void setWindowFileName(const juce::String& fileName);
    void buttonClicked (juce::Button*) override;
    void timerCallback() override;
    void refreshModulesList();
    void doConnect();
    void doSave();
    void doLoad();
    void startAudition();
    void stopAudition();

    juce::TextButton btnAddVCO { "Add VCO" };
    juce::TextButton btnAddVCF { "Add VCF" };
    juce::TextButton btnAddVCA { "Add VCA" };
    juce::TextButton btnConnect { "Connect" };
    juce::TextButton btnSave { "Save Preset" };
    juce::TextButton btnLoad { "Load Preset" };

    juce::ListBox listModules { "Modules", nullptr };
    struct ModulesModel : public juce::ListBoxModel
    {
        juce::StringArray rows;
        int getNumRows() override { return rows.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
        {
            g.fillAll (sel ? juce::Colours::dimgrey : juce::Colours::transparentBlack);
            g.setColour (juce::Colours::white);
            if (row >= 0 && row < rows.size()) g.drawText (rows[row], 6, 0, w - 12, h, juce::Justification::centredLeft);
        }
    } modulesModel;

    juce::ComboBox cbSrc, cbDst, cbSrcChan, cbDstChan;
    juce::TextEditor log;
    std::unique_ptr<juce::FileLogger> fileLogger;
    std::unique_ptr<class ImGuiNodeEditorComponent> editor;

    std::unique_ptr<ModularSynthProcessor> synth;
    double sampleRate { 48000.0 };
    int blockSize { 512 };

    std::unique_ptr<juce::FileChooser> saveChooser;
    std::unique_ptr<juce::FileChooser> loadChooser;

    juce::AudioDeviceManager& deviceManager;
    juce::AudioPluginFormatManager& pluginFormatManager;
    juce::KnownPluginList& knownPluginList;
    juce::AudioProcessorPlayer processorPlayer;
    bool auditioning { false };
};


