#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "../audio/graph/VoiceProcessor.h"
#include "../audio/assets/SampleBank.h"

// Forward declaration
class AudioEngine;
#include "../audio/AudioEngine.h"

// Simple on-screen logger used by the harness
struct OnScreenLogger
{
    static void attach (juce::TextEditor* editor);
    static void log (const juce::String& msg);
};

class TestHarnessComponent : public juce::Component, // Changed from AudioAppComponent
                             private juce::Button::Listener,
                             private juce::Slider::Listener,
                             private juce::ComboBox::Listener,
                             private juce::Timer
{
public:
    // Accept the shared AudioDeviceManager from the parent so we can show audio settings
    TestHarnessComponent(juce::AudioDeviceManager& adm);
    ~TestHarnessComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate);
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill);
    void releaseResources();

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    
    // Connection to AudioEngine
    void setAudioEngine(AudioEngine* engine);
    // List models (nested types)
    struct DirListModel : public juce::ListBoxModel {
        DirListModel(TestHarnessComponent& ownerRef) : owner(ownerRef) {}
        int getNumRows() override;
        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged (int lastRowSelected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        TestHarnessComponent& owner;
    };
    struct SampleListModel : public juce::ListBoxModel {
        SampleListModel(TestHarnessComponent& ownerRef) : owner(ownerRef) {}
        int getNumRows() override;
        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged (int lastRowSelected) override;
        TestHarnessComponent& owner;
    };

    struct VoiceListModel : public juce::ListBoxModel {
        VoiceListModel(TestHarnessComponent& ownerRef) : owner(ownerRef) {}
        int getNumRows() override;
        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged (int lastRowSelected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        TestHarnessComponent& owner;
    };

private:
    void buttonClicked (juce::Button* b) override;
    void sliderValueChanged (juce::Slider* s) override;
    void comboBoxChanged (juce::ComboBox* c) override;

    // AudioEngine connection
    AudioEngine* audioEngine = nullptr;
    
    // This vector will hold the synced voice list from the engine
    std::vector<AudioEngine::VoiceInfo> engineVoices;
    juce::uint64 controlledVoiceId { 0 };
    bool isSyncingSliders = false;

    juce::AudioBuffer<float> tempMixBuffer; // For manual mixing
    
    // --- Audio settings integration ---
    juce::AudioDeviceManager& deviceManager; // reference to shared device manager
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSetupComp;
    juce::TextButton btnAudioSettings { "Audio Settings..." };
    SampleBank sampleBank; // To provide samples
    juce::TextButton btnCreateSynth { "Create Synth" };
    juce::TextButton btnCreateSample { "Create Sample" };
    juce::TextButton btnCreateNoise { "Create Noise" };
    juce::TextButton btnCreateModular { "Create Modular" };
    juce::TextButton btnLoadPreset { "Load Preset for Modular" };
    juce::TextButton btnDestroy { "Destroy Last Voice" };
    juce::TextButton btnDestroyRandom { "Destroy Random Voice" };
    juce::TextButton btnDestroySelected { "Destroy Selected Voice" };
    juce::TextButton btnRandomPitch { "Random Pitch" };
    juce::TextButton btnRandomTime { "Random Time" };
    juce::TextButton btnResetFx { "Reset FX Defaults" };
    juce::ToggleButton btnChaos { "Enable Chaos Mode" };
    juce::Slider sliderGain, sliderPan;
    juce::Label lblGain { {}, "Gain" }, lblPan { {}, "Pan" }, lblStatus;
    juce::Label lblDevice { {}, "Device: -" }, lblVoices { {}, "Voices: 0" }, lblPeak { {}, "Peak: 0.0" };
    juce::TextEditor logView;
    juce::ToggleButton btnManualFx { "Manual FX Control" };
    juce::Label lEngine { {}, "Engine" };
    juce::ComboBox comboEngine;

    // FX sliders
    juce::Slider sFilterCutoff, sFilterRes, sChRate, sChDepth, sChMix;
    juce::Slider sPhRate, sPhDepth, sPhCentre, sPhFb, sPhMix;
    juce::Slider sRvRoom, sRvDamp, sRvWidth, sRvMix;
    juce::Slider sDlTime, sDlFb, sDlMix;
    juce::Slider sCpThresh, sCpRatio, sCpAtk, sCpRel, sCpMake;
    juce::Slider sLmThresh, sLmRel;
    juce::Slider sDrAmt, sDrMix;
    juce::Slider sGtThresh, sGtAtk, sGtRel;
    juce::Slider sTsRatio, sPtSemis;

    juce::Label lFilterCutoff { {}, "Filt Cutoff" }, lFilterRes { {}, "Filt Q" }, lChRate { {}, "Ch Rate" }, lChDepth { {}, "Ch Depth" }, lChMix { {}, "Ch Mix" };
    juce::Label lPhRate { {}, "Ph Rate" }, lPhDepth { {}, "Ph Depth" }, lPhCentre { {}, "Ph Ctr" }, lPhFb { {}, "Ph FB" }, lPhMix { {}, "Ph Mix" };
    juce::Label lRvRoom { {}, "Rv Room" }, lRvDamp { {}, "Rv Damp" }, lRvWidth { {}, "Rv Width" }, lRvMix { {}, "Rv Mix" };
    juce::Label lDlTime { {}, "Dly ms" }, lDlFb { {}, "Dly FB" }, lDlMix { {}, "Dly Mix" };
    juce::Label lCpThresh { {}, "Cp Thr" }, lCpRatio { {}, "Cp Ratio" }, lCpAtk { {}, "Cp Atk" }, lCpRel { {}, "Cp Rel" }, lCpMake { {}, "Cp Make" };
    juce::Label lLmThresh { {}, "Lm Thr" }, lLmRel { {}, "Lm Rel" };
    juce::Label lDrAmt { {}, "Drv Amt" }, lDrMix { {}, "Drv Mix" };
    juce::Label lGtThresh { {}, "Gate Thr" }, lGtAtk { {}, "Gate Atk" }, lGtRel { {}, "Gate Rel" };
    juce::Label lTsRatio { {}, "Time" }, lPtSemis { {}, "Pitch" };
    juce::Slider sPtRatio; juce::Label lPtRatio { {}, "Pitch x" };

    // Sample loader UI
    juce::ListBox listDirs { "Folders" };
    juce::ListBox listSamples { "Samples" };
    juce::ListBox listVoices { "Voices" };
    DirListModel dirModel { *this };
    SampleListModel sampleModel { *this };
    VoiceListModel voiceModel { *this };
    juce::StringArray dirNames;
    juce::Array<juce::File> dirPaths;
    juce::StringArray sampleNames;
    juce::Array<juce::File> samplePaths;
    int selectedDirIndex { -1 };
    int selectedSampleIndex { -1 };
    juce::File samplesRoot;
    std::unique_ptr<juce::FileChooser> loadChooser;

    void refreshDirectories();
    void refreshSamples();
    void refreshVoicesList();
    void syncSlidersWithSelectedVoice();
    void setControlledVoiceByIndex (int index);

    juce::uint64 lastVoiceId { 0 };
    bool hasEngineConfirmedReady { false };
    double harnessStartMs { 0.0 };
    // (Adapter handles modular voices as normal VoiceProcessor instances)

    // (Removed VCO verification fields)
};


