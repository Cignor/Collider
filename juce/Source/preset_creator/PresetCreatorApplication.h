#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

class PresetCreatorApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Preset Creator"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    
    // Accessors for shared components
    juce::AudioDeviceManager& getAudioDeviceManager() { return audioDeviceManager; }
    juce::AudioPluginFormatManager& getPluginFormatManager() { return pluginFormatManager; }
    juce::KnownPluginList& getKnownPluginList() { return knownPluginList; }
    
    // Static getter for global access
    static PresetCreatorApplication& getApp()
    {
        return *dynamic_cast<PresetCreatorApplication*>(juce::JUCEApplication::getInstance());
    }
    
    juce::PropertiesFile* getProperties() { return appProperties.get(); }
    
    void initialise(const juce::String&) override;
    void shutdown() override;

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name, 
                   juce::AudioDeviceManager& adm,
                   juce::AudioPluginFormatManager& fm,
                   juce::KnownPluginList& kl);
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        
    private:
        juce::AudioDeviceManager& deviceManager;
        juce::AudioPluginFormatManager& pluginFormatManager;
        juce::KnownPluginList& knownPluginList;
    };

private:
    // Shared components for the entire application
    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioPluginFormatManager pluginFormatManager;
    juce::KnownPluginList knownPluginList;
    juce::File pluginScanListFile;
    
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::FileLogger> fileLogger;
    std::unique_ptr<juce::PropertiesFile> appProperties;
};

