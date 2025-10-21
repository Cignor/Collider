#include "PresetCreatorApplication.h"
#include "PresetCreatorComponent.h"
#include "../utils/RtLogger.h"

void PresetCreatorApplication::initialise(const juce::String&)
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
        
        // Initialize plugin management
        pluginFormatManager.addDefaultFormats();
        
        // Initialize application properties
        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = appDataDir.getFullPathName();
        appProperties = std::make_unique<juce::PropertiesFile>(options);
        
        // Define where to save the plugin list XML
        auto deadMansPedalFile = appDataDir.getChildFile("blacklisted_plugins.txt");
        pluginScanListFile = appDataDir.getChildFile("known_plugins.xml");
        
        // Load the list from the XML file
        if (pluginScanListFile.existsAsFile())
        {
            auto pluginListXml = juce::XmlDocument::parse(pluginScanListFile);
            if (pluginListXml != nullptr)
            {
                knownPluginList.recreateFromXml(*pluginListXml);
                juce::Logger::writeToLog("Loaded " + juce::String(knownPluginList.getNumTypes()) + " plugin(s) from cache");
            }
        }
        else
        {
            juce::Logger::writeToLog("No cached plugin list found");
        }
        
        juce::Logger::writeToLog("Attempting to create MainWindow...");
        mainWindow.reset (new MainWindow (getApplicationName(), 
                                         audioDeviceManager,
                                         pluginFormatManager,
                                         knownPluginList));
        juce::Logger::writeToLog("MainWindow created successfully");
}

void PresetCreatorApplication::shutdown()
{ 
        // Save persistent audio settings
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
        
        // Save plugin list
        if (auto pluginListXml = knownPluginList.createXml())
        {
            if (pluginListXml->writeTo(pluginScanListFile))
            {
                juce::Logger::writeToLog("Plugin list saved to: " + pluginScanListFile.getFullPathName());
            }
        }
        
        // Save application properties
        if (appProperties)
            appProperties->saveIfNeeded();
        
        RtLogger::shutdown(); 
        mainWindow = nullptr; 
        juce::Logger::setCurrentLogger (nullptr); 
        fileLogger = nullptr; 
}

PresetCreatorApplication::MainWindow::MainWindow(juce::String name, 
                                                 juce::AudioDeviceManager& adm,
                                                 juce::AudioPluginFormatManager& fm,
                                                 juce::KnownPluginList& kl)
    : DocumentWindow(name,
                     juce::Desktop::getInstance().getDefaultLookAndFeel()
                         .findColour(ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons),
      deviceManager(adm),
      pluginFormatManager(fm),
      knownPluginList(kl)
{
    juce::Logger::writeToLog("MainWindow constructor starting...");
    setUsingNativeTitleBar(true);
    juce::Logger::writeToLog("Attempting to create PresetCreatorComponent...");
    setContentOwned(new PresetCreatorComponent(deviceManager, pluginFormatManager, knownPluginList), true);
    juce::Logger::writeToLog("PresetCreatorComponent created and set.");
    centreWithSize(2600, 1080);
    setVisible(true);
    toFront(true);
    juce::Logger::writeToLog("MainWindow setup complete");
}

START_JUCE_APPLICATION (PresetCreatorApplication)


