#include "PresetCreatorApplication.h"
#include "PresetCreatorComponent.h"
#include "SplashScreenComponent.h"
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
        fileLogger = std::make_unique<juce::FileLogger> (logFile, "Pikon Raditsz Session", 0);
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
        
        // Always ensure main window is visible
        if (mainWindow != nullptr)
        {
            mainWindow->setVisible(true);
            mainWindow->toFront(true);
            juce::Logger::writeToLog("MainWindow made visible");
        }
        
        // Show splash screen if enabled (non-blocking, on top)
        // Use callAsync to show splash after main window is fully initialized
        if (SplashScreenComponent::shouldShowSplashScreen(appProperties.get()))
        {
            juce::MessageManager::callAsync([this]()
            {
                showSplashScreen();
            });
        }
}

void PresetCreatorApplication::showSplashScreen()
{
    try
    {
        if (mainWindow == nullptr)
        {
            juce::Logger::writeToLog("[Splash] Cannot show splash - main window is null");
            return;
        }
        
        // Ensure main window is visible first
        mainWindow->setVisible(true);
        
        // Create splash screen component
        auto splash = std::make_unique<SplashScreenComponent>();
        
        // Get bounds before moving ownership
        auto splashBounds = splash->getBounds();
        juce::Logger::writeToLog("[Splash] Splash component created, size: " + juce::String(splashBounds.getWidth()) + "x" + juce::String(splashBounds.getHeight()));
        
        // Create a custom transparent window for the splash screen (no JUCE branding)
        splashWindowPtr = std::make_unique<TransparentSplashWindow>();
        juce::Logger::writeToLog("[Splash] Transparent window created");
        
        // Set up dismiss callback
        auto* splashPtr = splash.get();
        splashPtr->onDismiss = [this]()
        {
            // Dismiss splash window
            if (splashWindowPtr != nullptr)
            {
                splashWindowPtr->setVisible(false);
                splashWindowPtr.reset();
            }
            
            // Ensure main window is focused
            if (mainWindow != nullptr)
            {
                mainWindow->setVisible(true);
                mainWindow->toFront(true);
            }
        };
        
        // Add splash component to window (capture pointer before releasing ownership)
        auto* splashComponent = splash.get();
        splashWindowPtr->addAndMakeVisible(splash.release());
        
        // Center splash on screen
        auto screenBounds = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        splashBounds.setPosition(
            screenBounds.getCentreX() - splashBounds.getWidth() / 2,
            screenBounds.getCentreY() - splashBounds.getHeight() / 2
        );
        splashWindowPtr->setBounds(splashBounds);
        
        // Ensure splash component fills entire window bounds
        if (splashComponent != nullptr)
        {
            splashComponent->setBounds(0, 0, splashBounds.getWidth(), splashBounds.getHeight());
        }
        
        // Show splash (non-modal, won't block)
        splashWindowPtr->setVisible(true);
        splashWindowPtr->toFront(true);
        
        // Grab keyboard focus for the window and component
        splashWindowPtr->grabKeyboardFocus();
        if (splashComponent != nullptr)
        {
            splashComponent->grabKeyboardFocus();
        }
        
        juce::Logger::writeToLog("[Splash] Window visible: " + juce::String(splashWindowPtr->isVisible() ? "yes" : "no"));
        juce::Logger::writeToLog("[Splash] Window bounds: " + splashWindowPtr->getBounds().toString());
        juce::Logger::writeToLog("[Splash] Splash screen shown successfully");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[Splash] Error showing splash screen: " + juce::String(e.what()));
        // Ensure main window is visible even if splash fails
        if (mainWindow != nullptr)
        {
            mainWindow->setVisible(true);
            mainWindow->toFront(true);
        }
    }
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
        
        // Save window state and application properties
        if (appProperties)
        {
            if (mainWindow != nullptr && ! mainWindow->isFullScreen())
                appProperties->setValue ("presetCreatorWindowState", mainWindow->getWindowStateAsString());
            appProperties->saveIfNeeded();
        }
        
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
    setResizable (true, true);
    setResizeLimits (900, 600, 8192, 8192);

    // Try to restore previous window state from properties
    if (auto* props = PresetCreatorApplication::getApp().getProperties())
    {
        auto state = props->getValue ("presetCreatorWindowState");
        if (state.isNotEmpty())
        {
            if (! restoreWindowStateFromString (state))
                centreWithSize (2600, 1080);
        }
        else
        {
            centreWithSize (2600, 1080);
        }
    }
    else
    {
        centreWithSize (2600, 1080);
    }
    setVisible(true);
    toFront(true);
    
    // Clamp to work area
    auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getDisplayForRect (getBounds());
    if (display == nullptr) display = displays.getPrimaryDisplay();
    if (display != nullptr) setBounds (getBounds().constrainedWithin (display->userArea));
    juce::Logger::writeToLog("MainWindow setup complete");
}

bool PresetCreatorApplication::MainWindow::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::F11Key)
    {
        setFullScreen (! isFullScreen());
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::returnKey && key.getModifiers().isAltDown())
    {
        if (isFullScreen())
            setFullScreen (false);
        else if (! isMaximizedLike)
            applyMaximizeLike();
        else
            restoreFromMaximizeLike();
        return true;
    }
    return DocumentWindow::keyPressed (key);
}

void PresetCreatorApplication::MainWindow::applyMaximizeLike()
{
    if (! isMaximizedLike)
    {
        lastNormalBounds = getBounds();
        auto& displays = juce::Desktop::getInstance().getDisplays();
        if (auto* d = displays.getDisplayForRect (getBounds())) setBounds (d->userArea);
        else if (auto* p = displays.getPrimaryDisplay()) setBounds (p->userArea);
        isMaximizedLike = true;
    }
}

void PresetCreatorApplication::MainWindow::restoreFromMaximizeLike()
{
    if (isMaximizedLike)
    {
        if (! lastNormalBounds.isEmpty()) setBounds (lastNormalBounds);
        isMaximizedLike = false;
    }
}

START_JUCE_APPLICATION (PresetCreatorApplication)


