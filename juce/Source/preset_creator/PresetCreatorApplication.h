#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../utils/VersionInfo.h"

// Forward declaration
class SplashScreenComponent;

class PresetCreatorApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return VersionInfo::getApplicationName(); }
    const juce::String getApplicationVersion() override { return VersionInfo::getFullVersionString(); }
    
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
    
    // Show splash screen on startup
    void showSplashScreen();

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name, 
                   juce::AudioDeviceManager& adm,
                   juce::AudioPluginFormatManager& fm,
                   juce::KnownPluginList& kl);
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        bool keyPressed (const juce::KeyPress& key) override;
        
    private:
        juce::AudioDeviceManager& deviceManager;
        juce::AudioPluginFormatManager& pluginFormatManager;
        juce::KnownPluginList& knownPluginList;

        bool isMaximizedLike = false;
        juce::Rectangle<int> lastNormalBounds;

        void applyMaximizeLike();
        void restoreFromMaximizeLike();
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
    std::unique_ptr<juce::DialogWindow> splashWindowPtr;
};

