#include <juce_gui_extra/juce_gui_extra.h>
#include "PresetCreatorComponent.h"
#include "../utils/RtLogger.h"

class PresetCreatorApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Preset Creator"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    
    // ADD: Accessor for shared AudioDeviceManager
    juce::AudioDeviceManager& getAudioDeviceManager() { return audioDeviceManager; }
    
    void initialise (const juce::String&) override
    {
        DBG("[PresetCreator] initialise() starting"); RtLogger::init();
        // Crash handler to capture unexpected exceptions
        std::set_terminate([]{
            auto bt = juce::SystemStats::getStackBacktrace();
            juce::Logger::writeToLog("[PresetCreator][FATAL] terminate called. Backtrace:\n" + bt);
            std::abort();
        });
        // Set up file logger for diagnostics
        auto logsDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                           .getParentDirectory().getChildFile ("juce").getChildFile ("logs");
        logsDir.createDirectory();
        auto logFile = logsDir.getChildFile ("preset_creator_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S") + ".log");
        fileLogger = std::make_unique<juce::FileLogger> (logFile, "Preset Creator Session", 0);
        juce::Logger::setCurrentLogger (fileLogger.get());
        DBG("[PresetCreator] Logger initialised at: " + logFile.getFullPathName());
        juce::Logger::writeToLog("PresetCreatorApplication::initialise called");
        
        // ADD: Load persistent audio settings
        // Define where to store the settings file
        auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile(getApplicationName());
        appDataDir.createDirectory(); // Ensure the directory exists
        auto settingsFile = appDataDir.getChildFile("audio_settings.xml");

        std::unique_ptr<juce::XmlElement> savedState;
        if (settingsFile.existsAsFile())
        {
            savedState = juce::XmlDocument::parse(settingsFile);
            juce::Logger::writeToLog("Loading audio settings from: " + settingsFile.getFullPathName());
        }
        else
        {
            juce::Logger::writeToLog("No saved audio settings found, using defaults");
        }
        
        // Pass the saved state to the device manager.
        // It will automatically use the saved settings or fall back to defaults.
        audioDeviceManager.initialise(2, 2, savedState.get(), true);
        
        mainWindow.reset (new MainWindow (getApplicationName(), audioDeviceManager));
        juce::Logger::writeToLog("MainWindow created successfully");
    }
    void shutdown() override 
    { 
        // ADD: Save persistent audio settings
        // Get the current audio device settings as an XML element
        std::unique_ptr<juce::XmlElement> currentState(audioDeviceManager.createStateXml());

        if (currentState != nullptr)
        {
            // Define the same settings file path as in initialise()
            auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                    .getChildFile(getApplicationName());
            auto settingsFile = appDataDir.getChildFile("audio_settings.xml");

            // Write the XML to the file
            if (currentState->writeTo(settingsFile))
            {
                juce::Logger::writeToLog("Audio settings saved to: " + settingsFile.getFullPathName());
            }
            else
            {
                juce::Logger::writeToLog("Failed to save audio settings to: " + settingsFile.getFullPathName());
            }
        }
        
        RtLogger::shutdown(); 
        mainWindow = nullptr; 
        juce::Logger::setCurrentLogger (nullptr); 
        fileLogger = nullptr; 
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, juce::AudioDeviceManager& adm)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons),
              deviceManager(adm)
        {
            juce::Logger::writeToLog("MainWindow constructor starting...");
            setUsingNativeTitleBar (true);
            juce::Logger::writeToLog("Creating PresetCreatorComponent...");
            setContentOwned (new PresetCreatorComponent(deviceManager), true);
            juce::Logger::writeToLog("PresetCreatorComponent created, setting up window...");
            centreWithSize (2600, 1080);
            setVisible (true);
            toFront (true);
            juce::Logger::writeToLog("MainWindow setup complete");
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        
    private:
        juce::AudioDeviceManager& deviceManager;
    };

private:
    // ADD: Shared AudioDeviceManager for the entire application
    juce::AudioDeviceManager audioDeviceManager;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::FileLogger> fileLogger;
};

START_JUCE_APPLICATION (PresetCreatorApplication)


